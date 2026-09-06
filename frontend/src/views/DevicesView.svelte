<script>
  import { onMount, onDestroy } from 'svelte'
  import {
    Laptop,
    Smartphone,
    Tv,
    Gamepad,
    Cpu,
    Wifi,
    Shield,
    Clock,
    Search,
    RefreshCw,
    Ban,
    CheckCircle2,
    Sliders,
    HardDrive,
    Server,
    Activity,
    LayoutGrid,
    Table as TableIcon,
    ArrowDown,
    ArrowUp,
    UserCheck,
    Radio
  } from '@lucide/svelte'
  import Card from '../components/ui/Card.svelte'
  import StatCard from '../components/ui/StatCard.svelte'
  import Modal from '../components/ui/Modal.svelte'
  import SignalBars from '../components/ui/SignalBars.svelte'
  import {
    apiGetDevices,
    apiBlockDevice,
    apiSetDeviceWaiver
  } from '../services/api.service.js'
  import { lookupOUI } from '../utils/oui.js'
  import { formatBytes } from '../types/models.js'
  import { showToast } from '../services/toast.service.js'

  let loading = $state(true)
  let searchQuery = $state('')
  let devices = $state([])
  let pollInterval = null

  // View options
  let viewMode = $state('grid') // 'grid' | 'table'
  let categoryFilter = $state('all') // 'all' | 'home' | 'guest'
  let sortMode = $state('usage') // 'usage' | 'name' | 'status'

  // Waiver modal states
  let isWaiverModalOpen = $state(false)
  let selectedDevice = $state(null)
  let waiverMinutes = $state(30)
  let settingWaiver = $state(false)

  const WAIVER_PRESETS = [15, 30, 60, 120]

  async function loadDevices() {
    try {
      const res = await apiGetDevices()
      // Resilient parsing across all backend versions and schemas
      const rawList = res?.devices || res?.connected || res?.all || res?.usage || (Array.isArray(res) ? res : [])

      if (rawList && Array.isArray(rawList)) {
        devices = rawList.map((d) => {
          const oui = lookupOUI(d.mac)
          return {
            mac: d.mac || '00:00:00:00:00:00',
            ip: d.ip || '—',
            hostname: d.hostname || '',
            netbios: d.netbios || '',
            band: d.band || '2.4G',
            rssi: d.rssi !== undefined ? d.rssi : -65,
            online: d.online !== undefined ? d.online : true,
            isBlocked: d.isBlocked !== undefined ? d.isBlocked : (d.blocked || false),
            waiverSecRemaining: d.waiverSecRemaining || (d.waiver ? 1800 : 0),
            rxBytes: d.rxBytes || d.dlBytes || d.download_bytes || 0,
            txBytes: d.txBytes || d.ulBytes || d.upload_bytes || 0,
            hourlyUsage: d.hourlyUsage || d.hourlyUsageBytes || 0,
            hitCount: d.hitCount || d.hourlyLimitHitCount || 0,
            vendor: d.vendor || oui.vendor,
            deviceType: d.deviceType || oui.type,
            icon: oui.icon,
            isRandomized: oui.isRandomized,
          }
        })
      }
    } catch (err) {
      console.error('Failed to load devices', err)
    } finally {
      loading = false
    }
  }

  // Category counts
  let countAll = $derived(devices.length)
  let countHome = $derived(devices.filter((d) => d.band !== 'Guest').length)
  let countGuest = $derived(devices.filter((d) => d.band === 'Guest').length)
  let blockedCount = $derived(devices.filter((d) => d.isBlocked).length)
  let activeWaiverCount = $derived(
    devices.filter((d) => (d.waiverSecRemaining || 0) > 0).length
  )

  // Total Traffic
  let totalRx = $derived(devices.reduce((acc, d) => acc + (d.rxBytes || 0), 0))
  let totalTx = $derived(devices.reduce((acc, d) => acc + (d.txBytes || 0), 0))
  let totalTraffic = $derived(totalRx + totalTx)

  // Filtered and Sorted list
  let displayDevices = $derived(
    devices
      .filter((d) => {
        // Category check
        if (categoryFilter === 'guest' && d.band !== 'Guest') return false
        if (categoryFilter === 'home' && d.band === 'Guest') return false

        // Search check
        if (!searchQuery) return true
        const s = searchQuery.toLowerCase()
        return (
          d.mac.toLowerCase().includes(s) ||
          (d.ip && d.ip.toLowerCase().includes(s)) ||
          (d.hostname && d.hostname.toLowerCase().includes(s)) ||
          (d.netbios && d.netbios.toLowerCase().includes(s)) ||
          (d.vendor && d.vendor.toLowerCase().includes(s))
        )
      })
      .sort((a, b) => {
        if (sortMode === 'usage') {
          const uA = (a.rxBytes || 0) + (a.txBytes || 0)
          const uB = (b.rxBytes || 0) + (b.txBytes || 0)
          return uB - uA
        }
        if (sortMode === 'name') {
          const nA = (a.hostname || a.ip || a.mac).toLowerCase()
          const nB = (b.hostname || b.ip || b.mac).toLowerCase()
          return nA.localeCompare(nB)
        }
        if (sortMode === 'status') {
          return (b.online ? 1 : 0) - (a.online ? 1 : 0)
        }
        return 0
      })
  )

  async function toggleBlock(device) {
    const nextState = !device.isBlocked
    try {
      await apiBlockDevice(device.mac, nextState)
      device.isBlocked = nextState
      if (nextState) {
        device.waiverSecRemaining = 0
      }
      showToast(
        nextState
          ? `Blocked device ${device.hostname || device.mac}`
          : `Allowed device ${device.hostname || device.mac}`,
        nextState ? 'warning' : 'success'
      )
    } catch (err) {
      showToast('Action failed: ' + err.message, 'error')
    }
  }

  async function quickWaiver(device, mins) {
    try {
      await apiSetDeviceWaiver(device.mac, mins)
      device.waiverSecRemaining = mins * 60
      device.isBlocked = false
      showToast(`+${mins}m waiver granted for ${device.hostname || device.mac}`, 'success')
    } catch (err) {
      showToast('Failed to grant waiver: ' + err.message, 'error')
    }
  }

  function openWaiverModal(device) {
    selectedDevice = device
    waiverMinutes = 30
    isWaiverModalOpen = true
  }

  async function handleGrantWaiver() {
    if (!selectedDevice) return
    settingWaiver = true
    try {
      await apiSetDeviceWaiver(selectedDevice.mac, waiverMinutes)
      selectedDevice.waiverSecRemaining = waiverMinutes * 60
      selectedDevice.isBlocked = false
      isWaiverModalOpen = false
      showToast(
        `Temporary ${waiverMinutes}m waiver granted for ${selectedDevice.hostname || selectedDevice.mac}`,
        'success'
      )
    } catch (err) {
      showToast('Failed to set waiver: ' + err.message, 'error')
    } finally {
      settingWaiver = false
    }
  }

  function formatWaiverTime(sec) {
    if (!sec || sec <= 0) return null
    const m = Math.floor(sec / 60)
    const s = sec % 60
    return `${m}m ${s < 10 ? '0' : ''}${s}s`
  }

  function getDeviceIcon(type) {
    switch (type) {
      case 'smartphone':
      case 'tablet':
        return Smartphone
      case 'laptop':
        return Laptop
      case 'tv':
        return Tv
      case 'gamepad':
        return Gamepad
      case 'cpu':
        return Cpu
      case 'server':
        return Server
      default:
        return Laptop
    }
  }

  onMount(() => {
    loadDevices()
    pollInterval = setInterval(loadDevices, 3000)
  })

  onDestroy(() => {
    if (pollInterval) clearInterval(pollInterval)
  })
