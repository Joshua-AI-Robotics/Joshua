import * as THREE from 'three';
import { loadPart, type LoadedPart } from '../ldraw/loadPart';
import type { SnapHotspot } from '../ldraw/snapParser';
import {
  instancePoseMatrix,
  type JointInstance,
  type PartInstance,
} from './store';

export type SceneOverlap = {
  id: string;
  aInstanceId: string;
  bInstanceId: string;
  volume: number;
};

export type PartTransform = {
  instanceId?: string;
  partId: string;
  transform: THREE.Matrix4;
};

export type ConnectorOverlapAllowance = {
  connectorInstanceId: string;
  counterpartInstanceId: string;
  center: THREE.Vector3;
  axis: THREE.Vector3;
  radius: number;
  halfLength: number;
};

type LoadedCollisionPart = {
  instance: PartInstance;
  part: LoadedPart;
  transform: THREE.Matrix4;
  bounds: THREE.Box3;
  boxes: CollisionBox[];
};

type CollisionBox = {
  instanceId: string;
  box: THREE.Box3;
};

const COLLISION_MARGIN_LDU = 1;
const COLLISION_SURFACE_PADDING_LDU = 0;
const CONNECTOR_RADIUS_CLEARANCE_LDU = 4;
const CONNECTOR_LENGTH_CLEARANCE_LDU = 4;
export const COLLISION_VOLUME_THRESHOLD = 500;
const CONNECTOR_CENTER_ALIGNMENT_TOLERANCE_LDU = 2;
const CONNECTOR_AXIS_ALIGNMENT_DOT = 0.98;
const CONTAINMENT_SAMPLE_LIMIT = 240;
const CONTAINMENT_REQUIRED_HITS = 3;
const CONTAINMENT_PENALTY_VOLUME = COLLISION_VOLUME_THRESHOLD + 1;
const CONTAINMENT_OVERLAP_EPSILON_LDU3 = 1;
const CONTAINMENT_RAY_DIRECTION = new THREE.Vector3(0.827, 0.379, 0.416).normalize();

const localCollisionBoxCache = new Map<string, Promise<THREE.Box3[]>>();
const localTriangleCache = new Map<string, Promise<THREE.Triangle[]>>();

function snapProfile(hotspot: SnapHotspot): SnapHotspot['profile'] {
  return hotspot.profile ?? 'round';
}

function areCompatibleSnapProfiles(a: SnapHotspot, b: SnapHotspot): boolean {
  const aProfile = snapProfile(a);
  const bProfile = snapProfile(b);
  if (aProfile === bProfile) return true;
  return (
    (aProfile === 'axle' && bProfile === 'round') ||
    (aProfile === 'round' && bProfile === 'axle')
  );
}

export async function overlapVolumeForPartTransform(
  partId: string,
  transform: THREE.Matrix4,
  others: PartInstance[],
  allowances: ConnectorOverlapAllowance[] = [],
): Promise<number> {
  return overlapVolumeForPartTransforms([{ partId, transform }], others, allowances);
}

export async function overlapVolumeForPartTransforms(
  partTransforms: PartTransform[],
  others: PartInstance[],
  allowances: ConnectorOverlapAllowance[] = [],
): Promise<number> {
  const moving = await Promise.all(
    partTransforms.map(async ({ instanceId, partId, transform }, index) => {
      const part = await loadPart(partId);
      const resolvedInstanceId = instanceId ?? `moving_${index}`;
      return {
        instanceId: resolvedInstanceId,
        partId,
        transform,
        bounds: transformedBounds(part.bounds, transform),
        boxes: await transformedCollisionBoxes(
          resolvedInstanceId,
          partId,
          transform,
        ),
      };
    }),
  );
  let overlapVolume = 0;

  for (const other of others) {
    const otherPart = await loadPart(other.partId);
    const otherTransform = instancePoseMatrix(other);
    const otherBounds = transformedBounds(otherPart.bounds, otherTransform);
    const otherBoxes = await transformedCollisionBoxes(
      other.instanceId,
      other.partId,
      otherTransform,
    );
    for (const placement of moving) {
      const broadVolume = boxOverlapVolume(placement.bounds, otherBounds);
      if (broadVolume <= 0) continue;
      const collisionOverlap = overlapVolumeBetweenCollisionBoxes(
        placement.boxes,
        otherBoxes,
        allowances,
      );
      overlapVolume += collisionOverlap;
      if (
        collisionOverlap > CONTAINMENT_OVERLAP_EPSILON_LDU3 ||
        allowances.length === 0
      ) {
        overlapVolume += await containmentPenalty(
          placement.boxes,
          placement.partId,
          placement.transform,
          otherBoxes,
          other.partId,
          otherTransform,
          allowances,
        );
      }
    }
  }

  return overlapVolume;
}

