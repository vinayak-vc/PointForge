import { useCallback, useEffect, useRef, useState } from 'react';

// ---------------------------------------------------------------------------
// Protocol v2 settings sync. The server broadcasts {"t":"cfg",...} ~1 Hz to
// authed clients and immediately after applying a change. The client edits a
// setting with {"t":"set","k":<key>,"v":<value>}.
//
// Echo handling: while the user is actively dragging a control we must NOT
// let the periodic cfg broadcast fight the slider. We keep a per-key
// timestamp of the last local edit and skip incoming values for keys edited
// less than ECHO_SUPPRESS_MS ago. Outgoing "set" messages are throttled per
// key to ~10 Hz with a trailing send, so the final value of a drag always
// goes out.
// ---------------------------------------------------------------------------

export type Vec3 = [number, number, number];

export type Annotation = {
  p: Vec3;
  label: string;
};

export type SceneCloud = {
  name: string;
  pts: number;
  visible: boolean;
};

// Full cfg snapshot as broadcast by the server (assumed complete each time;
// merged defensively so a partial broadcast would still work after the first).
export type Cfg = {
  quality: number; // 0-3
  pointSize: number; // 1-16
  sse: number; // 0.3-8
  colorMode: number; // 0-4
  solidColor: Vec3; // 0-1 floats
  edl: boolean;
  edlStrength: number; // 0.1-5
  edlRadius: number; // 0.5-4
  gpuBudget: number; // 128-8192 MB
  uploads: number; // 1-256
  round: boolean;
  attenuate: boolean;
  background: Vec3;
  ortho: boolean;
  orthoSize: number; // 1-5000
  speed: number; // 0.1-10
  stereo: boolean;
  eyeSep: number; // 0.01-0.2
  focalDist: number; // 1-100
  tool: number; // 0 nav | 1 measure | 2 clip | 3 annotate
  clip: boolean;
  clipMin: Vec3;
  clipMax: Vec3;
  clipExt: number; // clip slider range is -clipExt..clipExt (0 = no cloud)
  measurePts: Vec3[];
  measureTotal: number;
  annotations: Annotation[];
  ui: boolean;
  stats: boolean;
  fullscreen: boolean;
  darkTheme: boolean;
  recent: string[];
  file: string;
  pts: number;
  nodes: number;
  cubeSize: number;
  /** World Z of the octree cube bottom; zmax = zmin + cubeSize (legend labels). */
  zmin: number;
  /** PF_VERSION_STRING, e.g. "1.0.4" — shown in the status bar and browser tab title. */
  version: string;
  streamAvailable: boolean;
  webrtcAvailable: boolean;
  preferredStream: number;
  /** Camera bookmark names for the loaded cloud; recall/save/delete via
   *  cmds bookmark_goto/bookmark_add/bookmark_del (v = index). */
  bookmarks: string[];
  /** Camera path (keyframed fly-through) for the loaded cloud. Preview
   *  transport via cmds path_play/path_stop; authoring + MP4 export are
   *  PC-only. */
  pathKeys: number;
  pathDuration: number;
  pathPlaying: boolean;
  /** Multi-cloud scene rows (name, point count, visibility). Toggle a
   *  cloud via cmd cloud_vis with v = [index, on ? 1 : 0, 0]; loading and
   *  closing clouds stay PC-only. */
  clouds: SceneCloud[];
};

// Keys the client may write back with {"t":"set",...}. (tool/fullscreen and
// measure state change via one-shot cmds instead.)
export type SettableKey =
  | 'quality'
  | 'pointSize'
  | 'sse'
  | 'colorMode'
  | 'solidColor'
  | 'edl'
  | 'edlStrength'
  | 'edlRadius'
  | 'gpuBudget'
  | 'uploads'
  | 'round'
  | 'attenuate'
  | 'background'
  | 'ortho'
  | 'orthoSize'
  | 'speed'
  | 'stereo'
  | 'eyeSep'
  | 'focalDist'
  | 'clip'
  | 'clipMin'
  | 'clipMax'
  | 'ui'
  | 'stats'
  | 'darkTheme'
  | 'preferredStream';

export type SetValueFn = <K extends SettableKey>(k: K, v: Cfg[K]) => void;

const ECHO_SUPPRESS_MS = 800; // ignore server echo for keys edited this recently
const SEND_MIN_INTERVAL_MS = 100; // ~10 Hz outgoing per key while dragging

