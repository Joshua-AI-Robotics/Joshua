import { useEffect, useMemo, useRef, useState } from 'react';
import { useFrame } from '@react-three/fiber';
import * as THREE from 'three';
import { loadPart, type LoadedPart, clonePartGroup } from '../ldraw/loadPart';
import { motorSpecForPart } from '../ldraw/motorSpecs';
import { useSceneStore, type PartInstance } from './store';

type Props = {
  instance: PartInstance;
  onLoaded?: (loaded: LoadedPart) => void;
  onGrab?: (instance: PartInstance) => void;
  onContextMenu?: (instance: PartInstance) => void;
  ghost?: boolean;
};

const MOTOR_OUTPUT_SPEED_RAD_PER_SEC = Math.PI * 1.5;

function includesSnapHotspot(
  intersections: { object: THREE.Object3D }[],
): boolean {
  return intersections.some((hit) => hit.object.userData.snapHotspot);
}

function isSecondaryPointer(
  event: { button: number; nativeEvent: PointerEvent },
): boolean {
  return event.button === 2 ||
    event.nativeEvent.button === 2 ||
    event.nativeEvent.buttons === 2;
}

function findDirectChildByFile(group: THREE.Group, fileName: string): THREE.Object3D | null {
  return group.children.find((child) => child.userData?.fileName === fileName) ?? null;
}

export function PlacedPart({ instance, onLoaded, onGrab, onContextMenu, ghost }: Props) {
  const groupRef = useRef<THREE.Group>(null);
  const [loaded, setLoaded] = useState<LoadedPart | null>(null);
  const selected = useSceneStore((s) => s.selectedInstance === instance.instanceId);
  const select = useSceneStore((s) => s.selectInstance);
  const motorAnimating = useSceneStore((s) => !!s.motorAnimations[instance.instanceId]);
  const motorAngle = useSceneStore((s) => s.motorAngles[instance.instanceId] ?? 0);
  const advanceMotorAngle = useSceneStore((s) => s.advanceMotorAngle);
  const motorSpec = motorSpecForPart(instance.partId);

  useEffect(() => {
    let cancelled = false;
    loadPart(instance.partId).then((p) => {
      if (!cancelled) {
        setLoaded(p);
        onLoaded?.(p);
      }
    });
    return () => {
      cancelled = true;
    };
  }, [instance.partId, onLoaded]);

  const clone = useMemo(
    () => (loaded ? clonePartGroup(loaded) : null),
    [loaded],
  );

  const motorOutput = useMemo(
    () => (clone && motorSpec ? findDirectChildByFile(clone, motorSpec.outputFile) : null),
    [clone, motorSpec],
  );
  const motorOutputBaseQuaternion = useMemo(
    () => motorOutput?.quaternion.clone() ?? null,
    [motorOutput],
  );

  const motorAxis = useMemo(
    () =>
      motorSpec
        ? new THREE.Vector3(...motorSpec.axis).normalize()
        : new THREE.Vector3(0, 1, 0),
    [motorSpec],
  );

  useFrame((_, delta) => {
    if (!motorAnimating || !motorSpec) return;
    advanceMotorAngle(instance.instanceId, delta * MOTOR_OUTPUT_SPEED_RAD_PER_SEC);
  });

  useEffect(() => {
    if (!motorOutput || !motorOutputBaseQuaternion || !motorSpec) return;
    const rotation = new THREE.Quaternion().setFromAxisAngle(motorAxis, motorAngle);
    motorOutput.quaternion.copy(motorOutputBaseQuaternion).multiply(rotation);
  }, [motorOutput, motorOutputBaseQuaternion, motorSpec, motorAxis, motorAngle]);

  // Toggle ghost appearance (semi-transparent) while the part is being moved.
  useEffect(() => {
    if (!clone) return;
    clone.traverse((obj) => {
      const mesh = obj as THREE.Mesh;
      if (!mesh.isMesh) return;
      const mats = Array.isArray(mesh.material) ? mesh.material : [mesh.material];
      for (const m of mats) {
        const mm = m as THREE.Material & { transparent: boolean; opacity: number; depthWrite: boolean };
        mm.transparent = !!ghost;
        mm.opacity = ghost ? 0.35 : 1;
        mm.depthWrite = !ghost;
        mm.needsUpdate = true;
      }
    });
  }, [clone, ghost]);

  if (!loaded || !clone) return null;

  return (
    <group
      ref={groupRef}
      position={instance.position}
      quaternion={instance.quaternion}
      scale={instance.scale ?? [1, 1, 1]}
      onPointerDown={(e) => {
        const secondary = isSecondaryPointer(e);
        if (secondary) {
          if (includesSnapHotspot(e.intersections)) return;
          e.stopPropagation();
          e.nativeEvent.preventDefault();
          select(instance.instanceId);
          onContextMenu?.(instance);
          return;
        }
        e.stopPropagation();
        select(instance.instanceId);
        onGrab?.(instance);
      }}
      onContextMenu={(e) => {
        if (includesSnapHotspot(e.intersections)) {
          return;
        }
        e.stopPropagation();
        e.nativeEvent.preventDefault();
        select(instance.instanceId);
        onContextMenu?.(instance);
      }}
    >
      <primitive object={clone} />
      {selected && (
        <mesh>
          <boxGeometry args={[40, 40, 40]} />
          <meshBasicMaterial wireframe color="#ffcc00" />
        </mesh>
      )}
    </group>
  );
}