export async function findSceneOverlaps(
  parts: PartInstance[],
  joints: JointInstance[] = [],
): Promise<SceneOverlap[]> {
  const loaded = await Promise.all(
    parts.map(async (instance) => {
      const part = await loadPart(instance.partId);
      const transform = instancePoseMatrix(instance);
      return {
        instance,
        part,
        transform,
        bounds: transformedBounds(part.bounds, transform),
        boxes: await transformedCollisionBoxes(
          instance.instanceId,
          instance.partId,
          transform,
        ),
      };
    }),
  );
  const loadedByInstance = new Map(
    loaded.map((entry) => [entry.instance.instanceId, entry]),
  );
  const overlaps: SceneOverlap[] = [];

  for (let a = 0; a < loaded.length; a += 1) {
    for (let b = a + 1; b < loaded.length; b += 1) {
      if (boxOverlapVolume(loaded[a].bounds, loaded[b].bounds) <= 0) continue;
      const allowances = connectorAllowancesForJointPair(
        loaded[a].instance.instanceId,
        loaded[b].instance.instanceId,
        joints,
        loadedByInstance,
      );
      const collisionOverlap = overlapVolumeBetweenCollisionBoxes(
        loaded[a].boxes,
        loaded[b].boxes,
        allowances,
      );
      let volume = collisionOverlap;
      if (
        collisionOverlap > CONTAINMENT_OVERLAP_EPSILON_LDU3 ||
        allowances.length === 0
      ) {
        volume += await containmentPenalty(
          loaded[a].boxes,
          loaded[a].instance.partId,
          loaded[a].transform,
          loaded[b].boxes,
          loaded[b].instance.partId,
          loaded[b].transform,
          allowances,
        );
      }
      if (volume <= COLLISION_VOLUME_THRESHOLD) continue;
      overlaps.push({
        id: `${loaded[a].instance.instanceId}|${loaded[b].instance.instanceId}`,
        aInstanceId: loaded[a].instance.instanceId,
        bInstanceId: loaded[b].instance.instanceId,
        volume,
      });
    }
  }

  return overlaps;
}

export function connectorAllowanceFromHotspot(
  connectorInstanceId: string,
  counterpartInstanceId: string,
  hotspot: { center: THREE.Vector3; axis: THREE.Vector3; radius: number; length: number },
  transform: THREE.Matrix4,
): ConnectorOverlapAllowance {
  return {
    connectorInstanceId,
    counterpartInstanceId,
    center: hotspot.center.clone().applyMatrix4(transform),
    axis: hotspot.axis.clone().transformDirection(transform).normalize(),
    radius: Math.max(4, hotspot.radius + CONNECTOR_RADIUS_CLEARANCE_LDU),
    halfLength: Math.max(6, hotspot.length / 2 + CONNECTOR_LENGTH_CLEARANCE_LDU),
  };
}

export function virtualConnectorAllowancesFromFemaleHotspots(
  aInstanceId: string,
  aHotspot: { center: THREE.Vector3; axis: THREE.Vector3; radius: number; length: number },
  aTransform: THREE.Matrix4,
  bInstanceId: string,
  bHotspot: { center: THREE.Vector3; axis: THREE.Vector3; radius: number; length: number },
  bTransform: THREE.Matrix4,
): ConnectorOverlapAllowance[] {
  const aCenter = aHotspot.center.clone().applyMatrix4(aTransform);
  const bCenter = bHotspot.center.clone().applyMatrix4(bTransform);
  const aAxis = aHotspot.axis.clone().transformDirection(aTransform).normalize();
  const center = aCenter.clone().add(bCenter).multiplyScalar(0.5);
  const centerDistance = aCenter.distanceTo(bCenter);
  const radius = Math.max(
    4,
    Math.max(aHotspot.radius, bHotspot.radius) + CONNECTOR_RADIUS_CLEARANCE_LDU,
  );
  const halfLength = Math.max(
    6,
    centerDistance / 2 +
      Math.max(aHotspot.length, bHotspot.length) / 2 +
      CONNECTOR_LENGTH_CLEARANCE_LDU,
  );
  const base = { center, axis: aAxis, radius, halfLength };
  return [
    {
      ...base,
      connectorInstanceId: aInstanceId,
      counterpartInstanceId: bInstanceId,
    },
    {
      ...base,
      connectorInstanceId: bInstanceId,
      counterpartInstanceId: aInstanceId,
    },
  ];
}

