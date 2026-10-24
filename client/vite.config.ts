import { fileURLToPath } from 'node:url'
import { defineConfig, loadEnv } from 'vite'
import { svelte } from '@sveltejs/vite-plugin-svelte'

// Resolve .env.development.local from client/ even when Vite is invoked with
// --prefix from the repository root.
const clientRoot = fileURLToPath(new URL('.', import.meta.url))

// https://vite.dev/config/
export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, clientRoot, '')
  const proxyTarget = env.DEV_PROXY_TARGET || 'http://localhost:2290'

  return {
    plugins: [svelte()],
    server: {
      host: true,
      port: 8120,
      allowedHosts: ['wayreth.home'],
      proxy: {
        '/api': {
          target: proxyTarget,
          changeOrigin: true,
        },
        '/images': {
          target: proxyTarget,
          changeOrigin: true,
        },
        '/ws': {
          target: proxyTarget,
          changeOrigin: true,
          ws: true,
        },
      },
    },
    preview: {
      proxy: {
        '/api': {
          target: proxyTarget,
          changeOrigin: true,
        },
        '/images': {
          target: proxyTarget,
          changeOrigin: true,
        },
        '/ws': {
          target: proxyTarget,
          changeOrigin: true,
          ws: true,
        },
      },
    },
  }
})