</script>

<div class="flex flex-col gap-8 animate-in fade-in duration-300">
  <!-- Header Title -->
  <div class="flex flex-col sm:flex-row sm:items-center justify-between gap-4">
    <div>
      <h1 class="text-2xl lg:text-3xl font-bold tracking-tight text-white flex items-center gap-3">
        <Laptop class="w-8 h-8 text-indigo-400" />
        Connected Client Inventory
      </h1>
      <p class="text-sm text-slate-400 mt-1">
        75+ OUI hardware vendor database, NetBIOS identity probes, and dynamic access control
      </p>
    </div>
    <div class="flex items-center gap-3">
      <!-- View Mode Buttons -->
      <div class="flex items-center bg-slate-900 border border-white/10 rounded-xl p-1">
        <button
          onclick={() => (viewMode = 'grid')}
          class="p-1.5 rounded-lg text-xs font-semibold transition-colors {viewMode === 'grid' ? 'bg-indigo-600 text-white shadow' : 'text-slate-400 hover:text-white'}"
          title="Card Grid View"
        >
          <LayoutGrid class="w-4 h-4" />
        </button>
        <button
          onclick={() => (viewMode = 'table')}
          class="p-1.5 rounded-lg text-xs font-semibold transition-colors {viewMode === 'table' ? 'bg-indigo-600 text-white shadow' : 'text-slate-400 hover:text-white'}"
          title="Table View"
        >
          <TableIcon class="w-4 h-4" />
        </button>
      </div>

      <button
        onclick={loadDevices}
        class="inline-flex items-center gap-2 px-3.5 py-2 rounded-xl text-xs font-semibold bg-white/5 hover:bg-white/10 text-slate-300 border border-white/10 transition-colors"
      >
        <RefreshCw class="w-3.5 h-3.5" />
        Refresh
      </button>
    </div>
  </div>

  <!-- Metric Counters -->
  <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
    <StatCard
      title="Total Tracked Stations"
      value={String(countAll)}
      subtitle="{countHome} Home · {countGuest} Guest"
      color="indigo"
      icon={Laptop}
    />
    <StatCard
      title="Total Data Traffic"
      value={formatBytes(totalTraffic)}
      subtitle="↓ {formatBytes(totalRx)} · ↑ {formatBytes(totalTx)}"
      color="emerald"
      icon={Activity}
    />
    <StatCard
      title="Access Blocked"
      value={String(blockedCount)}
      subtitle="Restricted by curfew or policy"
      color="rose"
      icon={Ban}
    />
    <StatCard
      title="Active Waivers"
      value={String(activeWaiverCount)}
      subtitle="Temporary overrides active"
      color="amber"
      icon={Clock}
    />
  </div>

  <!-- Filter & Controls Toolbar -->
  <Card>
    <div class="flex flex-col md:flex-row md:items-center justify-between gap-4">
      <!-- Category Filter Pills -->
      <div class="flex items-center gap-2">
        <button
          onclick={() => (categoryFilter = 'all')}
          class="px-3 py-1.5 rounded-xl text-xs font-semibold border transition-all {categoryFilter === 'all' ? 'bg-indigo-600 text-white border-indigo-500 shadow' : 'bg-white/5 text-slate-400 border-white/5 hover:text-white'}"
        >
          All Stations ({countAll})
        </button>
        <button
          onclick={() => (categoryFilter = 'home')}
          class="px-3 py-1.5 rounded-xl text-xs font-semibold border transition-all {categoryFilter === 'home' ? 'bg-indigo-600 text-white border-indigo-500 shadow' : 'bg-white/5 text-slate-400 border-white/5 hover:text-white'}"
        >
          Home Wi-Fi ({countHome})
        </button>
        <button
          onclick={() => (categoryFilter = 'guest')}
          class="px-3 py-1.5 rounded-xl text-xs font-semibold border transition-all {categoryFilter === 'guest' ? 'bg-amber-600 text-white border-amber-500 shadow' : 'bg-white/5 text-slate-400 border-white/5 hover:text-white'}"
        >
          Guest Wi-Fi ({countGuest})
        </button>
      </div>

      <!-- Search & Sort Controls -->
      <div class="flex items-center gap-3">
        <!-- Sort Dropdown -->
        <div class="flex items-center gap-1.5 text-xs text-slate-400">
          <span>Sort:</span>
          <select
            bind:value={sortMode}
            class="bg-slate-900 border border-white/10 rounded-lg px-2.5 py-1.5 text-xs text-white focus:outline-none focus:border-indigo-500"
          >
            <option value="usage">Data Traffic</option>
            <option value="name">Station Name</option>
            <option value="status">Online Status</option>
          </select>
        </div>

        <!-- Search Bar -->
        <div class="relative">
          <Search class="w-3.5 h-3.5 absolute left-3 top-1/2 -translate-y-1/2 text-slate-500" />
          <input
            type="text"
            bind:value={searchQuery}
            placeholder="Search vendor, IP, MAC, name..."
            class="bg-slate-900 border border-white/10 rounded-xl pl-9 pr-3 py-1.5 text-xs text-white placeholder-slate-500 focus:outline-none focus:border-indigo-500 w-full sm:w-64"
          />
        </div>
      </div>
    </div>
  </Card>

  <!-- Devices Content Display -->
  {#if displayDevices.length === 0}
    <Card class="py-16 text-center text-slate-500 text-sm">
      {searchQuery ? 'No devices found matching your search filter.' : 'Waiting for network client activity...'}
    </Card>
  {:else if viewMode === 'grid'}
    <!-- CARD GRID VIEW -->
    <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-5">
      {#each displayDevices as dev}
        {@const IconComp = getDeviceIcon(dev.icon)}
        {@const hasWaiver = (dev.waiverSecRemaining || 0) > 0}
        <Card hover={true} class="flex flex-col justify-between gap-4 border {dev.isBlocked ? 'border-rose-500/30 bg-rose-500/[0.02]' : 'border-white/10'}">
          <!-- Top Row: Icon + Name + Status -->
          <div class="flex items-start justify-between gap-3">
            <div class="flex items-center gap-3">
              <div class="w-10 h-10 rounded-xl bg-white/5 border border-white/10 flex items-center justify-center text-indigo-400 shrink-0 shadow">
                <IconComp class="w-5 h-5" />
              </div>
              <div>
                <div class="flex items-center gap-1.5">
                  <span class="font-bold text-white text-sm">
                    {dev.hostname || 'Station'}
                  </span>
                  {#if dev.netbios}
                    <span class="px-1.5 py-0.2 rounded bg-indigo-500/20 text-indigo-300 border border-indigo-500/30 text-[9px] font-mono">
                      {dev.netbios}
                    </span>
                  {/if}
                </div>
                <div class="flex items-center gap-1.5 mt-0.5">
                  <span class="text-xs text-slate-400 font-medium">{dev.vendor}</span>
                  {#if dev.isRandomized}
                    <span class="text-[9px] px-1 py-0.2 rounded bg-amber-500/10 text-amber-400 border border-amber-500/20">
                      Private MAC
                    </span>
                  {/if}
                </div>
              </div>
            </div>

            <!-- Status Badge -->
            {#if dev.isBlocked}
              <span class="inline-flex items-center gap-1 text-[10px] font-semibold text-rose-400 bg-rose-500/10 px-2 py-0.5 rounded-full border border-rose-500/20">
                <Ban class="w-3 h-3" />
                Blocked
              </span>
            {:else if hasWaiver}
              <span class="inline-flex items-center gap-1 text-[10px] font-semibold text-amber-400 bg-amber-500/10 px-2 py-0.5 rounded-full border border-amber-500/20">
                <Clock class="w-3 h-3" />
                {formatWaiverTime(dev.waiverSecRemaining)}
              </span>
            {:else}
              <span class="inline-flex items-center gap-1 text-[10px] font-semibold text-emerald-400 bg-emerald-500/10 px-2 py-0.5 rounded-full border border-emerald-500/20">
                <CheckCircle2 class="w-3 h-3" />
                Online
              </span>
            {/if}
          </div>

          <!-- Middle Row: IP, MAC, Band, Signal -->
          <div class="p-3 rounded-xl bg-white/[0.02] border border-white/5 grid grid-cols-2 gap-2 text-xs font-mono">
            <div>
              <span class="text-slate-500 text-[10px] block font-sans">IPv4 Address</span>
              <span class="text-slate-200">{dev.ip}</span>
            </div>
            <div>
              <span class="text-slate-500 text-[10px] block font-sans">MAC Hardware</span>
              <span class="text-slate-400 text-[11px] uppercase">{dev.mac}</span>
            </div>
            <div>
              <span class="text-slate-500 text-[10px] block font-sans">Band / Link</span>
              <span class="text-indigo-400 font-sans">{dev.band}</span>
            </div>
            <div>
              <span class="text-slate-500 text-[10px] block font-sans">Data Traffic</span>
              <span class="text-emerald-400">{formatBytes(dev.rxBytes + dev.txBytes)}</span>
            </div>
          </div>

          <!-- Bottom Row: 1-Click Waiver Shortcuts & Action Button -->
          <div class="flex flex-col gap-2 pt-2 border-t border-white/5">
            <div class="flex items-center justify-between">
              <span class="text-[11px] text-slate-400">Quick Waiver:</span>
              <div class="flex items-center gap-1">
                <button
                  onclick={() => quickWaiver(dev, 15)}
                  class="px-2 py-0.5 rounded bg-white/5 hover:bg-white/10 text-slate-300 text-[10px] font-semibold border border-white/10"
                >
                  +15m
                </button>
                <button
                  onclick={() => quickWaiver(dev, 30)}
                  class="px-2 py-0.5 rounded bg-white/5 hover:bg-white/10 text-slate-300 text-[10px] font-semibold border border-white/10"
                >
                  +30m
                </button>
                <button
                  onclick={() => openWaiverModal(dev)}
                  class="px-2 py-0.5 rounded bg-amber-500/10 hover:bg-amber-500/20 text-amber-300 text-[10px] font-semibold border border-amber-500/20"
                >
                  Custom
                </button>
              </div>
            </div>

            <button
              onclick={() => toggleBlock(dev)}
              class="w-full py-2 rounded-xl text-xs font-semibold transition-all {dev.isBlocked ? 'bg-emerald-600/20 hover:bg-emerald-600/30 text-emerald-300 border border-emerald-500/30' : 'bg-rose-600/20 hover:bg-rose-600/30 text-rose-300 border border-rose-500/30'}"
            >
              {dev.isBlocked ? 'Allow Device Access' : 'Block Device Access'}
            </button>
          </div>
        </Card>
      {/each}
    </div>
  {:else}
    <!-- DETAILED TABLE VIEW -->
    <Card>
      <div class="overflow-x-auto">
        <table class="w-full text-left text-xs">
          <thead class="bg-white/[0.02] border-b border-white/5 text-slate-400 uppercase tracking-wider text-[10px]">
            <tr>
              <th class="py-3 px-4 font-semibold">Device / Vendor</th>
              <th class="py-3 px-4 font-semibold">IP & MAC Address</th>
              <th class="py-3 px-4 font-semibold">Band / Signal</th>
              <th class="py-3 px-4 font-semibold">Data Traffic</th>
              <th class="py-3 px-4 font-semibold">Status</th>
              <th class="py-3 px-4 font-semibold text-right">Actions</th>
            </tr>
          </thead>
          <tbody class="divide-y divide-white/5 font-mono">
            {#each displayDevices as dev}
              {@const IconComp = getDeviceIcon(dev.icon)}
              {@const hasWaiver = (dev.waiverSecRemaining || 0) > 0}
              <tr class="hover:bg-white/[0.02] transition-colors">
                <td class="py-3 px-4 font-sans">
                  <div class="flex items-center gap-3">
                    <div class="w-8 h-8 rounded-lg bg-white/5 border border-white/10 flex items-center justify-center text-indigo-400 shrink-0">
                      <IconComp class="w-4 h-4" />
                    </div>
                    <div>
                      <div class="font-bold text-white text-xs">{dev.hostname || 'Station'}</div>
                      <div class="text-slate-400 text-[11px]">{dev.vendor}</div>
                    </div>
                  </div>
                </td>
                <td class="py-3 px-4 text-[11px]">
                  <div class="text-white font-medium">{dev.ip}</div>
                  <div class="text-slate-500 uppercase text-[10px]">{dev.mac}</div>
                </td>
                <td class="py-3 px-4 font-sans">
                  <div class="flex items-center gap-2">
                    <span class="px-1.5 py-0.5 rounded bg-indigo-500/10 text-indigo-300 text-[10px]">{dev.band}</span>
                    <SignalBars rssi={dev.rssi || -65} />
                  </div>
                </td>
                <td class="py-3 px-4 text-emerald-400 text-[11px]">
                  {formatBytes(dev.rxBytes + dev.txBytes)}
                </td>
                <td class="py-3 px-4 font-sans">
                  {#if dev.isBlocked}
                    <span class="inline-flex items-center gap-1 text-[10px] font-semibold text-rose-400 bg-rose-500/10 px-2 py-0.5 rounded-full border border-rose-500/20">
                      Blocked
                    </span>
                  {:else if hasWaiver}
                    <span class="inline-flex items-center gap-1 text-[10px] font-semibold text-amber-400 bg-amber-500/10 px-2 py-0.5 rounded-full border border-amber-500/20">
                      Waiver ({formatWaiverTime(dev.waiverSecRemaining)})
                    </span>
                  {:else}
                    <span class="inline-flex items-center gap-1 text-[10px] font-semibold text-emerald-400 bg-emerald-500/10 px-2 py-0.5 rounded-full border border-emerald-500/20">
                      Online
                    </span>
                  {/if}
                </td>
                <td class="py-3 px-4 text-right font-sans">
                  <div class="inline-flex items-center gap-1.5">
                    <button
                      onclick={() => quickWaiver(dev, 15)}
                      class="px-2 py-1 rounded bg-white/5 hover:bg-white/10 text-slate-300 text-xs border border-white/10"
                    >
                      +15m
                    </button>
                    <button
                      onclick={() => toggleBlock(dev)}
                      class="px-2.5 py-1 rounded text-xs font-semibold transition-colors {dev.isBlocked ? 'bg-emerald-600/20 hover:bg-emerald-600/30 text-emerald-300 border border-emerald-500/30' : 'bg-rose-600/20 hover:bg-rose-600/30 text-rose-300 border border-rose-500/30'}"
                    >
                      {dev.isBlocked ? 'Allow' : 'Block'}
                    </button>
                  </div>
                </td>
              </tr>
            {/each}
          </tbody>
        </table>
      </div>
    </Card>
  {/if}

  <!-- Temporary Waiver Modal -->
  <Modal
    isOpen={isWaiverModalOpen}
    title="Grant Temporary Access Waiver"
    onClose={() => (isWaiverModalOpen = false)}
  >
    {#if selectedDevice}
      <div class="flex flex-col gap-4 text-xs">
        <div class="p-3 rounded-xl bg-white/[0.03] border border-white/5 flex flex-col gap-1">
          <div class="flex items-center justify-between">
            <span class="text-slate-400">Target Station</span>
            <span class="font-bold text-white">{selectedDevice.hostname || 'Device'}</span>
          </div>
          <div class="flex items-center justify-between font-mono text-[11px]">
            <span class="text-slate-400">MAC Address</span>
            <span class="text-indigo-400">{selectedDevice.mac}</span>
          </div>
        </div>

        <div>
          <span class="block font-semibold text-slate-300 mb-2">Select Override Duration:</span>
          <div class="grid grid-cols-4 gap-2">
            {#each WAIVER_PRESETS as mins}
              <button
                type="button"
                onclick={() => (waiverMinutes = mins)}
                class="py-2.5 rounded-xl font-bold border transition-all {waiverMinutes === mins ? 'bg-indigo-600 text-white border-indigo-500 shadow-lg shadow-indigo-600/30' : 'bg-white/5 border-white/10 text-slate-300 hover:bg-white/10'}"
              >
                {mins < 60 ? `${mins}m` : `${mins / 60}h`}
              </button>
            {/each}
          </div>
        </div>

        <p class="text-[11px] text-slate-400 leading-relaxed">
          During the waiver period, curfew locks and bandwidth throttling will be temporarily bypassed for this station.
        </p>

        <div class="flex items-center justify-end gap-2 pt-2 border-t border-white/10">
          <button
            onclick={() => (isWaiverModalOpen = false)}
            class="px-3 py-2 rounded-xl text-slate-400 hover:text-white transition-colors"
          >
            Cancel
          </button>
          <button
            onclick={handleGrantWaiver}
            disabled={settingWaiver}
            class="px-4 py-2 rounded-xl bg-amber-500 hover:bg-amber-400 text-slate-950 font-bold shadow-lg shadow-amber-500/20 transition-all disabled:opacity-50"
          >
            {settingWaiver ? 'Granting...' : 'Grant Waiver'}
          </button>
        </div>
      </div>
    {/if}
  </Modal>
</div>