function connectorAllowancesForJointPair(
  aInstanceId: string,
  bInstanceId: string,
  joints: JointInstance[],
  loadedByInstance: Map<string, LoadedCollisionPart>,
): ConnectorOverlapAllowance[] {
  const allowances: ConnectorOverlapAllowance[] = [];
  let hasPairJoint = false;
  for (const joint of joints) {
    const matchesPair =
      (joint.parentInstance === aInstanceId && joint.childInstance === bInstanceId) ||
      (joint.parentInstance === bInstanceId && joint.childInstance === aInstanceId);
    if (!matchesPair || !joint.parentHotspotId || !joint.childHotspotId) continue;
    hasPairJoint = true;

    const parent = loadedByInstance.get(joint.parentInstance);
    const child = loadedByInstance.get(joint.childInstance);
    if (!parent || !child) continue;
    const parentHotspot = parent?.part.hotspots.find(
      (hotspot) => hotspot.id === joint.parentHotspotId,
    );
    const childHotspot = child?.part.hotspots.find(
      (hotspot) => hotspot.id === joint.childHotspotId,
    );

    const compatibleProfile =
      parentHotspot &&
      childHotspot &&
      areCompatibleSnapProfiles(parentHotspot, childHotspot);

    if (
      parentHotspot?.gender &&
      childHotspot?.gender &&
      parentHotspot.gender !== childHotspot.gender &&
      compatibleProfile
    ) {
      const parentTransform = instancePoseMatrix(parent.instance);
      const childTransform = instancePoseMatrix(child.instance);
      allowances.push(
        parentHotspot.gender === 'M'
          ? connectorAllowanceFromHotspot(
              parent.instance.instanceId,
              child.instance.instanceId,
              parentHotspot,
              parentTransform,
            )
          : connectorAllowanceFromHotspot(
              child.instance.instanceId,
              parent.instance.instanceId,
              childHotspot,
              childTransform,
            ),
      );
    } else if (
      parentHotspot?.gender === 'F' &&
      childHotspot?.gender === 'F' &&
      compatibleProfile
    ) {
      allowances.push(
        ...virtualConnectorAllowancesFromFemaleHotspots(
          parent.instance.instanceId,
          parentHotspot,
          instancePoseMatrix(parent.instance),
          child.instance.instanceId,
          childHotspot,
          instancePoseMatrix(child.instance),
        ),
      );
    }
  }
  const a = loadedByInstance.get(aInstanceId);
  const b = loadedByInstance.get(bInstanceId);
  if (hasPairJoint && a && b) {
    allowances.push(...alignedConnectorAllowancesForParts(a, b));
  }
  return allowances;
}

function alignedConnectorAllowancesForParts(
  a: LoadedCollisionPart,
  b: LoadedCollisionPart,
): ConnectorOverlapAllowance[] {
  const allowances: ConnectorOverlapAllowance[] = [];
  for (const aHotspot of a.part.hotspots) {
    if (!aHotspot.gender) continue;
    const aCenter = aHotspot.center.clone().applyMatrix4(a.transform);
    const aAxis = aHotspot.axis.clone().transformDirection(a.transform).normalize();
    for (const bHotspot of b.part.hotspots) {
      if (!bHotspot.gender || aHotspot.gender === bHotspot.gender) continue;
      if (!areCompatibleSnapProfiles(aHotspot, bHotspot)) continue;
      const bCenter = bHotspot.center.clone().applyMatrix4(b.transform);
      if (aCenter.distanceTo(bCenter) > CONNECTOR_CENTER_ALIGNMENT_TOLERANCE_LDU) {
        continue;
      }
      const bAxis = bHotspot.axis.clone().transformDirection(b.transform).normalize();
      if (Math.abs(aAxis.dot(bAxis)) < CONNECTOR_AXIS_ALIGNMENT_DOT) continue;
      allowances.push(
        aHotspot.gender === 'M'
          ? connectorAllowanceFromHotspot(
              a.instance.instanceId,
              b.instance.instanceId,
              aHotspot,
              a.transform,
            )
          : connectorAllowanceFromHotspot(
              b.instance.instanceId,
              a.instance.instanceId,
              bHotspot,
              b.transform,
            ),
      );
    }
  }
  return allowances;
}

