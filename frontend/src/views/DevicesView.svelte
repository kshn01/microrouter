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
    ShieldAlert,
    ShieldCheck,
    Clock,
    Search,
    RefreshCw,
    Ban,
    CheckCircle2,
    Sliders,
    HardDrive,
    Server,
    Activity
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

  // Waiver modal states
  let isWaiverModalOpen = $state(false)
  let selectedDevice = $state(null)
  let waiverMinutes = $state(30)
  let settingWaiver = $state(false)

  const WAIVER_PRESETS = [15, 30, 60, 120]

  async function loadDevices() {
    try {
      const res = await apiGetDevices()
      if (res && res.devices) {
        devices = res.devices.map((d) => {
          const oui = lookupOUI(d.mac)
          return {
            ...d,
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

  let filteredDevices = $derived(
    devices.filter((d) => {
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
  )

  let totalDevicesCount = $derived(devices.length)
  let blockedCount = $derived(devices.filter((d) => d.isBlocked).length)
  let activeWaiverCount = $derived(
    devices.filter((d) => (d.waiverSecRemaining || 0) > 0).length
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
    pollInterval = setInterval(loadDevices, 4000)
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
        75+ OUI hardware vendor database, NetBIOS host identification, and dynamic access control
      </p>
    </div>
    <div class="flex items-center gap-3">
      <button
        onclick={loadDevices}
        class="inline-flex items-center gap-2 px-3 py-2 rounded-xl text-xs font-semibold bg-white/5 hover:bg-white/10 text-slate-300 border border-white/10 transition-colors"
      >
        <RefreshCw class="w-3.5 h-3.5" />
        Refresh
      </button>
    </div>
  </div>

  <!-- Metric Counters -->
  <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
    <StatCard
      title="Connected Devices"
      value={String(totalDevicesCount)}
      subtitle="Active wireless & wired hosts"
      color="indigo"
      icon={Laptop}
    />
    <StatCard
      title="Online & Active"
      value={String(totalDevicesCount - blockedCount)}
      subtitle="Full gateway access"
      color="emerald"
      icon={CheckCircle2}
    />
    <StatCard
      title="Access Blocked"
      value={String(blockedCount)}
      subtitle="Restricted by policy or admin"
      color="rose"
      icon={Ban}
    />
    <StatCard
      title="Active Waivers"
      value={String(activeWaiverCount)}
      subtitle="Temporary bypasses in effect"
      color="amber"
      icon={Clock}
    />
  </div>

  <!-- Devices Table & Filtering -->
  <Card>
    <div class="flex flex-col sm:flex-row sm:items-center justify-between gap-4 pb-4 mb-4 border-b border-white/10">
      <div class="flex items-center gap-3">
        <div class="w-8 h-8 rounded-lg bg-indigo-500/20 text-indigo-400 flex items-center justify-center">
          <Activity class="w-4 h-4" />
        </div>
        <div>
          <h2 class="text-base font-bold text-white tracking-tight">Active Client Stations</h2>
          <span class="text-xs text-slate-400">Inventory with vendor OUI identification</span>
        </div>
      </div>

      <!-- Search Input -->
      <div class="relative">
        <Search class="w-3.5 h-3.5 absolute left-3 top-1/2 -translate-y-1/2 text-slate-500" />
        <input
          type="text"
          bind:value={searchQuery}
          placeholder="Search by vendor, IP, MAC, name..."
          class="bg-slate-900 border border-white/10 rounded-xl pl-9 pr-3 py-1.5 text-xs text-white placeholder-slate-500 focus:outline-none focus:border-indigo-500 w-full sm:w-80"
        />
      </div>
    </div>

    <!-- Table -->
    <div class="overflow-x-auto">
      <table class="w-full text-left text-xs">
        <thead class="bg-white/[0.02] border-b border-white/5 text-slate-400 uppercase tracking-wider text-[10px]">
          <tr>
            <th class="py-3 px-4 font-semibold">Device / Vendor</th>
            <th class="py-3 px-4 font-semibold">IP & MAC Address</th>
            <th class="py-3 px-4 font-semibold">Signal</th>
            <th class="py-3 px-4 font-semibold">Bandwidth</th>
            <th class="py-3 px-4 font-semibold">Status / Waiver</th>
            <th class="py-3 px-4 font-semibold text-right">Actions</th>
          </tr>
        </thead>
        <tbody class="divide-y divide-white/5">
          {#if filteredDevices.length === 0}
            <tr>
              <td colspan="6" class="py-12 text-center text-slate-500 font-sans">
                {searchQuery ? 'No devices found matching the filter.' : 'No devices detected on network.'}
              </td>
            </tr>
          {:else}
            {#each filteredDevices as dev}
              {@const IconComp = getDeviceIcon(dev.icon)}
              {@const hasWaiver = (dev.waiverSecRemaining || 0) > 0}
              <tr class="hover:bg-white/[0.02] transition-colors">
                <!-- Device / Vendor -->
                <td class="py-3 px-4">
                  <div class="flex items-center gap-3">
                    <div class="w-9 h-9 rounded-xl bg-white/5 border border-white/10 flex items-center justify-center text-indigo-400 shrink-0">
                      <IconComp class="w-4 h-4" />
                    </div>
                    <div>
                      <div class="flex items-center gap-2">
                        <span class="font-bold text-white text-xs">
                          {dev.hostname || 'Unknown Device'}
                        </span>
                        {#if dev.netbios}
                          <span class="px-1.5 py-0.2 rounded bg-indigo-500/20 text-indigo-300 border border-indigo-500/30 text-[9px] font-mono">
                            {dev.netbios}
                          </span>
                        {/if}
                      </div>
                      <div class="flex items-center gap-1.5 mt-0.5">
                        <span class="text-[11px] text-slate-400 font-medium">{dev.vendor}</span>
                        {#if dev.isRandomized}
                          <span class="text-[9px] px-1 py-0.2 rounded bg-amber-500/10 text-amber-400 border border-amber-500/20">
                            Private MAC
                          </span>
                        {/if}
                      </div>
                    </div>
                  </div>
                </td>

                <!-- IP & MAC -->
                <td class="py-3 px-4 font-mono text-[11px]">
                  <div class="text-white font-medium">{dev.ip}</div>
                  <div class="text-slate-500 text-[10px] uppercase">{dev.mac}</div>
                </td>

                <!-- Signal -->
                <td class="py-3 px-4">
                  <div class="flex items-center gap-1.5">
                    <SignalBars rssi={dev.rssi || -65} />
                    <span class="font-mono text-[11px] text-slate-400">{dev.rssi || -65} dBm</span>
                  </div>
                </td>

                <!-- Bandwidth -->
                <td class="py-3 px-4 text-[11px] text-slate-300 font-mono">
                  <div>↓ {formatBytes(dev.rxBytes || 0)}</div>
                  <div class="text-slate-500 text-[10px]">↑ {formatBytes(dev.txBytes || 0)}</div>
                </td>

                <!-- Status / Waiver -->
                <td class="py-3 px-4">
                  {#if dev.isBlocked}
                    <span class="inline-flex items-center gap-1 text-[10px] font-semibold text-rose-400 bg-rose-500/10 px-2 py-0.5 rounded-full border border-rose-500/20">
                      <Ban class="w-3 h-3" />
                      Blocked
                    </span>
                  {:else if hasWaiver}
                    <span class="inline-flex items-center gap-1 text-[10px] font-semibold text-amber-400 bg-amber-500/10 px-2 py-0.5 rounded-full border border-amber-500/20">
                      <Clock class="w-3 h-3" />
                      Waiver: {formatWaiverTime(dev.waiverSecRemaining)}
                    </span>
                  {:else}
                    <span class="inline-flex items-center gap-1 text-[10px] font-semibold text-emerald-400 bg-emerald-500/10 px-2 py-0.5 rounded-full border border-emerald-500/20">
                      <CheckCircle2 class="w-3 h-3" />
                      Allowed
                    </span>
                  {/if}
                </td>

                <!-- Actions -->
                <td class="py-3 px-4 text-right">
                  <div class="inline-flex items-center gap-2">
                    <button
                      onclick={() => openWaiverModal(dev)}
                      class="px-2.5 py-1 rounded-lg text-xs font-medium bg-white/5 hover:bg-white/10 text-amber-300 border border-amber-500/20 transition-colors"
                      title="Grant temporary access waiver"
                    >
                      Waiver
                    </button>
                    <button
                      onclick={() => toggleBlock(dev)}
                      class="px-2.5 py-1 rounded-lg text-xs font-semibold transition-colors {dev.isBlocked ? 'bg-emerald-600/20 hover:bg-emerald-600/30 text-emerald-300 border border-emerald-500/30' : 'bg-rose-600/20 hover:bg-rose-600/30 text-rose-300 border border-rose-500/30'}"
                    >
                      {dev.isBlocked ? 'Allow' : 'Block'}
                    </button>
                  </div>
                </td>
              </tr>
            {/each}
          {/if}
        </tbody>
      </table>
    </div>
  </Card>

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
            <span class="text-slate-400">Target Client</span>
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
