import {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
  type SetStateAction,
} from 'react';
import * as THREE from 'three';
import { Canvas } from '@react-three/fiber';
import { Html, OrbitControls } from '@react-three/drei';
import { PartsPalette } from './ui/PartsPalette';
import { Inspector } from './ui/Inspector';
import { Toolbar } from './ui/Toolbar';
import { PlacedPart } from './scene/PlacedPart';
import { SnapOverlay } from './scene/SnapOverlay';
import type { TextFileOpenResult } from './io/browserFiles';
import {
  hotspotKey,
  useInstanceHotspots,
  type InstanceHotspot,
} from './scene/snapHotspots';
import {
  computeSnapConnectionCandidates,
  computeSnapPose,
  connectSnapCandidate,
  placePartOnSnap,
  attachPartToSnap,
  snapCandidateRequiresTransformChange,
  type SnapConnectionCandidate,
} from './scene/snapPlacement';
import {
  connectedInstanceIds,
  useSceneStore,
  type PartInstance,
  type TransformAxis,
} from './scene/store';
import { loadPart, clonePartGroup } from './ldraw/loadPart';
import { findSceneOverlaps, type SceneOverlap } from './scene/collision';
import { UsdaPreview } from './usd/UsdaPreview';
import {
  parseUsdaPreview,
  type UsdaPreviewScene,
} from './usd/usdaPreviewParser';
import './App.css';

const SCREEN_SNAP_THRESHOLD_PX = 50;

type Pose = {
  position: [number, number, number];
  quaternion: [number, number, number, number];
  scale: [number, number, number];
};

type DraggedPartPose = {
  instanceId: string;
  partId: string;
  originalPose: Pose;
  originalMatrix: THREE.Matrix4;
};

type DragState = {
  partId: string;
  source: 'palette' | 'scene';
  instanceId?: string;
  originalPose?: Pose;
  connectedPoses?: DraggedPartPose[];
  moved?: boolean;
  target: InstanceHotspot | null;
  pose: Pose | null;
};

type SnapConnectState = {
  first: InstanceHotspot | null;
  second: InstanceHotspot | null;
  candidates: SnapConnectionCandidate[];
  previewIndex: number;
  checking: boolean;
};

function emptySnapConnectState(): SnapConnectState {
  return { first: null, second: null, candidates: [], previewIndex: 0, checking: false };
}

