import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  plugins: [vue()],
  server: {
    proxy: {
      '/api': 'http://127.0.0.1:8008',
      '/backend': 'http://127.0.0.1:8008',
      '/health': 'http://127.0.0.1:8008',
      '/static': 'http://127.0.0.1:8008'
    }
  }
})
