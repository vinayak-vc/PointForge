import { useCallback, useEffect, useRef, useState } from 'react';
import ActionBar from './ActionBar';
import { loadStoredPin, useWebSocket } from './useWebSocket';
import type { WsStatus } from './useWebSocket';
import TabBar from './TabBar';
import type { Tab } from './TabBar';
import FlyTab, { ZERO_MOVE } from './FlyTab';
import type { MoveValues } from './FlyTab';
import DisplayTab from './DisplayTab';
import CameraTab from './CameraTab';
import ToolsTab from './ToolsTab';
import { useCfg } from './cfg';
import VideoLayer, { VideoLayerHandle } from './VideoLayer';
import { VIDEO_PRESETS, VideoQuality } from './stream';
import { useWebRTC } from './useWebRTC';

// Phone-browser remote for pfview. Landscape-first: left stick flies
// (f/s), right stick looks (yaw/pit), centre column holds UP/DOWN/BOOST.
// Move messages go out on a single 33 ms (30 Hz) interval that reads the
// latest control values from a ref — plus exactly one final all-zeros move
// when every control is released.

const round3 = (v: number) => Math.round(v * 1000) / 1000;

// ---------------------------------------------------------------------------
// PIN / connection screen — shown whenever we are not fully connected.
// ---------------------------------------------------------------------------
interface ConnectScreenProps {
  status: WsStatus;
  submitPin: (pin: string) => void;
}

function ConnectScreen({ status, submitPin }: ConnectScreenProps) {
  const [pin, setPin] = useState(loadStoredPin());
  const inputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    inputRef.current?.focus();
  }, []);

  const pinReady = /^\d{4}$/.test(pin);
  const canSubmit = pinReady && (status === 'pin' || status === 'pin_bad');

  const handleChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const v = e.target.value.replace(/\D/g, '').slice(0, 4);
    setPin(v);
    // Auto-submit as soon as the 4th digit is typed.
    if (v.length === 4 && (status === 'pin' || status === 'pin_bad')) {
      submitPin(v);
    }
  };

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    if (canSubmit) submitPin(pin);
  };

  return (
    <div className="connect-screen">
      <h1 className="app-title">ViitorX Remote</h1>
      <form className="pin-form" onSubmit={handleSubmit}>
        <input
          ref={inputRef}
          className="pin-input"
          type="text"
          inputMode="numeric"
          autoComplete="one-time-code"
          pattern="\d{4}"
          maxLength={4}
          placeholder="PIN"
          value={pin}
          onChange={handleChange}
          autoFocus
        />
        <button className="pin-connect" type="submit" disabled={!canSubmit}>
          Connect
        </button>
      </form>
      {status === 'pin_bad' && (
        <p className="pin-error">Wrong PIN — check the viewer and try again.</p>
      )}
      {status === 'connecting' && <p className="pin-status">Connecting…</p>}
      {status === 'disconnected' && (
        <p className="pin-status">
          <span className="reconnect-spinner" /> Disconnected — reconnecting…
        </p>
      )}
      {status === 'pin' && (
        <p className="pin-hint">Enter the 4-digit PIN shown in pfview.</p>
      )}
    </div>
  );
}

