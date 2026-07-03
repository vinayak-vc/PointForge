import { forwardRef, useEffect, useImperativeHandle, useRef } from 'react';

// Fullscreen <img> layer that sits BEHIND all controls and shows the JPEG
// frames the server pushes as binary WebSocket messages. Frames arrive as
// Blobs; each one becomes an object URL assigned to img.src. The previously
// displayed URL is queued and revoked on the img's next `load` event, so
// URLs never leak — even when frames arrive faster than they decode (a
// replaced-before-load URL just waits in the queue for the next load).

export interface VideoLayerProps {
  rtcStream?: MediaStream | null;
  type: 'jpeg' | 'webrtc';
}

export interface VideoLayerHandle {
  pushFrame: (blob: Blob) => void;
}

const VideoLayer = forwardRef<VideoLayerHandle, VideoLayerProps>(function VideoLayer(props, ref) {
  const imgRef = useRef<HTMLImageElement>(null);
  const currentUrlRef = useRef<string | null>(null); // URL currently set as src
  const revokeQueueRef = useRef<string[]>([]); // superseded URLs awaiting revoke

  useImperativeHandle(
    ref,
    () => ({
      pushFrame: (blob: Blob) => {
        const img = imgRef.current;
        if (!img) return;
        const url = URL.createObjectURL(blob);
        if (currentUrlRef.current) revokeQueueRef.current.push(currentUrlRef.current);
        currentUrlRef.current = url;
        img.src = url;
      },
    }),
    [],
  );

  const handleLoad = () => {
    // The new frame is decoded and on screen — safe to drop the old URLs.
    for (const u of revokeQueueRef.current) URL.revokeObjectURL(u);
    revokeQueueRef.current = [];
  };

  // Revoke everything on unmount (video toggled off / disconnect).
  useEffect(() => {
    return () => {
      for (const u of revokeQueueRef.current) URL.revokeObjectURL(u);
      revokeQueueRef.current = [];
      if (currentUrlRef.current) {
        URL.revokeObjectURL(currentUrlRef.current);
        currentUrlRef.current = null;
      }
    };
  }, []);

  const videoRef = useRef<HTMLVideoElement>(null);

  useEffect(() => {
    if (props.type === 'webrtc' && videoRef.current && props.rtcStream) {
      videoRef.current.srcObject = props.rtcStream;
      videoRef.current.play().catch(e => console.warn('Video auto-play prevented:', e));
    }
  }, [props.type, props.rtcStream]);

  if (props.type === 'webrtc') {
    return (
      <video
        ref={videoRef}
        className="video-layer"
        autoPlay
        playsInline
        muted
      />
    );
  }

  return (
    <img
      ref={imgRef}
      className="video-layer"
      alt=""
      draggable={false}
      onLoad={handleLoad}
    />
  );
});

export default VideoLayer;
