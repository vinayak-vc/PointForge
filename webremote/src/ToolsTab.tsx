import type { Cfg, SetValueFn, Vec3 } from './cfg';
import { Card, Slider, Toggle } from './controls';
import { formatPoints } from './StatusHUD';

// Tools / Files tab: measure tool (tool==1), clip box (tool==2, six plane
// sliders over -clipExt..+clipExt), loaded-file info and the recent-files
// list (tap -> confirm -> {"t":"cmd","n":"loadrecent","v":<index>}).

export interface ToolsTabProps {
  cfg: Cfg | null;
  setValue: SetValueFn;
  send: (obj: unknown) => void;
}

const AXES = ['X', 'Y', 'Z'] as const;

const f2 = (v: number) => v.toFixed(2);

/** Last path segment after / or \ (recent entries are full PC paths). */
function basename(p: string): string {
  const parts = p.split(/[\\/]/).filter((s) => s.length > 0);
  return parts.length > 0 ? parts[parts.length - 1] : p;
}

function withAxis(v: Vec3, axis: number, value: number): Vec3 {
  const next: Vec3 = [v[0], v[1], v[2]];
  next[axis] = value;
  return next;
}

export default function ToolsTab({ cfg, setValue, send }: ToolsTabProps) {
  const cmd = (n: string) => () => send({ t: 'cmd', n });

  if (!cfg) {
    return (
      <div className="panel">
        <p className="panel-empty">Waiting for viewer settings…</p>
      </div>
    );
  }

  // clipExt == 0 means no cloud loaded yet — sliders are disabled and get a
  // dummy 1-unit range so the <input type="range"> stays well-formed.
  const clipDisabled = !(cfg.clipExt > 0);
  const clipRange = clipDisabled ? 1 : cfg.clipExt;
  const clipStep = clipRange / 200;

  return (
    <div className="panel">
      <Card title="Measure">
        <button
          type="button"
          className={`big-btn${cfg.tool === 1 ? ' active' : ''}`}
          onClick={cmd('measure')}
        >
          {cfg.tool === 1 ? 'Stop measuring' : 'Start measuring'}
        </button>
        {cfg.tool === 1 && <p className="dim">Tap the video to place a point.</p>}
        <p className="dim">
          {cfg.measurePts.length} point{cfg.measurePts.length === 1 ? '' : 's'} — total{' '}
          {cfg.measureTotal.toFixed(2)}
        </p>
        {cfg.measurePts.length > 0 && (
          <ol className="measure-list">
            {cfg.measurePts.map((p, i) => {
              const prev = i > 0 ? cfg.measurePts[i - 1] : null;
              const seg = prev
                ? Math.hypot(p[0] - prev[0], p[1] - prev[1], p[2] - prev[2])
                : 0;
              return (
                <li key={i}>
                  {p[0].toFixed(2)}, {p[1].toFixed(2)}, {p[2].toFixed(2)}
                  {prev && <span className="dim"> — seg {seg.toFixed(2)}</span>}
                </li>
              );
            })}
          </ol>
        )}
        <div className="btn-grid">
          <button type="button" onClick={cmd('measure_undo')}>
            Undo
          </button>
          <button type="button" onClick={cmd('measure_clear')}>
            Clear
          </button>
        </div>
      </Card>

      <Card title="Clip box">
        <Toggle label="Enable clipping" on={cfg.clip} onChange={(v) => setValue('clip', v)} />
        <button
          type="button"
          className={`big-btn${cfg.tool === 2 ? ' active' : ''}`}
          onClick={cmd('clip_tool')}
        >
          {cfg.tool === 2 ? 'Clip tool (active)' : 'Clip tool'}
        </button>
        {AXES.map((axis, i) => (
          <Slider
            key={`min${axis}`}
            label={`Min ${axis}`}
            min={-clipRange}
            max={clipRange}
            step={clipStep}
            value={cfg.clipMin[i]}
            format={f2}
            disabled={clipDisabled}
            onChange={(v) => setValue('clipMin', withAxis(cfg.clipMin, i, v))}
          />
        ))}
        {AXES.map((axis, i) => (
          <Slider
            key={`max${axis}`}
            label={`Max ${axis}`}
            min={-clipRange}
            max={clipRange}
            step={clipStep}
            value={cfg.clipMax[i]}
            format={f2}
            disabled={clipDisabled}
            onChange={(v) => setValue('clipMax', withAxis(cfg.clipMax, i, v))}
          />
        ))}
        <div className="btn-grid">
          <button type="button" onClick={cmd('clip_reset')}>
            Reset planes
          </button>
        </div>
      </Card>

      <Card title="File">
        {cfg.file ? (
          <>
            <div className="kv">
              <span className="k">Name</span>
              <span>{cfg.file}</span>
            </div>
            <div className="kv">
              <span className="k">Points</span>
              <span>{formatPoints(cfg.pts)}</span>
            </div>
            <div className="kv">
              <span className="k">Nodes</span>
              <span>{cfg.nodes.toLocaleString()}</span>
            </div>
            <div className="kv">
              <span className="k">Cube size</span>
              <span>{cfg.cubeSize.toFixed(1)}</span>
            </div>
          </>
        ) : (
          <p className="dim">No file loaded.</p>
        )}
      </Card>

      <Card title="Recent files">
        {cfg.recent.length === 0 ? (
          <p className="dim">No recent files.</p>
        ) : (
          cfg.recent.map((p, i) => (
            <button
              key={`${i}-${p}`}
              type="button"
              className="recent-btn"
              onClick={() => {
                if (window.confirm(`Load ${basename(p)}?`)) {
                  send({ t: 'cmd', n: 'loadrecent', v: i });
                }
              }}
            >
              {basename(p)}
            </button>
          ))
        )}
      </Card>
    </div>
  );
}
