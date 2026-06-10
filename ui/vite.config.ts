import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import path from 'path'

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [react()],
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src'),
    },
  },
  server: {
    port: 3000,
    host: '0.0.0.0', // Allow external connections (needed for Docker)
    open: false, // Don't auto-open in Docker
    watch: {
      // Enable polling for better file watching in Docker
      usePolling: true,
    },
    hmr: {
      // Enable HMR (Hot Module Replacement)
      clientPort: 3000,
    },
    proxy: {
      // Proxy /api/zenoh requests to Zenoh bridge REST API
      // Use host.docker.internal when running in Docker to access host network services
      '/api/zenoh': {
        target: process.env.ZENOH_BRIDGE_URL || 'http://host.docker.internal:8000',
        changeOrigin: true,
        rewrite: (path) => path.replace(/^\/api\/zenoh/, ''),
        configure: (proxy, _options) => {
          proxy.on('error', (err, _req, _res) => {
            console.error('Zenoh proxy error:', err);
          });
        },
      },
    },
  },
  build: {
    outDir: 'dist',
    sourcemap: true,
    chunkSizeWarningLimit: 1000,
  },
})

