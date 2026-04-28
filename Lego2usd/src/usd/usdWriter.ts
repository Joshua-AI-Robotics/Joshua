import * as THREE from 'three';
import type { PartInstance, JointInstance } from '../scene/store';
import { instancePoseMatrix } from '../scene/store';
import { serializePartGroup, computePartAabb, LDU_TO_M } from './meshSerializer';
import type { LoadedPart } from '../ldraw/loadPart';
import {
  MOTOR_SPECS,
  motorAxisInSceneFrame,
  motorPivotInSceneFrame,
  type MotorSpec,
} from '../ldraw/motorSpecs';

const ABS_DENSITY_KG_PER_M3 = 1000; // ABS plastic ≈ 1040; rounded.
const MIN_MASS_KG = 0.001;
const MAX_MASS_KG = 1.0;

// Default PhysicsDriveAPI gains (RL-ready starting point for Lego-scale parts).
const REVOLUTE_STIFFNESS = 200;
const REVOLUTE_DAMPING = 20;
const REVOLUTE_MAX_FORCE = 200;
const PRISMATIC_STIFFNESS = 2000;
const PRISMATIC_DAMPING = 100;
const PRISMATIC_MAX_FORCE = 500;

// Default joint limits when the store has none.
const DEFAULT_REVOLUTE_LOWER_DEG = -360;
const DEFAULT_REVOLUTE_UPPER_DEG = 360;
const DEFAULT_PRISMATIC_LOWER_M = -0.05;
const DEFAULT_PRISMATIC_UPPER_M = 0.05;
const LDRAW_TO_SCENE = new THREE.Matrix4().makeRotationX(Math.PI);
const SCENE_TO_LDRAW = new THREE.Matrix4().copy(LDRAW_TO_SCENE).invert();

function safeName(s: string): string {
  const cleaned = s.replace(/[^A-Za-z0-9_]/g, '_');
  return /^[A-Za-z_]/.test(cleaned) ? cleaned : `_${cleaned}`;
}

function fmt(n: number): string {
  if (!Number.isFinite(n)) return '0';
  return Math.abs(n) < 1e-9 ? '0' : n.toFixed(7).replace(/\.?0+$/, '');
}

function writeIntArray(vals: number[]): string {
  return vals.join(', ');
}

function writePointArray(flat: number[]): string {
  const parts: string[] = [];
  for (let i = 0; i < flat.length; i += 3) {
    parts.push(`(${fmt(flat[i])}, ${fmt(flat[i + 1])}, ${fmt(flat[i + 2])})`);
  }
  return parts.join(', ');
}

// Build a 4x4 "matrix4d" literal in USD format from a THREE.Matrix4.
// The supplied matrix is in LDU-scene-space; we convert to Z-up meters here.
function matrix4dLiteral(world: THREE.Matrix4): string {
  const C = new THREE.Matrix4().set(
    LDU_TO_M, 0,        0,         0,
    0,        0,        -LDU_TO_M, 0,
    0,        -LDU_TO_M, 0,        0,
    0,        0,        0,         1,
  );
  const Cinv = new THREE.Matrix4().copy(C).invert();
  const out = new THREE.Matrix4().multiplyMatrices(C, world).multiply(Cinv);
  const e = out.elements;
  const row = (r: number) =>
    `(${fmt(e[r])}, ${fmt(e[r + 4])}, ${fmt(e[r + 8])}, ${fmt(e[r + 12])})`;
  return `( ${row(0)}, ${row(1)}, ${row(2)}, ${row(3)} )`;
}

function ldPointToUsd(v: [number, number, number]): [number, number, number] {
  return [v[0] * LDU_TO_M, -v[2] * LDU_TO_M, -v[1] * LDU_TO_M];
}

function ldDirToUsd(v: [number, number, number]): [number, number, number] {
  const out: [number, number, number] = [v[0], -v[2], -v[1]];
  const len = Math.hypot(out[0], out[1], out[2]) || 1;
  return [out[0] / len, out[1] / len, out[2] / len];
}

// Rotation that takes +X to `axis`. Returned as (w, x, y, z).
function quatAlignXTo(axis: [number, number, number]): [number, number, number, number] {
  const to = new THREE.Vector3(...axis).normalize();
  const from = new THREE.Vector3(1, 0, 0);
  const q = new THREE.Quaternion().setFromUnitVectors(from, to);
  return [q.w, q.x, q.y, q.z];
}

