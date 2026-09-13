import tailwindcss from '@tailwindcss/vite'
import { svelte } from '@sveltejs/vite-plugin-svelte'
import { defineConfig } from 'vite'
import path from 'path'
import { execSync } from 'child_process'

function getGitVersion() {
  try {
    try {
      execSync('git fetch --tags -q', { timeout: 2000, stdio: 'ignore' })
    } catch {}
    const tag = execSync('git describe --tags --always').toString().trim()
    return tag.replace(/^v/, '')
  } catch {
    return '1.0.5'
  }
}

const APP_VERSION = getGitVersion()

// https://vite.dev/config/
export default defineConfig({
  define: {
    __APP_VERSION__: JSON.stringify(APP_VERSION),
  },
  plugins: [tailwindcss(), svelte()],

  // Build output goes to ../data/ for ESP32 LittleFS upload
  build: {
    outDir: path.resolve(import.meta.dirname, '../data'),
    emptyOutDir: true,
    // Optimize for ESP32's limited flash
    minify: 'terser',
    terserOptions: {
      compress: {
        drop_console: true,   // Remove console.log in production
        drop_debugger: true,
      },
    },
    rollupOptions: {
      output: {
        // Keep filenames short for LittleFS
        entryFileNames: 'js/[name].js',
        chunkFileNames: 'js/[name].js',
        assetFileNames: (assetInfo) => {
          if (assetInfo.name && assetInfo.name.endsWith('.css')) {
            return 'css/[name][extname]'
          }
          return 'assets/[name][extname]'
        },
      },
    },
  },

  // Dev server proxy to ESP32 for API/WebSocket during development
  server: {
    port: 5173,
    proxy: {
      '/api': {
        target: 'http://microrouter.local',
        changeOrigin: true,
      },
      '/ws': {
        target: 'ws://microrouter.local',
        ws: true,
      },
      '/update': {
        target: 'http://microrouter.local',
        changeOrigin: true,
      },
    },
  },

  // Preview server proxy to ESP32 for testing production builds locally
  preview: {
    port: 4173,
    proxy: {
      '/api': {
        target: 'http://microrouter.local',
        changeOrigin: true,
      },
      '/ws': {
        target: 'ws://microrouter.local',
        ws: true,
      },
      '/update': {
        target: 'http://microrouter.local',
        changeOrigin: true,
      },
    },
  },
})