type Sendable = number | boolean | Vec3;

const r3 = (v: number) => Math.round(v * 1000) / 1000;

function roundValue(v: Sendable): Sendable {
  if (typeof v === 'number') return r3(v);
  if (Array.isArray(v)) return [r3(v[0]), r3(v[1]), r3(v[2])];
  return v;
}

// --------------------------------------------------------- color helpers ---

/** [r,g,b] floats (0-1) -> "#rrggbb" for <input type="color">. */
export function rgbToHex(c: Vec3): string {
  const to2 = (v: number) =>
    Math.round(Math.min(1, Math.max(0, v)) * 255)
      .toString(16)
      .padStart(2, '0');
  return `#${to2(c[0])}${to2(c[1])}${to2(c[2])}`;
}

/** "#rrggbb" -> [r,g,b] floats (0-1). Falls back to black on garbage. */
export function hexToRgb(hex: string): Vec3 {
  const h = hex.startsWith('#') ? hex.slice(1) : hex;
  const chan = (i: number) => {
    const v = parseInt(h.slice(i, i + 2), 16);
    return Number.isNaN(v) ? 0 : v / 255;
  };
  return [chan(0), chan(2), chan(4)];
}

// ------------------------------------------------------------------ hook ---

interface ThrottleEntry {
  lastSent: number;
  timer: ReturnType<typeof setTimeout> | null;
  pending: { v: Sendable } | null; // boxed so pending `false` isn't falsy
}

export interface UseCfgResult {
  cfg: Cfg | null;
  /** Merge an incoming {"t":"cfg",...} broadcast (with echo suppression). */
  applyServerCfg: (msg: Record<string, unknown>) => void;
  /** Optimistically set a value locally and send {"t":"set",...} (throttled). */
  setValue: SetValueFn;
}

export function useCfg(send: (obj: unknown) => void): UseCfgResult {
  const [cfg, setCfg] = useState<Cfg | null>(null);
  const editedAtRef = useRef<Map<string, number>>(new Map());
  const throttleRef = useRef<Map<string, ThrottleEntry>>(new Map());

  // Clear any trailing-send timers on unmount.
  useEffect(() => {
    const map = throttleRef.current;
    return () => {
      for (const e of map.values()) {
        if (e.timer !== null) clearTimeout(e.timer);
      }
      map.clear();
    };
  }, []);

  const applyServerCfg = useCallback((msg: Record<string, unknown>) => {
    const now = Date.now();
    setCfg((prev) => {
      const next: Record<string, unknown> = prev ? { ...prev } : {};
      for (const [k, v] of Object.entries(msg)) {
        if (k === 't') continue;
        // Local-echo suppression: skip keys the user edited very recently
        // (only once we have a baseline — the first cfg is applied whole).
        if (prev) {
          const editedAt = editedAtRef.current.get(k);
          if (editedAt !== undefined && now - editedAt < ECHO_SUPPRESS_MS) continue;
        }
        next[k] = v;
      }
      return next as unknown as Cfg;
    });
  }, []);

  const setValue = useCallback(
    <K extends SettableKey>(k: K, v: Cfg[K]) => {
      editedAtRef.current.set(k, Date.now());
      // Optimistic local update so the UI tracks the drag immediately.
      setCfg((prev) => (prev ? { ...prev, [k]: v } : prev));

      const map = throttleRef.current;
      let entry = map.get(k);
      if (!entry) {
        entry = { lastSent: 0, timer: null, pending: null };
        map.set(k, entry);
      }
      const e = entry;
      const now = Date.now();
      const elapsed = now - e.lastSent;
      if (e.timer === null && elapsed >= SEND_MIN_INTERVAL_MS) {
        e.lastSent = now;
        send({ t: 'set', k, v: roundValue(v) });
      } else {
        // Too soon — remember the latest value and send it on the trailing
        // edge, so the final value of a drag is never dropped.
        e.pending = { v };
        if (e.timer === null) {
          e.timer = setTimeout(
            () => {
              e.timer = null;
              if (e.pending) {
                const pv = e.pending.v;
                e.pending = null;
                e.lastSent = Date.now();
                send({ t: 'set', k, v: roundValue(pv) });
              }
            },
            Math.max(16, SEND_MIN_INTERVAL_MS - elapsed),
          );
        }
      }
    },
    [send],
  );

  return { cfg, applyServerCfg, setValue };
}
