import { useMemo, useState } from 'react';
import { hotspotKey, type InstanceHotspot } from './snapHotspots';
import { useSceneStore } from './store';

type SnapOverlayProps = {
  spots: InstanceHotspot[];
  onPickTarget: (spot: InstanceHotspot) => void;
  onGrabTarget?: (spot: InstanceHotspot) => void;
  onContextTarget?: (spot: InstanceHotspot) => void;
  highlightKeys?: string[];
  collisionInstanceIds?: string[];
};

function isSecondaryPointer(
  event: { button: number; nativeEvent: PointerEvent },
): boolean {
  return event.button === 2 ||
    event.nativeEvent.button === 2 ||
    event.nativeEvent.buttons === 2;
}

function noRaycast(): void {}

function spotWithPreferredSide(
  spot: InstanceHotspot,
  point: { x: number; y: number; z: number },
): InstanceHotspot {
  const projection =
    (point.x - spot.worldCenter.x) * spot.worldAxis.x +
    (point.y - spot.worldCenter.y) * spot.worldAxis.y +
    (point.z - spot.worldCenter.z) * spot.worldAxis.z;
  if (Math.abs(projection) < 0.5) return spot;
  return { ...spot, preferredSide: projection > 0 ? 1 : -1 };
}

export function SnapOverlay({
  spots,
  onPickTarget,
  onGrabTarget,
  onContextTarget,
  highlightKeys = [],
  collisionInstanceIds = [],
}: SnapOverlayProps) {
  const joints = useSceneStore((s) => s.joints);
  const [hoveredKey, setHoveredKey] = useState<string | null>(null);
  const collisionInstances = useMemo(
    () => new Set(collisionInstanceIds),
    [collisionInstanceIds],
  );
  const connectedKeys = useMemo(() => {
    const keys = new Set<string>();
    for (const joint of joints) {
      if (joint.parentHotspotId) {
        keys.add(`${joint.parentInstance}|${joint.parentHotspotId}`);
      }
      if (joint.childHotspotId) {
        keys.add(`${joint.childInstance}|${joint.childHotspotId}`);
      }
    }
    return keys;
  }, [joints]);

  return (
    <group>
      {spots.map((s) => {
        const key = hotspotKey(s);
        const active = highlightKeys.includes(key);
        const hovered = hoveredKey === key;
        const visible = active || hovered;
        const connected = connectedKeys.has(key);
        const colliding = collisionInstances.has(s.instanceId);
        const radiusScale = active ? 1.8 : colliding ? 1.5 : connected ? 1.3 : 1;
        const baseRadius = Math.max(3, s.hotspot.radius);
        const hitRadius = Math.max(14, baseRadius * 2.4);
        const color = active
          ? '#ffcc00'
          : colliding
            ? '#ff4f4f'
            : connected
            ? '#2ee67d'
            : s.hotspot.slide
              ? '#00c8ff'
              : '#ff8800';
        return (
          <group
            key={key}
            position={s.worldCenter.toArray()}
            onPointerOver={(e) => {
              e.stopPropagation();
              setHoveredKey(key);
            }}
            onPointerOut={(e) => {
              e.stopPropagation();
              setHoveredKey((prev) => (prev === key ? null : prev));
            }}
            onPointerDown={(e) => {
              e.stopPropagation();
              if (isSecondaryPointer(e)) {
                e.nativeEvent.preventDefault();
                onContextTarget?.(s);
                return;
              }
              onGrabTarget?.(s);
            }}
            onClick={(e) => {
              e.stopPropagation();
              onPickTarget(spotWithPreferredSide(s, e.point));
            }}
            onContextMenu={(e) => {
              e.stopPropagation();
              e.nativeEvent.preventDefault();
              onContextTarget?.(s);
            }}
          >
            <mesh userData={{ snapHotspot: true }}>
              <sphereGeometry args={[hitRadius, 12, 8]} />
              <meshBasicMaterial
                color="#ffffff"
                transparent
                opacity={0}
                depthTest={false}
                depthWrite={false}
              />
            </mesh>
            {visible && (
              <mesh
                renderOrder={999}
                raycast={noRaycast}
                userData={{ snapHotspot: true }}
              >
                <sphereGeometry args={[baseRadius * radiusScale, 16, 10]} />
                <meshBasicMaterial
                  color={color}
                  transparent
                  opacity={active ? 0.95 : colliding ? 0.9 : connected ? 0.9 : 0.7}
                  depthTest={false}
                  depthWrite={false}
                />
              </mesh>
            )}
          </group>
        );
      })}
    </group>
  );
}
