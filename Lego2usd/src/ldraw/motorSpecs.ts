import * as THREE from 'three';

export type MotorSpec = {
  bodyFile: string;
  outputFile: string;
  outputHotspotIds: readonly string[];
  pivot: [number, number, number];
  axis: [number, number, number];
};

const LDRAW_TO_SCENE = new THREE.Matrix4().makeRotationX(Math.PI);

export const MOTOR_SPECS: Record<string, MotorSpec> = {
  '54675': {
    bodyFile: 'u9367c01.dat',
    outputFile: 'u9363.dat',
    outputHotspotIds: [
      'axleHole3_32',
      'axleHole3_33',
      'connhol3_34',
      'connhol3_35',
      'connhol3_36',
    ],
    pivot: [0, -50, 0],
    axis: [0, 1, 0],
  },
  '54696p01': {
    bodyFile: 'u9364c01.dat',
    outputFile: 'u9363p01.dat',
    outputHotspotIds: [
      'axleHole3_18',
      'axleHole3_19',
      'connhol3_20',
      'connhol3_21',
      'connhol3_22',
    ],
    pivot: [0, -50, 0],
    axis: [0, 1, 0],
  },
};

export function motorSpecForPart(partId: string): MotorSpec | undefined {
  return MOTOR_SPECS[partId];
}

export function motorPivotInSceneFrame(spec: MotorSpec): THREE.Vector3 {
  return new THREE.Vector3(...spec.pivot).applyMatrix4(LDRAW_TO_SCENE);
}

export function motorAxisInSceneFrame(spec: MotorSpec): THREE.Vector3 {
  return new THREE.Vector3(...spec.axis)
    .transformDirection(LDRAW_TO_SCENE)
    .normalize();
}