async function transformedCollisionBoxes(
  instanceId: string,
  partId: string,
  transform: THREE.Matrix4,
): Promise<CollisionBox[]> {
  const localBoxes = await localCollisionBoxesForPart(partId);
  return localBoxes.map((box) => ({
    instanceId,
    box: box
      .clone()
      .applyMatrix4(transform)
      .expandByScalar(COLLISION_SURFACE_PADDING_LDU),
  }));
}

async function localCollisionBoxesForPart(partId: string): Promise<THREE.Box3[]> {
  if (localCollisionBoxCache.has(partId)) {
    return localCollisionBoxCache.get(partId)!;
  }

  const promise = (async () => {
    const part = await loadPart(partId);
    const boxes: THREE.Box3[] = [];
    const vertex = new THREE.Vector3();
    const triangle = new THREE.Box3();
    part.group.updateMatrixWorld(true);
    part.group.traverse((object) => {
      const mesh = object as THREE.Mesh;
      if (!mesh.isMesh) return;
      const position = mesh.geometry.getAttribute('position');
      if (!position) return;
      const index = mesh.geometry.getIndex();
      const triangleCount = Math.floor((index?.count ?? position.count) / 3);

      for (let tri = 0; tri < triangleCount; tri += 1) {
        triangle.makeEmpty();
        for (let corner = 0; corner < 3; corner += 1) {
          const attributeIndex = index
            ? index.getX(tri * 3 + corner)
            : tri * 3 + corner;
          vertex.fromBufferAttribute(position, attributeIndex);
          triangle.expandByPoint(vertex.applyMatrix4(mesh.matrixWorld));
        }
        if (!triangle.isEmpty()) boxes.push(triangle.clone());
      }
    });

    return boxes.length > 0 ? boxes : [part.bounds.clone()];
  })();
  localCollisionBoxCache.set(partId, promise);
  return promise;
}

async function localTrianglesForPart(partId: string): Promise<THREE.Triangle[]> {
  if (localTriangleCache.has(partId)) {
    return localTriangleCache.get(partId)!;
  }

  const promise = (async () => {
    const part = await loadPart(partId);
    const triangles: THREE.Triangle[] = [];
    const vertices = [
      new THREE.Vector3(),
      new THREE.Vector3(),
      new THREE.Vector3(),
    ] as const;
    part.group.updateMatrixWorld(true);
    part.group.traverse((object) => {
      const mesh = object as THREE.Mesh;
      if (!mesh.isMesh) return;
      const position = mesh.geometry.getAttribute('position');
      if (!position) return;
      const index = mesh.geometry.getIndex();
      const triangleCount = Math.floor((index?.count ?? position.count) / 3);

      for (let tri = 0; tri < triangleCount; tri += 1) {
        for (let corner = 0; corner < 3; corner += 1) {
          const attributeIndex = index
            ? index.getX(tri * 3 + corner)
            : tri * 3 + corner;
          vertices[corner].fromBufferAttribute(position, attributeIndex);
          vertices[corner].applyMatrix4(mesh.matrixWorld);
        }
        triangles.push(
          new THREE.Triangle(
            vertices[0].clone(),
            vertices[1].clone(),
            vertices[2].clone(),
          ),
        );
      }
    });
    return triangles;
  })();
  localTriangleCache.set(partId, promise);
  return promise;
}

function overlapVolumeBetweenCollisionBoxes(
  aBoxes: CollisionBox[],
  bBoxes: CollisionBox[],
  allowances: ConnectorOverlapAllowance[],
): number {
  let volume = 0;
  const sortedB = [...bBoxes].sort((a, b) => a.box.min.x - b.box.min.x);
  for (const a of aBoxes) {
    for (const b of sortedB) {
      if (b.box.min.x > a.box.max.x) break;
      if (b.box.max.x < a.box.min.x || !a.box.intersectsBox(b.box)) continue;
      if (isAllowedConnectorOverlap(a, b, allowances)) continue;
      volume += boxOverlapVolume(a.box, b.box);
    }
  }
  return volume;
}

function isAllowedConnectorOverlap(
  a: CollisionBox,
  b: CollisionBox,
  allowances: ConnectorOverlapAllowance[],
): boolean {
  return allowances.some((allowance) => {
    const aIsConnector = a.instanceId === allowance.connectorInstanceId;
    const bIsConnector = b.instanceId === allowance.connectorInstanceId;
    if (aIsConnector && b.instanceId === allowance.counterpartInstanceId) {
      return boxFallsInsideConnectorAllowance(a.box, allowance);
    }
    if (bIsConnector && a.instanceId === allowance.counterpartInstanceId) {
      return boxFallsInsideConnectorAllowance(b.box, allowance);
    }
    return false;
  });
}

