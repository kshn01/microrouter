// ═══════════════════════════════════════════════════════════════════
// Resilient WebSocket Service with Offline Simulation Auto-Fallback
// ═══════════════════════════════════════════════════════════════════

import { API_ENDPOINTS, TIMING, STORAGE_KEYS } from '../config/constants.js'
import { startMockTelemetryStream } from './mock.service.js'
import { showToast } from './toast.service.js'
import { setApiSimulationMode } from './api.service.js'

let socket = null
let reconnectTimer = null
let stopMockStream = null
let telemetrySubscriber = null
let connectionStateSubscriber = null

// Manual simulation flag (ONLY available in DEV mode)
let isManualSimulation = Boolean(import.meta.env.DEV && localStorage.getItem(STORAGE_KEYS.SIMULATION_ENABLED) === 'true')
let isCurrentlySimulated = isManualSimulation

if (!import.meta.env.DEV) {
  try {
    localStorage.removeItem(STORAGE_KEYS.SIMULATION_ENABLED)
  } catch {}
}

/**
 * Initialize the connection subsystem
 * @param {Function} onTelemetryData
 * @param {Function} onConnectionChange
 */
export function initTelemetryConnection(onTelemetryData, onConnectionChange) {
  telemetrySubscriber = onTelemetryData
  connectionStateSubscriber = onConnectionChange

  if (import.meta.env.DEV && isManualSimulation) {
    activateSimulationMode('Manual Simulation Mode enabled', true)
  } else {
    connectLiveWebSocket()
  }
}

/**
 * Attempt connection to the real ESP32 WebSocket server
 */
function connectLiveWebSocket() {
  if (import.meta.env.DEV && isManualSimulation) return

  if (socket) {
    try { socket.close() } catch {}
    socket = null
  }

  const isHttps = location.protocol === 'https:'
  const protocol = isHttps ? 'wss:' : 'ws:'
  const host = location.host || 'microrouter.local'
  const wsUrl = `${protocol}//${host}${API_ENDPOINTS.WEBSOCKET}`

  try {
    socket = new WebSocket(wsUrl)
  } catch (err) {
    handleLiveConnectionFailure()
    return
  }

  socket.onopen = () => {
    isCurrentlySimulated = false
    setApiSimulationMode(false)
    localStorage.setItem(STORAGE_KEYS.SIMULATION_ENABLED, 'false')

    if (stopMockStream) {
      stopMockStream()
      stopMockStream = null
    }

    if (connectionStateSubscriber) {
      connectionStateSubscriber({ isConnected: true, isSimulated: false })
    }
    showToast('Connected to ESP32 MicroRouter', 'success')
  }

  socket.onmessage = (event) => {
    try {
      const msg = JSON.parse(event.data)
      if (msg.type === 'stats' && telemetrySubscriber) {
        telemetrySubscriber(msg.data || msg)
      }
    } catch (e) {
      console.error('[WS] Failed to parse message', e)
    }
  }

  socket.onerror = () => {
    handleLiveConnectionFailure()
  }

  socket.onclose = () => {
    handleLiveConnectionFailure()
  }
}

/**
 * Handle live connection failure by activating fallback mock simulation
 * without permanently locking the user into simulation mode.
 */
function handleLiveConnectionFailure() {
  if (isManualSimulation) return

  if (import.meta.env.DEV) {
    isCurrentlySimulated = true
    setApiSimulationMode(true)
  }

  if (connectionStateSubscriber) {
    connectionStateSubscriber({ 
      isConnected: false, 
      isSimulated: import.meta.env.DEV ? true : false 
    })
  }

  if (!stopMockStream && import.meta.env.DEV) {
    stopMockStream = startMockTelemetryStream((data) => {
      if (telemetrySubscriber) {
        telemetrySubscriber(data)
      }
    })
  }

  // Periodically retry connecting to live hardware in the background
  clearTimeout(reconnectTimer)
  reconnectTimer = setTimeout(() => {
    if (!isManualSimulation) {
      connectLiveWebSocket()
    }
  }, TIMING.WS_RECONNECT_INTERVAL_MS)
}

/**
 * Activate the realistic telemetry simulation engine
 * @param {string} [noticeMessage]
 * @param {boolean} [isManual]
 */
export function activateSimulationMode(noticeMessage, isManual = false) {
  if (!import.meta.env.DEV) return

  isCurrentlySimulated = true
  setApiSimulationMode(true)

  if (isManual) {
    isManualSimulation = true
    localStorage.setItem(STORAGE_KEYS.SIMULATION_ENABLED, 'true')
  }

  if (socket) {
    try { socket.close() } catch {}
    socket = null
  }

  if (connectionStateSubscriber) {
    connectionStateSubscriber({ isConnected: true, isSimulated: true })
  }

  if (!stopMockStream && import.meta.env.DEV) {
    stopMockStream = startMockTelemetryStream((data) => {
      if (telemetrySubscriber) {
        telemetrySubscriber(data)
      }
    })
  }

  if (noticeMessage) {
    showToast(noticeMessage, 'info')
  }
}

/**
 * Toggle simulation mode on/off from the UI
 */
export function toggleSimulationMode() {
  clearTimeout(reconnectTimer)

  if (isCurrentlySimulated || isManualSimulation) {
    // User wants Live Mode
    isManualSimulation = false
    isCurrentlySimulated = false
    localStorage.setItem(STORAGE_KEYS.SIMULATION_ENABLED, 'false')

    if (stopMockStream) {
      stopMockStream()
      stopMockStream = null
    }

    if (connectionStateSubscriber) {
      connectionStateSubscriber({ isConnected: false, isSimulated: false })
    }

    showToast('Connecting to live ESP32...', 'info')
    connectLiveWebSocket()
  } else {
    // User wants Simulation Mode
    activateSimulationMode('Simulation Mode enabled', true)
  }
}

/**
 * Send an action command over WebSocket
 * @param {string} action
 */
export function sendWsCommand(action) {
  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify({ action }))
  } else {
    showToast(`Command "${action}" sent (Simulated)`, 'info')
  }
}
