import * as THREE from 'three';
import { loadPart } from '../ldraw/loadPart';
import type { SnapHotspot } from '../ldraw/snapParser';
import {
  connectedInstanceIds,
  useSceneStore,
  instancePoseMatrix,
  type JointInstance,
  type PartInstance,
} from './store';
import type { InstanceHotspot } from './snapHotspots';
import {
  motorAxisInSceneFrame,
  motorPivotInSceneFrame,
  motorSpecForPart,
} from '../ldraw/motorSpecs';
import { LDU_TO_M } from '../usd/meshSerializer';
import {
  COLLISION_VOLUME_THRESHOLD,
  connectorAllowanceFromHotspot,
  overlapVolumeForPartTransforms,
  virtualConnectorAllowancesFromFemaleHotspots,
  type ConnectorOverlapAllowance,
} from './collision';

const SNAP_ALIGNMENT_TOLERANCE_LDU = 2;
const SNAP_AXIS_ALIGNMENT_DOT = 0.98;
const CONNECTOR_CENTER_ALIGNMENT_TOLERANCE_LDU = 2;
const CONNECTOR_AXIS_ALIGNMENT_DOT = 0.98;
const CURRENT_ALIGNMENT_TOLERANCE_LDU = 0.25;
const CURRENT_AXIS_ALIGNMENT_DOT = 0.99996;
const NO_CHANGE_TRANSLATION_TOLERANCE_LDU = 0.5;
const NO_CHANGE_ROTATION_TOLERANCE_DEG = 0.5;
const NO_CHANGE_SCALE_TOLERANCE = 0.001;

export type SnapPoseResult = {
  transform: THREE.Matrix4;
  sourceHotspot: SnapHotspot | null;
};

export type SnapConnectionCandidate = {
  id: string;
  label: string;
  fixed: InstanceHotspot;
  moving: InstanceHotspot;
  side?: -1 | 1;
  movingSide?: -1 | 1;
  angleDeg?: number;
  transform: THREE.Matrix4;
  assemblyTransforms: SnapConnectionTransform[];
  blocked: boolean;
  blockedReason?: string;
  overlapVolume: number;
};

export type SnapConnectionTransform = {
  instanceId: string;
  partId: string;
  transform: THREE.Matrix4;
};

type CandidateChangeMetrics = {
  maxTranslation: number;
  maxRotationDeg: number;
  maxScaleDelta: number;
};

/**
 * Pure computation of the placement transform for a new part whose hotspot
 * should align with `target`. No store mutations. Used by both the live
 * drag-preview and the commit path.
 */
export async function computeSnapPose(
  partId: string,
  target: InstanceHotspot,
): Promise<SnapPoseResult> {
  const source = await loadPart(partId);
  const srcHotspot =
    source.hotspots.find((h) => areCompatibleSnaps(target.hotspot, h)) || null;

  const transform = new THREE.Matrix4();
  if (srcHotspot) {
    const fromDir = srcHotspot.axis.clone().normalize();
    const toDir = target.worldAxis.clone().negate().normalize();
    const q = new THREE.Quaternion().setFromUnitVectors(fromDir, toDir);
    const centerAfterRotation = srcHotspot.center.clone().applyQuaternion(q);
    const side = rankedConnectionSidePairs(target)[0]?.fixedSide ?? 1;
    const desiredCenter = target.worldCenter
      .clone()
      .addScaledVector(target.worldAxis, side * snapCenterOffset(target.hotspot, srcHotspot));
    const translation = desiredCenter.sub(centerAfterRotation);
    transform.compose(translation, q, new THREE.Vector3(1, 1, 1));
  } else {
    transform.setPosition(target.worldCenter);
  }
  return { transform, sourceHotspot: srcHotspot };
}

/**
 * Commit path: compute the pose, add the part to the store, and auto-create
 * a joint in the parent's local LDU frame.
 */
export async function placePartOnSnap(
  partId: string,
  target: InstanceHotspot,
): Promise<string> {
  const { transform, sourceHotspot } = await computeSnapPose(partId, target);
  if (!sourceHotspot) {
    throw new Error('No compatible snap found for target');
  }
  const newInstanceId = useSceneStore.getState().addPart(partId, transform);
  addSnapJoint(target.instanceId, newInstanceId, target, sourceHotspot);
  return newInstanceId;
}

/**
 * Move an already-placed part so its hotspot snaps to `target`, and create
 * the matching joint. Existing joints involving this instance should be
 * removed by the caller before invoking this.
 */
export async function reattachPartOnSnap(
  instanceId: string,
  target: InstanceHotspot,
): Promise<void> {
  const inst = useSceneStore.getState().parts.find((p) => p.instanceId === instanceId);
  if (!inst) return;
  const { transform, sourceHotspot } = await computeSnapPose(inst.partId, target);
  if (!sourceHotspot) return;
  const currentScale = new THREE.Vector3(...(inst.scale ?? [1, 1, 1]));
  const p = new THREE.Vector3();
  const q = new THREE.Quaternion();
  transform.decompose(p, q, new THREE.Vector3());
  useSceneStore.getState().setTransform(
    instanceId,
    new THREE.Matrix4().compose(p, q, currentScale),
  );
  addSnapJoint(target.instanceId, instanceId, target, sourceHotspot);
}

export async function attachPartToSnap(
  instanceId: string,
  target: InstanceHotspot,
): Promise<void> {
  const inst = useSceneStore.getState().parts.find((p) => p.instanceId === instanceId);
  if (!inst || inst.instanceId === target.instanceId) return;
  const source = await loadPart(inst.partId);
  const sourceSpot = alignedCompatibleSpotForInstance(inst, source.hotspots, target);
  if (!sourceSpot) return;
  addSnapJoint(target.instanceId, instanceId, target, sourceSpot.hotspot);
}

export async function computeSnapConnectionCandidates(
  first: InstanceHotspot,
  second: InstanceHotspot,
): Promise<SnapConnectionCandidate[]> {
  if (first.instanceId === second.instanceId) return [];
  const state = useSceneStore.getState();
  const graph = makeRigidSnapGraph(state.parts, state.joints);
  const sameAssembly = rigidlyConnectedHotspots(first, second, graph);
  const sameKinematicAssembly = connectedInstanceIds(
    first.instanceId,
    state.joints,
  ).has(second.instanceId);
  if (sameKinematicAssembly) {
    const constrained = await makeConstrainedKinematicConnectionCandidates(
      first,
      second,
      graph,
      state.parts,
      state.joints,
    );
    if (constrained.length > 0) {
      return markAndSortCandidates(constrained);
    }
    const sameKinematicCandidate = makeSameAssemblyConnectionCandidate(first, second);
    return sameKinematicCandidate.blocked ? [] : [sameKinematicCandidate];
  }
  if (sameAssembly) {
    const sameAssemblyCandidate = makeSameAssemblyConnectionCandidate(first, second);
    return sameAssemblyCandidate.blocked ? [] : [sameAssemblyCandidate];
  }
  const [fixed, moving] = shouldSwapCandidateDirection(first, second, graph, state.joints)
    ? [second, first]
    : [first, second];
  const movingAssemblyIds = movableAssemblyPartIds(moving, graph, state.joints);
  const candidates: SnapConnectionCandidate[] = [];
  if (snapsAreCurrentlyAligned(fixed, moving)) {
    candidates.push(
      makeCurrentAlignmentConnectionCandidate(
        fixed,
        moving,
        movingAssemblyIds,
      ),
    );
  }
  for (const side of rankedConnectionSidePairs(fixed, moving)) {
    for (const angleDeg of [0, 90, 180, 270]) {
      candidates.push(
        makeConnectionCandidate(
          fixed,
          moving,
          side.fixedSide,
          side.movingSide,
          side.label,
          angleDeg,
        ),
      );
    }
  }
  return markAndSortCandidates(candidates);
}

