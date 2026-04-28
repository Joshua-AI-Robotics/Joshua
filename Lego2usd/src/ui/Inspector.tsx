import { useSceneStore, type JointKind } from '../scene/store';
import { PART_CATALOG } from '../ldraw/partCatalog';
import { motorSpecForPart } from '../ldraw/motorSpecs';

export function Inspector() {
  const parts = useSceneStore((s) => s.parts);
  const joints = useSceneStore((s) => s.joints);
  const selectedInstance = useSceneStore((s) => s.selectedInstance);
  const selectedJoint = useSceneStore((s) => s.selectedJoint);
  const removePart = useSceneStore((s) => s.removePart);
  const removeJoint = useSceneStore((s) => s.removeJoint);
  const updateJoint = useSceneStore((s) => s.updateJoint);
  const selectJoint = useSceneStore((s) => s.selectJoint);
  const setWorldAnchor = useSceneStore((s) => s.setWorldAnchor);
  const motorAnimations = useSceneStore((s) => s.motorAnimations);
  const toggleMotorAnimation = useSceneStore((s) => s.toggleMotorAnimation);

  const inst = parts.find((p) => p.instanceId === selectedInstance);
  const joint = joints.find((j) => j.jointId === selectedJoint);
  const isMotor = !!(inst && motorSpecForPart(inst.partId));
  const motorAnimating = !!(inst && motorAnimations[inst.instanceId]);

  const limitUnit = joint?.kind === 'prismatic' ? 'm' : 'deg';
  const limitStep = joint?.kind === 'prismatic' ? 0.005 : 5;
  const scale = inst?.scale ?? [1, 1, 1];

  return (
    <div className="inspector">
      <h3>Inspector</h3>

      {inst && (
        <div className="section">
          <h4>Part</h4>
          <div className="row">
            <span>Catalog:</span>
            <b>{PART_CATALOG.find((p) => p.id === inst.partId)?.name ?? inst.partId}</b>
          </div>
          <div className="row">
            <span>Position (LDU):</span>
            <code>{inst.position.map((n) => n.toFixed(1)).join(', ')}</code>
          </div>
          <div className="row">
            <span>Scale:</span>
            <code>{scale.map((n) => n.toFixed(0)).join(', ')}</code>
          </div>
          <p className="hint" style={{ marginTop: 4 }}>
            Right-click the part to rotate, mirror, or remove.
          </p>
          <div className="row">
            <label style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
              <input
                type="checkbox"
                checked={!!inst.worldAnchor}
                onChange={(e) => setWorldAnchor(inst.instanceId, e.target.checked)}
              />
              <span style={{ color: '#e8e8ee' }}>Pin to world</span>
            </label>
          </div>
          {isMotor && (
            <button
              className={`motor-run-button${motorAnimating ? ' active' : ''}`}
              onClick={() => toggleMotorAnimation(inst.instanceId)}
            >
              {motorAnimating ? 'Stop motor animation' : 'Animate motor'}
            </button>
          )}
          <button onClick={() => removePart(inst.instanceId)}>Delete part</button>
        </div>
      )}

      {joint && (
        <div className="section">
          <h4>Joint</h4>
          <div className="row">
            <span>Kind:</span>
            <select
              value={joint.kind}
              onChange={(e) => updateJoint(joint.jointId, { kind: e.target.value as JointKind })}
            >
              <option value="revolute">revolute</option>
              <option value="prismatic">prismatic</option>
              <option value="fixed">fixed</option>
            </select>
          </div>
          <div className="row">
            <span>Axis:</span>
            <code>{joint.axis.map((n) => n.toFixed(2)).join(', ')}</code>
          </div>
          {joint.kind !== 'fixed' && (
            <>
              <div className="row">
                <span>Lower ({limitUnit}):</span>
                <input
                  type="number"
                  step={limitStep}
                  value={joint.limitLower ?? ''}
                  placeholder="default"
                  onChange={(e) =>
                    updateJoint(joint.jointId, {
                      limitLower: e.target.value === '' ? undefined : Number(e.target.value),
                    })
                  }
                  style={{ width: 90 }}
                />
              </div>
              <div className="row">
                <span>Upper ({limitUnit}):</span>
                <input
                  type="number"
                  step={limitStep}
                  value={joint.limitUpper ?? ''}
                  placeholder="default"
                  onChange={(e) =>
                    updateJoint(joint.jointId, {
                      limitUpper: e.target.value === '' ? undefined : Number(e.target.value),
                    })
                  }
                  style={{ width: 90 }}
                />
              </div>
            </>
          )}
          <button onClick={() => removeJoint(joint.jointId)}>Delete joint</button>
        </div>
      )}

      {!inst && !joint && <p className="hint">Nothing selected.</p>}

      <div className="section">
        <h4>All joints</h4>
        <ul className="joint-list">
          {joints.map((j) => (
            <li
              key={j.jointId}
              className={j.jointId === selectedJoint ? 'sel' : ''}
              onClick={() => selectJoint(j.jointId)}
            >
              {j.kind} — {j.parentInstance.slice(-6)} ↔ {j.childInstance.slice(-6)}
            </li>
          ))}
        </ul>
      </div>
    </div>
  );
}
