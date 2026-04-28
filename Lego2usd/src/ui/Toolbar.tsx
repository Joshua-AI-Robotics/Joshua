import { useState } from 'react';
import {
  openTextFile,
  saveTextFile,
  type TextFileOpenResult,
} from '../io/browserFiles';
import { useSceneStore } from '../scene/store';
import {
  createSavedSceneSnapshot,
  parseSavedSceneSnapshot,
  rememberSavedSceneSnapshot,
  savedSceneToJson,
} from '../scene/scenePersistence';
import { saveUsdaFile } from '../usd/exportScene';

type Props = {
  onClearDrag?: () => void;
  onOpenUsdaPreview?: (file: TextFileOpenResult) => void | Promise<void>;
};

export function Toolbar({ onClearDrag, onOpenUsdaPreview }: Props) {
  const clear = useSceneStore((s) => s.clear);
  const loadScene = useSceneStore((s) => s.loadScene);
  const [status, setStatus] = useState<string>('');
  const [busyAction, setBusyAction] = useState<
    'save' | 'load' | 'export' | 'open-usda' | null
  >(null);

  const saveScene = async () => {
    const state = useSceneStore.getState();
    const saved = createSavedSceneSnapshot({
      parts: state.parts,
      joints: state.joints,
      motorAngles: state.motorAngles,
      selectedInstance: state.selectedInstance,
      selectedJoint: state.selectedJoint,
    });

    setBusyAction('save');
    setStatus('Saving...');
    try {
      const result = await saveTextFile({
        suggestedName: 'lego-build.json',
        text: savedSceneToJson(saved),
        mimeType: 'application/json',
        types: [
          {
            description: 'Lego2USD build',
            accept: {
              'application/json': ['.json', '.lego2usd'],
            },
          },
        ],
      });
      if (result === 'cancelled') {
        setStatus('Save cancelled');
        return;
      }
      rememberSavedSceneSnapshot(saved);
      setStatus(result === 'saved' ? 'Saved file' : 'Downloaded save');
    } catch (err) {
      console.warn('save scene failed', err);
      setStatus('Save failed');
    } finally {
      setBusyAction(null);
    }
  };

  const loadSavedScene = async () => {
    setBusyAction('load');
    setStatus('Loading...');
    try {
      const opened = await openTextFile({
        types: [
          {
            description: 'Lego2USD build',
            accept: {
              'application/json': ['.json', '.lego2usd'],
            },
          },
        ],
      });
      if (!opened) {
        setStatus('Load cancelled');
        return;
      }

      const saved = parseSavedSceneSnapshot(opened.text);
      if (!saved) {
        setStatus('Invalid save file');
        return;
      }

      onClearDrag?.();
      loadScene(saved.scene);
      rememberSavedSceneSnapshot(saved);
      setStatus(`Loaded ${opened.name}`);
    } catch (err) {
      console.warn('load scene failed', err);
      setStatus('Load failed');
    } finally {
      setBusyAction(null);
    }
  };

  const exportUsda = async () => {
    setBusyAction('export');
    setStatus('Exporting...');
    try {
      const result = await saveUsdaFile();
      if (result === 'cancelled') {
        setStatus('Export cancelled');
        return;
      }
      setStatus(result === 'saved' ? 'Exported .usda' : 'Downloaded .usda');
    } catch (err) {
      console.warn('export usda failed', err);
      setStatus('Export failed');
    } finally {
      setBusyAction(null);
    }
  };

  const openUsdaPreview = async () => {
    setBusyAction('open-usda');
    setStatus('Opening USDA...');
    try {
      const opened = await openTextFile({
        types: [
          {
            description: 'USD ASCII scene',
            accept: {
              'text/plain': ['.usda'],
            },
          },
        ],
      });
      if (!opened) {
        setStatus('Open cancelled');
        return;
      }

      await onOpenUsdaPreview?.(opened);
      setStatus(`Opened ${opened.name}`);
    } catch (err) {
      console.warn('open usda failed', err);
      setStatus('Open USDA failed');
    } finally {
      setBusyAction(null);
    }
  };

  return (
    <div className="toolbar">
      <span className="brand">Lego2USD</span>
      <button onClick={saveScene} disabled={busyAction !== null}>
        Save build
      </button>
      <button onClick={loadSavedScene} disabled={busyAction !== null}>
        Load
      </button>
      <button onClick={exportUsda} disabled={busyAction !== null}>
        Export .usda
      </button>
      <button onClick={openUsdaPreview} disabled={busyAction !== null}>
        Open .usda
      </button>
      <button
        onClick={() => {
          onClearDrag?.();
          clear();
        }}
        disabled={busyAction !== null}
      >
        Clear scene
      </button>
      <span className="save-status" aria-live="polite">
        {status}
      </span>
    </div>
  );
}