export function snapCandidateRequiresTransformChange(
  candidate: SnapConnectionCandidate,
): boolean {
  const metrics = candidateChangeMetrics(candidate);
  return (
    metrics.maxTranslation > NO_CHANGE_TRANSLATION_TOLERANCE_LDU ||
    metrics.maxRotationDeg > NO_CHANGE_ROTATION_TOLERANCE_DEG ||
    metrics.maxScaleDelta > NO_CHANGE_SCALE_TOLERANCE
  );
}

export function connectSnapCandidate(candidate: SnapConnectionCandidate): boolean {
  if (candidate.blocked) return false;
  const state = useSceneStore.getState();
  const liveCandidate = rebaseCandidateToCurrentState(candidate, state);
  if (!liveCandidate) return false;
  const alignmentBlockReason = candidateAlignmentBlockReason(liveCandidate, state.parts);
  if (alignmentBlockReason) {
    console.warn('snap candidate no longer aligns', alignmentBlockReason);
    return false;
  }

  state.setTransforms(
    liveCandidate.assemblyTransforms.map((placement) => ({
      instanceId: placement.instanceId,
      transform: placement.transform,
    })),
  );
  addSnapJoint(
    liveCandidate.fixed.instanceId,
    liveCandidate.moving.instanceId,
    liveCandidate.fixed,
    liveCandidate.moving.hotspot,
  );
  state.selectInstance(liveCandidate.moving.instanceId);
  return true;
}

function makeConnectionCandidate(
  fixed: InstanceHotspot,
  moving: InstanceHotspot,
  side: -1 | 1,
  movingSide: -1 | 1,
  sideLabel: string,
  angleDeg: number,
): SnapConnectionCandidate {
  const state = useSceneStore.getState();
  const graph = makeRigidSnapGraph(state.parts, state.joints);
  const movingAssemblyIds = movableAssemblyPartIds(moving, graph, state.joints);
  const movingInst = state.parts.find((part) => part.instanceId === moving.instanceId);
  const transform =
    movingAssemblyIds.has(fixed.instanceId) || !movingInst
      ? movingInst
        ? instancePoseMatrix(movingInst)
        : new THREE.Matrix4()
      : computeExactSnapPose(moving, fixed, side, movingSide, angleDeg);
  const assemblyTransforms = movingInst
    ? computeAssemblyTransforms(state.parts, movingAssemblyIds, movingInst, transform)
    : [];
  const blockedReason = connectionBlockReason(
    fixed,
    moving,
    movingAssemblyIds,
    state.joints,
  ) ?? connectionAlignmentBlockReason(
    fixed,
    moving,
    side,
    movingSide,
    assemblyTransforms,
    state.parts,
  );
  return {
    id: `${side}:${movingSide}:${angleDeg}`,
    label: `${sideLabel} - ${angleDeg} deg`,
    fixed,
    moving,
    side,
    movingSide,
    angleDeg,
    transform,
    assemblyTransforms,
    blocked: Boolean(blockedReason),
    blockedReason,
    overlapVolume: 0,
  };
}

function makeSameAssemblyConnectionCandidate(
  fixed: InstanceHotspot,
  moving: InstanceHotspot,
): SnapConnectionCandidate {
  const state = useSceneStore.getState();
  const graph = makeRigidSnapGraph(state.parts, state.joints);
  const movingAssemblyIds = rigidAssemblyPartIds(moving, graph);
  return makeCurrentAlignmentConnectionCandidate(
    fixed,
    moving,
    movingAssemblyIds,
    true,
  );
}

function makeCurrentAlignmentConnectionCandidate(
  fixed: InstanceHotspot,
  moving: InstanceHotspot,
  movingAssemblyIds: Set<string>,
  allowSameConnectedAssembly = false,
): SnapConnectionCandidate {
  const state = useSceneStore.getState();
  const movingInst = state.parts.find((part) => part.instanceId === moving.instanceId);
  const transform = movingInst ? instancePoseMatrix(movingInst) : new THREE.Matrix4();
  const assemblyTransforms = movingInst
    ? state.parts
        .filter((part) => movingAssemblyIds.has(part.instanceId))
        .map((part) => ({
          instanceId: part.instanceId,
          partId: part.partId,
          transform: instancePoseMatrix(part),
        }))
    : [];
  const blockedReason = currentAlignmentConnectionBlockReason(
    fixed,
    moving,
    movingAssemblyIds,
    allowSameConnectedAssembly,
  );

  return {
    id: 'current',
    label: 'Current alignment',
    fixed,
    moving,
    side: preferredConnectionSide(fixed, moving),
    movingSide: moving.preferredSide,
    transform,
    assemblyTransforms,
    blocked: Boolean(blockedReason),
    blockedReason,
    overlapVolume: 0,
  };
}

async function makeConstrainedKinematicConnectionCandidates(
  first: InstanceHotspot,
  second: InstanceHotspot,
  graph: RigidSnapGraph,
  parts: PartInstance[],
  joints: JointInstance[],
): Promise<SnapConnectionCandidate[]> {
  if (isMotorOutputSpot(first) === isMotorOutputSpot(second)) {
    return [];
  }

  const fixed = isMotorOutputSpot(first) ? first : second;
  const moving = fixed === first ? second : first;
  const movingInst = parts.find((part) => part.instanceId === moving.instanceId);
  if (!movingInst) return [];

  const movingAssemblyIds = rigidAssemblyPartIds(moving, graph);
  const anchor = await motorOutputAnchorForAssembly(
    fixed,
    moving,
    movingAssemblyIds,
    graph,
    parts,
    joints,
  );
  if (!anchor) return [];

  const candidates: SnapConnectionCandidate[] = [];
  for (const side of rankedConnectionSidePairs(fixed, moving)) {
    candidates.push(
      makeConstrainedRotationCandidate(
        fixed,
        moving,
        movingInst,
        movingAssemblyIds,
        anchor,
        side.fixedSide,
        side.movingSide,
        side.label,
        joints,
      ),
    );
  }
  return candidates;
}

