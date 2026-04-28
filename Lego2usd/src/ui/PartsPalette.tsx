import { PART_CATALOG } from '../ldraw/partCatalog';
import { useSceneStore } from '../scene/store';

type Props = {
  draggingId: string | null;
  onPartDragStart: (id: string) => void;
};

export function PartsPalette({ draggingId, onPartDragStart }: Props) {
  const addPart = useSceneStore((s) => s.addPart);

  return (
    <div className="palette">
      <h3>Parts</h3>
      <p className="hint">
        Drag a part into the scene. Release on an orange/blue snap marker to
        connect it; release on empty space to drop the first part at origin.
        Use <b>+</b> to add at origin without dragging.
      </p>
      <ul>
        {PART_CATALOG.map((p) => {
          const pending = draggingId === p.id;
          return (
            <li key={p.id}>
              <button
                className={pending ? 'pending' : ''}
                onPointerDown={() => onPartDragStart(p.id)}
              >
                <span className="pid">{p.id}</span>
                <span className="pname">{p.name}</span>
              </button>
              <button
                className="secondary"
                title="Add at origin"
                onClick={() => addPart(p.id)}
              >
                +
              </button>
            </li>
          );
        })}
      </ul>
    </div>
  );
}
