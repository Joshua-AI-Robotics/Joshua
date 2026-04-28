import { useSceneStore } from '../scene/store';
import { loadPart, type LoadedPart } from '../ldraw/loadPart';
import { saveTextFile, type FileSaveResult } from '../io/browserFiles';
import { writeUsdaScene } from './usdWriter';

export async function exportSceneToUsda(): Promise<string> {
  const { parts, joints, motorAngles } = useSceneStore.getState();
  const uniquePartIds = Array.from(new Set(parts.map((p) => p.partId)));
  const loadedEntries = await Promise.all(
    uniquePartIds.map(async (id) => [id, await loadPart(id)] as const),
  );
  const map = new Map<string, LoadedPart>(loadedEntries);
  return writeUsdaScene(parts, joints, map, motorAngles);
}

export async function saveUsdaFile(filename = 'lego.usda'): Promise<FileSaveResult> {
  return saveTextFile({
    suggestedName: filename,
    text: exportSceneToUsda,
    mimeType: 'text/plain',
    types: [
      {
        description: 'USD ASCII scene',
        accept: {
          'text/plain': ['.usda'],
        },
      },
    ],
  });
}

export async function downloadUsda(filename = 'lego.usda'): Promise<void> {
  await saveUsdaFile(filename);
}
