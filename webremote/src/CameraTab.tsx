import type { Cfg, SetValueFn } from './cfg';
import { Card, Slider, Toggle } from './controls';

// Camera tab: projection (ortho + size), fly speed, view/one-shot commands,
// stereo 3D, and PC display toggles (stats / dark theme / hide UI).

export interface CameraTabProps {
  cfg: Cfg | null;
  setValue: SetValueFn;
  send: (obj: unknown) => void;
}

export default function CameraTab({ cfg, setValue, send }: CameraTabProps) {
  const cmd = (n: string) => () => send({ t: 'cmd', n });

  if (!cfg) {
    return (
      <div className="panel">
        <p className="panel-empty">Waiting for viewer settings…</p>
      </div>
    );
  }

  return (
    <div className="panel">
      <Card title="Projection">
        <Toggle label="Orthographic" on={cfg.ortho} onChange={(v) => setValue('ortho', v)} />
        {cfg.ortho && (
          <Slider
            label="Ortho size"
            min={1}
            max={5000}
            log
            value={cfg.orthoSize}
            format={(v) => (v >= 100 ? v.toFixed(0) : v.toFixed(1))}
            onChange={(v) => setValue('orthoSize', v)}
          />
        )}
      </Card>

      <Card title="Navigation">
        <Slider
          label="Fly speed"
          min={0.1}
          max={10}
          log
          value={cfg.speed}
          format={(v) => `${v < 1 ? v.toFixed(2) : v.toFixed(1)}x`}
          onChange={(v) => setValue('speed', v)}
        />
        <div className="btn-grid">
          <button type="button" onClick={cmd('reset_view')}>
            Reset View
          </button>
          <button type="button" onClick={cmd('frame')}>
            Frame All
          </button>
          <button type="button" onClick={cmd('preset1')}>
            Front
          </button>
          <button type="button" onClick={cmd('preset3')}>
            Side
          </button>
          <button type="button" onClick={cmd('preset7')}>
            Top
          </button>
          <button type="button" onClick={cmd('fullscreen')}>
            Fullscreen (PC)
          </button>
        </div>
      </Card>

      <Card title="Stereo 3D">
        <Toggle
          label="Stereo (side-by-side)"
          on={cfg.stereo}
          onChange={(v) => setValue('stereo', v)}
        />
        {cfg.stereo && (
          <>
            <Slider
              label="Eye separation"
              min={0.01}
              max={0.2}
              step={0.005}
              value={cfg.eyeSep}
              format={(v) => v.toFixed(3)}
              onChange={(v) => setValue('eyeSep', v)}
            />
            <Slider
              label="Focal distance"
              min={1}
              max={100}
              step={0.5}
              value={cfg.focalDist}
              format={(v) => v.toFixed(1)}
              onChange={(v) => setValue('focalDist', v)}
            />
          </>
        )}
      </Card>

      <Card title="PC display">
        <Toggle label="Stats overlay" on={cfg.stats} onChange={(v) => setValue('stats', v)} />
        <Toggle label="Dark theme" on={cfg.darkTheme} onChange={(v) => setValue('darkTheme', v)} />
        <Toggle label="Hide PC UI" on={!cfg.ui} onChange={(hidden) => setValue('ui', !hidden)} />
      </Card>
    </div>
  );
}
