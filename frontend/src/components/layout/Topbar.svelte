<script>
  import { Menu, RotateCcw, Power, Wifi, ShieldAlert, Cpu } from '@lucide/svelte'
  import { connectionState, toggleSimulation } from '../../stores/connection.store.js'
  import { wifiSsid, wifiConnected } from '../../stores/telemetry.store.js'
  import { rebootDevice } from '../../stores/system.store.js'

  let { onToggleMobile = () => {} } = $props()

  async function handleRestart() {
    if (confirm('Reboot MicroRouter ESP32-S3?')) {
      await rebootDevice()
    }
  }
</script>

<header class="h-16 px-4 lg:px-8 flex items-center justify-between glass-panel border-b border-white/10 sticky top-0 z-30">
  <div class="flex items-center gap-3">
    <button
      onclick={onToggleMobile}
      class="lg:hidden p-2 rounded-xl text-slate-300 hover:text-white hover:bg-white/5 transition-colors"
      aria-label="Toggle menu"
    >
      <Menu class="w-5 h-5" />
    </button>

    <!-- Telemetry Status Pill -->
    <div class="flex items-center gap-2.5 px-3 py-1.5 rounded-full bg-white/[0.04] border border-white/10 text-xs font-medium">
      {#if $connectionState.isConnected && !$connectionState.isSimulated}
        <span class="w-2 h-2 rounded-full bg-emerald-400 shadow-[0_0_8px_#34d399] pulse-glow"></span>
        <span class="text-slate-200">ESP32 LIVE</span>
      {:else if $connectionState.isSimulated}
        <span class="w-2 h-2 rounded-full bg-amber-400 shadow-[0_0_8px_#fbbf24]"></span>
        <span class="text-amber-300">SIMULATION MODE</span>
      {:else}
        <span class="w-2 h-2 rounded-full bg-rose-400 shadow-[0_0_8px_#f43f5e]"></span>
        <span class="text-rose-300">RECONNECTING...</span>
      {/if}
    </div>
  </div>

  <div class="flex items-center gap-3">
    <!-- WiFi Status Badge -->
    <div class="hidden sm:flex items-center gap-1.5 px-3 py-1.5 rounded-xl border text-xs font-semibold {$wifiConnected ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' : 'border-amber-500/30 bg-amber-500/10 text-amber-300'}">
      <Wifi class="w-3.5 h-3.5" />
      <span>{$wifiConnected ? $wifiSsid : 'MicroRouter-Setup AP'}</span>
    </div>

    <!-- Toggle Simulation Button (Dev Tool) -->
    {#if import.meta.env.DEV}
    <button
      onclick={toggleSimulation}
      class="px-3 py-1.5 rounded-xl text-xs font-medium border border-white/10 hover:border-white/20 transition-all flex items-center gap-1.5 {$connectionState.isSimulated ? 'bg-amber-500/10 text-amber-300' : 'bg-white/5 text-slate-300'}"
      title="Toggle between hardware connection and offline mock simulation"
    >
      <Cpu class="w-3.5 h-3.5" />
      <span class="hidden md:inline">{$connectionState.isSimulated ? 'Mock Active' : 'Live Mode'}</span>
    </button>
    {/if}

    <!-- Quick Reboot Action -->
    <button
      onclick={handleRestart}
      class="px-3 py-1.5 rounded-xl text-xs font-medium border border-rose-500/20 bg-rose-500/10 text-rose-300 hover:bg-rose-500/20 transition-colors flex items-center gap-1.5"
      title="Reboot ESP32 Gateway"
    >
      <Power class="w-3.5 h-3.5" />
      <span class="hidden sm:inline">Reboot</span>
    </button>
  </div>
</header>