function makeConstrainedRotationCandidate(
  fixed: InstanceHotspot,
  moving: InstanceHotspot,
  movingInst: PartInstance,
  movingAssemblyIds: Set<string>,
  anchor: KinematicAnchor,
  side: -1 | 1,
  movingSide: -1 | 1,
  sideLabel: string,
  joints: JointInstance[],
): SnapConnectionCandidate {
  const alignment = computeConstrainedRotationTransform(
    fixed,
    moving,
    movingInst,
    anchor,
    side,
    movingSide,
  );
  const transform = alignment?.transform ?? instancePoseMatrix(movingInst);
  const assemblyTransforms = computeAssemblyTransforms(
    useSceneStore.getState().parts,
    movingAssemblyIds,
    movingInst,
    transform,
  );
  const blockedReason =
    constrainedConnectionBlockReason(
      fixed,
      moving,
      movingAssemblyIds,
      joints,
      anchor.jointId,
    ) ??
    alignment?.blockedReason ??
    connectionAlignmentBlockReason(
      fixed,
      moving,
      side,
      movingSide,
      assemblyTransforms,
      useSceneStore.getState().parts,
    );

  return {
    id: `constrained:${side}:${movingSide}`,
    label: `Rotate from existing snap - ${sideLabel}`,
    fixed,
    moving,
    side,
    movingSide,
    transform,
    assemblyTransforms,
    blocked: Boolean(blockedReason),
    blockedReason,
    overlapVolume: 0,
  };
}

async function markCandidateCollision(
  candidate: SnapConnectionCandidate,
): Promise<SnapConnectionCandidate> {
  if (candidate.blocked) return candidate;
  const state = useSceneStore.getState();
  const assemblyIds = new Set(
    candidate.assemblyTransforms.map((placement) => placement.instanceId),
  );
  const others = state.parts.filter((inst) => !assemblyIds.has(inst.instanceId));
  const allowances = await connectorAllowancesForCandidate(candidate, others);
  const overlapVolume = await overlapVolumeForPartTransforms(
    candidate.assemblyTransforms,
    others,
    allowances,
  );
  const blocked = overlapVolume > COLLISION_VOLUME_THRESHOLD;

  return {
    ...candidate,
    overlapVolume,
    blocked,
    blockedReason: blocked ? 'Overlaps existing parts' : candidate.blockedReason,
  };
}

async function markAndSortCandidates(
  candidates: SnapConnectionCandidate[],
): Promise<SnapConnectionCandidate[]> {
  const marked = await Promise.all(candidates.map(markCandidateCollision));
  return marked
    .filter((candidate) => !candidate.blocked)
    .sort(compareSnapConnectionCandidates);
}

function compareSnapConnectionCandidates(
  a: SnapConnectionCandidate,
  b: SnapConnectionCandidate,
): number {
  const preferredSide =
    preferredConnectionSide(a.fixed, a.moving) ??
    preferredConnectionSide(b.fixed, b.moving);
  if (preferredSide !== undefined && a.side !== b.side) {
    if (a.side === preferredSide) return -1;
    if (b.side === preferredSide) return 1;
  }
  const preferredMovingSide = a.moving.preferredSide ?? b.moving.preferredSide;
  if (preferredMovingSide !== undefined && a.movingSide !== b.movingSide) {
    if (a.movingSide === preferredMovingSide) return -1;
    if (b.movingSide === preferredMovingSide) return 1;
  }

  const aChange = candidateChangeMetrics(a);
  const bChange = candidateChangeMetrics(b);
  return (
    aChange.maxTranslation - bChange.maxTranslation ||
    aChange.maxRotationDeg - bChange.maxRotationDeg ||
    aChange.maxScaleDelta - bChange.maxScaleDelta ||
    a.label.localeCompare(b.label)
  );
}

function candidateChangeMetrics(
  candidate: SnapConnectionCandidate,
): CandidateChangeMetrics {
  const partsById = new Map(
    useSceneStore.getState().parts.map((part) => [part.instanceId, part]),
  );
  const metrics: CandidateChangeMetrics = {
    maxTranslation: 0,
    maxRotationDeg: 0,
    maxScaleDelta: 0,
  };

  for (const placement of candidate.assemblyTransforms) {
    const part = partsById.get(placement.instanceId);
    if (!part) continue;

    const currentPosition = new THREE.Vector3();
    const currentRotation = new THREE.Quaternion();
    const currentScale = new THREE.Vector3();
    instancePoseMatrix(part).decompose(
      currentPosition,
      currentRotation,
      currentScale,
    );

    const nextPosition = new THREE.Vector3();
    const nextRotation = new THREE.Quaternion();
    const nextScale = new THREE.Vector3();
    placement.transform.decompose(nextPosition, nextRotation, nextScale);

    const rotationDot = Math.abs(
      THREE.MathUtils.clamp(currentRotation.dot(nextRotation), -1, 1),
    );
    const rotationDeg = THREE.MathUtils.radToDeg(2 * Math.acos(rotationDot));
    const scaleDelta = Math.max(
      Math.abs(currentScale.x - nextScale.x),
      Math.abs(currentScale.y - nextScale.y),
      Math.abs(currentScale.z - nextScale.z),
    );

    metrics.maxTranslation = Math.max(
      metrics.maxTranslation,
      currentPosition.distanceTo(nextPosition),
    );
    metrics.maxRotationDeg = Math.max(metrics.maxRotationDeg, rotationDeg);
    metrics.maxScaleDelta = Math.max(metrics.maxScaleDelta, scaleDelta);
  }

  return metrics;
}

function alignedCompatibleSpotForInstance(
  inst: PartInstance,
  hotspots: SnapHotspot[],
  target: InstanceHotspot,
): InstanceHotspot | null {
  const transform = instancePoseMatrix(inst);
  const aligned = hotspots
    .filter((hotspot) => areCompatibleSnaps(target.hotspot, hotspot))
    .map((hotspot) => {
      const currentHotspot = currentHotspotForPart(inst.instanceId, inst.partId, hotspot);
      const worldCenter = currentHotspot.center.clone().applyMatrix4(transform);
      const worldAxis = currentHotspot.axis.clone().transformDirection(transform).normalize();
      const spot: InstanceHotspot = {
        instanceId: inst.instanceId,
        partId: inst.partId,
        hotspot,
        worldCenter,
        worldAxis,
      };
      return {
        spot,
        distance: snapCenterDistance(target, spot),
      };
    })
    .filter(({ spot }) =>
      snapsAreAligned(
        target,
        spot,
        spot.worldCenter,
        spot.worldAxis,
        CURRENT_ALIGNMENT_TOLERANCE_LDU,
        CURRENT_AXIS_ALIGNMENT_DOT,
      ),
    )
    .sort((a, b) => a.distance - b.distance);

  return aligned[0]?.spot ?? null;
}

function snapCenterDistance(fixed: InstanceHotspot, moving: InstanceHotspot): number {
  const fixedAxis = fixed.worldAxis.clone().normalize();
  const expectedOffset = snapCenterOffset(fixed.hotspot, moving.hotspot);
  const delta = moving.worldCenter.clone().sub(fixed.worldCenter);
  const alongAxis = delta.dot(fixedAxis);
  const radialOffset = delta.addScaledVector(fixedAxis, -alongAxis).length();
  return radialOffset + Math.abs(Math.abs(alongAxis) - expectedOffset);
}

