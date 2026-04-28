import { useEffect, useLayoutEffect, useMemo, useRef } from 'react';
import * as THREE from 'three';
import type { UsdaPreviewBody, UsdaPreviewScene } from './usdaPreviewParser';

type Props = {
  scene: UsdaPreviewScene;
};

type PreviewBodyProps = {
  body: UsdaPreviewBody;
};

function PreviewBody({ body }: PreviewBodyProps) {
  const groupRef = useRef<THREE.Group>(null);
  const meshes = useMemo(
    () =>
      body.meshes.map((mesh) => {
        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute(
          'position',
          new THREE.BufferAttribute(mesh.positions, 3),
        );
        geometry.computeVertexNormals();
        geometry.computeBoundingSphere();
        return {
          name: mesh.name,
          geometry,
          color: new THREE.Color(mesh.color[0], mesh.color[1], mesh.color[2]),
        };
      }),
    [body.meshes],
  );

  useLayoutEffect(() => {
    if (!groupRef.current) return;
    groupRef.current.matrix.copy(body.matrix);
    groupRef.current.matrixAutoUpdate = false;
  }, [body.matrix]);

  useEffect(() => {
    return () => {
      for (const mesh of meshes) mesh.geometry.dispose();
    };
  }, [meshes]);

  return (
    <group ref={groupRef} matrixAutoUpdate={false}>
      {meshes.map((mesh) => (
        <mesh key={mesh.name} geometry={mesh.geometry}>
          <meshStandardMaterial
            color={mesh.color}
            roughness={0.65}
            metalness={0}
            side={THREE.DoubleSide}
          />
        </mesh>
      ))}
    </group>
  );
}

export function UsdaPreview({ scene }: Props) {
  return (
    <group>
      {scene.bodies.map((body) => (
        <PreviewBody key={body.name} body={body} />
      ))}
    </group>
  );
}