function poseFromMatrix(m: THREE.Matrix4): Pose {
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

function matrixFromPose(pose: Pose): THREE.Matrix4 {
  return new THREE.Matrix4().compose(
    new THREE.Vector3(...pose.position),
    new THREE.Quaternion(...pose.quaternion),
    new THREE.Vector3(...pose.scale),
  );
}

function poseFromPart(instance: PartInstance): Pose {
  return {
    position: instance.position,
    quaternion: instance.quaternion,
    scale: instance.scale ?? [1, 1, 1],
  };
}

function firstAvailableCandidateIndex(candidates: SnapConnectionCandidate[]): number {
  const index = candidates.findIndex((candidate) => !candidate.blocked);
  return index === -1 ? 0 : index;
}

function jointForSnapPair(a: InstanceHotspot, b: InstanceHotspot): string | null {
  const state = useSceneStore.getState();
  const aHotspotId = a.hotspot.id;
  const bHotspotId = b.hotspot.id;
  const joint = state.joints.find(
    (candidate) =>
      (candidate.parentInstance === a.instanceId &&
        candidate.parentHotspotId === aHotspotId &&
        candidate.childInstance === b.instanceId &&
        candidate.childHotspotId === bHotspotId) ||
      (candidate.parentInstance === b.instanceId &&
        candidate.parentHotspotId === bHotspotId &&
        candidate.childInstance === a.instanceId &&
        candidate.childHotspotId === aHotspotId),
  );
  return joint?.jointId ?? null;
}

function connectedPartPoses(rootInstanceId: string): DraggedPartPose[] {
  const state = useSceneStore.getState();
  const connected = connectedInstanceIds(rootInstanceId, state.joints);

  return state.parts
    .filter((part) => connected.has(part.instanceId))
    .map((part) => {
      const originalPose = poseFromPart(part);
      return {
        instanceId: part.instanceId,
        partId: part.partId,
        originalPose,
        originalMatrix: matrixFromPose(originalPose),
      };
    });
}

export default function App() {
  const [drag, setDrag] = useState<DragState | null>(null);
  const [snapConnect, setSnapConnectRaw] = useState<SnapConnectState>(
    emptySnapConnectState,
  );
  const [sceneOverlaps, setSceneOverlaps] = useState<SceneOverlap[]>([]);
  const [contextMenuFor, setContextMenuFor] = useState<string | null>(null);
  const [gizmoDragging, setGizmoDragging] = useState(false);
  const [usdaPreview, setUsdaPreview] = useState<UsdaPreviewScene | null>(null);
  const dragRef = useRef<DragState | null>(null);
  const snapConnectRef = useRef<SnapConnectState>(emptySnapConnectState());
  const setSnapConnect = useCallback((action: SetStateAction<SnapConnectState>) => {
    setSnapConnectRaw((prev) => {
      const next =
        typeof action === 'function'
          ? (action as (previous: SnapConnectState) => SnapConnectState)(prev)
          : action;
      snapConnectRef.current = next;
      return next;
    });
  }, []);

  const cameraRef = useRef<THREE.Camera | null>(null);
  const canvasContainerRef = useRef<HTMLDivElement>(null);

  const parts = useSceneStore((s) => s.parts);
  const addPart = useSceneStore((s) => s.addPart);
  const selectInstance = useSceneStore((s) => s.selectInstance);
  const selectedInstance = useSceneStore((s) => s.selectedInstance);
  const selectedJoint = useSceneStore((s) => s.selectedJoint);
  const joints = useSceneStore((s) => s.joints);
  const removePart = useSceneStore((s) => s.removePart);
  const removeJoint = useSceneStore((s) => s.removeJoint);
  const anyMotorAnimating = useSceneStore((s) =>
    Object.values(s.motorAnimations).some(Boolean),
  );
  const motorAngles = useSceneStore((s) => s.motorAngles);

  const hotspots = useInstanceHotspots();
  const hotspotsRef = useRef<InstanceHotspot[]>([]);

  useEffect(() => {
    dragRef.current = drag;
  }, [drag]);

  useEffect(() => {
    snapConnectRef.current = snapConnect;
  }, [snapConnect]);

  useEffect(() => {
    hotspotsRef.current = hotspots;
  }, [hotspots]);

  const grid = useMemo(
    () => new THREE.GridHelper(2000, 100, 0x555555, 0x333333),
    [],
  );
  const axes = useMemo(() => new THREE.AxesHelper(80), []);
  const previewCandidate = snapConnect.candidates[snapConnect.previewIndex] ?? null;
  const contextMenuInstance = useMemo(
    () => parts.find((p) => p.instanceId === contextMenuFor) ?? null,
    [parts, contextMenuFor],
  );
  const previewPoses = useMemo(
    () =>
      previewCandidate
        ? previewCandidate.assemblyTransforms.map((placement) => ({
            instanceId: placement.instanceId,
            partId: placement.partId,
            pose: poseFromMatrix(placement.transform),
          }))
        : [],
    [previewCandidate],
  );
  const highlightedSnapKeys: string[] = [];
  if (drag?.target) {
    highlightedSnapKeys.push(hotspotKey(drag.target));
  } else {
    if (snapConnect.first) highlightedSnapKeys.push(hotspotKey(snapConnect.first));
    if (snapConnect.second) highlightedSnapKeys.push(hotspotKey(snapConnect.second));
  }
  const motionActive = Boolean(drag) || gizmoDragging || anyMotorAnimating;
  const visibleSceneOverlaps = useMemo(
    () => (motionActive ? [] : sceneOverlaps),
    [motionActive, sceneOverlaps],
  );
  const collisionInstanceIds = useMemo(() => {
    const ids = new Set<string>();
    for (const overlap of visibleSceneOverlaps) {
      ids.add(overlap.aInstanceId);
      ids.add(overlap.bInstanceId);
    }
    return [...ids];
  }, [visibleSceneOverlaps]);

  useEffect(() => {
    if (motionActive) return;

    let cancelled = false;
    const handle = window.setTimeout(() => {
      findSceneOverlaps(parts, joints, motorAngles)
        .then((overlaps) => {
          if (!cancelled) setSceneOverlaps(overlaps);
        })
        .catch((err) => console.warn('findSceneOverlaps failed', err));
    }, 120);

    return () => {
      cancelled = true;
      window.clearTimeout(handle);
    };
  }, [parts, joints, motorAngles, motionActive]);

  const restoreIfMoved = useCallback((d: DragState | null) => {
    if (!d || d.source !== 'scene' || !d.moved) {
      return;
    }
    const poses = d.connectedPoses ?? [];
    if (poses.length > 0) {
      useSceneStore.getState().setTransforms(
        poses.map((pose) => ({
          instanceId: pose.instanceId,
          transform: pose.originalMatrix,
        })),
      );
      return;
    }
    if (d.originalPose && d.instanceId) {
      useSceneStore.getState().setTransform(d.instanceId, matrixFromPose(d.originalPose));
    }
  }, []);

  const cancelDrag = useCallback(() => {
    restoreIfMoved(dragRef.current);
    dragRef.current = null;
    setDrag(null);
    setSnapConnect(emptySnapConnectState());
  }, [restoreIfMoved, setSnapConnect]);

  const beginDrag = useCallback((partId: string) => {
    setSnapConnect(emptySnapConnectState());
    const next: DragState = { partId, source: 'palette', target: null, pose: null };
    dragRef.current = next;
    setDrag(next);
  }, [setSnapConnect]);

  const openContextMenuFor = useCallback((inst: PartInstance) => {
    setSnapConnect(emptySnapConnectState());
    setContextMenuFor(inst.instanceId);
  }, [setSnapConnect]);

  const openContextMenuForHotspot = useCallback((spot: InstanceHotspot) => {
    const inst = useSceneStore
      .getState()
      .parts.find((part) => part.instanceId === spot.instanceId);
    if (!inst) return;
    openContextMenuFor(inst);
  }, [openContextMenuFor]);

  const closeContextMenu = useCallback(() => setContextMenuFor(null), []);

  const openContextMenuForSelection = useCallback(() => {
    const selected = useSceneStore.getState().selectedInstance;
    if (!selected) return;
    setSnapConnect(emptySnapConnectState());
    setContextMenuFor(selected);
  }, [setSnapConnect]);

  const removeContextMenuPart = useCallback(
    (instanceId: string) => {
      removePart(instanceId);
      setContextMenuFor(null);
      setSnapConnect(emptySnapConnectState());
    },
    [removePart, setSnapConnect],
  );

  const beginMove = useCallback((inst: PartInstance) => {
    const connectedPoses = connectedPartPoses(inst.instanceId);
    setSnapConnect(emptySnapConnectState());
    const next: DragState = {
      partId: inst.partId,
      source: 'scene',
      instanceId: inst.instanceId,
      originalPose: poseFromPart(inst),
      connectedPoses,
      moved: false,
      target: null,
      pose: poseFromPart(inst),
    };
    dragRef.current = next;
    setDrag(next);
  }, [setSnapConnect]);

  const beginMoveFromHotspot = useCallback((spot: InstanceHotspot) => {
    const inst = useSceneStore
      .getState()
      .parts.find((part) => part.instanceId === spot.instanceId);
    if (!inst) return;
    selectInstance(inst.instanceId);
    beginMove(inst);
  }, [beginMove, selectInstance]);

  // Keep pointer listeners mounted so drags that start on snap hit-targets do
  // not lose the first fast pointermove before React commits drag state.
  useEffect(() => {
    const applyScenePose = (current: DragState, pose: Pose) => {
      const movingInstanceId = current.instanceId;
      if (!movingInstanceId) return;
      const connectedPoses = current.connectedPoses ?? [];
      const nextRoot = matrixFromPose(pose);
      const rootOriginal = connectedPoses.find(
        (entry) => entry.instanceId === movingInstanceId,
      );
      const state = useSceneStore.getState();
      if (!rootOriginal) {
        state.setTransform(movingInstanceId, nextRoot);
        return;
      }

      const delta = new THREE.Matrix4().multiplyMatrices(
        nextRoot,
        rootOriginal.originalMatrix.clone().invert(),
      );
      state.setTransforms(
        connectedPoses.map((entry) => ({
          instanceId: entry.instanceId,
          transform: new THREE.Matrix4().multiplyMatrices(
            delta,
            entry.originalMatrix,
          ),
        })),
      );
    };

    let scenePoseFrame: number | null = null;
    let pendingScenePose: {
      partId: string;
      instanceId?: string;
      pose: Pose;
    } | null = null;

    const flushScenePose = () => {
      const pending = pendingScenePose;
      pendingScenePose = null;
      scenePoseFrame = null;
      if (!pending) return;
      const activeDrag = dragRef.current;
      if (
        !activeDrag ||
        activeDrag.source !== 'scene' ||
        activeDrag.partId !== pending.partId ||
        activeDrag.instanceId !== pending.instanceId
      ) {
        return;
      }
      applyScenePose(activeDrag, pending.pose);
    };

    const scheduleScenePose = (current: DragState, pose: Pose) => {
      pendingScenePose = {
        partId: current.partId,
        instanceId: current.instanceId,
        pose,
      };
      if (scenePoseFrame !== null) return;
      scenePoseFrame = window.requestAnimationFrame(flushScenePose);
    };

    const onMove = (ev: PointerEvent) => {
      const current = dragRef.current;
      if (!current) return;
      const dragPartId = current.partId;
      const dragSource = current.source;
      const movingInstanceId = current.instanceId;
      const originalQ = current.originalPose?.quaternion ?? ([0, 0, 0, 1] as const);
      const originalScale = current.originalPose?.scale ?? ([1, 1, 1] as const);
      const connectedPoses = current.connectedPoses ?? [];
      const movingInstanceIds = new Set(connectedPoses.map((pose) => pose.instanceId));
      if (movingInstanceId) movingInstanceIds.add(movingInstanceId);

      const container = canvasContainerRef.current;
      const camera = cameraRef.current;
      if (!container || !camera) return;
      const rect = container.getBoundingClientRect();
      const inside =
        ev.clientX >= rect.left &&
        ev.clientX <= rect.right &&
        ev.clientY >= rect.top &&
        ev.clientY <= rect.bottom;
      if (!inside) {
        const next = { ...current, target: null, pose: null };
        dragRef.current = next;
        if (dragSource === 'palette' || current.target || current.pose) {
          setDrag((prev) => {
            if (!prev || prev.partId !== dragPartId) return prev;
            return next;
          });
        }
        return;
      }
      const screenX = ev.clientX - rect.left;
      const screenY = ev.clientY - rect.top;
      const ndc = new THREE.Vector2(
        (screenX / rect.width) * 2 - 1,
        -((screenY / rect.height) * 2 - 1),
      );

      // Find nearest hotspot within screen threshold (skip the dragged assembly).
      let nearest: InstanceHotspot | null = null;
      let nearestDistPx = SCREEN_SNAP_THRESHOLD_PX;
      const projected = new THREE.Vector3();
      for (const s of hotspotsRef.current) {
        if (movingInstanceIds.has(s.instanceId)) continue;
        projected.copy(s.worldCenter).project(camera);
        if (projected.z <= -1 || projected.z >= 1) continue;
        const dxPx = ((projected.x - ndc.x) * rect.width) / 2;
        const dyPx = ((projected.y - ndc.y) * rect.height) / 2;
        const d = Math.hypot(dxPx, dyPx);
        if (d < nearestDistPx) {
          nearest = s;
          nearestDistPx = d;
        }
      }

      if (nearest) {
        const tgt = nearest;
        const nextTargetKey = hotspotKey(tgt);
        const currentTargetKey = current.target ? hotspotKey(current.target) : null;
        if (currentTargetKey === nextTargetKey && current.pose) return;
        computeSnapPose(dragPartId, tgt)
          .then(({ transform }) => {
            const activeDrag = dragRef.current;
            if (!activeDrag ||
              activeDrag.partId !== dragPartId ||
              activeDrag.source !== dragSource ||
              activeDrag.instanceId !== movingInstanceId
            ) {
              return;
            }
            const p = new THREE.Vector3();
            const q = new THREE.Quaternion();
            const sv = new THREE.Vector3();
            transform.decompose(p, q, sv);
            const nextPose: Pose = {
              position: [p.x, p.y, p.z],
              quaternion: [q.x, q.y, q.z, q.w],
              scale:
                dragSource === 'scene'
                  ? [originalScale[0], originalScale[1], originalScale[2]]
                  : [sv.x, sv.y, sv.z],
            };
            const previousTargetKey = activeDrag.target
              ? hotspotKey(activeDrag.target)
              : null;
            const next = {
              ...activeDrag,
              target: tgt,
              pose: nextPose,
              moved: dragSource === 'scene' ? true : activeDrag.moved,
            };
            dragRef.current = next;
            if (dragSource === 'scene') scheduleScenePose(next, nextPose);
            if (dragSource === 'palette' || previousTargetKey !== nextTargetKey) {
              setDrag((prev) => {
                if (!prev || prev.partId !== dragPartId) return prev;
                return next;
              });
            }
          })
          .catch((err) => console.warn('computeSnapPose failed', err));
        return;
      }

      // Fall back: ray-plane intersection with y=0 in LDU scene-space.
      const ray = new THREE.Raycaster();
      ray.setFromCamera(ndc, camera);
      const plane = new THREE.Plane(new THREE.Vector3(0, 1, 0), 0);
      const hit = new THREE.Vector3();
      const intersected = ray.ray.intersectPlane(plane, hit);
      const fallbackQ: [number, number, number, number] =
        dragSource === 'scene'
          ? [originalQ[0], originalQ[1], originalQ[2], originalQ[3]]
          : [0, 0, 0, 1];
      const nextPose: Pose | null = intersected
        ? {
            position: [hit.x, hit.y, hit.z],
            quaternion: fallbackQ,
            scale:
              dragSource === 'scene'
                ? [originalScale[0], originalScale[1], originalScale[2]]
                : [1, 1, 1],
          }
        : null;
      const next = {
        ...current,
        target: null,
        pose: nextPose,
        moved: dragSource === 'scene' && nextPose ? true : current.moved,
      };
      dragRef.current = next;
      if (dragSource === 'scene' && nextPose) scheduleScenePose(next, nextPose);
      if (dragSource === 'palette' || current.target) {
        setDrag((prev) => {
          if (!prev || prev.partId !== dragPartId) return prev;
          return next;
        });
      }
    };

    const onUp = async (ev: PointerEvent) => {
      flushScenePose();
      const snapshot = dragRef.current;
      dragRef.current = null;
      setDrag(null);
      if (!snapshot) return;

      const container = canvasContainerRef.current;
      if (!container) return;
      const rect = container.getBoundingClientRect();
      const inside =
        ev.clientX >= rect.left &&
        ev.clientX <= rect.right &&
        ev.clientY >= rect.top &&
        ev.clientY <= rect.bottom;

      if (snapshot.source === 'scene') {
        if (!inside) {
          restoreIfMoved(snapshot);
          return;
        }
        if (!snapshot.moved) return;
        if (snapshot.target && snapshot.instanceId) {
          try {
            await attachPartToSnap(snapshot.instanceId, snapshot.target);
          } catch (e) {
            console.error('attachPartToSnap failed', e);
          }
        }
        return;
      }

      // Palette source.
      if (!inside) return;
      if (snapshot.target) {
        try {
          await placePartOnSnap(snapshot.partId, snapshot.target);
        } catch (e) {
          console.error('placePartOnSnap failed', e);
        }
        return;
      }
      const state = useSceneStore.getState();
      if (state.parts.length === 0 && snapshot.pose) {
        const m = new THREE.Matrix4();
        m.compose(
          new THREE.Vector3(...snapshot.pose.position),
          new THREE.Quaternion(...snapshot.pose.quaternion),
          new THREE.Vector3(...snapshot.pose.scale),
        );
        addPart(snapshot.partId, m);
      }
    };

    window.addEventListener('pointermove', onMove);
    window.addEventListener('pointerup', onUp);
    return () => {
      if (scenePoseFrame !== null) {
        window.cancelAnimationFrame(scenePoseFrame);
      }
      window.removeEventListener('pointermove', onMove);
      window.removeEventListener('pointerup', onUp);
    };
  }, [addPart, restoreIfMoved]);

  // Keyboard: Esc cancels drag, Delete/Backspace removes selection.
  useEffect(() => {
    const onKey = (ev: KeyboardEvent) => {
      const tag = (document.activeElement?.tagName ?? '').toUpperCase();
      const inField =
        tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA';
      if (ev.key === 'Escape') {
        restoreIfMoved(dragRef.current);
        dragRef.current = null;
        setDrag(null);
        setSnapConnect(emptySnapConnectState());
        setContextMenuFor(null);
        return;
      }
      if ((ev.key === 'Delete' || ev.key === 'Backspace') && !inField) {
        if (selectedInstance) {
          removePart(selectedInstance);
          ev.preventDefault();
        } else if (selectedJoint) {
          removeJoint(selectedJoint);
          ev.preventDefault();
        }
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [
    selectedInstance,
    selectedJoint,
    removePart,
    removeJoint,
    restoreIfMoved,
    setSnapConnect,
  ]);

  const handlePick = useCallback(
    (spot: InstanceHotspot) => {
      const activeDrag = dragRef.current;
      if (activeDrag) {
        if (
          activeDrag.source !== 'scene' ||
          activeDrag.moved ||
          activeDrag.instanceId !== spot.instanceId
        ) {
          return;
        }
        dragRef.current = null;
        setDrag(null);
      }
      selectInstance(spot.instanceId);
      setSnapConnect((prev) => {
        const spotKey = hotspotKey(spot);
        if (!prev.first || prev.candidates.length > 0) {
          return {
            first: spot,
            second: null,
            candidates: [],
            previewIndex: 0,
            checking: false,
          };
        }
        if (hotspotKey(prev.first) === spotKey) {
          return emptySnapConnectState();
        }
        if (prev.first.instanceId === spot.instanceId) {
          return {
            first: spot,
            second: null,
            candidates: [],
            previewIndex: 0,
            checking: false,
          };
        }
        const first = prev.first;
        const connectedJointId = jointForSnapPair(first, spot);
        if (connectedJointId) {
          useSceneStore.getState().removeJoint(connectedJointId);
          return emptySnapConnectState();
        }
        void computeSnapConnectionCandidates(first, spot).then((candidates) => {
          const current = snapConnectRef.current;
          if (
            !current.first ||
            !current.second ||
            hotspotKey(current.first) !== hotspotKey(first) ||
            hotspotKey(current.second) !== spotKey
          ) {
            return;
          }

          const immediate = candidates.find(
            (candidate) =>
              candidate.id === 'current' &&
              !candidate.blocked &&
              !snapCandidateRequiresTransformChange(candidate),
          );
          if (immediate && connectSnapCandidate(immediate)) {
            setSnapConnect(emptySnapConnectState());
            return;
          }

          setSnapConnect({
            ...current,
            candidates,
            previewIndex: firstAvailableCandidateIndex(candidates),
            checking: false,
          });
        }).catch((err) => {
          console.warn('computeSnapConnectionCandidates failed', err);
          const current = snapConnectRef.current;
          if (
            !current.first ||
            !current.second ||
            hotspotKey(current.first) !== hotspotKey(first) ||
            hotspotKey(current.second) !== spotKey
          ) {
            return;
          }
          setSnapConnect({ ...current, checking: false });
        });
        return {
          first,
          second: spot,
          candidates: [],
          previewIndex: 0,
          checking: true,
        };
      });
    },
    [selectInstance, setSnapConnect],
  );

  const commitSnapChoice = useCallback((candidate: SnapConnectionCandidate) => {
    if (connectSnapCandidate(candidate)) {
      setSnapConnect(emptySnapConnectState());
    }
  }, [setSnapConnect]);

  const previewSnapChoice = useCallback((previewIndex: number) => {
    setSnapConnect((prev) => ({ ...prev, previewIndex }));
  }, [setSnapConnect]);

  const openUsdaPreview = useCallback(
    (file: TextFileOpenResult) => {
      restoreIfMoved(dragRef.current);
      dragRef.current = null;
      setDrag(null);
      setSnapConnect(emptySnapConnectState());
      setContextMenuFor(null);
      setUsdaPreview(parseUsdaPreview(file.name, file.text));
    },
    [restoreIfMoved, setSnapConnect],
  );

  return (
    <div className="app">
      <Toolbar onClearDrag={cancelDrag} onOpenUsdaPreview={openUsdaPreview} />
      <div className="main">
        <PartsPalette
          draggingId={drag?.partId ?? null}
          onPartDragStart={beginDrag}
        />
        <div
          ref={canvasContainerRef}
          className="viewport"
          onContextMenu={(e) => {
            e.preventDefault();
            openContextMenuForSelection();
          }}
        >
          <Canvas
            camera={{ position: [200, 200, 200], fov: 45, near: 1, far: 5000 }}
            onCreated={({ camera }) => {
              cameraRef.current = camera;
            }}
            onPointerMissed={() => {
              selectInstance(null);
              setSnapConnect(emptySnapConnectState());
              setContextMenuFor(null);
            }}
          >
            <color attach="background" args={['#1a1a22']} />
            <ambientLight intensity={0.6} />
            <directionalLight position={[200, 400, 300]} intensity={0.9} />
            <directionalLight position={[-300, 200, -200]} intensity={0.35} />
            <primitive object={grid} />
            <primitive object={axes} />
            {usdaPreview ? (
              <UsdaPreview scene={usdaPreview} />
            ) : (
              <>
                {parts.map((p) => (
                  <PlacedPart
                    key={p.instanceId}
                    instance={p}
                    onGrab={beginMove}
                    onContextMenu={openContextMenuFor}
                    ghost={
                      drag?.source === 'scene' &&
                      (drag.connectedPoses?.some(
                        (pose) => pose.instanceId === p.instanceId,
                      ) ??
                        drag.instanceId === p.instanceId)
                    }
                  />
                ))}
                {contextMenuInstance && (
                  <PartActionGizmo
                    instance={contextMenuInstance}
                    onClose={closeContextMenu}
                    onRemove={removeContextMenuPart}
                    onDragChange={setGizmoDragging}
                  />
                )}
                <SnapOverlay
                  spots={hotspots}
                  onPickTarget={handlePick}
                  onGrabTarget={beginMoveFromHotspot}
                  onContextTarget={openContextMenuForHotspot}
                  highlightKeys={highlightedSnapKeys}
                  collisionInstanceIds={collisionInstanceIds}
                />
                {drag && drag.pose && drag.source === 'palette' && (
                  <GhostPreview partId={drag.partId} pose={drag.pose} />
                )}
                {previewCandidate &&
                  previewPoses.map((preview) => (
                    <GhostPreview
                      key={preview.instanceId}
                      partId={preview.partId}
                      pose={preview.pose}
                      invalid={previewCandidate.blocked}
                    />
                  ))}
              </>
            )}
            <OrbitControls
              makeDefault
              enableDamping
              enabled={!drag && !gizmoDragging}
            />
          </Canvas>
          {usdaPreview && (
            <div className="usda-preview-panel">
              <div className="usda-preview-title">USDA Preview</div>
              <div className="usda-preview-name">{usdaPreview.fileName}</div>
              <div className="usda-preview-meta">
                {usdaPreview.bodies.length} bodies · {usdaPreview.meshCount} meshes ·{' '}
                {usdaPreview.joints.length} joints
              </div>
              <button onClick={() => setUsdaPreview(null)}>Close preview</button>
            </div>
          )}
          {!usdaPreview && drag && (
            <div className="pending-banner">
              {drag.target
                ? 'Release to connect'
                : drag.source === 'scene'
                  ? 'Drag assembly, snap to a marker, or release in empty space · Esc restores'
                  : parts.length === 0
                    ? 'Release on the ground to place'
                    : 'Drag over a snap marker · Esc cancels'}
            </div>
          )}
          {!usdaPreview && !drag && snapConnect.first && snapConnect.candidates.length === 0 && (
            <div className="pending-banner">
              {snapConnect.checking
                ? 'Checking connection'
                : snapConnect.second
                  ? 'No possible connection'
                  : 'Select another snap'}
            </div>
          )}
          {!usdaPreview && !drag && snapConnect.candidates.length > 0 && (
            <SnapChoicePanel
              candidates={snapConnect.candidates}
              previewIndex={snapConnect.previewIndex}
              onPreview={previewSnapChoice}
              onCommit={commitSnapChoice}
              onCancel={() => setSnapConnect(emptySnapConnectState())}
            />
          )}
          {!usdaPreview && visibleSceneOverlaps.length > 0 && (
            <div className="scene-warning">
              Overlap detected
              <span>
                {visibleSceneOverlaps.length === 1
                  ? '1 part pair conflicts'
                  : `${visibleSceneOverlaps.length} part pairs conflict`}
              </span>
            </div>
          )}
        </div>
        {usdaPreview ? (
          <UsdaPreviewInspector
            scene={usdaPreview}
            onClose={() => setUsdaPreview(null)}
          />
        ) : (
          <Inspector />
        )}
      </div>
    </div>
  );
}

function UsdaPreviewInspector({
  scene,
  onClose,
}: {
  scene: UsdaPreviewScene;
  onClose: () => void;
}) {
  return (
    <div className="inspector">
      <h3>USDA Preview</h3>

      <div className="section">
        <h4>File</h4>
        <div className="row">
          <span>Name:</span>
          <code>{scene.fileName}</code>
        </div>
        <div className="row">
          <span>Bodies:</span>
          <code>{scene.bodies.length}</code>
        </div>
        <div className="row">
          <span>Meshes:</span>
          <code>{scene.meshCount}</code>
        </div>
        <div className="row">
          <span>Joints:</span>
          <code>{scene.joints.length}</code>
        </div>
        <button onClick={onClose}>Close preview</button>
      </div>

      <div className="section">
        <h4>Bodies</h4>
        <ul className="joint-list">
          {scene.bodies.map((body) => (
            <li key={body.name}>
              {body.name} · {body.meshes.length} mesh
              {body.meshes.length === 1 ? '' : 'es'}
            </li>
          ))}
        </ul>
      </div>

      <div className="section">
        <h4>Joints</h4>
        {scene.joints.length === 0 ? (
          <p className="hint">No joints found.</p>
        ) : (
          <ul className="joint-list">
            {scene.joints.map((joint) => (
              <li key={joint.name}>
                {joint.type.replace(/^Physics|Joint$/g, '').toLowerCase()} -{' '}
                {(joint.body0 ?? '?').slice(-8)} ↔ {(joint.body1 ?? '?').slice(-8)}
              </li>
            ))}
          </ul>
        )}
      </div>
    </div>
  );
}

function SnapChoicePanel({
  candidates,
  previewIndex,
  onPreview,
  onCommit,
  onCancel,
}: {
  candidates: SnapConnectionCandidate[];
  previewIndex: number;
  onPreview: (index: number) => void;
  onCommit: (candidate: SnapConnectionCandidate) => void;
  onCancel: () => void;
}) {
  return (
    <div className="snap-choice-panel">
      <div className="snap-choice-title">Connection result</div>
      <div className="snap-choice-options">
        {candidates.map((candidate, index) => (
          <button
            key={candidate.id}
            className={[
              index === previewIndex ? 'active' : '',
              candidate.blocked ? 'blocked' : '',
            ].join(' ')}
            onPointerEnter={() => onPreview(index)}
            onFocus={() => onPreview(index)}
            onClick={() => {
              if (!candidate.blocked) onCommit(candidate);
            }}
            aria-disabled={candidate.blocked}
            title={
              candidate.blocked
                ? `Blocked: ${candidate.blockedReason ?? 'not possible'}${
                    candidate.overlapVolume > 0
                      ? ` (${candidate.overlapVolume.toFixed(0)} LDU^3)`
                      : ''
                  }`
                : undefined
            }
          >
            {candidate.blocked ? `Blocked - ${candidate.label}` : candidate.label}
          </button>
        ))}
      </div>
      <button className="snap-choice-cancel" onClick={onCancel}>
        Cancel
      </button>
    </div>
  );
}

const ROTATE_AXES: TransformAxis[] = ['x', 'y', 'z'];

function PartActionGizmo({
  instance,
  onClose,
  onRemove,
  onDragChange,
}: {
  instance: PartInstance;
  onClose: () => void;
  onRemove: (instanceId: string) => void;
  onDragChange?: (dragging: boolean) => void;
}) {
  const rotatePart = useSceneStore((s) => s.rotatePart);
  const mirrorPart = useSceneStore((s) => s.mirrorPart);
  const [offset, setOffset] = useState({ x: 0, y: 0 });
  const [dragging, setDragging] = useState(false);
  const dragStateRef = useRef<{
    pointerId: number;
    startClientX: number;
    startClientY: number;
    startOffsetX: number;
    startOffsetY: number;
  } | null>(null);
  const stop = (e: React.MouseEvent | React.PointerEvent) => e.stopPropagation();

  // Make sure we always release the App-level "gizmoDragging" flag if this
  // component unmounts mid-drag.
  useEffect(() => {
    return () => {
      if (dragStateRef.current) {
        onDragChange?.(false);
        dragStateRef.current = null;
      }
    };
  }, [onDragChange]);

  const onPanelPointerDown = (e: React.PointerEvent<HTMLDivElement>) => {
    // Always stop propagation so React listeners on the canvas don't react.
    e.stopPropagation();
    // If the press lands on a button, let the button's click run normally.
    const target = e.target as HTMLElement;
    if (target.closest('button')) return;
    // preventDefault keeps the browser from initiating native gestures
    // (text selection, etc.).
    e.preventDefault();

    // Capture the pointer so all subsequent pointer events route to the panel.
    // This is what actually keeps the underlying canvas from picking up the
    // drag — react event propagation alone isn't enough because R3F /
    // OrbitControls listen via native DOM listeners on the canvas.
    e.currentTarget.setPointerCapture(e.pointerId);

    dragStateRef.current = {
      pointerId: e.pointerId,
      startClientX: e.clientX,
      startClientY: e.clientY,
      startOffsetX: offset.x,
      startOffsetY: offset.y,
    };
    setDragging(true);
    onDragChange?.(true);
  };

  const onPanelPointerMove = (e: React.PointerEvent<HTMLDivElement>) => {
    const ds = dragStateRef.current;
    if (!ds || ds.pointerId !== e.pointerId) return;
    e.stopPropagation();
    setOffset({
      x: ds.startOffsetX + (e.clientX - ds.startClientX),
      y: ds.startOffsetY + (e.clientY - ds.startClientY),
    });
  };

  const finishDrag = (e: React.PointerEvent<HTMLDivElement>) => {
    const ds = dragStateRef.current;
    if (!ds || ds.pointerId !== e.pointerId) return;
    e.stopPropagation();
    if (e.currentTarget.hasPointerCapture(e.pointerId)) {
      e.currentTarget.releasePointerCapture(e.pointerId);
    }
    dragStateRef.current = null;
    setDragging(false);
    onDragChange?.(false);
  };

  return (
    <Html
      position={instance.position}
      center
      zIndexRange={[100, 0]}
      style={{ pointerEvents: 'auto' }}
    >
      <div
        className={`part-action-gizmo${dragging ? ' dragging' : ''}`}
        style={{ transform: `translate(${offset.x}px, ${offset.y}px)` }}
        onPointerDown={onPanelPointerDown}
        onPointerMove={onPanelPointerMove}
        onPointerUp={finishDrag}
        onPointerCancel={finishDrag}
        onClick={stop}
        onContextMenu={(e) => {
          e.preventDefault();
          stop(e);
        }}
      >
        <div
          className="pag-drag-handle"
          title="Drag to reposition"
          aria-hidden="true"
        >
          <span className="pag-grip" />
          <span className="pag-grip" />
          <span className="pag-grip" />
        </div>
        <button
          className="pag-close"
          onClick={onClose}
          title="Close (Esc)"
          aria-label="Close"
        >
          ×
        </button>
        <div className="pag-section">
          <div className="pag-section-label">Rotation</div>
          {ROTATE_AXES.map((axis) => (
            <div key={`rot-${axis}`} className={`pag-row pag-axis-${axis}`}>
              <button
                className="pag-arrow"
                onClick={() => rotatePart(instance.instanceId, axis, -90)}
                title={`Rotate -90° around ${axis.toUpperCase()}`}
                aria-label={`Rotate -90 around ${axis.toUpperCase()}`}
              >
                ↺
              </button>
              <span className="pag-axis-label">{axis.toUpperCase()}</span>
              <button
                className="pag-arrow"
                onClick={() => rotatePart(instance.instanceId, axis, 90)}
                title={`Rotate +90° around ${axis.toUpperCase()}`}
                aria-label={`Rotate +90 around ${axis.toUpperCase()}`}
              >
                ↻
              </button>
            </div>
          ))}
        </div>
        <div className="pag-section">
          <div className="pag-section-label">Mirroring</div>
          <div className="pag-mirror-row">
            {ROTATE_AXES.map((axis) => (
              <button
                key={`mir-${axis}`}
                className={`pag-mirror pag-axis-${axis}`}
                onClick={() => mirrorPart(instance.instanceId, axis)}
                title={`Mirror across ${axis.toUpperCase()}`}
                aria-label={`Mirror across ${axis.toUpperCase()}`}
              >
                ⇋ {axis.toUpperCase()}
              </button>
            ))}
          </div>
        </div>
        <div className="pag-section pag-danger-section">
          <button
            className="pag-remove"
            onClick={() => onRemove(instance.instanceId)}
          >
            Remove part
          </button>
        </div>
      </div>
    </Html>
  );
}

function GhostPreview({
  partId,
  pose,
  invalid = false,
}: {
  partId: string;
  pose: Pose;
  invalid?: boolean;
}) {
  const [group, setGroup] = useState<THREE.Group | null>(null);

  useEffect(() => {
    let cancelled = false;
    loadPart(partId).then((p) => {
      if (cancelled) return;
      const clone = clonePartGroup(p);
      clone.traverse((obj) => {
        const mesh = obj as THREE.Mesh;
        if (!mesh.isMesh) return;
        const orig = mesh.material as THREE.Material | THREE.Material[];
        const mats = Array.isArray(orig) ? orig : [orig];
        const ghosts = mats.map((m) => {
          const nm = m.clone() as THREE.Material & {
            transparent: boolean;
            opacity: number;
            depthWrite: boolean;
            color?: THREE.Color;
          };
          nm.transparent = true;
          nm.opacity = invalid ? 0.3 : 0.4;
          nm.depthWrite = false;
          if (invalid && nm.color) nm.color.set('#ff4f4f');
          return nm;
        });
        mesh.material = Array.isArray(orig) ? ghosts : ghosts[0];
      });
      setGroup(clone);
    });
    return () => {
      cancelled = true;
    };
  }, [partId, invalid]);

  if (!group) return null;
  return (
    <group position={pose.position} quaternion={pose.quaternion} scale={pose.scale}>
      <primitive object={group} />
    </group>
  );
}