function rebaseCandidateToCurrentState(
  candidate: SnapConnectionCandidate,
  state: ReturnType<typeof useSceneStore.getState>,
): SnapConnectionCandidate | null {
  const fixed = refreshSpotFromCurrentState(candidate.fixed, state.parts);
  const moving = refreshSpotFromCurrentState(candidate.moving, state.parts);
  if (!fixed || !moving) return null;

  if (
    candidate.angleDeg !== undefined &&
    candidate.side !== undefined &&
    candidate.movingSide !== undefined
  ) {
    const graph = makeRigidSnapGraph(state.parts, state.joints);
    const movingAssemblyIds = movableAssemblyPartIds(moving, graph, state.joints);
    const movingInst = state.parts.find((part) => part.instanceId === moving.instanceId);
    if (!movingInst) return null;
    const transform =
      movingAssemblyIds.has(fixed.instanceId)
        ? instancePoseMatrix(movingInst)
        : computeExactSnapPose(
            moving,
            fixed,
            candidate.side,
            candidate.movingSide,
            candidate.angleDeg,
          );
    return {
      ...candidate,
      fixed,
      moving,
      transform,
      assemblyTransforms: computeAssemblyTransforms(
        state.parts,
        movingAssemblyIds,
        movingInst,
        transform,
      ),
    };
  }

  if (candidate.id === 'current') {
    const graph = makeRigidSnapGraph(state.parts, state.joints);
    const movingAssemblyIds = movableAssemblyPartIds(moving, graph, state.joints);
    const movingInst = state.parts.find((part) => part.instanceId === moving.instanceId);
    if (!movingInst) return null;
    const transform = instancePoseMatrix(movingInst);
    return {
      ...candidate,
      fixed,
      moving,
      transform,
      assemblyTransforms: state.parts
        .filter((part) => movingAssemblyIds.has(part.instanceId))
        .map((part) => ({
          instanceId: part.instanceId,
          partId: part.partId,
          transform: instancePoseMatrix(part),
        })),
    };
  }

  return {
    ...candidate,
    fixed,
    moving,
  };
}

function refreshSpotFromCurrentState(
  spot: InstanceHotspot,
  parts: PartInstance[],
): InstanceHotspot | null {
  const inst = parts.find((part) => part.instanceId === spot.instanceId);
  if (!inst) return null;
  const transform = instancePoseMatrix(inst);
  const currentHotspot = currentHotspotForPart(inst.instanceId, inst.partId, spot.hotspot);
  return {
    ...spot,
    worldCenter: currentHotspot.center.clone().applyMatrix4(transform),
    worldAxis: currentHotspot.axis.clone().transformDirection(transform).normalize(),
  };
}

function areOppositeGenderSnaps(a: SnapHotspot, b: SnapHotspot): boolean {
  return Boolean(a.gender && b.gender && a.gender !== b.gender);
}

function areFemaleFemaleSnaps(a: SnapHotspot, b: SnapHotspot): boolean {
  return a.gender === 'F' && b.gender === 'F';
}

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

function areCompatibleSnaps(a: SnapHotspot, b: SnapHotspot): boolean {
  return (
    areCompatibleSnapProfiles(a, b) &&
    (areOppositeGenderSnaps(a, b) || areFemaleFemaleSnaps(a, b))
  );
}

async function connectorAllowancesForCandidate(
  candidate: SnapConnectionCandidate,
  others: PartInstance[],
): Promise<ConnectorOverlapAllowance[]> {
  if (!areCompatibleSnaps(candidate.fixed.hotspot, candidate.moving.hotspot)) {
    return [];
  }
  const state = useSceneStore.getState();
  const transforms = new Map(
    candidate.assemblyTransforms.map((placement) => [
      placement.instanceId,
      placement.transform,
    ]),
  );
  const movingParts = candidate.assemblyTransforms;
  const stationaryParts = others
    .filter((part) => part.instanceId === candidate.fixed.instanceId)
    .map((part) => ({ ...part, transform: instancePoseMatrix(part) }));
  const allowances: ConnectorOverlapAllowance[] = [];

  for (const movingPart of movingParts) {
    const movingLoaded = await loadPart(movingPart.partId);
    const movingTransform = transforms.get(movingPart.instanceId);
    if (!movingTransform) continue;

    for (const stationaryPart of stationaryParts) {
      const stationaryLoaded = await loadPart(stationaryPart.partId);
      allowances.push(
        ...alignedConnectorAllowancesForParts(
          movingPart.instanceId,
          movingLoaded.hotspots,
          movingTransform,
          stationaryPart.instanceId,
          stationaryLoaded.hotspots,
          stationaryPart.transform,
        ),
      );
    }
  }

  // Keep the selected connector explicitly allowed even when the visual
  // hotspot centers have tiny numerical drift.
  allowances.push(...selectedConnectorAllowancesForCandidate(candidate, state.parts));
  return allowances;
}

function selectedConnectorAllowancesForCandidate(
  candidate: SnapConnectionCandidate,
  parts: PartInstance[],
): ConnectorOverlapAllowance[] {
  const fixedInst = parts.find((part) => part.instanceId === candidate.fixed.instanceId);
  const movingTransform = candidate.assemblyTransforms.find(
    (placement) => placement.instanceId === candidate.moving.instanceId,
  )?.transform;
  if (!fixedInst || !movingTransform) return [];
  const fixedTransform = instancePoseMatrix(fixedInst);
  const fixedHotspot = currentHotspotForSpot(candidate.fixed);
  const movingHotspot = currentHotspotForSpot(candidate.moving);

  if (areFemaleFemaleSnaps(candidate.fixed.hotspot, candidate.moving.hotspot)) {
    return virtualConnectorAllowancesFromFemaleHotspots(
      candidate.fixed.instanceId,
      fixedHotspot,
      fixedTransform,
      candidate.moving.instanceId,
      movingHotspot,
      movingTransform,
    );
  }

  if (candidate.moving.hotspot.gender === 'M') {
    return [
      connectorAllowanceFromHotspot(
        candidate.moving.instanceId,
        candidate.fixed.instanceId,
        movingHotspot,
        movingTransform,
      ),
    ];
  }

  return [
    connectorAllowanceFromHotspot(
      candidate.fixed.instanceId,
      candidate.moving.instanceId,
      fixedHotspot,
      fixedTransform,
    ),
  ];
}

function alignedConnectorAllowancesForParts(
  aInstanceId: string,
  aHotspots: SnapHotspot[],
  aTransform: THREE.Matrix4,
  bInstanceId: string,
  bHotspots: SnapHotspot[],
  bTransform: THREE.Matrix4,
): ConnectorOverlapAllowance[] {
  const allowances: ConnectorOverlapAllowance[] = [];
  for (const aHotspot of aHotspots) {
    if (!aHotspot.gender) continue;
    const aCenter = aHotspot.center.clone().applyMatrix4(aTransform);
    const aAxis = aHotspot.axis.clone().transformDirection(aTransform).normalize();
    for (const bHotspot of bHotspots) {
      if (!bHotspot.gender || aHotspot.gender === bHotspot.gender) continue;
      if (!areCompatibleSnapProfiles(aHotspot, bHotspot)) continue;
      const bCenter = bHotspot.center.clone().applyMatrix4(bTransform);
      if (aCenter.distanceTo(bCenter) > CONNECTOR_CENTER_ALIGNMENT_TOLERANCE_LDU) {
        continue;
      }
      const bAxis = bHotspot.axis.clone().transformDirection(bTransform).normalize();
      if (Math.abs(aAxis.dot(bAxis)) < CONNECTOR_AXIS_ALIGNMENT_DOT) continue;
      allowances.push(
        aHotspot.gender === 'M'
          ? connectorAllowanceFromHotspot(
              aInstanceId,
              bInstanceId,
              aHotspot,
              aTransform,
            )
          : connectorAllowanceFromHotspot(
              bInstanceId,
              aInstanceId,
              bHotspot,
              bTransform,
            ),
      );
    }
  }
  return allowances;
}

