import { useEffect, useRef, useState } from 'react';

export function useWebRTC(
  send: (msg: unknown) => void,
  enabled: boolean
) {
  const pcRef = useRef<RTCPeerConnection | null>(null);
  // Remote ICE candidates that arrive before setRemoteDescription resolves.
  // addIceCandidate throws in that state, so they wait here and flush after.
  const pendingCandidatesRef = useRef<RTCIceCandidateInit[]>([]);
  const remoteDescSetRef = useRef(false);
  const [stream, setStream] = useState<MediaStream | null>(null);

  useEffect(() => {
    if (!enabled) {
      if (pcRef.current) {
        pcRef.current.close();
        pcRef.current = null;
      }
      pendingCandidatesRef.current = [];
      remoteDescSetRef.current = false;
      setStream(null);
      return;
    }

    const pc = new RTCPeerConnection({
      iceServers: [{ urls: 'stun:stun.l.google.com:19302' }]
    });
    pcRef.current = pc;
    pendingCandidatesRef.current = [];
    remoteDescSetRef.current = false;

    pc.addTransceiver('video', { direction: 'recvonly' });

    // Diagnostics — open browser console to see where a broken stream fails:
    // ICE stuck at "checking" = candidates/connectivity; connected but no
    // framesDecoded = codec/packetization issue server-side.
    pc.oniceconnectionstatechange = () => console.log('[webrtc] ice:', pc.iceConnectionState);
    pc.onconnectionstatechange = () => console.log('[webrtc] pc:', pc.connectionState);
    const statsTimer = setInterval(async () => {
      if (pc.connectionState !== 'connected') return;
      const stats = await pc.getStats();
      stats.forEach(r => {
        if (r.type === 'inbound-rtp' && r.kind === 'video') {
          console.log(`[webrtc] frames=${r.framesDecoded} bytes=${r.bytesReceived} keyframes=${r.keyFramesDecoded}`);
        }
      });
    }, 2000);

    pc.ontrack = (e) => {
      if (e.streams && e.streams[0]) {
        setStream(e.streams[0]);
      } else {
        const ms = new MediaStream();
        ms.addTrack(e.track);
        setStream(ms);
      }
    };

    pc.onicecandidate = (e) => {
      if (e.candidate) {
        send({
          t: 'webrtc_ice',
          candidate: e.candidate.candidate,
          sdpMid: e.candidate.sdpMid,
          sdpMLineIndex: e.candidate.sdpMLineIndex
        });
      }
    };

    pc.onnegotiationneeded = async () => {
      try {
        const offer = await pc.createOffer();
        await pc.setLocalDescription(offer);
        send({ t: 'webrtc_offer', type: 'offer', sdp: offer.sdp });
      } catch (e) {
        console.warn('WebRTC offer failed:', e);
      }
    };

    return () => {
      clearInterval(statsTimer);
      pc.close();
      pcRef.current = null;
      pendingCandidatesRef.current = [];
      remoteDescSetRef.current = false;
      setStream(null);
    };
  }, [enabled, send]);

  const addCandidate = (pc: RTCPeerConnection, init: RTCIceCandidateInit) => {
    pc.addIceCandidate(new RTCIceCandidate(init))
      .catch(e => console.warn('WebRTC addIceCandidate failed:', e));
  };

  const handleMessage = (msg: any) => {
    const pc = pcRef.current;
    if (!pc) return;
    if (msg.t === 'webrtc_answer' && msg.sdp) {
      pc.setRemoteDescription(new RTCSessionDescription({ type: 'answer', sdp: msg.sdp }))
        .then(() => {
          remoteDescSetRef.current = true;
          for (const c of pendingCandidatesRef.current) addCandidate(pc, c);
          pendingCandidatesRef.current = [];
        })
        .catch(e => console.warn('WebRTC setRemoteDescription failed:', e));
    } else if ((msg.t === 'webrtc_ice' || msg.t === 'webrtc_candidate') && msg.candidate) {
      // Server (libdatachannel) sends sdpMid but no sdpMLineIndex — default
      // the mid so the browser can match the m-line.
      const init: RTCIceCandidateInit = {
        candidate: msg.candidate,
        sdpMid: msg.sdpMid ?? 'video',
        sdpMLineIndex: msg.sdpMLineIndex ?? 0
      };
      if (remoteDescSetRef.current) {
        addCandidate(pc, init);
      } else {
        pendingCandidatesRef.current.push(init);
      }
    }
  };

  return { stream, handleMessage };
}
