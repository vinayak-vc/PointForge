import { useEffect, useRef, useState } from 'react';

export function useWebRTC(
  send: (msg: unknown) => void,
  enabled: boolean
) {
  const pcRef = useRef<RTCPeerConnection | null>(null);
  const [stream, setStream] = useState<MediaStream | null>(null);

  useEffect(() => {
    if (!enabled) {
      if (pcRef.current) {
        pcRef.current.close();
        pcRef.current = null;
      }
      setStream(null);
      return;
    }

    const pc = new RTCPeerConnection({
      iceServers: [{ urls: 'stun:stun.l.google.com:19302' }]
    });
    pcRef.current = pc;

    pc.addTransceiver('video', { direction: 'recvonly' });

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
      const offer = await pc.createOffer();
      await pc.setLocalDescription(offer);
      send({ t: 'webrtc_offer', sdp: offer.sdp });
    };

    return () => {
      pc.close();
      pcRef.current = null;
      setStream(null);
    };
  }, [enabled, send]);

  const handleMessage = (msg: any) => {
    const pc = pcRef.current;
    if (!pc) return;
    if (msg.t === 'webrtc_answer' && msg.sdp) {
      pc.setRemoteDescription(new RTCSessionDescription({ type: 'answer', sdp: msg.sdp }));
    } else if (msg.t === 'webrtc_ice' && msg.candidate) {
      pc.addIceCandidate(new RTCIceCandidate({
        candidate: msg.candidate,
        sdpMid: msg.sdpMid,
        sdpMLineIndex: msg.sdpMLineIndex
      }));
    }
  };

  return { stream, handleMessage };
}