function computeAssemblyTransforms(
  parts: PartInstance[],
  assemblyIds: Set<string>,
  movingInst: PartInstance,
  movingTransform: THREE.Matrix4,
): SnapConnectionTransform[] {
  const originalMovingTransform = instancePoseMatrix(movingInst);
  const delta = new THREE.Matrix4().multiplyMatrices(
    movingTransform,
    originalMovingTransform.clone().invert(),
  );
  return parts
    .filter((part) => assemblyIds.has(part.instanceId))
    .map((part) => ({
      instanceId: part.instanceId,
      partId: part.partId,
      transform: new THREE.Matrix4().multiplyMatrices(delta, instancePoseMatrix(part)),
    }));
}

function connectionBlockReason(
  fixed: InstanceHotspot,
  moving: InstanceHotspot,
  movingAssemblyIds: Set<string>,
  joints: JointInstance[],
): string | undefined {
  if (movingAssemblyIds.has(fixed.instanceId)) {
    return 'Already in the same connected assembly';
  }
  if (!areCompatibleSnaps(fixed.hotspot, moving.hotspot)) {
    return 'Incompatible snap types';
  }
  if (isHotspotConnected(fixed)) {
    return 'Fixed snap is already connected';
  }
  if (isHotspotConnected(moving)) {
    return 'Moving snap is already connected';
  }
  if (wouldBreakExistingConnection(movingAssemblyIds, joints)) {
    return 'Would break existing connection';
  }
  return undefined;
}

function constrainedConnectionBlockReason(
  fixed: InstanceHotspot,
  moving: InstanceHotspot,
  movingAssemblyIds: Set<string>,
  joints: JointInstance[],
  anchorJointId: string,
): string | undefined {
  if (!areCompatibleSnaps(fixed.hotspot, moving.hotspot)) {
    return 'Incompatible snap types';
  }
  if (isHotspotConnected(fixed)) {
    return 'Fixed snap is already connected';
  }
  if (isHotspotConnected(moving)) {
    return 'Moving snap is already connected';
  }
  if (wouldBreakExistingConnectionExcept(movingAssemblyIds, joints, anchorJointId)) {
    return 'Would break existing connection';
  }
  return undefined;
}

function currentAlignmentConnectionBlockReason(
  fixed: InstanceHotspot,
  moving: InstanceHotspot,
  movingAssemblyIds: Set<string>,
  allowSameConnectedAssembly: boolean,
): string | undefined {
  if (!areCompatibleSnaps(fixed.hotspot, moving.hotspot)) {
    return 'Incompatible snap types';
  }
  if (isHotspotConnected(fixed)) {
    return 'Fixed snap is already connected';
  }
  if (isHotspotConnected(moving)) {
    return 'Moving snap is already connected';
  }
  if (!snapsAreCurrentlyAligned(fixed, moving)) {
    return 'Would break existing assembly';
  }
  if (!selectedSidesFaceEachOther(fixed, moving)) {
    return 'Would break existing assembly';
  }
  if (!allowSameConnectedAssembly && movingAssemblyIds.has(fixed.instanceId)) {
    return 'Already in the same connected assembly';
  }
  return undefined;
}

function selectedSidesFaceEachOther(
  fixed: InstanceHotspot,
  moving: InstanceHotspot,
): boolean {
  if (fixed.preferredSide === undefined || moving.preferredSide === undefined) {
    return true;
  }
  const expectedDot = -fixed.preferredSide * moving.preferredSide;
  return (
    fixed.worldAxis.clone().normalize().dot(moving.worldAxis.clone().normalize()) *
      expectedDot >=
    CURRENT_AXIS_ALIGNMENT_DOT
  );
}

function snapsAreCurrentlyAligned(
  fixed: InstanceHotspot,
  moving: InstanceHotspot,
): boolean {
  return snapsAreAligned(
    fixed,
    moving,
    moving.worldCenter,
    moving.worldAxis,
    CURRENT_ALIGNMENT_TOLERANCE_LDU,
    CURRENT_AXIS_ALIGNMENT_DOT,
  );
}

function snapsAreAligned(
  fixed: InstanceHotspot,
  moving: InstanceHotspot,
  movingCenter: THREE.Vector3,
  movingAxis: THREE.Vector3,
  positionTolerance = SNAP_ALIGNMENT_TOLERANCE_LDU,
  axisDotThreshold = SNAP_AXIS_ALIGNMENT_DOT,
): boolean {
  const fixedAxis = fixed.worldAxis.clone().normalize();
  const normalizedMovingAxis = movingAxis.clone().normalize();
  if (Math.abs(normalizedMovingAxis.dot(fixedAxis)) < axisDotThreshold) {
    return false;
  }

  const expectedOffset = snapCenterOffset(fixed.hotspot, moving.hotspot);
  const delta = movingCenter.clone().sub(fixed.worldCenter);
  const alongAxis = delta.dot(fixedAxis);
  const radialOffset = delta.addScaledVector(fixedAxis, -alongAxis).length();
  if (radialOffset > positionTolerance) {
    return false;
  }

  if (expectedOffset === 0) {
    return Math.abs(alongAxis) <= positionTolerance;
  }
  return Math.abs(Math.abs(alongAxis) - expectedOffset) <= positionTolerance;
}

function connectionAlignmentBlockReason(
  fixed: InstanceHotspot,
  moving: InstanceHotspot,
  side: -1 | 1,
  movingSide: -1 | 1,
  assemblyTransforms: SnapConnectionTransform[],
  parts: PartInstance[],
): string | undefined {
  const finalFixed = finalSpotWorld(fixed, assemblyTransforms, parts);
  const finalMoving = finalSpotWorld(moving, assemblyTransforms, parts);
  if (!finalFixed || !finalMoving) return 'Snaps do not meet';
  return snapWorldsMeetWithSides(
    fixed.hotspot,
    moving.hotspot,
    finalFixed,
    finalMoving,
    side,
    movingSide,
  );
}

function candidateAlignmentBlockReason(
  candidate: SnapConnectionCandidate,
  parts: PartInstance[],
): string | undefined {
  const finalFixed = finalSpotWorld(candidate.fixed, candidate.assemblyTransforms, parts);
  const finalMoving = finalSpotWorld(candidate.moving, candidate.assemblyTransforms, parts);
  if (!finalFixed || !finalMoving) return 'Snaps do not meet';
  if (
    candidate.side !== undefined &&
    candidate.movingSide !== undefined
  ) {
    return snapWorldsMeetWithSides(
      candidate.fixed.hotspot,
      candidate.moving.hotspot,
      finalFixed,
      finalMoving,
      candidate.side,
      candidate.movingSide,
    );
  }

  const refreshedFixed: InstanceHotspot = {
    ...candidate.fixed,
    worldCenter: finalFixed.center,
    worldAxis: finalFixed.axis,
  };
  const refreshedMoving: InstanceHotspot = {
    ...candidate.moving,
    worldCenter: finalMoving.center,
    worldAxis: finalMoving.axis,
  };
  return snapsAreAligned(
    refreshedFixed,
    refreshedMoving,
    finalMoving.center,
    finalMoving.axis,
    CURRENT_ALIGNMENT_TOLERANCE_LDU,
    CURRENT_AXIS_ALIGNMENT_DOT,
  )
    ? undefined
    : 'Snaps do not meet';
}

