<script>
  import { Wifi, RefreshCw, Lock, Unlock, Check, ShieldAlert, Radio } from '@lucide/svelte'
  import Card from '../components/ui/Card.svelte'
  import Modal from '../components/ui/Modal.svelte'
  import SignalBars from '../components/ui/SignalBars.svelte'
  import {
    wifiSsid,
    wifiIp,
    wifiRssi,
    wifiConnected,
  } from '../stores/telemetry.store.js'
  import {
    scannedNetworks,
    isScanning,
    isConnecting,
    scanNetworks,
    connectNetwork,
    forgetNetwork,
  } from '../stores/wifi.store.js'

  let selectedNetwork = $state(null)
  let password = $state('')
  let modalOpen = $state(false)

  function openConnectModal(net) {
    selectedNetwork = net
    password = ''
    modalOpen = true
  }

  async function handleConnect() {
    if (!selectedNetwork) return
    try {
      await connectNetwork(selectedNetwork.ssid, password)
      modalOpen = false
    } catch {
      // toast shown in store
    }
  }

  async function handleForget() {
    if (confirm('Erase saved WiFi credentials? The device will restart in AP setup mode.')) {
      await forgetNetwork()
    }
  }
</script>

<div class="flex flex-col gap-8 animate-in fade-in duration-300">
  <!-- Page Header -->
  <div class="flex items-center justify-between">
    <div class="flex flex-col gap-1">
      <h1 class="text-2xl lg:text-3xl font-bold tracking-tight text-white">WiFi Configuration</h1>
      <p class="text-sm text-slate-400">Manage wireless uplink, scan frequencies, and configure access points</p>
    </div>

    <button
      onclick={scanNetworks}
      disabled={$isScanning}
      class="px-4 py-2.5 rounded-xl bg-gradient-to-r from-indigo-500 to-purple-500 hover:from-indigo-600 hover:to-purple-600 text-white font-medium text-sm shadow-[0_0_20px_rgba(99,102,241,0.4)] transition-all flex items-center gap-2 disabled:opacity-50"
    >
      <RefreshCw class="w-4 h-4 {$isScanning ? 'animate-spin' : ''}" />
      <span>{$isScanning ? 'Scanning Airwaves...' : 'Scan Networks'}</span>
    </button>
  </div>

  <!-- Current Connection Status Card -->
  <Card>
    <div class="flex flex-col sm:flex-row sm:items-center justify-between gap-4">
      <div class="flex items-center gap-4">
        <div class="w-12 h-12 rounded-2xl flex items-center justify-center {$wifiConnected ? 'bg-emerald-500/10 text-emerald-400 border border-emerald-500/20' : 'bg-amber-500/10 text-amber-400 border border-amber-500/20'}">
          <Wifi class="w-6 h-6" />
        </div>
        <div>
          <div class="flex items-center gap-2">
            <h2 class="text-lg font-bold text-white tracking-tight">
              {$wifiConnected ? $wifiSsid : 'MicroRouter Fallback AP'}
            </h2>
            <span class="text-[11px] font-semibold px-2 py-0.5 rounded-full {$wifiConnected ? 'bg-emerald-500/10 text-emerald-300 border border-emerald-500/20' : 'bg-amber-500/10 text-amber-300 border border-amber-500/20'}">
              {$wifiConnected ? 'Connected' : 'Access Point Mode'}
            </span>
          </div>
          <p class="text-xs text-slate-400 mt-1">
            IP: <span class="font-mono text-slate-300">{$wifiIp}</span> · Signal: <span class="font-mono text-slate-300">{$wifiRssi} dBm</span>
          </p>
        </div>
      </div>

      {#if $wifiConnected}
        <button
          onclick={handleForget}
          class="px-3.5 py-2 rounded-xl text-xs font-medium text-rose-300 border border-rose-500/20 bg-rose-500/10 hover:bg-rose-500/20 transition-colors self-start sm:self-auto"
        >
          Disconnect & Forget
        </button>
      {/if}
    </div>
  </Card>

  <!-- Discovered Networks Section -->
  <div class="flex flex-col gap-4">
    <div class="flex items-center justify-between">
      <h3 class="text-base font-bold text-white tracking-tight flex items-center gap-2">
        <Radio class="w-4 h-4 text-indigo-400" />
        Available 2.4 GHz Networks
      </h3>
      <span class="text-xs text-slate-400">{$scannedNetworks.length} discovered</span>
    </div>

    {#if $scannedNetworks.length === 0}
      <Card class="text-center py-12 flex flex-col items-center justify-center gap-3">
        <Wifi class="w-10 h-10 text-slate-600 animate-pulse" />
        <p class="text-sm font-medium text-slate-300">No scanned networks yet</p>
        <p class="text-xs text-slate-500 max-w-sm">
          Click "Scan Networks" above to discover nearby 2.4 GHz wireless access points.
        </p>
        <button
          onclick={scanNetworks}
          class="mt-2 px-4 py-2 rounded-xl bg-white/5 hover:bg-white/10 text-xs font-medium text-slate-200 border border-white/10 transition-colors"
        >
          Start Scan
        </button>
      </Card>
    {:else}
      <div class="grid grid-cols-1 gap-2.5">
        {#each $scannedNetworks as net}
          {@const isCurrent = net.ssid === $wifiSsid}
          <div class="glass-card glass-panel-hover rounded-xl p-4 border border-white/10 flex items-center justify-between transition-all duration-200">
            <div class="flex items-center gap-4 min-w-0">
              <SignalBars rssi={net.rssi} />

              <div class="flex flex-col min-w-0">
                <div class="flex items-center gap-2">
                  <span class="font-semibold text-sm text-white truncate">{net.ssid}</span>
                  {#if isCurrent}
                    <span class="text-[10px] font-bold px-2 py-0.5 rounded-full bg-emerald-500/20 text-emerald-300 border border-emerald-500/30">
                      Active
                    </span>
                  {/if}
                </div>
                <div class="flex items-center gap-3 text-xs text-slate-400 mt-0.5">
                  <span class="font-mono">{net.rssi} dBm</span>
                  <span>·</span>
                  <span>CH {net.channel ?? '—'}</span>
                  <span>·</span>
                  <span class="flex items-center gap-1">
                    {#if net.secure}
                      <Lock class="w-3 h-3 text-amber-400" />
                      WPA2/WPA3
                    {:else}
                      <Unlock class="w-3 h-3 text-slate-400" />
                      Open
                    {/if}
                  </span>
                </div>
              </div>
            </div>

            <button
              onclick={() => openConnectModal(net)}
              disabled={isCurrent}
              class="px-3.5 py-1.5 rounded-lg text-xs font-semibold transition-all {isCurrent ? 'bg-white/5 text-slate-500 cursor-default' : 'bg-indigo-600/20 text-indigo-300 hover:bg-indigo-600/30 border border-indigo-500/30'}"
            >
              {isCurrent ? 'Connected' : 'Connect'}
            </button>
          </div>
        {/each}
      </div>
    {/if}
  </div>

  <!-- Connect Dialog Modal -->
  <Modal
    isOpen={modalOpen}
    title="Connect to {selectedNetwork?.ssid}"
    onClose={() => (modalOpen = false)}
  >
    <div class="flex flex-col gap-4">
      {#if selectedNetwork?.secure}
        <div class="flex flex-col gap-1.5">
          <label for="wifi-password" class="text-xs font-medium text-slate-300">Network Password</label>
          <input
            id="wifi-password"
            type="password"
            bind:value={password}
            placeholder="Enter security key"
            class="w-full px-3.5 py-2.5 rounded-xl bg-slate-950/60 border border-white/10 text-white text-sm focus:outline-none focus:border-indigo-500 transition-colors"
          />
        </div>
      {:else}
        <p class="text-xs text-slate-400">
          This network is unsecured. No password is required to connect.
        </p>
      {/if}

      <div class="flex items-center justify-end gap-2.5 mt-2">
        <button
          onclick={() => (modalOpen = false)}
          class="px-4 py-2 rounded-xl text-xs font-medium text-slate-400 hover:text-white hover:bg-white/5 transition-colors"
        >
          Cancel
        </button>
        <button
          onclick={handleConnect}
          disabled={$isConnecting}
          class="px-4 py-2 rounded-xl bg-indigo-600 hover:bg-indigo-500 text-white text-xs font-semibold shadow-[0_0_15px_rgba(99,102,241,0.5)] transition-all disabled:opacity-50"
        >
          {$isConnecting ? 'Connecting...' : 'Connect Now'}
        </button>
      </div>
    </div>
  </Modal>
</div>
