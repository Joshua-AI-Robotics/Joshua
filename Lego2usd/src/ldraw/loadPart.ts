import * as THREE from 'three';
import { LDrawLoader } from 'three/addons/loaders/LDrawLoader.js';
import { LDrawConditionalLineMaterial } from 'three/addons/materials/LDrawConditionalLineMaterial.js';
import { parseShadowPart, type SnapHotspot } from './snapParser';
import { PART_CATALOG } from './partCatalog';

export type LoadedPart = {
  id: string;
  file: string;
  name: string;
  group: THREE.Group;
  bounds: THREE.Box3;
  hotspots: SnapHotspot[];
};

const LDRAW_ROOT = '/ldraw/';
const LDRAW_TO_SCENE = new THREE.Matrix4().makeRotationX(Math.PI);

let loaderPromise: Promise<LDrawLoader> | null = null;
const partCache = new Map<string, Promise<LoadedPart>>();

function getLoader(): Promise<LDrawLoader> {
  if (loaderPromise) return loaderPromise;
  const loader = new LDrawLoader();
  loader.setPartsLibraryPath(LDRAW_ROOT);
  loader.setConditionalLineMaterial(LDrawConditionalLineMaterial);
  loader.smoothNormals = true;
  loaderPromise = loader
    .preloadMaterials(LDRAW_ROOT + 'LDConfig.ldr')
    .then(() => loader);
  return loaderPromise;
}

export function loadPart(catalogId: string): Promise<LoadedPart> {
  if (partCache.has(catalogId)) return partCache.get(catalogId)!;

  const entry = PART_CATALOG.find((p) => p.id === catalogId);
  if (!entry) throw new Error(`Unknown catalog id: ${catalogId}`);

  const promise = (async () => {
    const loader = await getLoader();
    const group = (await loader.loadAsync(LDRAW_ROOT + entry.file)) as unknown as THREE.Group;
    // LDrawLoader orients parts with -Y up; undo so our editor is Y-up
    group.rotation.x = Math.PI;
    group.updateMatrixWorld(true);
    const bounds = new THREE.Box3().setFromObject(group);

    let hotspots: SnapHotspot[] = [];
    try {
      hotspots = (await parseShadowPart(entry.file, LDRAW_ROOT + 'shadow/', LDRAW_ROOT)).map(
        hotspotToSceneFrame,
      );
    } catch (err) {
      console.warn(`no shadow data for ${entry.id}`, err);
    }

    return { id: entry.id, file: entry.file, name: entry.name, group, bounds, hotspots };
  })();

  partCache.set(catalogId, promise);
  return promise;
}

function hotspotToSceneFrame(hotspot: SnapHotspot): SnapHotspot {
  return {
    ...hotspot,
    center: hotspot.center.clone().applyMatrix4(LDRAW_TO_SCENE),
    axis: hotspot.axis.clone().transformDirection(LDRAW_TO_SCENE).normalize(),
  };
}

export function clonePartGroup(part: LoadedPart): THREE.Group {
  return part.group.clone(true);
}
