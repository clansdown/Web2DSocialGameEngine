import { defineConfig } from 'vite'
import { svelte } from '@sveltejs/vite-plugin-svelte'

// https://vite.dev/config/
export default defineConfig({
  plugins: [svelte()],
  server: {
    host: true,
    port: 8120,
    allowedHosts: ['wayreth.home'],
    proxy: {
      '/api': {
        target: 'http://localhost:2290',
        changeOrigin: true,
      },
      '/images': {
        target: 'http://localhost:2290',
        changeOrigin: true,
      },
    },
  },
})
