// Video stream quality presets. The client subscribes to the server's JPEG
// viewport stream with {"t":"stream","on":1,"w":...,"fps":...,"q":...} and
// unsubscribes with {"t":"stream","on":0}. Only changed values are re-sent
// while a subscription is live.

export type VideoQuality = 'low' | 'med' | 'high';

export interface StreamParams {
  w: number;
  fps: number;
  q: number;
}

export const VIDEO_PRESETS: Record<VideoQuality, StreamParams> = {
  low: { w: 960, fps: 10, q: 55 },
  med: { w: 1280, fps: 15, q: 70 },
  high: { w: 1600, fps: 20, q: 80 },
};