// ---------------------------------------------------------------------------
// App
// ---------------------------------------------------------------------------
export default function App() {
  const [tab, setTab] = useState<Tab>('fly');
  const videoLayerRef = useRef<VideoLayerHandle>(null);

  // useCfg needs `send`, but `useWebSocket` needs `useCfg`'s applyServerCfg.
  // Break the circular dependency with a ref to the latest send function.
  const sendRef = useRef<(obj: unknown) => void>(() => {});

  const { cfg, applyServerCfg, setValue } = useCfg(
    useCallback((obj: unknown) => sendRef.current(obj), [])
  );

  const streamType = (cfg?.preferredStream === 1) ? 'webrtc' : 'jpeg';

  // placeholder for WebRTC message handler to avoid forward reference
  let handleWebRTCMessage = (_msg: unknown) => {};

  const { status, lastState, send, submitPin } = useWebSocket({
    onCfg: applyServerCfg,
    onFrame: useCallback((blob: Blob) => {
      if (streamType === 'jpeg') {
        videoLayerRef.current?.pushFrame(blob);
      }
    }, [streamType]),
    onWebRTC: handleWebRTCMessage,
  });

  // video control state
  const [videoOn, setVideoOn] = useState(false);
  const [videoQuality, setVideoQuality] = useState<VideoQuality>('med');
  const streamAvailable = Boolean(cfg?.streamAvailable);

  // Update useWebRTC after status and streamType are defined
  const { stream, handleMessage: webrtcHandleMessage } = useWebRTC(
    useCallback((msg) => sendRef.current(msg), []),
    status === 'connected' && streamType === 'webrtc'
  );
  // Assign the actual handler to the placeholder used by useWebSocket
  handleWebRTCMessage = webrtcHandleMessage;

  useEffect(() => {
    sendRef.current = send;
  }, [send]);

  const moveRef = useRef<MoveValues>({ ...ZERO_MOVE });

  // 30 Hz move loop — only while connected. Sends while any control is
  // active, plus one trailing all-zeros message on release, then goes quiet.
  useEffect(() => {
    if (status !== 'connected') return;
    moveRef.current = { ...ZERO_MOVE }; // never carry stale input across (re)connects
    let wasActive = false;
    const id = setInterval(() => {
      const m = moveRef.current;
      const active =
        m.f !== 0 || m.s !== 0 || m.u !== 0 || m.yaw !== 0 || m.pit !== 0 || m.boost !== 0;
      if (active || wasActive) {
        send({
          t: 'move',
          f: round3(m.f),
          s: round3(m.s),
          u: m.u,
          yaw: round3(m.yaw),
          pit: round3(m.pit),
          boost: m.boost,
        });
        
        // Reset deltas for gesture support, keep continuous values (boost) intact if needed, 
        // but for now we reset everything and let the touch loop replenish them.
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
      if (streamType === 'jpeg') {
        send({ t: 'stream', on: 1, ...VIDEO_PRESETS.med });
      } else {
        // webrtc negotiated via useWebRTC hook, disable jpeg stream
        send({ t: 'stream', on: 0 });
      }
    }
  }, [status, send, streamType]);

  // Keep the phone screen awake while connected; re-acquire when the tab
  // becomes visible again. Wrapped in try/catch — not all browsers support it.
  useEffect(() => {
    if (status !== 'connected') return;
    let sentinel: { release?: () => Promise<void> } | null = null;
    let stopped = false;

    const acquire = async () => {
      try {
        const wakeLock = (navigator as Navigator & {
          wakeLock?: { request: (type: 'screen') => Promise<never> };
        }).wakeLock;
        if (!wakeLock) return;
        const s = await wakeLock.request('screen');
        if (stopped) {
          (s as { release?: () => Promise<void> }).release?.();
        } else {
          sentinel = s;
        }
      } catch {
        /* unsupported or denied — non-fatal */
      }
    };

    const onVisibility = () => {
      if (document.visibilityState === 'visible' && !stopped) acquire();
    };

    acquire();
    document.addEventListener('visibilitychange', onVisibility);
    return () => {
      stopped = true;
      document.removeEventListener('visibilitychange', onVisibility);
      try {
        sentinel?.release?.();
      } catch {
        /* ignore */
      }
      sentinel = null;
    };
  }, [status]);

  if (status !== 'connected') {
    return <ConnectScreen status={status} submitPin={submitPin} />;
  }

  return (
    <div className="app">
      <VideoLayer ref={videoLayerRef} rtcStream={stream} type={streamType} />
        <ActionBar
          send={send}
          cfg={cfg}
          setValue={setValue}
          videoOn={videoOn}
          onVideoToggle={setVideoOn}
          videoQuality={videoQuality}
          onVideoQuality={setVideoQuality}
          streamAvailable={streamAvailable}
        />
      <div className="main-area">
        <TabBar tab={tab} onSelect={setTab} />
        <div className="tab-content">
          {tab === 'fly' && <FlyTab moveRef={moveRef} lastState={lastState} status={status} />}
          {tab === 'display' && <DisplayTab cfg={cfg} setValue={setValue} streamType={streamType} />}
          {tab === 'camera' && <CameraTab cfg={cfg} setValue={setValue} send={send} />}
          {tab === 'tools' && <ToolsTab cfg={cfg} setValue={setValue} send={send} />}
        </div>
      </div>
    </div>
  );
}
