import { useState } from 'react';
import type { Cfg, SetValueFn } from './cfg';
import { Card, ColorRow, Segmented, Select, Slider, Toggle } from './controls';

// Display tab: render quality, point size, color mode (+ solid color when
// colorMode==2), EDL, background, and an Advanced section (LOD/GPU budgets,
// uploads, round/attenuate). All values mirror the server's cfg broadcast;
// edits go out as {"t":"set",...} via setValue.

export interface DisplayTabProps {
  cfg: Cfg | null;
  setValue: SetValueFn;
  streamType: 'jpeg' | 'webrtc';
}

const f0 = (v: number) => v.toFixed(0);
const f1 = (v: number) => v.toFixed(1);

export default function DisplayTab({ cfg, setValue, streamType }: DisplayTabProps) {
  const [advOpen, setAdvOpen] = useState(false);

  if (!cfg) {
    return (
      <div className="panel">
        <p className="panel-empty">Waiting for viewer settings…</p>
      </div>
    );
  }

  return (
    <div className="panel">
      {(cfg.webrtcAvailable || cfg.streamAvailable) && (
        <Card title="Stream Engine">
          <div className="seg">
            <button
              type="button"
              className={`seg-btn ${streamType === 'jpeg' ? 'active' : ''}`}
              onClick={() => setValue('preferredStream', 0)}
            >
              JPEG (Compat)
            </button>
            <button
              type="button"
              className={`seg-btn ${streamType === 'webrtc' ? 'active' : ''}`}
              onClick={() => setValue('preferredStream', 1)}
              disabled={!cfg.webrtcAvailable}
            >
              WebRTC (Fast)
            </button>
          </div>
        </Card>
      )}

      <Card title="Render quality">
        <Segmented
          ariaLabel="Render quality"
          options={['Low', 'Medium', 'High', 'Ultra']}
          value={cfg.quality}
          onChange={(i) => setValue('quality', i)}
        />
      </Card>

      <Card title="Points">
        <Slider
          label="Point size"
          min={1}
          max={16}
          step={1}
          value={cfg.pointSize}
          format={f0}
          onChange={(v) => setValue('pointSize', v)}
        />
      </Card>

      <Card title="Color">
        <Select
          label="Mode"
          ariaLabel="Color mode"
          options={['True Color', 'Elevation', 'Solid Color', 'Intensity', 'Classification']}
          value={cfg.colorMode}
          onChange={(i) => setValue('colorMode', i)}
        />
        {cfg.colorMode === 2 && (
          <ColorRow
            label="Solid color"
            value={cfg.solidColor}
            onChange={(c) => setValue('solidColor', c)}
          />
        )}
        <ColorRow
          label="Background"
          value={cfg.background}
          onChange={(c) => setValue('background', c)}
        />
      </Card>

      <Card title="Eye-dome lighting">
        <Toggle label="EDL" on={cfg.edl} onChange={(v) => setValue('edl', v)} />
        {cfg.edl && (
          <>
            <Slider
              label="Strength"
              min={0.1}
              max={5}
              step={0.1}
              value={cfg.edlStrength}
              format={f1}
              onChange={(v) => setValue('edlStrength', v)}
            />
            <Slider
              label="Radius"
              min={0.5}
              max={4}
              step={0.1}
              value={cfg.edlRadius}
              format={f1}
              onChange={(v) => setValue('edlRadius', v)}
            />
          </>
        )}
      </Card>

      <Card>
        <button
          type="button"
          className="collapse-btn"
          onClick={() => setAdvOpen((o) => !o)}
          aria-expanded={advOpen}
        >
          {advOpen ? '▾' : '▸'} Advanced
        </button>
        {advOpen && (
          <>
            <Slider
              label="LOD budget (SSE)"
              min={0.3}
              max={8}
              step={0.1}
              value={cfg.sse}
              format={f1}
              onChange={(v) => setValue('sse', v)}
            />
            <Slider
              label="GPU budget (MB)"
              min={128}
              max={8192}
              step={64}
              value={cfg.gpuBudget}
              format={f0}
              onChange={(v) => setValue('gpuBudget', v)}
            />
            <Slider
              label="Uploads / frame"
              min={1}
              max={256}
              step={1}
              value={cfg.uploads}
              format={f0}
              onChange={(v) => setValue('uploads', v)}
            />
            <Toggle label="Round points" on={cfg.round} onChange={(v) => setValue('round', v)} />
            <Toggle
              label="Distance attenuation"
              on={cfg.attenuate}
              onChange={(v) => setValue('attenuate', v)}
            />
          </>
        )}
      </Card>
    </div>
  );
}
