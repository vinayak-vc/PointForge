import { useCallback, useEffect, useRef, useState } from 'react';
import type { LucideIcon } from 'lucide-react';
import {
  Layers, Camera, Wrench
} from 'lucide-react';
import ActionBar from './ActionBar';
import { loadStoredPin, useWebSocket } from './useWebSocket';
import type { WsStatus } from './useWebSocket';
import FlyTab, { ZERO_MOVE } from './FlyTab';
import type { MoveValues } from './FlyTab';
import DisplayTab from './DisplayTab';
import CameraTab from './CameraTab';
import ToolsTab from './ToolsTab';
import { useCfg } from './cfg';
import VideoLayer, { VideoLayerHandle } from './VideoLayer';
import { VIDEO_PRESETS, VideoQuality } from './stream';
import { useWebRTC } from './useWebRTC';
import StatusHUD from './StatusHUD';

export type Tab = 'fly' | 'display' | 'camera' | 'tools';

const round3 = (v: number) => Math.round(v * 1000) / 1000;

// ---------------------------------------------------------------------------
// Premium Connect Screen
// ---------------------------------------------------------------------------
interface ConnectScreenProps {
  status: WsStatus;
  submitPin: (pin: string) => void;
}

function ConnectScreen({ status, submitPin }: ConnectScreenProps) {
  const [pin, setPin] = useState(loadStoredPin());
  const inputRef = useRef<HTMLInputElement>(null);

  const pinReady = /^\d{4}$/.test(pin);
  const canSubmit = pinReady && (status === 'pin' || status === 'pin_bad');

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    if (canSubmit) submitPin(pin);
  };

  const handleKeypad = (num: string) => {
    if (num === 'C') {
      setPin('');
      return;
    }
    if (num === 'DEL') {
      setPin((prev) => prev.slice(0, -1));
      return;
    }
    setPin((prev) => {
      const next = (prev + num).slice(0, 4);
      if (next.length === 4 && (status === 'pin' || status === 'pin_bad')) {
        submitPin(next);
      }
      return next;
    });
  };

  return (
    <div className="connect-screen">
      <div className="connect-logo-wrap">
        <div className="connect-icon-ring">
          <img src="/logo.svg" alt="Logo" style={{ width: 40, height: 40 }} />
        </div>
        <h1 className="app-title">ViitorXPC</h1>
        <p className="app-subtitle">LiDAR Remote Controller</p>
      </div>

      <div className="connect-card">
        <p className="connect-card-label">Authentication</p>
        <form className="pin-form" onSubmit={handleSubmit}>
          <input
            ref={inputRef}
            className="pin-input"
            type="text"
            placeholder="PIN"
            value={pin}
            readOnly
          />
          <button className="pin-connect" type="submit" disabled={!canSubmit}>
            Connect
          </button>
        </form>

        <div className="pin-keypad">
          {['1', '2', '3', '4', '5', '6', '7', '8', '9', 'C', '0', 'DEL'].map((k) => (
            <button
              key={k}
              type="button"
              className="keypad-btn"
              onClick={() => handleKeypad(k)}
            >
              {k}
            </button>
          ))}
        </div>

        {status === 'pin_bad' && <p className="pin-error">Wrong PIN — check the viewer and try again.</p>}
        {status === 'connecting' && <p className="pin-status">Connecting…</p>}
        {status === 'disconnected' && (
          <p className="pin-status">
            <span className="reconnect-spinner" /> Disconnected — reconnecting…
          </p>
        )}
        {status === 'pin' && <p className="pin-hint">Enter the 4-digit PIN shown in ViitorXPC Viewer.</p>}
      </div>
    </div>
  );
}

// ---------------------------------------------------------------------------
// Inspector tab definitions
// ---------------------------------------------------------------------------
const INSPECTOR_TABS: ReadonlyArray<{ id: Tab; label: string; Icon: LucideIcon }> = [
  { id: 'display', label: 'Display', Icon: Layers },
  { id: 'camera',  label: 'Camera',  Icon: Camera },
  { id: 'tools',   label: 'Tools',   Icon: Wrench },
];

