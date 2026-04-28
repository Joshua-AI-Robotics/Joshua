import type { JointInstance, PartInstance, SceneSnapshot } from './store';

const SCENE_SAVE_KEY = 'lego2usd.scene.v1';

export type SavedScene = {
  version: 1;
  savedAt: string;
  scene: SceneSnapshot;
};

function isNumberTuple(value: unknown, length: number): value is number[] {
  return (
    Array.isArray(value) &&
    value.length === length &&
    value.every((entry) => typeof entry === 'number' && Number.isFinite(entry))
  );
}

function isStringOrUndefined(value: unknown): value is string | undefined {
  return value === undefined || typeof value === 'string';
}

function isNumberOrUndefined(value: unknown): value is number | undefined {
  return value === undefined || (typeof value === 'number' && Number.isFinite(value));
}

function isPartInstance(value: unknown): value is PartInstance {
  if (!value || typeof value !== 'object') return false;
  const part = value as Partial<PartInstance>;
  return (
    typeof part.instanceId === 'string' &&
    typeof part.partId === 'string' &&
    isNumberTuple(part.position, 3) &&
    isNumberTuple(part.quaternion, 4) &&
    (part.scale === undefined || isNumberTuple(part.scale, 3)) &&
    (part.worldAnchor === undefined || typeof part.worldAnchor === 'boolean')
  );
}

function isJointInstance(value: unknown): value is JointInstance {
  if (!value || typeof value !== 'object') return false;
  const joint = value as Partial<JointInstance>;
  return (
    typeof joint.jointId === 'string' &&
    (joint.kind === 'revolute' || joint.kind === 'prismatic' || joint.kind === 'fixed') &&
    typeof joint.parentInstance === 'string' &&
    typeof joint.childInstance === 'string' &&
    isStringOrUndefined(joint.parentHotspotId) &&
    isStringOrUndefined(joint.childHotspotId) &&
    isNumberTuple(joint.pivot, 3) &&
    isNumberTuple(joint.axis, 3) &&
    isNumberOrUndefined(joint.limitLower) &&
    isNumberOrUndefined(joint.limitUpper)
  );
}

function isNumberRecord(value: unknown): value is Record<string, number> {
  return (
    !!value &&
    typeof value === 'object' &&
    Object.values(value).every((entry) => typeof entry === 'number' && Number.isFinite(entry))
  );
}

function isSavedScene(value: unknown): value is SavedScene {
  if (!value || typeof value !== 'object') return false;
  const saved = value as Partial<SavedScene>;
  const scene = saved.scene as Partial<SceneSnapshot> | undefined;
  return (
    saved.version === 1 &&
    typeof saved.savedAt === 'string' &&
    !!scene &&
    Array.isArray(scene.parts) &&
    scene.parts.every(isPartInstance) &&
    Array.isArray(scene.joints) &&
    scene.joints.every(isJointInstance) &&
    isNumberRecord(scene.motorAngles) &&
    (scene.selectedInstance === null || typeof scene.selectedInstance === 'string') &&
    (scene.selectedJoint === null || typeof scene.selectedJoint === 'string')
  );
}

function snapshotFromCurrentScene(scene: SceneSnapshot): SceneSnapshot {
  return {
    parts: scene.parts.map((part) => ({
      ...part,
      position: [...part.position],
      quaternion: [...part.quaternion],
      scale: part.scale ? [...part.scale] : undefined,
    })),
    joints: scene.joints.map((joint) => ({
      ...joint,
      pivot: [...joint.pivot],
      axis: [...joint.axis],
    })),
    motorAngles: { ...scene.motorAngles },
    selectedInstance: scene.selectedInstance,
    selectedJoint: scene.selectedJoint,
  };
}

export function createSavedSceneSnapshot(scene: SceneSnapshot): SavedScene {
  return {
    version: 1,
    savedAt: new Date().toISOString(),
    scene: snapshotFromCurrentScene(scene),
  };
}

export function rememberSavedSceneSnapshot(saved: SavedScene): void {
  window.localStorage.setItem(SCENE_SAVE_KEY, JSON.stringify(saved));
}

export function savedSceneToJson(saved: SavedScene): string {
  return `${JSON.stringify(saved, null, 2)}\n`;
}

export function parseSavedSceneSnapshot(text: string): SavedScene | null {
  try {
    const parsed: unknown = JSON.parse(text);
    return isSavedScene(parsed) ? parsed : null;
  } catch {
    return null;
  }
}

export function saveSceneSnapshot(scene: SceneSnapshot): SavedScene {
  const saved = createSavedSceneSnapshot(scene);
  rememberSavedSceneSnapshot(saved);
  return saved;
}

export function loadSavedSceneSnapshot(): SavedScene | null {
  const raw = window.localStorage.getItem(SCENE_SAVE_KEY);
  if (!raw) return null;
  return parseSavedSceneSnapshot(raw);
}