function instancePrimName(p: PartInstance): string {
  return safeName(`${p.partId}_${p.instanceId}`);
}

function motorComponentPrimName(p: PartInstance, role: 'body' | 'output'): string {
  return safeName(`${p.partId}_${p.instanceId}_${role}`);
}

function findDirectChildByFile(group: THREE.Group, fileName: string): THREE.Group | null {
  return (
    group.children.find((child) => child.userData?.fileName === fileName) as
      | THREE.Group
      | undefined
  ) ?? null;
}

function motorOutputRotation(spec: MotorSpec, motorAngle: number): THREE.Matrix4 {
  if (Math.abs(motorAngle) < 1e-9) return new THREE.Matrix4();
  return new THREE.Matrix4().makeRotationAxis(
    new THREE.Vector3(...spec.axis).normalize(),
    motorAngle,
  );
}

function componentSceneLocalMatrix(
  component: THREE.Object3D,
  extraLocalRotation = new THREE.Matrix4(),
): THREE.Matrix4 {
  component.updateMatrix();
  const localLdraw = new THREE.Matrix4()
    .copy(component.matrix)
    .multiply(extraLocalRotation);
  return new THREE.Matrix4()
    .multiplyMatrices(LDRAW_TO_SCENE, localLdraw)
    .multiply(SCENE_TO_LDRAW);
}

function componentWorldMatrix(
  part: PartInstance,
  component: THREE.Object3D,
  extraLocalRotation = new THREE.Matrix4(),
): THREE.Matrix4 {
  return new THREE.Matrix4().multiplyMatrices(
    instancePoseMatrix(part),
    componentSceneLocalMatrix(component, extraLocalRotation),
  );
}

function endpointPrimName(part: PartInstance, hotspotId?: string): string {
  const spec = MOTOR_SPECS[part.partId];
  if (!spec) return instancePrimName(part);
  return motorComponentPrimName(
    part,
    hotspotId && spec.outputHotspotIds.includes(hotspotId) ? 'output' : 'body',
  );
}

function endpointWorldMatrix(
  part: PartInstance,
  hotspotId: string | undefined,
  loadedByPartId: Map<string, LoadedPart>,
  motorAngles: Record<string, number>,
): THREE.Matrix4 {
  const spec = MOTOR_SPECS[part.partId];
  if (!spec) return instancePoseMatrix(part);

  const loaded = loadedByPartId.get(part.partId);
  const isOutputEndpoint = !!hotspotId && spec.outputHotspotIds.includes(hotspotId);
  const fileName = isOutputEndpoint ? spec.outputFile : spec.bodyFile;
  const component = loaded ? findDirectChildByFile(loaded.group, fileName) : null;
  if (!component) return instancePoseMatrix(part);
  return componentWorldMatrix(
    part,
    component,
    isOutputEndpoint ? motorOutputRotation(spec, motorAngles[part.instanceId] ?? 0) : undefined,
  );
}

function computeMassKg(volumeM3: number): number {
  const raw = volumeM3 * ABS_DENSITY_KG_PER_M3;
  return Math.min(MAX_MASS_KG, Math.max(MIN_MASS_KG, raw));
}

function writeRigidBodyPrim(
  lines: string[],
  primName: string,
  transform: THREE.Matrix4,
  group: THREE.Group,
  meshLabel: string,
): void {
  const matLit = matrix4dLiteral(transform);
  const aabb = computePartAabb(group);
  const mass = computeMassKg(aabb.volumeM3);

  lines.push(`    def Xform "${primName}" (`);
  lines.push('        prepend apiSchemas = ["PhysicsRigidBodyAPI", "PhysicsMassAPI"]');
  lines.push('    )');
  lines.push('    {');
  lines.push(`        matrix4d xformOp:transform = ${matLit}`);
  lines.push('        uniform token[] xformOpOrder = ["xformOp:transform"]');
  lines.push(`        float physics:mass = ${fmt(mass)}`);
  lines.push(
    `        point3f physics:centerOfMass = (${fmt(aabb.center[0])}, ${fmt(aabb.center[1])}, ${fmt(aabb.center[2])})`,
  );

  const meshes = serializePartGroup(group, meshLabel);
  for (const m of meshes) {
    lines.push(`        def Mesh "${safeName(m.name)}" (`);
    lines.push('            prepend apiSchemas = ["PhysicsCollisionAPI", "PhysicsMeshCollisionAPI"]');
    lines.push('        )');
    lines.push('        {');
    lines.push(`            int[] faceVertexCounts = [${writeIntArray(m.faceVertexCounts)}]`);
    lines.push(`            int[] faceVertexIndices = [${writeIntArray(m.faceVertexIndices)}]`);
    lines.push(`            point3f[] points = [${writePointArray(m.points)}]`);
    lines.push(
      `            color3f[] primvars:displayColor = [(${fmt(m.displayColor[0])}, ${fmt(
        m.displayColor[1],
      )}, ${fmt(m.displayColor[2])})]`,
    );
    lines.push('            uniform token physics:approximation = "convexDecomposition"');
    lines.push('        }');
  }
  lines.push('    }');
  lines.push('');
}

