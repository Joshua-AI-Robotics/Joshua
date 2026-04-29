import { create } from 'zustand';
import * as THREE from 'three';
import {
  motorAxisInSceneFrame,
  motorPivotInSceneFrame,
  motorSpecForPart,
} from '../ldraw/motorSpecs';

export type JointKind = 'revolute' | 'prismatic' | 'fixed';
export type TransformAxis = 'x' | 'y' | 'z';

export type PartInstance = {
  instanceId: string;
  partId: string;              // catalog id, e.g. "32009"
  // Transform is stored as plain numbers so the store is serialization-friendly.
  position: [number, number, number];
  quaternion: [number, number, number, number];
  // Optional only for compatibility with already-live Vite state created before this field existed.
  scale?: [number, number, number];
  // When true, the part is tied to world via a PhysicsFixedJoint on export.
  worldAnchor?: boolean;
};

export type JointInstance = {
  jointId: string;
  kind: JointKind;
  parentInstance: string;
  childInstance: string;
  parentHotspotId?: string;
  childHotspotId?: string;
  // Pivot & axis are stored in the parent's local frame, in LDU-scaled scene space.
  pivot: [number, number, number];
  axis: [number, number, number];
  // Joint limits — degrees for revolute, meters for prismatic.
  limitLower?: number;
  limitUpper?: number;
};

export type SceneSnapshot = {
  parts: PartInstance[];
  joints: JointInstance[];
  motorAngles: Record<string, number>;
  selectedInstance: string | null;
  selectedJoint: string | null;
};

type SceneState = {
  parts: PartInstance[];
  joints: JointInstance[];
  motorAnimations: Record<string, boolean>;
  motorAngles: Record<string, number>;
  selectedInstance: string | null;
  selectedJoint: string | null;

  addPart: (partId: string, transform?: THREE.Matrix4) => string;
  removePart: (instanceId: string) => void;
  setTransform: (instanceId: string, m: THREE.Matrix4) => void;
  setTransforms: (updates: { instanceId: string; transform: THREE.Matrix4 }[]) => void;
  rotatePart: (instanceId: string, axis: TransformAxis, degrees: number) => void;
  mirrorPart: (instanceId: string, axis: TransformAxis) => void;
  toggleMotorAnimation: (instanceId: string) => void;
  advanceMotorAngle: (instanceId: string, deltaRadians: number) => void;
  setWorldAnchor: (instanceId: string, value: boolean) => void;

  addJoint: (j: Omit<JointInstance, 'jointId'>) => string;
  updateJoint: (jointId: string, patch: Partial<JointInstance>) => void;
  removeJoint: (jointId: string) => void;

  selectInstance: (id: string | null) => void;
  selectJoint: (id: string | null) => void;

  loadScene: (snapshot: SceneSnapshot) => void;
  clear: () => void;
};

let counter = 0;
const uid = (prefix: string) => `${prefix}_${Date.now().toString(36)}_${counter++}`;

function matrixToPose(m: THREE.Matrix4): {
  position: [number, number, number];
  quaternion: [number, number, number, number];
  scale: [number, number, number];
} {
  const p = new THREE.Vector3();
  const q = new THREE.Quaternion();
  const s = new THREE.Vector3();
  m.decompose(p, q, s);
  return {
    position: [p.x, p.y, p.z],
    quaternion: [q.x, q.y, q.z, q.w],
    scale: [s.x, s.y, s.z],
  };
}

function axisVector(axis: TransformAxis): THREE.Vector3 {
  if (axis === 'x') return new THREE.Vector3(1, 0, 0);
  if (axis === 'y') return new THREE.Vector3(0, 1, 0);
  return new THREE.Vector3(0, 0, 1);
}

function axisIndex(axis: TransformAxis): number {
  return axis === 'x' ? 0 : axis === 'y' ? 1 : 2;
}

function partScale(p: PartInstance): [number, number, number] {
  return p.scale ?? [1, 1, 1];
}

