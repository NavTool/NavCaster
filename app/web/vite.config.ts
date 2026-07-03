import { defineConfig, loadEnv } from 'vite'
import react from '@vitejs/plugin-react'

// https://vite.dev/config/
export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, '.', '')
  const adminProxyTarget =
    env.VITE_NAVCASTER_ADMIN_PROXY_TARGET ||
    env.NAVCASTER_ADMIN_PROXY_TARGET ||
    'http://127.0.0.1:8080'

  return {
    plugins: [react()],
    server: {
      proxy: {
        '/api': {
          target: adminProxyTarget,
          changeOrigin: true,
        },
      },
    },
  }
})
