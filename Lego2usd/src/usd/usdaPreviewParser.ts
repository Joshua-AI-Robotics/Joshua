import * as THREE from 'three';
import { LDU_TO_M } from './meshSerializer';

export type UsdaPreviewMeshData = {
  name: string;
  positions: Float32Array;
  color: [number, number, number];
};

export type UsdaPreviewBody = {
  name: string;
  matrix: THREE.Matrix4;
  meshes: UsdaPreviewMeshData[];
};

export type UsdaPreviewJoint = {
  name: string;
  type: string;
  body0?: string;
  body1?: string;
};

export type UsdaPreviewScene = {
  fileName: string;
  bodies: UsdaPreviewBody[];
  joints: UsdaPreviewJoint[];
  meshCount: number;
};

type MutableMesh = {
  name: string;
  points: number[];
  faceVertexCounts: number[];
  faceVertexIndices: number[];
  color: [number, number, number];
};

type MutableBody = {
  name: string;
  matrix: THREE.Matrix4;
  meshes: MutableMesh[];
};

const lduToUsd = new THREE.Matrix4().set(
  LDU_TO_M,
  0,
  0,
  0,
  0,
  0,
  -LDU_TO_M,
  0,
  0,
  -LDU_TO_M,
  0,
  0,
  0,
  0,
  0,
  1,
);
const usdToLdu = new THREE.Matrix4().copy(lduToUsd).invert();

function parseNumberList(text: string): number[] {
  return (
    text.match(/[+-]?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?/gi)?.map(Number) ?? []
  );
}

function valueText(line: string): string {
  const index = line.indexOf('=');
  return index === -1 ? '' : line.slice(index + 1);
}

function parseMatrix(line: string): THREE.Matrix4 {
  const values = parseNumberList(valueText(line));
  if (values.length !== 16) {
    throw new Error('USDA preview expected a 16-value matrix4d transform.');
  }
  const usdMatrix = new THREE.Matrix4().set(
    values[0],
    values[1],
    values[2],
    values[3],
    values[4],
    values[5],
    values[6],
    values[7],
    values[8],
    values[9],
    values[10],
    values[11],
    values[12],
    values[13],
    values[14],
    values[15],
  );
  return new THREE.Matrix4().multiplyMatrices(usdToLdu, usdMatrix).multiply(lduToUsd);
}

function parseColor(line: string): [number, number, number] {
  const values = parseNumberList(valueText(line));
  return [
    values[0] ?? 0.7,
    values[1] ?? 0.7,
    values[2] ?? 0.7,
  ];
}

function usdPointsToLdu(points: number[]): number[] {
  const converted: number[] = [];
  const point = new THREE.Vector3();
  for (let i = 0; i < points.length; i += 3) {
    point.set(points[i] ?? 0, points[i + 1] ?? 0, points[i + 2] ?? 0);
    point.applyMatrix4(usdToLdu);
    converted.push(point.x, point.y, point.z);
  }
  return converted;
}

function triangulateMesh(mesh: MutableMesh): UsdaPreviewMeshData {
  const localPoints = usdPointsToLdu(mesh.points);
  const positions: number[] = [];
  let cursor = 0;

  for (const count of mesh.faceVertexCounts) {
    const face = mesh.faceVertexIndices.slice(cursor, cursor + count);
    cursor += count;
    if (face.length < 3) continue;
    for (let i = 1; i < face.length - 1; i++) {
      const triangle = [face[0], face[i], face[i + 1]];
      for (const index of triangle) {
        const pointOffset = index * 3;
        positions.push(
          localPoints[pointOffset] ?? 0,
          localPoints[pointOffset + 1] ?? 0,
          localPoints[pointOffset + 2] ?? 0,
        );
      }
    }
  }

  return {
    name: mesh.name,
    positions: new Float32Array(positions),
    color: mesh.color,
  };
}

export function parseUsdaPreview(fileName: string, text: string): UsdaPreviewScene {
  if (!text.trim().startsWith('#usda')) {
    throw new Error('Only ASCII .usda files exported by Lego2USD can be previewed.');
  }

  const bodies: MutableBody[] = [];
  const joints: UsdaPreviewJoint[] = [];
  let currentBody: MutableBody | null = null;
  let currentMesh: MutableMesh | null = null;
  let currentJoint: UsdaPreviewJoint | null = null;

  for (const line of text.split(/\r?\n/)) {
    const bodyMatch = line.match(/^ {4}def Xform "([^"]+)"/);
    if (bodyMatch) {
      currentBody = {
        name: bodyMatch[1],
        matrix: new THREE.Matrix4(),
        meshes: [],
      };
      bodies.push(currentBody);
      currentMesh = null;
      currentJoint = null;
      continue;
    }

    const jointMatch = line.match(/^ {4}def (Physics\w+Joint) "([^"]+)"/);
    if (jointMatch) {
      currentJoint = {
        type: jointMatch[1],
        name: jointMatch[2],
      };
      joints.push(currentJoint);
      currentBody = null;
      currentMesh = null;
      continue;
    }

    if (currentBody) {
      const meshMatch = line.match(/^ {8}def Mesh "([^"]+)"/);
      if (meshMatch) {
        currentMesh = {
          name: meshMatch[1],
          points: [],
          faceVertexCounts: [],
          faceVertexIndices: [],
          color: [0.7, 0.7, 0.7],
        };
        currentBody.meshes.push(currentMesh);
        continue;
      }

      if (line.startsWith('        matrix4d xformOp:transform =')) {
        currentBody.matrix = parseMatrix(line);
        continue;
      }

      if (currentMesh) {
        if (line.startsWith('            int[] faceVertexCounts =')) {
          currentMesh.faceVertexCounts = parseNumberList(valueText(line)).map(Math.trunc);
          continue;
        }
        if (line.startsWith('            int[] faceVertexIndices =')) {
          currentMesh.faceVertexIndices = parseNumberList(valueText(line)).map(Math.trunc);
          continue;
        }
        if (line.startsWith('            point3f[] points =')) {
          currentMesh.points = parseNumberList(valueText(line));
          continue;
        }
        if (line.startsWith('            color3f[] primvars:displayColor =')) {
          currentMesh.color = parseColor(line);
          continue;
        }
        if (line.startsWith('        }')) {
          currentMesh = null;
          continue;
        }
      }

      if (line.startsWith('    }')) {
        currentBody = null;
      }
      continue;
    }

    if (currentJoint) {
      const relMatch = line.match(/^ {8}rel physics:(body[01]) = <\/Lego\/([^>]+)>/);
      if (relMatch) {
        if (relMatch[1] === 'body0') {
          currentJoint.body0 = relMatch[2];
        } else {
          currentJoint.body1 = relMatch[2];
        }
        continue;
      }
      if (line.startsWith('    }')) {
        currentJoint = null;
      }
    }
  }

  if (bodies.length === 0) {
    throw new Error('No rigid body Xforms were found in the USDA file.');
  }

  const previewBodies: UsdaPreviewBody[] = bodies.map((body) => ({
    name: body.name,
    matrix: body.matrix,
    meshes: body.meshes
      .filter(
        (mesh) =>
          mesh.points.length > 0 &&
          mesh.faceVertexCounts.length > 0 &&
          mesh.faceVertexIndices.length > 0,
      )
      .map(triangulateMesh),
  }));

  const meshCount = previewBodies.reduce((sum, body) => sum + body.meshes.length, 0);
  if (meshCount === 0) {
    throw new Error('No renderable mesh data was found in the USDA file.');
  }

  return {
    fileName,
    bodies: previewBodies,
    joints,
    meshCount,
  };
}
