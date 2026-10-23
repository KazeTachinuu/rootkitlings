import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'
import { defineConfig } from 'vite'

export default defineConfig({
  plugins: [react(), tailwindcss()],
  build: { outDir: 'dist' },
  server: {
    proxy: {
      '/ws': { target: 'ws://127.0.0.1:8080', ws: true, rewriteWsOrigin: true },
      '/shell': { target: 'ws://127.0.0.1:8080', ws: true, rewriteWsOrigin: true },
    },
  },
})
