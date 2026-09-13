<script>
  import { onDestroy } from 'svelte'
  import {
    AlertTriangle,
    Power,
    RotateCcw,
    RefreshCw,
    CheckCircle2,
    Cpu,
    Radio,
    ShieldAlert,
    X
  } from '@lucide/svelte'
  import { apiRebootRouter, apiFetchSystemInfo } from '../../services/api.service.js'
  import { loadSystemInfo } from '../../stores/system.store.js'
  import { showToast } from '../../services/toast.service.js'

  let {
    isOpen = false,
    onClose = () => {},
  } = $props()

  let rebootTarget = $state('gateway') // 'gateway' | 'full'
  let isRebooting = $state(false)
  let countdown = $state(25)
  let reconnectStatus = $state('idle') // 'idle' | 'rebooting' | 'reconnected' | 'timeout'
  let timerInterval = null
  let pollInterval = null

  function resetState() {
    isRebooting = false
    countdown = 25
    reconnectStatus = 'idle'
    if (timerInterval) {
      clearInterval(timerInterval)
      timerInterval = null
    }
    if (pollInterval) {
      clearInterval(pollInterval)
      pollInterval = null
    }
  }

  function handleClose() {
    if (isRebooting && reconnectStatus === 'rebooting') {
      // Don't close silently during active reboot unless user explicitly dismisses after timeout
      return
    }
    resetState()
    onClose()
  }

  async function handleConfirmReboot() {
    isRebooting = true
    reconnectStatus = 'rebooting'
    countdown = rebootTarget === 'full' ? 35 : 20

    try {
      // isSystemOnly = true for 'gateway' (ESP32 only), false for 'full' (ESP32 + ZTE)
      const isSystemOnly = rebootTarget === 'gateway'
      await apiRebootRouter(isSystemOnly)
      showToast(
        rebootTarget === 'full' ? 'Full system reboot initiated' : 'Gateway restart initiated',
        'warning'
      )
    } catch (err) {
      // Network drop is expected when reboot initiates
      showToast('Reboot command dispatched', 'info')
    }

    // Start countdown timer
    timerInterval = setInterval(() => {
      if (countdown > 0) {
        countdown -= 1
      } else {
        clearInterval(timerInterval)
        timerInterval = null
        if (reconnectStatus === 'rebooting') {
          reconnectStatus = 'timeout'
        }
      }
    }, 1000)

    // Wait 6 seconds before starting health-check polling
    setTimeout(() => {
      startPolling()
    }, 6000)
  }

  function startPolling() {
    if (pollInterval) clearInterval(pollInterval)
    pollInterval = setInterval(async () => {
      if (reconnectStatus === 'reconnected') return

      try {
        const info = await apiFetchSystemInfo()
        if (info && (info.chipModel || info.fwVersion || info.uptimeMs !== undefined)) {
          // Success! Gateway is back online
          reconnectStatus = 'reconnected'
          if (pollInterval) clearInterval(pollInterval)
          if (timerInterval) clearInterval(timerInterval)

          showToast('MicroRouter is back online!', 'success')
          await loadSystemInfo()

          setTimeout(() => {
            handleClose()
          }, 1500)
        }
      } catch {
        // Still rebooting / offline
      }
    }, 2000)
  }

  async function handleManualRetry() {
    reconnectStatus = 'rebooting'
    countdown = 15
    startPolling()
  }

  onDestroy(() => {
    if (timerInterval) clearInterval(timerInterval)
    if (pollInterval) clearInterval(pollInterval)
  })
</script>

