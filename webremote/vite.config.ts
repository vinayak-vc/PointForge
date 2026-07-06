import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// Served as static files from pfview's embedded civetweb doc root, so all
// asset URLs must be relative (base: './'). The dev-server proxy forwards
// /ws to a locally running viewer for development against the real backend.
export default defineConfig({
  base: './',
  plugins: [react()],
  server: {
    proxy: {
      '/ws': {
        target: 'ws://localhost:8899',
        ws: true,
      },
    },
  },
});