export const useSceneStore = create<SceneState>((set) => ({
  parts: [],
  joints: [],
  motorAnimations: {},
  motorAngles: {},
  selectedInstance: null,
  selectedJoint: null,

  addPart: (partId, transform) => {
    const instanceId = uid('part');
    const m = transform ?? new THREE.Matrix4();
    const pose = matrixToPose(m);
    set((s) => ({
      parts: [...s.parts, { instanceId, partId, ...pose }],
      selectedInstance: instanceId,
    }));
    return instanceId;
  },

  removePart: (instanceId) =>
    set((s) => ({
      parts: s.parts.filter((p) => p.instanceId !== instanceId),
      joints: s.joints.filter(
        (j) => j.parentInstance !== instanceId && j.childInstance !== instanceId,
      ),
      motorAnimations: Object.fromEntries(
        Object.entries(s.motorAnimations).filter(([id]) => id !== instanceId),
      ),
      motorAngles: Object.fromEntries(
        Object.entries(s.motorAngles).filter(([id]) => id !== instanceId),
      ),
      selectedInstance: s.selectedInstance === instanceId ? null : s.selectedInstance,
    })),

  setTransform: (instanceId, m) => {
    const pose = matrixToPose(m);
    set((s) => ({
      parts: s.parts.map((p) =>
        p.instanceId === instanceId ? { ...p, ...pose } : p,
      ),
    }));
  },

  setTransforms: (updates) => {
    if (updates.length === 0) return;
    const posesById = new Map(
      updates.map(({ instanceId, transform }) => [instanceId, matrixToPose(transform)]),
    );
    set((s) => {
      let changed = false;
      const parts = s.parts.map((part) => {
        const pose = posesById.get(part.instanceId);
        if (!pose) return part;
        changed = true;
        return { ...part, ...pose };
      });
      return changed ? { parts } : s;
    });
  },

  rotatePart: (instanceId, axis, degrees) =>
    set((s) => {
      const root = s.parts.find((p) => p.instanceId === instanceId);
      if (!root) return s;

      const connected = connectedInstanceIds(instanceId, s.joints);
      const pivot = new THREE.Vector3(...root.position);
      const rootQ = new THREE.Quaternion(...root.quaternion);
      const localAxis = axisVector(axis);
      const worldAxis = localAxis.clone().applyQuaternion(rootQ).normalize();
      const angle = THREE.MathUtils.degToRad(degrees);
      const worldRotation = new THREE.Matrix4().makeRotationAxis(worldAxis, angle);
      const operation = new THREE.Matrix4()
        .makeTranslation(pivot.x, pivot.y, pivot.z)
        .multiply(worldRotation)
        .multiply(new THREE.Matrix4().makeTranslation(-pivot.x, -pivot.y, -pivot.z));
      const localRotation = new THREE.Quaternion().setFromAxisAngle(localAxis, angle);

      return {
        parts: s.parts.map((part) => {
          if (!connected.has(part.instanceId)) return part;
          if (part.instanceId === instanceId) {
            const q = new THREE.Quaternion(...part.quaternion).multiply(localRotation);
            q.normalize();
            return { ...part, quaternion: [q.x, q.y, q.z, q.w] };
          }
          const next = new THREE.Matrix4().multiplyMatrices(
            operation,
            instancePoseMatrix(part),
          );
          return { ...part, ...matrixToPose(next) };
        }),
      };
    }),

  mirrorPart: (instanceId, axis) =>
    set((s) => {
      const root = s.parts.find((p) => p.instanceId === instanceId);
      if (!root) return s;

      const connected = connectedInstanceIds(instanceId, s.joints);
      const mirrorScale = new THREE.Vector3(1, 1, 1);
      mirrorScale.setComponent(axisIndex(axis), -1);
      const rootMatrix = instancePoseMatrix(root);
      const localMirror = new THREE.Matrix4().makeScale(
        mirrorScale.x,
        mirrorScale.y,
        mirrorScale.z,
      );
      const operation = new THREE.Matrix4()
        .copy(rootMatrix)
        .multiply(localMirror)
        .multiply(rootMatrix.clone().invert());

      return {
        parts: s.parts.map((part) => {
          if (!connected.has(part.instanceId)) return part;
          if (part.instanceId === instanceId) {
            const scale = [...partScale(part)] as [number, number, number];
            const idx = axisIndex(axis);
            scale[idx] *= -1;
            return { ...part, scale };
          }
          const next = new THREE.Matrix4().multiplyMatrices(
            operation,
            instancePoseMatrix(part),
          );
          return { ...part, ...matrixToPose(next) };
        }),
      };
    }),

  setWorldAnchor: (instanceId, value) =>
    set((s) => ({
      parts: s.parts.map((p) =>
        p.instanceId === instanceId ? { ...p, worldAnchor: value } : p,
      ),
    })),

  toggleMotorAnimation: (instanceId) =>
    set((s) => ({
      motorAnimations: {
        ...s.motorAnimations,
        [instanceId]: !s.motorAnimations[instanceId],
      },
    })),

  advanceMotorAngle: (instanceId, deltaRadians) =>
    set((s) => {
      const nextMotorAngles = {
        ...s.motorAngles,
        [instanceId]: ((s.motorAngles[instanceId] ?? 0) + deltaRadians) % (Math.PI * 2),
      };
      if (Math.abs(deltaRadians) < 1e-8) {
        return { motorAngles: nextMotorAngles };
      }

      const motorPart = s.parts.find((part) => part.instanceId === instanceId);
      const motorSpec = motorPart ? motorSpecForPart(motorPart.partId) : undefined;
      if (!motorPart || !motorSpec) {
        return { motorAngles: nextMotorAngles };
      }

      const drivenPartIds = motorOutputConnectedInstanceIds(
        instanceId,
        motorSpec.outputHotspotIds,
        s.joints,
      );
      if (drivenPartIds.size === 0) {
        return { motorAngles: nextMotorAngles };
      }

      const motorMatrix = instancePoseMatrix(motorPart);
      const pivot = motorPivotInSceneFrame(motorSpec).applyMatrix4(motorMatrix);
      const axis = motorAxisInSceneFrame(motorSpec)
        .transformDirection(motorMatrix)
        .normalize();
      const worldRotation = new THREE.Matrix4().makeRotationAxis(axis, deltaRadians);
      const operation = new THREE.Matrix4()
        .makeTranslation(pivot.x, pivot.y, pivot.z)
        .multiply(worldRotation)
        .multiply(new THREE.Matrix4().makeTranslation(-pivot.x, -pivot.y, -pivot.z));

      return {
        motorAngles: nextMotorAngles,
        parts: s.parts.map((part) => {
          if (!drivenPartIds.has(part.instanceId)) return part;
          const next = new THREE.Matrix4().multiplyMatrices(
            operation,
            instancePoseMatrix(part),
          );
          return { ...part, ...matrixToPose(next) };
        }),
      };
    }),

  addJoint: (j) => {
    const jointId = uid('joint');
    set((s) => ({ joints: [...s.joints, { jointId, ...j }] }));
    return jointId;
  },

  updateJoint: (jointId, patch) =>
    set((s) => ({
      joints: s.joints.map((j) => (j.jointId === jointId ? { ...j, ...patch } : j)),
    })),

  removeJoint: (jointId) =>
    set((s) => ({
      joints: s.joints.filter((j) => j.jointId !== jointId),
      selectedJoint: s.selectedJoint === jointId ? null : s.selectedJoint,
    })),

  selectInstance: (id) => set({ selectedInstance: id, selectedJoint: null }),
  selectJoint: (id) => set({ selectedJoint: id, selectedInstance: null }),

  loadScene: (snapshot) =>
    set({
      parts: snapshot.parts,
      joints: snapshot.joints,
      motorAngles: snapshot.motorAngles,
      motorAnimations: {},
      selectedInstance: snapshot.selectedInstance,
      selectedJoint: snapshot.selectedJoint,
    }),

  clear: () =>
    set({
      parts: [],
      joints: [],
      motorAnimations: {},
      motorAngles: {},
      selectedInstance: null,
      selectedJoint: null,
    }),
}));

