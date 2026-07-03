import type { Cfg, SetValueFn } from './cfg';
import type { VideoQuality } from './stream';

// Top strip on the Fly tab: one-shot commands, cfg-backed toggles (Ortho /
// Hide UI reflect the server's cfg broadcast), the Video toggle + quality
// picker, and the fly-speed slider. Speed edits go through setValue (protocol
// v2 {"t":"set","k":"speed",...}) which already throttles to ~10 Hz with a
// trailing send, so the final value always goes out.

export interface ActionBarProps {
  send: (obj: unknown) => void;
  cfg: Cfg | null;
  setValue: SetValueFn;
  videoOn: boolean;
  onVideoToggle: (on: boolean) => void;
  videoQuality: VideoQuality;
  onVideoQuality: (q: VideoQuality) => void;
  /** cfg.streamAvailable — the Video toggle is hidden when false. */
  streamAvailable: boolean;
}

const SPEED_MIN = 0.1;
const SPEED_MAX = 10;
const SPEED_DEFAULT = 2;

const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));

// Logarithmic mapping: slider t in [0,1] <-> speed in [0.1,10].
const sliderToSpeed = (t: number) => SPEED_MIN * Math.pow(SPEED_MAX / SPEED_MIN, t);
const speedToSlider = (v: number) => Math.log(v / SPEED_MIN) / Math.log(SPEED_MAX / SPEED_MIN);

const VQ_OPTIONS: ReadonlyArray<readonly [VideoQuality, string]> = [
  ['low', 'L'],
  ['med', 'M'],
  ['high', 'H'],
];

export default function ActionBar({
  send,
  cfg,
  setValue,
  videoOn,
  onVideoToggle,
  videoQuality,
  onVideoQuality,
  streamAvailable,
}: ActionBarProps) {
  const cmd = (n: string) => () => send({ t: 'cmd', n });
  const speed = clamp(cfg?.speed ?? SPEED_DEFAULT, SPEED_MIN, SPEED_MAX);

  return (
    <div className="actionbar">
      <button className="action-btn" onClick={cmd('frame')}>
        Frame
      </button>
      <button className="action-btn" onClick={cmd('preset1')}>
        1
      </button>
      <button className="action-btn" onClick={cmd('preset3')}>
        3
      </button>
      <button className="action-btn" onClick={cmd('preset7')}>
        7
      </button>
      <button
        className={`action-btn${cfg?.ortho ? ' active' : ''}`}
        onClick={() => {
          if (cfg) setValue('ortho', !cfg.ortho);
        }}
        aria-pressed={cfg?.ortho ?? false}
      >
        Ortho
      </button>
      <button
        className={`action-btn${cfg && !cfg.ui ? ' active' : ''}`}
        onClick={() => {
          if (cfg) setValue('ui', !cfg.ui);
        }}
        aria-pressed={cfg ? !cfg.ui : false}
      >
        Hide UI
      </button>
      <button className="action-btn" onClick={cmd('shot')}>
        Shot
      </button>
      {streamAvailable && (
        <button
          className={`action-btn${videoOn ? ' active' : ''}`}
          onClick={() => onVideoToggle(!videoOn)}
          aria-pressed={videoOn}
        >
          Video
        </button>
      )}
      {streamAvailable && videoOn && (
        <div className="vq" role="group" aria-label="Video quality">
          {VQ_OPTIONS.map(([q, label]) => (
            <button
              key={q}
              className={`action-btn vq-btn${videoQuality === q ? ' active' : ''}`}
              onClick={() => onVideoQuality(q)}
            >
              {label}
            </button>
          ))}
        </div>
      )}
      <div className="speed-control">
        <span className="speed-label">
          Speed {speed < 1 ? speed.toFixed(2) : speed.toFixed(1)}x
        </span>
        <input
          type="range"
          min={0}
          max={1}
          step={0.001}
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
