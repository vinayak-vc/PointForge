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
import Legend from './Legend';
import { useCfg } from './cfg';
import VideoLayer, { VideoLayerHandle } from './VideoLayer';
import { VIDEO_PRESETS, VideoQuality } from './stream';
import { useWebRTC } from './useWebRTC';
import StatusHUD from './StatusHUD';
import BrandingPanel from './BrandingPanel';
import GlassCard from './GlassCard';
import PinInput from './PinInput';
import NumberPad from './NumberPad';
import StatusIndicator from './StatusIndicator';
import PrimaryButton from './PrimaryButton';

export type Tab = 'fly' | 'display' | 'camera' | 'tools';

const round3 = (v: number) => Math.round(v * 1000) / 1000;

// ---------------------------------------------------------------------------
// Premium Connect Screen — a two-pane (branding / auth) industrial-control
// splash on desktop, stacking to a single column on tablet/mobile. Auth
// logic (pin state, auto-submit-at-4-digits, submitPin) is unchanged from
// the previous single-<input> version — only the input/keypad presentation
// moved into PinInput/NumberPad.
// ---------------------------------------------------------------------------
interface ConnectScreenProps {
  status: WsStatus;
  submitPin: (pin: string) => void;
}

function ConnectScreen({ status, submitPin }: ConnectScreenProps) {
  const [pin, setPin] = useState(loadStoredPin());

  const pinReady = /^\d{4}$/.test(pin);
  const canSubmit = pinReady && (status === 'pin' || status === 'pin_bad');
  const connecting = status === 'connecting';

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    if (canSubmit) submitPin(pin);
  };

  const setPinAndMaybeSubmit = (next: string) => {
    setPin(next);
    if (next.length === 4 && (status === 'pin' || status === 'pin_bad')) {
      submitPin(next);
    }
  };

  const handleKeypad = (key: string) => {
    if (key === 'C') { setPin(''); return; }
    if (key === 'DEL') { setPin((prev) => prev.slice(0, -1)); return; }
    setPinAndMaybeSubmit((pin + key).slice(0, 4));
  };

  return (
    <div className="connect-screen">
      <div className="connect-vignette" aria-hidden="true" />
      <div className="connect-layout">
        <BrandingPanel status={status} />

        <div className="connect-auth-pane">
          <GlassCard className="auth-card">
            <p className="connect-card-label">Authentication</p>

            <form onSubmit={handleSubmit}>
              <PinInput
                value={pin}
                onChange={setPinAndMaybeSubmit}
                error={status === 'pin_bad'}
                disabled={connecting}
                autoFocus
              />
              <PrimaryButton disabled={!canSubmit} loading={connecting}>
                Connect
              </PrimaryButton>
            </form>

            <NumberPad onPress={handleKeypad} disabled={connecting} />

            <StatusIndicator status={status} />
          </GlassCard>
        </div>
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

  // Browser tab title mirrors the connected viewer's build version.
  useEffect(() => {
    document.title = cfg?.version ? `ViitorXPC - v${cfg.version}` : 'ViitorXPC';
  }, [cfg?.version]);

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
          <Legend cfg={cfg} />
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
      <StatusHUD cfg={cfg} state={lastState} status={status} />
    </div>
  );
}