// ---------------------------------------------------------------------------
// App
// ---------------------------------------------------------------------------
export default function App() {
  const [tab, setTab] = useState<Tab>('display');
  const videoLayerRef = useRef<VideoLayerHandle>(null);
  const sendRef = useRef<(obj: unknown) => void>(() => {});

  const { cfg, applyServerCfg, setValue } = useCfg(
    useCallback((obj: unknown) => sendRef.current(obj), [])
  );

  const streamType = (cfg?.preferredStream === 1) ? 'webrtc' : 'jpeg';
  const webrtcMsgRef = useRef<(msg: Record<string, unknown>) => void>(() => {});

  // Screenshot grab: tapping Shot sends the cmd and arms a download; when the
  // server broadcasts shot_ready we pull /shot.png (PIN-gated). Shots taken on
  // the PC itself (F12) also broadcast shot_ready — ignored unless armed, so
  // the phone never downloads a capture it didn't ask for.
  const awaitingShotRef = useRef(0);
  const requestShot = useCallback(() => {
    awaitingShotRef.current = Date.now();
    sendRef.current({ t: 'cmd', n: 'shot' });
  }, []);

  const { status, lastState, send, submitPin } = useWebSocket({
    onCfg: applyServerCfg,
    onFrame: useCallback((blob: Blob) => {
      if (streamType === 'jpeg') videoLayerRef.current?.pushFrame(blob);
    }, [streamType]),
    onWebRTC: useCallback((msg: Record<string, unknown>) => webrtcMsgRef.current(msg), []),
    onShotReady: useCallback(() => {
      if (Date.now() - awaitingShotRef.current > 15000) return; // not armed
      awaitingShotRef.current = 0;
      const pin = loadStoredPin();
      const a = document.createElement('a');
      a.href = `/shot.png?pin=${encodeURIComponent(pin)}&t=${Date.now()}`;
      a.download = 'viitorxpc_shot.png';
      document.body.appendChild(a);
      a.click();
      a.remove();
    }, []),
  });

  const [videoOn, setVideoOn] = useState(false);
  const [videoQuality, setVideoQuality] = useState<VideoQuality>('med');
  const streamAvailable = Boolean(cfg?.streamAvailable);

  const { stream, handleMessage: webrtcHandleMessage } = useWebRTC(
    useCallback((msg) => sendRef.current(msg), []),
    status === 'connected' && streamType === 'webrtc'
  );
  webrtcMsgRef.current = webrtcHandleMessage;

  useEffect(() => { sendRef.current = send; }, [send]);

  const moveRef = useRef<MoveValues>({ ...ZERO_MOVE });

  // 30 Hz move loop
  useEffect(() => {
    if (status !== 'connected') return;
    moveRef.current = { ...ZERO_MOVE };
    let wasActive = false;
    const id = setInterval(() => {
      const m = moveRef.current;
      const active = m.f !== 0 || m.s !== 0 || m.u !== 0 || m.yaw !== 0 || m.pit !== 0 || m.boost !== 0;
      if (active || wasActive) {
        send({ t: 'move', f: round3(m.f), s: round3(m.s), u: m.u, yaw: round3(m.yaw), pit: round3(m.pit), boost: m.boost });
        moveRef.current.f = 0;
        moveRef.current.s = 0;
        moveRef.current.u = 0;
        moveRef.current.yaw = 0;
        moveRef.current.pit = 0;
      }
      wasActive = active;
    }, 33);
    return () => clearInterval(id);
  }, [status, send]);

  // Request video stream when connected
  useEffect(() => {
    if (status === 'connected') {
      if (streamType === 'jpeg') send({ t: 'stream', on: 1, ...VIDEO_PRESETS.med });
      else send({ t: 'stream', on: 0 });
    }
  }, [status, send, streamType]);

  // Wake lock
  useEffect(() => {
    if (status !== 'connected') return;
    let sentinel: { release?: () => Promise<void> } | null = null;
    let stopped = false;
    const acquire = async () => {
      try {
        const wakeLock = (navigator as Navigator & { wakeLock?: { request: (type: 'screen') => Promise<never> } }).wakeLock;
        if (!wakeLock) return;
        const s = await wakeLock.request('screen');
        if (stopped) (s as { release?: () => Promise<void> }).release?.();
        else sentinel = s;
      } catch { /* non-fatal */ }
    };
    const onVisibility = () => { if (document.visibilityState === 'visible' && !stopped) acquire(); };
    acquire();
    document.addEventListener('visibilitychange', onVisibility);
    return () => {
      stopped = true;
      document.removeEventListener('visibilitychange', onVisibility);
      try { sentinel?.release?.(); } catch { /* ignore */ }
      sentinel = null;
    };
  }, [status]);

  if (status !== 'connected') {
    return <ConnectScreen status={status} submitPin={submitPin} />;
  }

  return (
    <div className="app-shell">
      {/* ── Top Toolbar ── */}
      <ActionBar
        send={send}
        cfg={cfg}
        setValue={setValue}
        videoOn={videoOn}
        onVideoToggle={setVideoOn}
        videoQuality={videoQuality}
        onVideoQuality={setVideoQuality}
        streamAvailable={streamAvailable}
        onShot={requestShot}
      />

      {/* ── Workspace ── */}
      <div className="workspace">
        {/* Viewport: video + fly overlay */}
        <div className="viewport-area">
          <img
            src="/logo.svg"
            alt="Watermark"
            className="viewport-watermark"
          />
          <VideoLayer ref={videoLayerRef} rtcStream={stream} type={streamType} />
          <FlyTab moveRef={moveRef} lastState={lastState} status={status} />
        </div>

        {/* Inspector: vertical icon rail + content */}
        <div className="inspector">
          <div className="inspector-icon-rail">
            {INSPECTOR_TABS.map(({ id, label, Icon }) => (
              <button
                key={id}
                type="button"
                className={`inspector-tab${tab === id ? ' active' : ''}`}
                onClick={() => setTab(id)}
                title={label}
              >
                <Icon size={16} />
                {label}
              </button>
            ))}
          </div>

          <div className="inspector-body">
            {tab === 'display' && <DisplayTab cfg={cfg} setValue={setValue} streamType={streamType} />}
            {tab === 'camera'  && <CameraTab cfg={cfg} setValue={setValue} send={send} />}
            {tab === 'tools'   && <ToolsTab cfg={cfg} setValue={setValue} send={send} />}
          </div>
        </div>
      </div>

      {/* ── Status Bar ── */}
      <StatusHUD state={lastState} status={status} />
    </div>
  );
}
