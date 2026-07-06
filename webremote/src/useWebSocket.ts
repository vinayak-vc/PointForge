import { useCallback, useEffect, useRef, useState } from 'react';

// ---------------------------------------------------------------------------
// WebSocket protocol — see pfview embedded server.
// v1 (JSON text frames): client -> server hello / move / cmd; server ->
// client hello_ok / hello_bad / state.  v2 adds: server -> client
// {"t":"cfg",...} settings broadcasts (~1 Hz + after each applied change),
// client -> server {"t":"set","k":...,"v":...} and {"t":"stream",...}, and
// BINARY frames (JPEG viewport images) delivered as Blobs to `onFrame`.
// This hook owns the connection lifecycle: hello handshake with PIN,
// exponential-backoff reconnect (0.5 s -> 5 s cap) and automatic hello
// re-send with the last accepted PIN on reconnect.
// ---------------------------------------------------------------------------

export type WsStatus =
  | 'connecting'
  | 'pin'
  | 'connected'
  | 'pin_bad'
  | 'disconnected';

export interface StateMsg {
  t: 'state';
  fps: number;
  pts: number;
  pos: [number, number, number];
  ui: boolean;
  file: string;
}

const PIN_STORAGE_KEY = 'pfremote_pin';
const BACKOFF_MIN_MS = 500;
const BACKOFF_MAX_MS = 5000;

export function loadStoredPin(): string {
  try {
    return localStorage.getItem(PIN_STORAGE_KEY) ?? '';
  } catch {
    return ''; // storage may be unavailable (private mode etc.)
  }
}

export interface UseWebSocketOptions {
  /** Called for every binary frame (JPEG viewport image) from the server. */
  onFrame?: (blob: Blob) => void;
  /** Called for every {"t":"cfg",...} settings broadcast. */
  onCfg?: (msg: Record<string, unknown>) => void;
  /** Called for WebRTC signaling messages. */
  onWebRTC?: (msg: Record<string, unknown>) => void;
  /** Called when the server reports a fresh screenshot at /shot.png. */
  onShotReady?: () => void;
}

export interface UseWebSocketResult {
  status: WsStatus;
  lastState: StateMsg | null;
  send: (obj: unknown) => void;
  submitPin: (pin: string) => void;
}

export function useWebSocket(opts: UseWebSocketOptions = {}): UseWebSocketResult {
  const [status, setStatus] = useState<WsStatus>('connecting');
  const [lastState, setLastState] = useState<StateMsg | null>(null);

  // Callbacks live in a ref so the connection effect never has to re-run
  // (and thus reconnect) just because a render produced new closures.
  const optsRef = useRef(opts);
  optsRef.current = opts;

  const wsRef = useRef<WebSocket | null>(null);
  const statusRef = useRef<WsStatus>('connecting');
  // PIN we are allowed to auto-resend on reconnect. Cleared on hello_bad so a
  // rejected PIN is never resent in a loop (server closes after 3 bad tries).
  const submittedPinRef = useRef<string | null>(null);
  const backoffRef = useRef(BACKOFF_MIN_MS);
  const reconnectTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const disposedRef = useRef(false);
  const connectRef = useRef<() => void>(() => {});

  const setStatusBoth = useCallback((s: WsStatus) => {
    statusRef.current = s;
    setStatus(s);
  }, []);

  const scheduleReconnect = useCallback(() => {
    if (disposedRef.current || reconnectTimerRef.current !== null) return;
    const delay = backoffRef.current;
    backoffRef.current = Math.min(backoffRef.current * 2, BACKOFF_MAX_MS);
    reconnectTimerRef.current = setTimeout(() => {
      reconnectTimerRef.current = null;
      connectRef.current();
    }, delay);
  }, []);

  const connect = useCallback(() => {
    if (disposedRef.current) return;

    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    let ws: WebSocket;
    try {
      ws = new WebSocket(`${proto}//${location.host}/ws`);
    } catch {
      scheduleReconnect();
      return;
    }
    ws.binaryType = 'blob'; // binary frames = JPEG viewport images
    wsRef.current = ws;
    // Keep pin_bad visible across the reconnect the server forces on us.
    if (statusRef.current !== 'pin_bad') setStatusBoth('connecting');

    ws.onopen = () => {
      if (wsRef.current !== ws) return;
      backoffRef.current = BACKOFF_MIN_MS;
      if (submittedPinRef.current) {
        // Reconnect with a previously submitted PIN: re-hello automatically.
        ws.send(JSON.stringify({ t: 'hello', pin: submittedPinRef.current }));
        setStatusBoth('connecting');
      } else if (statusRef.current !== 'pin_bad') {
        setStatusBoth('pin');
      }
    };

    ws.onmessage = (ev: MessageEvent) => {
      if (wsRef.current !== ws) return;
      if (ev.data instanceof Blob) {
        // Binary frame = JPEG viewport image for the video layer.
        optsRef.current.onFrame?.(ev.data);
        return;
      }
      let msg: { t?: string };
      try {
        msg = JSON.parse(String(ev.data));
      } catch {
        return; // ignore malformed frames
      }
      switch (msg.t) {
        case 'hello_ok':
          setStatusBoth('connected');
          break;
        case 'hello_bad':
          submittedPinRef.current = null;
          setStatusBoth('pin_bad');
          break;
        case 'state':
          setLastState(msg as StateMsg);
          break;
        case 'cfg':
          optsRef.current.onCfg?.(msg as Record<string, unknown>);
          break;
        case 'webrtc_answer':
        case 'webrtc_ice':
        case 'webrtc_candidate': // server-side trickle ICE (libdatachannel)
          optsRef.current.onWebRTC?.(msg as Record<string, unknown>);
          break;
        case 'shot_ready':
          optsRef.current.onShotReady?.();
          break;
        default:
          break; // unknown message types are ignored (forward compat)
      }
    };

    ws.onclose = () => {
      if (wsRef.current !== ws) return; // stale socket (replaced or disposed)
      wsRef.current = null;
      if (disposedRef.current) return;
      if (statusRef.current !== 'pin_bad') setStatusBoth('disconnected');
      scheduleReconnect();
    };

    ws.onerror = () => {
      // onclose always follows; just make sure the socket dies.
      try {
        ws.close();
      } catch {
        /* already closed */
      }
    };
  }, [scheduleReconnect, setStatusBoth]);

  connectRef.current = connect;

  useEffect(() => {
    disposedRef.current = false;
    connect();
    return () => {
      disposedRef.current = true;
      if (reconnectTimerRef.current !== null) {
        clearTimeout(reconnectTimerRef.current);
        reconnectTimerRef.current = null;
      }
      const ws = wsRef.current;
      wsRef.current = null; // mark stale before closing so onclose ignores it
      try {
        ws?.close();
      } catch {
        /* ignore */
      }
    };
  }, [connect]);

  const send = useCallback((obj: unknown) => {
    const ws = wsRef.current;
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify(obj));
    }
  }, []);

  const submitPin = useCallback(
    (pin: string) => {
      submittedPinRef.current = pin;
      try {
        localStorage.setItem(PIN_STORAGE_KEY, pin);
      } catch {
        /* storage unavailable — PIN just won't be prefilled next time */
      }
      const ws = wsRef.current;
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ t: 'hello', pin }));
        setStatusBoth('connecting');
      }
      // If the socket is not open yet, onopen will send the hello for us.
    },
    [setStatusBoth],
  );

  return { status, lastState, send, submitPin };
}
