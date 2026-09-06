// ═══════════════════════════════════════════════════════════════════
// Connection State Store
// ═══════════════════════════════════════════════════════════════════

import { writable } from 'svelte/store'
import { toggleSimulationMode as toggleServiceSimulation } from '../services/websocket.service.js'

export const connectionState = writable({
  isConnected: false,
  isSimulated: true,
})

export function setConnectionStatus(status) {
  connectionState.set(status)
}

export function toggleSimulation() {
  toggleServiceSimulation()
}