{#if isOpen}
  <div
    class="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/75 backdrop-blur-md animate-in fade-in duration-200"
    role="dialog"
    tabindex="-1"
  >
    <div class="glass-panel w-full max-w-lg rounded-2xl p-6 shadow-2xl border border-white/10 transform transition-all animate-in zoom-in-95 duration-200">
      {#if !isRebooting}
        <!-- Step 1: Double Confirmation Dialog -->
        <div class="flex items-start justify-between pb-4 border-b border-white/10">
          <div class="flex items-center gap-3 text-rose-400">
            <div class="w-10 h-10 rounded-xl bg-rose-500/10 border border-rose-500/20 flex items-center justify-center">
              <ShieldAlert class="w-5 h-5" />
            </div>
            <div>
              <h3 class="text-base font-bold text-white tracking-tight">Confirm Router Reboot</h3>
              <p class="text-xs text-slate-400">Standard safety verification</p>
            </div>
          </div>
          <button
            onclick={handleClose}
            class="p-1 rounded-lg text-slate-400 hover:text-white hover:bg-white/5 transition-colors"
            aria-label="Close"
          >
            <X class="w-5 h-5" />
          </button>
        </div>

        <div class="py-4 space-y-4">
          <!-- Warning Notice -->
          <div class="p-3.5 rounded-xl bg-rose-500/10 border border-rose-500/20 text-xs text-rose-200/90 leading-relaxed flex items-start gap-2.5">
            <AlertTriangle class="w-4 h-4 text-rose-400 shrink-0 mt-0.5" />
            <span>
              Rebooting will temporarily disconnect all connected Wi-Fi devices, terminate active internet sessions, and restart network routing services.
            </span>
          </div>

          <!-- Reboot Scope Selection -->
          <div class="space-y-2">
            <div class="block text-xs font-semibold uppercase tracking-wider text-slate-400">
              Select Reboot Scope
            </div>

            <!-- Option 1: Gateway Only -->
            <label class="flex items-start gap-3 p-3 rounded-xl border transition-all cursor-pointer {rebootTarget === 'gateway' ? 'bg-indigo-600/10 border-indigo-500/40 text-white' : 'bg-white/[0.02] border-white/5 text-slate-400 hover:border-white/10'}">
              <input
                type="radio"
                name="reboot-scope"
                value="gateway"
                bind:group={rebootTarget}
                class="mt-1 accent-indigo-500"
              />
              <div class="flex-1 text-xs">
                <div class="font-semibold text-white flex items-center gap-1.5">
                  <Cpu class="w-3.5 h-3.5 text-indigo-400" />
                  MicroRouter Gateway (ESP32-S3 Only)
                  <span class="px-1.5 py-0.2 rounded bg-indigo-500/20 text-indigo-300 text-[10px] font-medium">Recommended</span>
                </div>
                <p class="text-slate-400 mt-0.5">
                  Fast restart (~15s). Restarts Wi-Fi management, DNS filter daemon, and captive portal without resetting upstream LTE modem.
                </p>
              </div>
            </label>

            <!-- Option 2: Full System Reboot -->
            <label class="flex items-start gap-3 p-3 rounded-xl border transition-all cursor-pointer {rebootTarget === 'full' ? 'bg-rose-600/10 border-rose-500/40 text-white' : 'bg-white/[0.02] border-white/5 text-slate-400 hover:border-white/10'}">
              <input
                type="radio"
                name="reboot-scope"
                value="full"
                bind:group={rebootTarget}
                class="mt-1 accent-rose-500"
              />
              <div class="flex-1 text-xs">
                <div class="font-semibold text-white flex items-center gap-1.5">
                  <Radio class="w-3.5 h-3.5 text-rose-400" />
                  Full System (Gateway + ZTE Cellular Modem)
                </div>
                <p class="text-slate-400 mt-0.5">
                  Complete cold restart (~35s). Sends reboot command to ZTE router and resets ESP32 gateway simultaneously.
                </p>
              </div>
            </label>
          </div>
        </div>

        <!-- Dialog Footer Actions -->
        <div class="flex items-center justify-end gap-3 pt-4 border-t border-white/10">
          <button
            onclick={handleClose}
            class="px-4 py-2 rounded-xl text-xs font-semibold bg-white/5 hover:bg-white/10 text-slate-300 border border-white/10 transition-colors"
          >
            Cancel
          </button>
          <button
            onclick={handleConfirmReboot}
            class="px-4 py-2 rounded-xl text-xs font-semibold bg-rose-600 hover:bg-rose-500 text-white shadow-lg shadow-rose-600/30 transition-all flex items-center gap-2"
          >
            <Power class="w-3.5 h-3.5" />
            <span>Confirm & Reboot</span>
          </button>
        </div>

      {:else}
        <!-- Step 2: Live In-Progress Reboot State -->
        <div class="py-6 flex flex-col items-center text-center space-y-4">
          {#if reconnectStatus === 'reconnected'}
            <div class="w-16 h-16 rounded-2xl bg-emerald-500/10 border border-emerald-500/30 flex items-center justify-center text-emerald-400 animate-in zoom-in-75">
              <CheckCircle2 class="w-8 h-8" />
            </div>
            <div>
              <h3 class="text-lg font-bold text-white tracking-tight">Reboot Complete!</h3>
              <p class="text-xs text-emerald-400 mt-1">MicroRouter gateway is back online and responding.</p>
            </div>

          {:else if reconnectStatus === 'timeout'}
            <div class="w-16 h-16 rounded-2xl bg-amber-500/10 border border-amber-500/30 flex items-center justify-center text-amber-400">
              <AlertTriangle class="w-8 h-8" />
            </div>
            <div>
              <h3 class="text-lg font-bold text-white tracking-tight">Reconnecting Taking Longer</h3>
              <p class="text-xs text-slate-400 mt-1 max-w-xs mx-auto">
                The router is still initializing. Ensure your device is connected to the MicroRouter Wi-Fi network.
              </p>
            </div>
            <div class="flex items-center gap-3 pt-2">
              <button
                onclick={handleManualRetry}
                class="px-4 py-2 rounded-xl text-xs font-semibold bg-indigo-600 hover:bg-indigo-500 text-white flex items-center gap-1.5"
              >
                <RefreshCw class="w-3.5 h-3.5" />
                Retry Connection
              </button>
              <button
                onclick={() => { resetState(); onClose(); }}
                class="px-4 py-2 rounded-xl text-xs font-semibold bg-white/5 hover:bg-white/10 text-slate-300 border border-white/10"
              >
                Dismiss
              </button>
            </div>

          {:else}
            <!-- In-Flight Reboot Progress -->
            <div class="relative w-16 h-16">
              <div class="absolute inset-0 rounded-2xl bg-indigo-500/20 border border-indigo-500/30 animate-pulse"></div>
              <div class="absolute inset-0 flex items-center justify-center text-indigo-400">
                <RotateCcw class="w-8 h-8 animate-spin" style="animation-duration: 2s;" />
              </div>
            </div>

            <div>
              <h3 class="text-lg font-bold text-white tracking-tight">Rebooting Router...</h3>
              <p class="text-xs text-slate-400 mt-1">
                Estimated restart time: <span class="font-mono font-bold text-indigo-400">~{countdown}s</span>
              </p>
            </div>

            <!-- Progress Bar -->
            <div class="w-full max-w-xs h-1.5 rounded-full bg-slate-800 overflow-hidden mt-2">
              <div
                class="h-full bg-gradient-to-r from-indigo-500 to-cyan-400 transition-all duration-1000"
                style="width: {Math.max(10, Math.min(100, Math.round(((rebootTarget === 'full' ? 35 : 20 - countdown) / (rebootTarget === 'full' ? 35 : 20)) * 100)))}%"
              ></div>
            </div>

            <p class="text-[11px] text-slate-500">
              Actively probing for gateway recovery on http://{typeof window !== 'undefined' ? (window.location.host || '192.168.4.1') : '192.168.4.1'}...
            </p>
          {/if}
        </div>
      {/if}
    </div>
  </div>
{/if}
