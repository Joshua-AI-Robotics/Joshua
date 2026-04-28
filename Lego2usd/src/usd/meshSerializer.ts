import * as THREE from 'three';

export type SerializedMesh = {
  name: string;
  points: number[];           // flat xyz, already in USD coords (Z-up, meters)
  faceVertexCounts: number[];
  faceVertexIndices: number[];
  displayColor: [number, number, number];
};

// LDraw is Y-down, 1 LDU = 0.4 mm.  USD target: Z-up, meters.
// (x, y, z)_ldraw  -->  (x, -z, -y)_usd  with scale 0.0004.
const LDU_TO_M = 0.0004;

function toUsd(v: THREE.Vector3): [number, number, number] {
  return [v.x * LDU_TO_M, -v.z * LDU_TO_M, -v.y * LDU_TO_M];
}

function materialColor(
  material: THREE.Material | THREE.Material[],
): THREE.Color | undefined {
  const mat = Array.isArray(material) ? material[0] : material;
  return 'color' in mat && mat.color instanceof THREE.Color ? mat.color : undefined;
}

/**
 * Walk a part's Three.js group (as returned by LDrawLoader) and emit one
 * SerializedMesh per color-group.  Vertex positions are baked into the
 * part-local frame *before* the coordinate conversion and expressed in the
 * part's local USD frame (meters).  The overall per-part world transform is
 * applied in usdWriter, not here.
 *
 * The LDrawLoader orientation fix applied in loadPart.ts (rotation.x = PI)
 * must be undone here: we want the raw LDraw-local vertex coordinates so
 * the LDU→USD transform is consistent.
 */
export function serializePartGroup(group: THREE.Group, partLabel: string): SerializedMesh[] {
  // Undo the display-orientation fix to work with true LDraw-local coordinates.
  const fix = new THREE.Matrix4().makeRotationX(-Math.PI);
  group.updateMatrixWorld(true);

  const byColor = new Map<string, SerializedMesh>();

  group.traverse((obj) => {
    if (!(obj as THREE.Mesh).isMesh) return;
    const mesh = obj as THREE.Mesh;
    const geom = mesh.geometry as THREE.BufferGeometry;
    const pos = geom.getAttribute('position');
    if (!pos) return;

    // worldMatrix relative to the group root, then undo the display flip:
    const relMat = new THREE.Matrix4().copy(mesh.matrixWorld);
    const groupInv = new THREE.Matrix4().copy(group.matrixWorld).invert();
    relMat.premultiply(groupInv);
    relMat.premultiply(fix);

    const c = materialColor(mesh.material);
    const color: [number, number, number] = c ? [c.r, c.g, c.b] : [0.7, 0.7, 0.7];
    const key = color.join(',');
    let out = byColor.get(key);
    if (!out) {
      out = {
        name: `${partLabel}_c${byColor.size}`,
        points: [],
        faceVertexCounts: [],
        faceVertexIndices: [],
        displayColor: color,
      };
      byColor.set(key, out);
    }

    const baseIndex = out.points.length / 3;
    const tmp = new THREE.Vector3();
    for (let i = 0; i < pos.count; i++) {
      tmp.fromBufferAttribute(pos, i).applyMatrix4(relMat);
      const usd = toUsd(tmp);
      out.points.push(usd[0], usd[1], usd[2]);
    }

    const index = geom.getIndex();
    if (index) {
      for (let i = 0; i < index.count; i += 3) {
        out.faceVertexCounts.push(3);
        out.faceVertexIndices.push(
          baseIndex + index.getX(i),
          baseIndex + index.getX(i + 1),
          baseIndex + index.getX(i + 2),
        );
      }
    } else {
      for (let i = 0; i < pos.count; i += 3) {
        out.faceVertexCounts.push(3);
        out.faceVertexIndices.push(baseIndex + i, baseIndex + i + 1, baseIndex + i + 2);
      }
    }
  });

  return Array.from(byColor.values());
}

export { LDU_TO_M, toUsd };

/**
 * Walk the part group and compute an axis-aligned bounding box in the
 * part-local USD frame (meters, Z-up). Uses the same matrix discipline as
 * `serializePartGroup` so the AABB aligns with the emitted vertex data.
 */
export function computePartAabb(group: THREE.Group): {
  min: [number, number, number];
  max: [number, number, number];
  center: [number, number, number];
  volumeM3: number;
} {
  const fix = new THREE.Matrix4().makeRotationX(-Math.PI);
  group.updateMatrixWorld(true);
  const groupInv = new THREE.Matrix4().copy(group.matrixWorld).invert();

  let minX = Infinity, minY = Infinity, minZ = Infinity;
  let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
  let any = false;

  const tmp = new THREE.Vector3();
  group.traverse((obj) => {
    if (!(obj as THREE.Mesh).isMesh) return;
    const mesh = obj as THREE.Mesh;
    const geom = mesh.geometry as THREE.BufferGeometry;
    const pos = geom.getAttribute('position');
    if (!pos) return;

    const relMat = new THREE.Matrix4().copy(mesh.matrixWorld);
    relMat.premultiply(groupInv);
    relMat.premultiply(fix);

    for (let i = 0; i < pos.count; i++) {
      tmp.fromBufferAttribute(pos, i).applyMatrix4(relMat);
      const usd = toUsd(tmp);
      if (usd[0] < minX) minX = usd[0];
      if (usd[1] < minY) minY = usd[1];
      if (usd[2] < minZ) minZ = usd[2];
      if (usd[0] > maxX) maxX = usd[0];
      if (usd[1] > maxY) maxY = usd[1];
      if (usd[2] > maxZ) maxZ = usd[2];
      any = true;
    }
  });

  if (!any) {
    return {
      min: [0, 0, 0],
      max: [0, 0, 0],
      center: [0, 0, 0],
      volumeM3: 0,
    };
  }

  const min: [number, number, number] = [minX, minY, minZ];
  const max: [number, number, number] = [maxX, maxY, maxZ];
  const center: [number, number, number] = [
    (minX + maxX) / 2,
    (minY + maxY) / 2,
    (minZ + maxZ) / 2,
  ];
  const volumeM3 = Math.max(
    0,
    (maxX - minX) * (maxY - minY) * (maxZ - minZ),
  );
  return { min, max, center, volumeM3 };
}
