import { useEffect, useMemo, useState } from 'react';
import * as THREE from 'three';
import { loadPart, type LoadedPart } from '../ldraw/loadPart';
import {
  motorAxisInSceneFrame,
  motorPivotInSceneFrame,
  motorSpecForPart,
} from '../ldraw/motorSpecs';
import type { SnapHotspot } from '../ldraw/snapParser';
import { instancePoseMatrix, useSceneStore } from './store';

type InstanceHotspot = {
  instanceId: string;
  partId: string;
  hotspot: SnapHotspot;
  worldCenter: THREE.Vector3;
  worldAxis: THREE.Vector3;
  preferredSide?: -1 | 1;
};

function useInstanceHotspots(): InstanceHotspot[] {
  const parts = useSceneStore((s) => s.parts);
  const motorAngles = useSceneStore((s) => s.motorAngles);
  const [loaded, setLoaded] = useState<Map<string, LoadedPart>>(new Map());
  const partIdsKey = useMemo(
    () => [...new Set(parts.map((p) => p.partId))].sort().join('\0'),
    [parts],
  );

  useEffect(() => {
    let cancelled = false;
    (async () => {
      const next = new Map<string, LoadedPart>();
      const partIds = partIdsKey ? partIdsKey.split('\0') : [];
      for (const partId of partIds) {
        next.set(partId, await loadPart(partId));
      }
      if (!cancelled) setLoaded(next);
    })();
    return () => {
      cancelled = true;
    };
  }, [partIdsKey]);

  return useMemo(() => {
    const out: InstanceHotspot[] = [];
    for (const inst of parts) {
      const part = loaded.get(inst.partId);
      if (!part) continue;
      const m = instancePoseMatrix(inst);
      const motorSpec = motorSpecForPart(inst.partId);
      const motorAngle = motorAngles[inst.instanceId] ?? 0;
      const motorPivot = motorSpec ? motorPivotInSceneFrame(motorSpec) : null;
      const motorAxis = motorSpec ? motorAxisInSceneFrame(motorSpec) : null;
      for (const h of part.hotspots) {
        const localCenter = h.center.clone();
        const localAxis = h.axis.clone();
        if (
          motorSpec &&
          motorPivot &&
          motorAxis &&
          motorSpec.outputHotspotIds.includes(h.id)
        ) {
          localCenter.sub(motorPivot).applyAxisAngle(motorAxis, motorAngle).add(motorPivot);
          localAxis.applyAxisAngle(motorAxis, motorAngle).normalize();
        }
        const wc = localCenter.applyMatrix4(m);
        const wa = localAxis.transformDirection(m).normalize();
        out.push({
          instanceId: inst.instanceId,
          partId: inst.partId,
          hotspot: h,
          worldCenter: wc,
          worldAxis: wa,
        });
      }
    }
    return out;
  }, [parts, loaded, motorAngles]);
}

function hotspotKey(s: InstanceHotspot): string {
  return s.instanceId + '|' + s.hotspot.id;
}

export { hotspotKey, useInstanceHotspots };
export type { InstanceHotspot };