function finalSpotWorld(
  spot: InstanceHotspot,
  assemblyTransforms: SnapConnectionTransform[],
  parts: PartInstance[],
): WorldHotspot | null {
  const inst = parts.find((part) => part.instanceId === spot.instanceId);
  if (!inst) return null;
  const transform =
    assemblyTransforms.find((placement) => placement.instanceId === spot.instanceId)
      ?.transform ?? instancePoseMatrix(inst);
  const currentHotspot = currentHotspotForPart(inst.instanceId, inst.partId, spot.hotspot);
  return {
    center: currentHotspot.center.clone().applyMatrix4(transform),
    axis: currentHotspot.axis.clone().transformDirection(transform).normalize(),
  };
}

function snapWorldsMeetWithSides(
  fixedHotspot: SnapHotspot,
  movingHotspot: SnapHotspot,
  fixed: WorldHotspot,
  moving: WorldHotspot,
  side: -1 | 1,
  movingSide: -1 | 1,
): string | undefined {
  const fixedAxis = fixed.axis.clone().normalize();
  const movingAxis = moving.axis.clone().normalize();
  const expectedMovingCenter = fixed.center
    .clone()
    .addScaledVector(fixedAxis, side * snapCenterOffset(fixedHotspot, movingHotspot));
  if (moving.center.distanceTo(expectedMovingCenter) > SNAP_ALIGNMENT_TOLERANCE_LDU) {
    return 'Snaps do not meet';
  }

  const expectedDot = -side * movingSide;
  if (fixedAxis.dot(movingAxis) * expectedDot < SNAP_AXIS_ALIGNMENT_DOT) {
    return 'Snaps do not face each other';
  }

  return undefined;
}

function isHotspotConnected(spot: InstanceHotspot): boolean {
  const state = useSceneStore.getState();
  return state.joints.some(
    (joint) =>
      (joint.parentInstance === spot.instanceId &&
        joint.parentHotspotId === spot.hotspot.id) ||
      (joint.childInstance === spot.instanceId &&
        joint.childHotspotId === spot.hotspot.id),
  );
}

function preferredConnectionSide(
  fixed: InstanceHotspot,
  moving?: InstanceHotspot,
): -1 | 1 | undefined {
  return fixed.preferredSide ?? moving?.preferredSide;
}

type ConnectionSidePair = {
  fixedSide: -1 | 1;
  movingSide: -1 | 1;
  label: string;
};

function rankedConnectionSidePairs(
  fixed: InstanceHotspot,
  moving?: InstanceHotspot,
): ConnectionSidePair[] {
  const fixedPreferredSide = fixed.preferredSide;
  const movingPreferredSide = moving?.preferredSide;
  if (fixedPreferredSide !== undefined && movingPreferredSide !== undefined) {
    return [
      {
        fixedSide: fixedPreferredSide,
        movingSide: movingPreferredSide,
        label: 'Clicked sides',
      },
    ];
  }

  if (fixedPreferredSide !== undefined) {
    return [
      {
        fixedSide: fixedPreferredSide,
        movingSide: fixedPreferredSide,
        label: 'Clicked side',
      },
      {
        fixedSide: fixedPreferredSide,
        movingSide: fixedPreferredSide === 1 ? -1 : 1,
        label: 'Clicked side - flipped',
      },
    ];
  }

  if (movingPreferredSide !== undefined) {
    return [
      {
        fixedSide: movingPreferredSide,
        movingSide: movingPreferredSide,
        label: 'Clicked side',
      },
      {
        fixedSide: movingPreferredSide === 1 ? -1 : 1,
        movingSide: movingPreferredSide,
        label: 'Clicked side - flipped',
      },
    ];
  }

  const fixedInst = useSceneStore
    .getState()
    .parts.find((p) => p.instanceId === fixed.instanceId);
  if (!fixedInst) {
    return [
      { fixedSide: -1, movingSide: -1, label: 'Side A' },
      { fixedSide: 1, movingSide: 1, label: 'Side B' },
    ];
  }

  const fixedOrigin = new THREE.Vector3(...fixedInst.position);
  const projection = fixed.worldAxis.dot(fixed.worldCenter.clone().sub(fixedOrigin));
  if (Math.abs(projection) < 1) {
    return [
      { fixedSide: -1, movingSide: -1, label: 'Side A' },
      { fixedSide: 1, movingSide: 1, label: 'Side B' },
    ];
  }

  const outerSide: -1 | 1 = projection > 0 ? 1 : -1;
  return [
    { fixedSide: outerSide, movingSide: outerSide, label: 'Outer side' },
    {
      fixedSide: outerSide === 1 ? -1 : 1,
      movingSide: outerSide === 1 ? -1 : 1,
      label: 'Inner side',
    },
  ];
}

function computeExactSnapPose(
  moving: InstanceHotspot,
  fixed: InstanceHotspot,
  side: -1 | 1,
  movingSide: -1 | 1,
  angleDeg: number,
): THREE.Matrix4 {
  const inst = useSceneStore
    .getState()
    .parts.find((p) => p.instanceId === moving.instanceId);
  if (!inst) return new THREE.Matrix4();

  const current = instancePoseMatrix(inst);
  const currentRotation = new THREE.Quaternion();
  const currentScale = new THREE.Vector3();
  current.decompose(new THREE.Vector3(), currentRotation, currentScale);

  const movingHotspot = currentHotspotForSpot(moving);
  const fromDir = moving.worldAxis.clone().normalize();
  const toDir = fixed.worldAxis
    .clone()
    .multiplyScalar(-side * movingSide)
    .normalize();
  const deltaRotation = new THREE.Quaternion().setFromUnitVectors(fromDir, toDir);
  const spin = new THREE.Quaternion().setFromAxisAngle(
    fixed.worldAxis.clone().normalize(),
    THREE.MathUtils.degToRad(angleDeg),
  );
  const nextRotation = spin.multiply(deltaRotation).multiply(currentRotation);
  const centerAfterRotation = movingHotspot.center
    .clone()
    .multiply(currentScale)
    .applyQuaternion(nextRotation);
  const centerOffset = snapCenterOffset(fixed.hotspot, moving.hotspot);
  const desiredCenter = fixed.worldCenter
    .clone()
    .addScaledVector(fixed.worldAxis, side * centerOffset);
  const translation = desiredCenter.sub(centerAfterRotation);

  return new THREE.Matrix4().compose(
    translation,
    nextRotation,
    currentScale,
  );
}