export function writeUsdaScene(
  parts: PartInstance[],
  joints: JointInstance[],
  loadedByPartId: Map<string, LoadedPart>,
  motorAngles: Record<string, number> = {},
): string {
  const lines: string[] = [];
  lines.push('#usda 1.0');
  lines.push('(');
  lines.push('    defaultPrim = "Lego"');
  lines.push('    upAxis = "Z"');
  lines.push('    metersPerUnit = 1');
  lines.push(')');
  lines.push('');
  lines.push('def Xform "Lego" (');
  lines.push('    prepend apiSchemas = ["PhysicsArticulationRootAPI"]');
  lines.push(')');
  lines.push('{');

  for (const part of parts) {
    const loaded = loadedByPartId.get(part.partId);
    if (!loaded) continue;
    const primName = instancePrimName(part);
    const worldLD = instancePoseMatrix(part);

    const motorSpec = MOTOR_SPECS[part.partId];
    if (motorSpec) {
      const bodyGroup = findDirectChildByFile(loaded.group, motorSpec.bodyFile);
      const outputGroup = findDirectChildByFile(loaded.group, motorSpec.outputFile);
      if (bodyGroup && outputGroup) {
        writeRigidBodyPrim(
          lines,
          motorComponentPrimName(part, 'body'),
          componentWorldMatrix(part, bodyGroup),
          bodyGroup,
          `${part.partId}_body`,
        );
        writeRigidBodyPrim(
          lines,
          motorComponentPrimName(part, 'output'),
          componentWorldMatrix(
            part,
            outputGroup,
            motorOutputRotation(motorSpec, motorAngles[part.instanceId] ?? 0),
          ),
          outputGroup,
          `${part.partId}_output`,
        );
        continue;
      }
    }

    writeRigidBodyPrim(lines, primName, worldLD, loaded.group, part.partId);
  }

  // Articulation joints (revolute / prismatic / fixed between two parts).
  for (const j of joints) {
    const parent = parts.find((p) => p.instanceId === j.parentInstance);
    const child = parts.find((p) => p.instanceId === j.childInstance);
    if (!parent || !child) continue;

    const driveSchema =
      j.kind === 'revolute'
        ? ['"PhysicsDriveAPI:angular"']
        : j.kind === 'prismatic'
          ? ['"PhysicsDriveAPI:linear"']
          : [];

    if (j.kind === 'fixed') {
      lines.push(`    def PhysicsFixedJoint "${safeName(j.jointId)}"`);
    } else if (j.kind === 'prismatic') {
      lines.push(`    def PhysicsPrismaticJoint "${safeName(j.jointId)}" (`);
      lines.push(`        prepend apiSchemas = [${driveSchema.join(', ')}]`);
      lines.push('    )');
    } else {
      lines.push(`    def PhysicsRevoluteJoint "${safeName(j.jointId)}" (`);
      lines.push(`        prepend apiSchemas = [${driveSchema.join(', ')}]`);
      lines.push('    )');
    }
    lines.push('    {');
    lines.push(`        rel physics:body0 = </Lego/${endpointPrimName(parent, j.parentHotspotId)}>`);
    lines.push(`        rel physics:body1 = </Lego/${endpointPrimName(child, j.childHotspotId)}>`);

    const parentPartM = instancePoseMatrix(parent);
    const parentBodyM = endpointWorldMatrix(
      parent,
      j.parentHotspotId,
      loadedByPartId,
      motorAngles,
    );
    const childBodyM = endpointWorldMatrix(
      child,
      j.childHotspotId,
      loadedByPartId,
      motorAngles,
    );
    const parentBodyInv = new THREE.Matrix4().copy(parentBodyM).invert();
    const childBodyInv = new THREE.Matrix4().copy(childBodyM).invert();
    const pivotWorldLD = new THREE.Vector3(...j.pivot).applyMatrix4(parentPartM);
    const pivotParentLD = pivotWorldLD.clone().applyMatrix4(parentBodyInv);
    const pivotParent = ldPointToUsd([
      pivotParentLD.x,
      pivotParentLD.y,
      pivotParentLD.z,
    ]);
    const pivotChildLD = pivotWorldLD.clone().applyMatrix4(childBodyInv);
    const pivotChild = ldPointToUsd([pivotChildLD.x, pivotChildLD.y, pivotChildLD.z]);

    lines.push(`        point3f physics:localPos0 = (${fmt(pivotParent[0])}, ${fmt(pivotParent[1])}, ${fmt(pivotParent[2])})`);
    lines.push(`        point3f physics:localPos1 = (${fmt(pivotChild[0])}, ${fmt(pivotChild[1])}, ${fmt(pivotChild[2])})`);

    // Axis rotation: localRot0 in body0-local USD, localRot1 in body1-local USD.
    const axisWorldLD = new THREE.Vector3(...j.axis)
      .transformDirection(parentPartM)
      .normalize();
    const axisLduParent = axisWorldLD
      .clone()
      .transformDirection(parentBodyInv)
      .normalize();
    const axisUsdParent = ldDirToUsd([
      axisLduParent.x,
      axisLduParent.y,
      axisLduParent.z,
    ]);
    const qParent = quatAlignXTo(axisUsdParent);
    const axisLduChild = axisWorldLD
      .clone()
      .transformDirection(childBodyInv)
      .normalize();
    const axisUsdChild = ldDirToUsd([
      axisLduChild.x,
      axisLduChild.y,
      axisLduChild.z,
    ]);
    const qChild = quatAlignXTo(axisUsdChild);

    lines.push(`        quatf physics:localRot0 = (${fmt(qParent[0])}, ${fmt(qParent[1])}, ${fmt(qParent[2])}, ${fmt(qParent[3])})`);
    lines.push(`        quatf physics:localRot1 = (${fmt(qChild[0])}, ${fmt(qChild[1])}, ${fmt(qChild[2])}, ${fmt(qChild[3])})`);

    if (j.kind !== 'fixed') {
      lines.push('        uniform token physics:axis = "X"');
      if (j.kind === 'revolute') {
        const lo = j.limitLower ?? DEFAULT_REVOLUTE_LOWER_DEG;
        const hi = j.limitUpper ?? DEFAULT_REVOLUTE_UPPER_DEG;
        lines.push(`        float physics:lowerLimit = ${fmt(lo)}`);
        lines.push(`        float physics:upperLimit = ${fmt(hi)}`);
        lines.push(`        float drive:angular:physics:stiffness = ${fmt(REVOLUTE_STIFFNESS)}`);
        lines.push(`        float drive:angular:physics:damping = ${fmt(REVOLUTE_DAMPING)}`);
        lines.push(`        float drive:angular:physics:maxForce = ${fmt(REVOLUTE_MAX_FORCE)}`);
        lines.push(`        float drive:angular:physics:targetPosition = 0`);
      } else {
        const lo = j.limitLower ?? DEFAULT_PRISMATIC_LOWER_M;
        const hi = j.limitUpper ?? DEFAULT_PRISMATIC_UPPER_M;
        lines.push(`        float physics:lowerLimit = ${fmt(lo)}`);
        lines.push(`        float physics:upperLimit = ${fmt(hi)}`);
        lines.push(`        float drive:linear:physics:stiffness = ${fmt(PRISMATIC_STIFFNESS)}`);
        lines.push(`        float drive:linear:physics:damping = ${fmt(PRISMATIC_DAMPING)}`);
        lines.push(`        float drive:linear:physics:maxForce = ${fmt(PRISMATIC_MAX_FORCE)}`);
        lines.push(`        float drive:linear:physics:targetPosition = 0`);
      }
    }
    lines.push('    }');
    lines.push('');
  }

  // Built-in motor output joints. These split supported motor shortcuts into
  // body and output rigid bodies so Isaac Sim can articulate the output hub.
  for (const part of parts) {
    const spec = MOTOR_SPECS[part.partId];
    if (!spec) continue;
    const loaded = loadedByPartId.get(part.partId);
    if (!loaded) continue;
    const bodyGroup = findDirectChildByFile(loaded.group, spec.bodyFile);
    const outputGroup = findDirectChildByFile(loaded.group, spec.outputFile);
    if (!bodyGroup || !outputGroup) continue;

    const partM = instancePoseMatrix(part);
    const bodyM = componentWorldMatrix(part, bodyGroup);
    const outputM = componentWorldMatrix(
      part,
      outputGroup,
      motorOutputRotation(spec, motorAngles[part.instanceId] ?? 0),
    );
    const bodyInv = new THREE.Matrix4().copy(bodyM).invert();
    const outputInv = new THREE.Matrix4().copy(outputM).invert();
    const pivotWorld = motorPivotInSceneFrame(spec).applyMatrix4(partM);
    const pivotBody = pivotWorld.clone().applyMatrix4(bodyInv);
    const pivotOutput = pivotWorld.clone().applyMatrix4(outputInv);
    const axisWorld = motorAxisInSceneFrame(spec)
      .transformDirection(partM)
      .normalize();
    const axisBody = axisWorld.clone().transformDirection(bodyInv).normalize();
    const axisOutput = axisWorld.clone().transformDirection(outputInv).normalize();
    const pivotBodyUsd = ldPointToUsd([pivotBody.x, pivotBody.y, pivotBody.z]);
    const pivotOutputUsd = ldPointToUsd([
      pivotOutput.x,
      pivotOutput.y,
      pivotOutput.z,
    ]);
    const qBody = quatAlignXTo(ldDirToUsd([axisBody.x, axisBody.y, axisBody.z]));
    const qOutput = quatAlignXTo(
      ldDirToUsd([axisOutput.x, axisOutput.y, axisOutput.z]),
    );

    lines.push(`    def PhysicsRevoluteJoint "${safeName(`${part.instanceId}_motorOutput`)}" (`);
    lines.push('        prepend apiSchemas = ["PhysicsDriveAPI:angular"]');
    lines.push('    )');
    lines.push('    {');
    lines.push(`        rel physics:body0 = </Lego/${motorComponentPrimName(part, 'body')}>`);
    lines.push(`        rel physics:body1 = </Lego/${motorComponentPrimName(part, 'output')}>`);
    lines.push(`        point3f physics:localPos0 = (${fmt(pivotBodyUsd[0])}, ${fmt(pivotBodyUsd[1])}, ${fmt(pivotBodyUsd[2])})`);
    lines.push(`        point3f physics:localPos1 = (${fmt(pivotOutputUsd[0])}, ${fmt(pivotOutputUsd[1])}, ${fmt(pivotOutputUsd[2])})`);
    lines.push(`        quatf physics:localRot0 = (${fmt(qBody[0])}, ${fmt(qBody[1])}, ${fmt(qBody[2])}, ${fmt(qBody[3])})`);
    lines.push(`        quatf physics:localRot1 = (${fmt(qOutput[0])}, ${fmt(qOutput[1])}, ${fmt(qOutput[2])}, ${fmt(qOutput[3])})`);
    lines.push('        uniform token physics:axis = "X"');
    lines.push(`        float physics:lowerLimit = ${fmt(DEFAULT_REVOLUTE_LOWER_DEG)}`);
    lines.push(`        float physics:upperLimit = ${fmt(DEFAULT_REVOLUTE_UPPER_DEG)}`);
    lines.push(`        float drive:angular:physics:stiffness = ${fmt(REVOLUTE_STIFFNESS)}`);
    lines.push(`        float drive:angular:physics:damping = ${fmt(REVOLUTE_DAMPING)}`);
    lines.push(`        float drive:angular:physics:maxForce = ${fmt(REVOLUTE_MAX_FORCE)}`);
    lines.push('        float drive:angular:physics:targetPosition = 0');
    lines.push('    }');
    lines.push('');
  }

  // World-anchor fixed joints (body0 empty → tied to world).
  for (const part of parts) {
    if (!part.worldAnchor) continue;
    const primName = MOTOR_SPECS[part.partId]
      ? motorComponentPrimName(part, 'body')
      : instancePrimName(part);
    lines.push(`    def PhysicsFixedJoint "${primName}_worldAnchor"`);
    lines.push('    {');
    lines.push(`        rel physics:body1 = </Lego/${primName}>`);
    lines.push('        point3f physics:localPos0 = (0, 0, 0)');
    lines.push('        point3f physics:localPos1 = (0, 0, 0)');
    lines.push('        quatf physics:localRot0 = (1, 0, 0, 0)');
    lines.push('        quatf physics:localRot1 = (1, 0, 0, 0)');
    lines.push('    }');
    lines.push('');
  }

  lines.push('}');
  lines.push('');
  return lines.join('\n');
}
