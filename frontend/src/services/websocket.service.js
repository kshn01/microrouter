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

let isSimulated = localStorage.getItem(STORAGE_KEYS.SIMULATION_ENABLED) === 'true'

/**
 * Initialize the connection subsystem
 * @param {Function} onTelemetryData
 * @param {Function} onConnectionChange
 */
export function initTelemetryConnection(onTelemetryData, onConnectionChange) {
  telemetrySubscriber = onTelemetryData
  connectionStateSubscriber = onConnectionChange

  if (isSimulated) {
    activateSimulationMode('Manual Simulation Mode enabled')
  } else {
    connectLiveWebSocket()
  }
}

/**
 * Attempt connection to the real ESP32 WebSocket server
 */
function connectLiveWebSocket() {
  if (stopMockStream) {
    stopMockStream()
    stopMockStream = null
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
    isSimulated = false
    setApiSimulationMode(false)
    localStorage.setItem(STORAGE_KEYS.SIMULATION_ENABLED, 'false')

    if (connectionStateSubscriber) {
      connectionStateSubscriber({ isConnected: true, isSimulated: false })
    }
    showToast('Connected to ESP32 MicroRouter', 'success')
  }

  socket.onmessage = (event) => {
    try {
      const msg = JSON.parse(event.data)
      if (msg.type === 'stats' && msg.data && telemetrySubscriber) {
        telemetrySubscriber(msg.data)
      }
    } catch {
      // ignore invalid telemetry payloads
    }
  }

  socket.onerror = () => {
    // If not already in simulation, activate simulation fallback
    handleLiveConnectionFailure()
  }

  socket.onclose = () => {
    handleLiveConnectionFailure()
  }
}

/**
 * Handle live connection failure by activating the mock simulation
 */
function handleLiveConnectionFailure() {
  if (connectionStateSubscriber) {
    connectionStateSubscriber({ isConnected: false, isSimulated: true })
  }

  if (!stopMockStream) {
    activateSimulationMode('ESP32 not detected. Switched to Simulation Mode for offline testing.')
  }

  // Attempt reconnect to hardware periodically in the background
  clearTimeout(reconnectTimer)
  reconnectTimer = setTimeout(() => {
    if (!isSimulated) {
      connectLiveWebSocket()
    }
  }, TIMING.WS_RECONNECT_INTERVAL_MS * 3)
}

/**
 * Activate the realistic telemetry simulation engine
 * @param {string} [noticeMessage]
 */
export function activateSimulationMode(noticeMessage) {
  isSimulated = true
  setApiSimulationMode(true)
  localStorage.setItem(STORAGE_KEYS.SIMULATION_ENABLED, 'true')

  if (socket) {
    try { socket.close() } catch {}
    socket = null
  }

  if (connectionStateSubscriber) {
    connectionStateSubscriber({ isConnected: true, isSimulated: true })
  }

  if (!stopMockStream) {
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
  if (isSimulated) {
    isSimulated = false
    localStorage.setItem(STORAGE_KEYS.SIMULATION_ENABLED, 'false')
    showToast('Attempting to connect to live ESP32...', 'info')
    connectLiveWebSocket()
  } else {
    activateSimulationMode('Simulation Mode enabled')
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