function computeConstrainedRotationTransform(
  fixed: InstanceHotspot,
  moving: InstanceHotspot,
  movingInst: PartInstance,
  anchor: KinematicAnchor,
  side: -1 | 1,
  movingSide: -1 | 1,
): { transform: THREE.Matrix4; blockedReason?: string } | null {
  const fixedAxis = fixed.worldAxis.clone().normalize();
  const movingAxis = moving.worldAxis.clone().normalize();
  const expectedDot = -side * movingSide;
  if (fixedAxis.dot(movingAxis) * expectedDot < SNAP_AXIS_ALIGNMENT_DOT) {
    return { transform: instancePoseMatrix(movingInst), blockedReason: 'Would break existing assembly' };
  }

  const desiredCenter = fixed.worldCenter
    .clone()
    .addScaledVector(fixedAxis, side * snapCenterOffset(fixed.hotspot, moving.hotspot));
  const pivot = anchor.pivot.clone();
  const axis = anchor.axis.clone().normalize();
  const from = moving.worldCenter.clone().sub(pivot);
  const to = desiredCenter.clone().sub(pivot);
  const fromAlong = from.dot(axis);
  const toAlong = to.dot(axis);
  if (Math.abs(fromAlong - toAlong) > SNAP_ALIGNMENT_TOLERANCE_LDU) {
    return { transform: instancePoseMatrix(movingInst), blockedReason: 'Would break existing assembly' };
  }

  const fromRadial = from.clone().addScaledVector(axis, -fromAlong);
  const toRadial = to.clone().addScaledVector(axis, -toAlong);
  if (fromRadial.length() < 1 || toRadial.length() < 1) {
    return { transform: instancePoseMatrix(movingInst), blockedReason: 'Would break existing assembly' };
  }

  fromRadial.normalize();
  toRadial.normalize();
  const signedAngle = Math.atan2(
    axis.dot(new THREE.Vector3().crossVectors(fromRadial, toRadial)),
    THREE.MathUtils.clamp(fromRadial.dot(toRadial), -1, 1),
  );
  const rotation = new THREE.Matrix4().makeRotationAxis(axis, signedAngle);
  const operation = new THREE.Matrix4()
    .makeTranslation(pivot.x, pivot.y, pivot.z)
    .multiply(rotation)
    .multiply(new THREE.Matrix4().makeTranslation(-pivot.x, -pivot.y, -pivot.z));
  const transform = new THREE.Matrix4().multiplyMatrices(
    operation,
    instancePoseMatrix(movingInst),
  );

  if (!snapsWouldAlignUnderTransform(fixed, moving, transform)) {
    return { transform, blockedReason: 'Would break existing assembly' };
  }
  return { transform };
}

function snapsWouldAlignUnderTransform(
  fixed: InstanceHotspot,
  moving: InstanceHotspot,
  movingTransform: THREE.Matrix4,
): boolean {
  const movingHotspot = currentHotspotForSpot(moving);
  const movingCenter = movingHotspot.center.clone().applyMatrix4(movingTransform);
  const movingAxis = movingHotspot.axis
    .clone()
    .transformDirection(movingTransform)
    .normalize();
  return snapsAreAligned(fixed, moving, movingCenter, movingAxis);
}

function snapCenterOffset(fixed: SnapHotspot, moving: SnapHotspot): number {
  if (fixed.gender && moving.gender && fixed.gender !== moving.gender) {
    return 0;
  }
  const fixedLength = fixed.length || 20;
  const movingLength = moving.length || 20;
  return (fixedLength + movingLength) / 2;
}

function addSnapJoint(
  parentInstanceId: string,
  childInstanceId: string,
  target: InstanceHotspot,
  sourceHotspot: SnapHotspot | null,
): void {
  const slide = target.hotspot.slide || (sourceHotspot?.slide ?? false);
  const kind = slide ? 'prismatic' : 'revolute';
  const state = useSceneStore.getState();
  const duplicate = state.joints.some(
    (joint) =>
      (joint.parentInstance === parentInstanceId &&
        joint.parentHotspotId === target.hotspot.id &&
        joint.childInstance === childInstanceId &&
        joint.childHotspotId === sourceHotspot?.id) ||
      (joint.parentInstance === childInstanceId &&
        joint.parentHotspotId === sourceHotspot?.id &&
        joint.childInstance === parentInstanceId &&
        joint.childHotspotId === target.hotspot.id),
  );
  if (duplicate) return;

  const parent = state.parts.find((p) => p.instanceId === parentInstanceId);
  if (!parent) return;
  const parentInv = new THREE.Matrix4().copy(instancePoseMatrix(parent)).invert();
  const pivotLocal = target.worldCenter.clone().applyMatrix4(parentInv);
  const axisLocal = target.worldAxis.clone().transformDirection(parentInv).normalize();
  const limits = kind === 'prismatic' ? prismaticLimits(target.hotspot, sourceHotspot) : {};
  state.addJoint({
    kind,
    parentInstance: parentInstanceId,
    childInstance: childInstanceId,
    parentHotspotId: target.hotspot.id,
    childHotspotId: sourceHotspot?.id,
    pivot: [pivotLocal.x, pivotLocal.y, pivotLocal.z],
    axis: [axisLocal.x, axisLocal.y, axisLocal.z],
    ...limits,
  });
}

function prismaticLimits(
  target: SnapHotspot,
  source: SnapHotspot | null,
): { limitLower?: number; limitUpper?: number } {
  const combinedLduLen = target.length + (source?.length ?? 0);
  const halfMeters = (combinedLduLen / 2) * LDU_TO_M;
  const lim = Math.max(0.005, halfMeters);
  return { limitLower: -lim, limitUpper: lim };
}

type RigidSnapGraph = {
  adjacency: Map<string, Set<string>>;
  partsById: Map<string, PartInstance>;
};

type KinematicAnchor = {
  jointId: string;
  pivot: THREE.Vector3;
  axis: THREE.Vector3;
};

type WorldHotspot = {
  center: THREE.Vector3;
  axis: THREE.Vector3;
};

function makeRigidSnapGraph(
  parts: PartInstance[],
  joints: JointInstance[],
): RigidSnapGraph {
  const partsById = new Map(parts.map((part) => [part.instanceId, part]));
  const adjacency = new Map<string, Set<string>>();
  const connect = (a: string, b: string) => {
    if (!adjacency.has(a)) adjacency.set(a, new Set());
    if (!adjacency.has(b)) adjacency.set(b, new Set());
    adjacency.get(a)!.add(b);
    adjacency.get(b)!.add(a);
  };

  for (const joint of joints) {
    connect(
      rigidEndpointNode(joint.parentInstance, joint.parentHotspotId, partsById),
      rigidEndpointNode(joint.childInstance, joint.childHotspotId, partsById),
    );
  }

  return { adjacency, partsById };
}

function rigidlyConnectedHotspots(
  a: InstanceHotspot,
  b: InstanceHotspot,
  graph: RigidSnapGraph,
): boolean {
  return rigidComponentNodes(rigidSpotNode(a, graph.partsById), graph).has(
    rigidSpotNode(b, graph.partsById),
  );
}

