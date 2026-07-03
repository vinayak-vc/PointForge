import {
  Maximize2, Camera, Wifi, WifiOff, ZoomIn, Frame, Monitor, LayoutGrid, ChevronDown
} from 'lucide-react';
import type { Cfg, SetValueFn } from './cfg';
import type { VideoQuality } from './stream';

export interface ActionBarProps {
  send: (obj: unknown) => void;
  cfg: Cfg | null;
  setValue: SetValueFn;
  videoOn: boolean;
  onVideoToggle: (on: boolean) => void;
  videoQuality: VideoQuality;
  onVideoQuality: (q: VideoQuality) => void;
  streamAvailable: boolean;
  /** Take a screenshot on the PC and download it to this device. */
  onShot: () => void;
}

const SPEED_MIN = 0.1;
const SPEED_MAX = 10;
const SPEED_DEFAULT = 2;
const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));
const sliderToSpeed = (t: number) => SPEED_MIN * Math.pow(SPEED_MAX / SPEED_MIN, t);
const speedToSlider = (v: number) => Math.log(v / SPEED_MIN) / Math.log(SPEED_MAX / SPEED_MIN);

const VQ_OPTIONS: ReadonlyArray<readonly [VideoQuality, string]> = [
  ['low', 'Low'],
  ['med', 'Med'],
  ['high', 'High'],
];

export default function ActionBar({
  send, cfg, setValue, videoOn, onVideoToggle, videoQuality, onVideoQuality, streamAvailable, onShot,
}: ActionBarProps) {
  const cmd = (n: string) => () => send({ t: 'cmd', n });
  const speed = clamp(cfg?.speed ?? SPEED_DEFAULT, SPEED_MIN, SPEED_MAX);

  return (
    <div className="toolbar">
      {/* Brand */}
      <div className="toolbar-brand">
        <div className="toolbar-brand-dot" />
        <span className="toolbar-brand-name">ViitorXPC</span>
      </div>

      {/* View presets */}
      <div className="toolbar-group">
        <button className="toolbar-btn" onClick={cmd('frame')} title="Frame all points">
          <Frame size={13} /> Frame
        </button>
        <button className="toolbar-btn" onClick={cmd('preset7')} title="Top view">
          <ChevronDown size={13} /> Top
        </button>
        <button className="toolbar-btn" onClick={cmd('preset1')} title="Front view">
          <Monitor size={13} /> Front
        </button>
        <button className="toolbar-btn" onClick={cmd('preset3')} title="Side view">
          <LayoutGrid size={13} /> Side
        </button>
      </div>

      <div className="toolbar-divider" />

      {/* View toggles */}
      <div className="toolbar-group">
        <button
          className={`toolbar-btn${cfg?.ortho ? ' active' : ''}`}
          onClick={() => { if (cfg) setValue('ortho', !cfg.ortho); }}
          title="Toggle orthographic"
        >
          <ZoomIn size={13} /> Ortho
        </button>
        <button
          className={`toolbar-btn${cfg && !cfg.ui ? ' active' : ''}`}
          onClick={() => { if (cfg) setValue('ui', !cfg.ui); }}
          title="Hide/show PC UI"
        >
          <Monitor size={13} /> UI
        </button>
        <button
          className={`toolbar-btn${cfg?.stereo ? ' active' : ''}`}
          onClick={() => { if (cfg) setValue('stereo', !cfg.stereo); }}
          title="Toggle stereo 3D"
        >
          <Maximize2 size={13} /> 3D
        </button>
      </div>

      <div className="toolbar-divider" />

      {/* Capture */}
      <div className="toolbar-group">
        <button className="toolbar-btn" onClick={onShot} title="Take screenshot on the PC and download it here">
          <Camera size={13} /> Shot
        </button>
        <button className="toolbar-btn" onClick={cmd('fullscreen')} title="Toggle fullscreen on PC">
          <Maximize2 size={13} /> Full
        </button>
      </div>

      {/* Stream */}
      {streamAvailable && (
        <>
          <div className="toolbar-divider" />
          <div className="toolbar-group">
            <button
              className={`toolbar-btn${videoOn ? ' active' : ''}`}
              onClick={() => onVideoToggle(!videoOn)}
              title="Toggle video stream"
            >
              {videoOn ? <Wifi size={13} /> : <WifiOff size={13} />}
              {videoOn ? 'Live' : 'Stream'}
            </button>
            {videoOn && VQ_OPTIONS.map(([q, label]) => (
              <button
                key={q}
                className={`toolbar-btn${videoQuality === q ? ' active' : ''}`}
                onClick={() => onVideoQuality(q)}
              >
                {label}
              </button>
            ))}
          </div>
        </>
      )}

      <div className="toolbar-spacer" />

      {/* Speed */}
      <div className="toolbar-speed">
        <span className="toolbar-speed-label">
          Speed {speed < 1 ? speed.toFixed(2) : speed.toFixed(1)}×
        </span>
        <input
          type="range"
          min={0} max={1} step={0.001}
          value={speedToSlider(speed)}
          onChange={(e) => {
            const v = sliderToSpeed(parseFloat(e.target.value));
            setValue('speed', Math.round(v * 100) / 100);
          }}
          aria-label="Fly speed"
        />
      </div>
    </div>
  );
}