export function instancePoseMatrix(p: PartInstance): THREE.Matrix4 {
  const m = new THREE.Matrix4();
  const q = new THREE.Quaternion(...p.quaternion);
  const pos = new THREE.Vector3(...p.position);
  const scale = new THREE.Vector3(...partScale(p));
  m.compose(pos, q, scale);
  return m;
}

export function connectedInstanceIds(
  rootInstanceId: string,
  joints: JointInstance[],
): Set<string> {
  const connected = new Set([rootInstanceId]);
  let changed = true;

  while (changed) {
    changed = false;
    for (const joint of joints) {
      const parentConnected = connected.has(joint.parentInstance);
      const childConnected = connected.has(joint.childInstance);
      if (parentConnected && !childConnected) {
        connected.add(joint.childInstance);
        changed = true;
      } else if (childConnected && !parentConnected) {
        connected.add(joint.parentInstance);
        changed = true;
      }
    }
  }

  return connected;
}

function motorOutputConnectedInstanceIds(
  motorInstanceId: string,
  outputHotspotIds: readonly string[],
  joints: JointInstance[],
): Set<string> {
  const outputHotspots = new Set(outputHotspotIds);
  const starts = new Set<string>();

  for (const joint of joints) {
    if (
      joint.parentInstance === motorInstanceId &&
      joint.parentHotspotId &&
      outputHotspots.has(joint.parentHotspotId)
    ) {
      starts.add(joint.childInstance);
    }
    if (
      joint.childInstance === motorInstanceId &&
      joint.childHotspotId &&
      outputHotspots.has(joint.childHotspotId)
    ) {
      starts.add(joint.parentInstance);
    }
  }

  const connected = new Set<string>();
  const queue = [...starts].filter((id) => id !== motorInstanceId);

  while (queue.length > 0) {
    const current = queue.shift();
    if (!current || connected.has(current)) continue;
    connected.add(current);

    for (const joint of joints) {
      let next: string | null = null;
      if (joint.parentInstance === current) {
        next = joint.childInstance;
      } else if (joint.childInstance === current) {
        next = joint.parentInstance;
      }
      if (!next || next === motorInstanceId || connected.has(next)) continue;
      queue.push(next);
    }
  }

  return connected;
}