async function containmentPenalty(
  aBoxes: CollisionBox[],
  aPartId: string,
  aTransform: THREE.Matrix4,
  bBoxes: CollisionBox[],
  bPartId: string,
  bTransform: THREE.Matrix4,
  allowances: ConnectorOverlapAllowance[],
): Promise<number> {
  if (await hasContainedCollisionSample(aBoxes, bPartId, bTransform, allowances)) {
    return CONTAINMENT_PENALTY_VOLUME;
  }
  if (await hasContainedCollisionSample(bBoxes, aPartId, aTransform, allowances)) {
    return CONTAINMENT_PENALTY_VOLUME;
  }
  return 0;
}

async function hasContainedCollisionSample(
  sampleBoxes: CollisionBox[],
  solidPartId: string,
  solidTransform: THREE.Matrix4,
  allowances: ConnectorOverlapAllowance[],
): Promise<boolean> {
  const triangles = await localTrianglesForPart(solidPartId);
  if (triangles.length === 0) return false;
  const inverse = solidTransform.clone().invert();
  const localPoint = new THREE.Vector3();
  const sampleCandidates = sampleBoxes.filter(
    (sample) => !sampleFallsInsideConnectorAllowance(sample, allowances),
  );
  const step = Math.max(
    1,
    Math.floor(sampleCandidates.length / CONTAINMENT_SAMPLE_LIMIT),
  );
  let tested = 0;
  let containedHits = 0;

  for (
    let index = 0;
    index < sampleCandidates.length && tested < CONTAINMENT_SAMPLE_LIMIT;
    index += step
  ) {
    const sample = sampleCandidates[index];
    sample.box.getCenter(localPoint).applyMatrix4(inverse);
    if (pointInsideTriangles(localPoint, triangles)) {
      containedHits += 1;
      if (containedHits >= CONTAINMENT_REQUIRED_HITS) return true;
      tested += 1;
      continue;
    }
    tested += 1;
  }
  return false;
}

function sampleFallsInsideConnectorAllowance(
  sample: CollisionBox,
  allowances: ConnectorOverlapAllowance[],
): boolean {
  return allowances.some(
    (allowance) =>
      (sample.instanceId === allowance.connectorInstanceId ||
        sample.instanceId === allowance.counterpartInstanceId) &&
      boxFallsInsideConnectorAllowance(sample.box, allowance),
  );
}

function pointInsideTriangles(
  point: THREE.Vector3,
  triangles: THREE.Triangle[],
): boolean {
  const ray = new THREE.Ray(point, CONTAINMENT_RAY_DIRECTION);
  const hit = new THREE.Vector3();
  const distances: number[] = [];

  for (const triangle of triangles) {
    const intersection = ray.intersectTriangle(
      triangle.a,
      triangle.b,
      triangle.c,
      false,
      hit,
    );
    if (!intersection) continue;
    const distance = hit.clone().sub(point).dot(CONTAINMENT_RAY_DIRECTION);
    if (distance <= 1e-4) continue;
    if (distances.some((existing) => Math.abs(existing - distance) < 1e-3)) {
      continue;
    }
    distances.push(distance);
  }

  return distances.length % 2 === 1;
}

function boxFallsInsideConnectorAllowance(
  box: THREE.Box3,
  allowance: ConnectorOverlapAllowance,
): boolean {
  const center = box.getCenter(new THREE.Vector3());
  const delta = center.sub(allowance.center);
  const axis = allowance.axis.clone().normalize();
  const alongAxis = delta.dot(axis);
  const radialOffset = delta.addScaledVector(axis, -alongAxis).length();
  return (
    Math.abs(alongAxis) <= allowance.halfLength + COLLISION_SURFACE_PADDING_LDU &&
    radialOffset <= allowance.radius + COLLISION_SURFACE_PADDING_LDU
  );
}

function transformedBounds(bounds: THREE.Box3, transform: THREE.Matrix4): THREE.Box3 {
  return bounds.clone().expandByScalar(-COLLISION_MARGIN_LDU).applyMatrix4(transform);
}

function boxOverlapVolume(a: THREE.Box3, b: THREE.Box3): number {
  const intersection = a.clone().intersect(b);
  if (intersection.isEmpty()) return 0;
  const size = intersection.getSize(new THREE.Vector3());
  return Math.max(0, size.x) * Math.max(0, size.y) * Math.max(0, size.z);
}