function rigidAssemblyPartIds(
  root: InstanceHotspot,
  graph: RigidSnapGraph,
): Set<string> {
  const ids = new Set<string>();
  for (const node of rigidComponentNodes(rigidSpotNode(root, graph.partsById), graph)) {
    if (node.startsWith('part:')) {
      ids.add(node.slice('part:'.length));
    } else if (node.startsWith('motor-body:')) {
      ids.add(node.slice('motor-body:'.length));
    }
  }
  return ids;
}

function movableAssemblyPartIds(
  root: InstanceHotspot,
  graph: RigidSnapGraph,
  joints: JointInstance[],
): Set<string> {
  const rigidIds = rigidAssemblyPartIds(root, graph);
  if (isMotorOutputSpot(root) || !wouldBreakExistingConnection(rigidIds, joints)) {
    return rigidIds;
  }
  return connectedInstanceIds(root.instanceId, joints);
}

function rigidComponentNodes(start: string, graph: RigidSnapGraph): Set<string> {
  const visited = new Set<string>();
  const queue = [start];

  while (queue.length > 0) {
    const node = queue.shift();
    if (!node || visited.has(node)) continue;
    visited.add(node);
    for (const next of graph.adjacency.get(node) ?? []) {
      if (!visited.has(next)) queue.push(next);
    }
  }

  return visited;
}

function rigidSpotNode(
  spot: InstanceHotspot,
  partsById: Map<string, PartInstance>,
): string {
  return rigidEndpointNode(spot.instanceId, spot.hotspot.id, partsById);
}

function rigidEndpointNode(
  instanceId: string,
  hotspotId: string | undefined,
  partsById: Map<string, PartInstance>,
): string {
  const part = partsById.get(instanceId);
  const motorSpec = part ? motorSpecForPart(part.partId) : undefined;
  if (!motorSpec) return `part:${instanceId}`;
  return hotspotId && motorSpec.outputHotspotIds.includes(hotspotId)
    ? `motor-output:${instanceId}`
    : `motor-body:${instanceId}`;
}

function shouldSwapCandidateDirection(
  first: InstanceHotspot,
  second: InstanceHotspot,
  graph: RigidSnapGraph,
  joints: JointInstance[],
): boolean {
  if (isMotorOutputSpot(second) && !isMotorOutputSpot(first)) {
    return true;
  }

  const firstWouldBreak = wouldBreakExistingConnection(
    movableAssemblyPartIds(first, graph, joints),
    joints,
  );
  const secondWouldBreak = wouldBreakExistingConnection(
    movableAssemblyPartIds(second, graph, joints),
    joints,
  );
  return secondWouldBreak && !firstWouldBreak;
}

function isMotorOutputSpot(spot: InstanceHotspot): boolean {
  return Boolean(
    motorSpecForPart(spot.partId)?.outputHotspotIds.includes(spot.hotspot.id),
  );
}

async function motorOutputAnchorForAssembly(
  fixed: InstanceHotspot,
  moving: InstanceHotspot,
  movingAssemblyIds: Set<string>,
  graph: RigidSnapGraph,
  parts: PartInstance[],
  joints: JointInstance[],
): Promise<KinematicAnchor | null> {
  const outputNode = rigidSpotNode(fixed, graph.partsById);

  for (const joint of joints) {
    const parentNode = rigidEndpointNode(
      joint.parentInstance,
      joint.parentHotspotId,
      graph.partsById,
    );
    const childNode = rigidEndpointNode(
      joint.childInstance,
      joint.childHotspotId,
      graph.partsById,
    );
    const parentIsOutput = parentNode === outputNode;
    const childIsOutput = childNode === outputNode;
    if (!parentIsOutput && !childIsOutput) continue;

    const movingEndpoint = parentIsOutput
      ? {
          instanceId: joint.childInstance,
          hotspotId: joint.childHotspotId,
        }
      : {
          instanceId: joint.parentInstance,
          hotspotId: joint.parentHotspotId,
        };
    if (
      !movingEndpoint.hotspotId ||
      !movingAssemblyIds.has(movingEndpoint.instanceId) ||
      (movingEndpoint.instanceId === moving.instanceId &&
        movingEndpoint.hotspotId === moving.hotspot.id)
    ) {
      continue;
    }

    const movingHotspot = await worldHotspotForEndpoint(
      movingEndpoint.instanceId,
      movingEndpoint.hotspotId,
      parts,
    );
    if (!movingHotspot) continue;

    return {
      jointId: joint.jointId,
      pivot: movingHotspot.center,
      axis: movingHotspot.axis,
    };
  }

  return null;
}

async function worldHotspotForEndpoint(
  instanceId: string,
  hotspotId: string,
  parts: PartInstance[],
): Promise<WorldHotspot | null> {
  const inst = parts.find((part) => part.instanceId === instanceId);
  if (!inst) return null;
  const loaded = await loadPart(inst.partId);
  const hotspot = loaded.hotspots.find((h) => h.id === hotspotId);
  if (!hotspot) return null;
  const currentHotspot = currentHotspotForPart(inst.instanceId, inst.partId, hotspot);
  const transform = instancePoseMatrix(inst);
  return {
    center: currentHotspot.center.clone().applyMatrix4(transform),
    axis: currentHotspot.axis.clone().transformDirection(transform).normalize(),
  };
}

function currentHotspotForSpot(spot: InstanceHotspot): SnapHotspot {
  return currentHotspotForPart(spot.instanceId, spot.partId, spot.hotspot);
}

function currentHotspotForPart(
  instanceId: string,
  partId: string,
  sourceHotspot: SnapHotspot,
): SnapHotspot {
  const hotspot = {
    ...sourceHotspot,
    center: sourceHotspot.center.clone(),
    axis: sourceHotspot.axis.clone(),
  };
  const motorSpec = motorSpecForPart(partId);
  if (!motorSpec || !motorSpec.outputHotspotIds.includes(sourceHotspot.id)) {
    return hotspot;
  }

  const motorAngle = useSceneStore.getState().motorAngles[instanceId] ?? 0;
  const motorPivot = motorPivotInSceneFrame(motorSpec);
  const motorAxis = motorAxisInSceneFrame(motorSpec);
  hotspot.center
    .sub(motorPivot)
    .applyAxisAngle(motorAxis, motorAngle)
    .add(motorPivot);
  hotspot.axis.applyAxisAngle(motorAxis, motorAngle).normalize();
  return hotspot;
}

function wouldBreakExistingConnection(
  movingAssemblyIds: Set<string>,
  joints: JointInstance[],
): boolean {
  if (movingAssemblyIds.size === 0) return false;
  return joints.some((joint) => {
    const parentMoving = movingAssemblyIds.has(joint.parentInstance);
    const childMoving = movingAssemblyIds.has(joint.childInstance);
    return parentMoving !== childMoving;
  });
}

function wouldBreakExistingConnectionExcept(
  movingAssemblyIds: Set<string>,
  joints: JointInstance[],
  preservedJointId: string,
): boolean {
  if (movingAssemblyIds.size === 0) return false;
  return joints.some((joint) => {
    if (joint.jointId === preservedJointId) return false;
    const parentMoving = movingAssemblyIds.has(joint.parentInstance);
    const childMoving = movingAssemblyIds.has(joint.childInstance);
    return parentMoving !== childMoving;
  });
}
