import type { ReactNode } from 'react';
import { ChevronDown } from 'lucide-react';
import { hexToRgb, rgbToHex } from './cfg';
import type { Vec3 } from './cfg';

// Premium shared building blocks: Card, Slider, Toggle, Segmented, Select,
// ColorRow. Same API as before — only markup/styling upgraded.

// -------------------------------------------------------------------- Card

export function Card({ title, children }: { title?: string; children: ReactNode }) {
  return (
    <section className="card">
      {title && <h3 className="card-title">{title}</h3>}
      {children}
    </section>
  );
}

// ------------------------------------------------------------------ Slider

const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));

export interface SliderProps {
  label: string;
  value: number;
  min: number;
  max: number;
  step?: number;
  log?: boolean;
  disabled?: boolean;
  format?: (v: number) => string;
  onChange: (v: number) => void;
}


export function Slider({ label, value, min, max, step, log, disabled, format, onChange }: SliderProps) {
  const fmt = format ?? ((v: number) => String(Math.round(v * 100) / 100));
  const toTrack = (v: number) =>
    log ? Math.log(clamp(v, min, max) / min) / Math.log(max / min) : clamp(v, min, max);
  const fromTrack = (t: number) => (log ? min * Math.pow(max / min, t) : t);

  return (
    <label className={`slider-row${disabled ? ' disabled' : ''}`}>
      <span className="slider-head">
        <span>{label}</span>
        <span className="slider-val">{fmt(value)}</span>
      </span>
      <input
        type="range"
        min={log ? 0 : min}
        max={log ? 1 : max}
        step={log ? 0.001 : (step ?? 0.01)}
        value={toTrack(value)}
        disabled={disabled}
        onChange={(e) => {
          const raw = fromTrack(parseFloat(e.target.value));
          onChange(Math.round(raw * 1000) / 1000);
        }}
      />
    </label>
  );
}

// ------------------------------------------------------------------ Toggle

export interface ToggleProps {
  label: string;
  on: boolean;
  disabled?: boolean;
  onChange: (on: boolean) => void;
}

export function Toggle({ label, on, disabled, onChange }: ToggleProps) {
  return (
    <button
      type="button"
      className={`toggle-row${on ? ' on' : ''}`}
      disabled={disabled}
      aria-pressed={on}
      onClick={() => onChange(!on)}
    >
      <span>{label}</span>
      <span className="toggle-knob" aria-hidden="true" />
    </button>
  );
}

// --------------------------------------------------------------- Segmented

export interface SegmentedProps {
  options: readonly string[];
  value: number;
  onChange: (index: number) => void;
  ariaLabel?: string;
}

export function Segmented({ options, value, onChange, ariaLabel }: SegmentedProps) {
  return (
    <div className="seg" role="group" aria-label={ariaLabel}>
      {options.map((opt, i) => (
        <button
          key={opt}
          type="button"
          className={`seg-btn${i === value ? ' active' : ''}`}
          aria-pressed={i === value}
          onClick={() => onChange(i)}
        >
          {opt}
        </button>
      ))}
    </div>
  );
}

// ------------------------------------------------------------------ Select

// Dropdown for mutually-exclusive lists too long for a Segmented control
// (native <select> keeps OS/mobile picker behaviour; styled to match).
export interface SelectProps {
  options: readonly string[];
  value: number;
  onChange: (index: number) => void;
  label?: string;
  ariaLabel?: string;
  /** Per-option disabled flags, same length/order as `options` (e.g. a
   *  transport that isn't available on this server build). */
  disabledOptions?: readonly boolean[];
}

export function Select({ options, value, onChange, label, ariaLabel, disabledOptions }: SelectProps) {
  const control = (
    <span className="select-wrap">
      <select
        className="select"
        value={value}
        aria-label={ariaLabel ?? label}
        onChange={(e) => onChange(Number(e.target.value))}
      >
        {options.map((opt, i) => (
          <option key={opt} value={i} disabled={disabledOptions?.[i]}>
            {opt}
          </option>
        ))}
      </select>
      <ChevronDown size={14} className="select-chevron" aria-hidden="true" />
    </span>
  );
  if (!label) return control;
  return (
    <label className="select-row">
      <span>{label}</span>
      {control}
    </label>
  );
}

// ---------------------------------------------------------------- ColorRow

export interface ColorRowProps {
  label: string;
  value: Vec3;
  onChange: (c: Vec3) => void;
}

export function ColorRow({ label, value, onChange }: ColorRowProps) {
  return (
    <label className="color-row">
      <span>{label}</span>
      <input
        type="color"
        value={rgbToHex(value)}
        onChange={(e) => onChange(hexToRgb(e.target.value))}
        aria-label={label}
      />
    </label>
  );
}
