<script>
  import { onMount } from 'svelte'
  import { Cpu, HardDrive, RefreshCw, Power, Server, ShieldCheck, Layers, Zap } from '@lucide/svelte'
  import Card from '../components/ui/Card.svelte'
  import ProgressBar from '../components/ui/ProgressBar.svelte'
  import RebootModal from '../components/modals/RebootModal.svelte'
  import { systemInfo, isLoadingSystem, loadSystemInfo } from '../stores/system.store.js'
  import { freeHeap, totalHeap, heapPercent } from '../stores/telemetry.store.js'
  import { formatBytes } from '../types/models.js'

  let isRebootModalOpen = $state(false)

  onMount(() => {
    if (!$systemInfo) {
      loadSystemInfo()
    }
  })

  let fsPercent = $derived(
    $systemInfo && $systemInfo.fsTotalBytes > 0
      ? Math.round(($systemInfo.fsUsedBytes / $systemInfo.fsTotalBytes) * 100)
      : 0
  )

  let sketchPercent = $derived(
    $systemInfo && ($systemInfo.sketchSize + $systemInfo.sketchFree) > 0
      ? Math.round(($systemInfo.sketchSize / ($systemInfo.sketchSize + $systemInfo.sketchFree)) * 100)
      : 0
  )
</script>

<div class="flex flex-col gap-8 animate-in fade-in duration-300">
  <!-- Page Header -->
  <div class="flex items-center justify-between">
    <div class="flex flex-col gap-1">
      <h1 class="text-2xl lg:text-3xl font-bold tracking-tight text-white">System Diagnostics</h1>
      <p class="text-sm text-slate-400">Micro-controller architecture, firmware versions, memory partitions, and hardware registers</p>
    </div>

    <div class="flex items-center gap-2">
      <button
        onclick={loadSystemInfo}
        disabled={$isLoadingSystem}
        class="px-3.5 py-2 rounded-xl bg-white/5 hover:bg-white/10 text-xs font-medium text-slate-300 border border-white/10 transition-colors flex items-center gap-2 disabled:opacity-50"
      >
        <RefreshCw class="w-3.5 h-3.5 {$isLoadingSystem ? 'animate-spin' : ''}" />
        <span>Refresh Specs</span>
      </button>

      <button
        onclick={() => (isRebootModalOpen = true)}
        class="px-3.5 py-2 rounded-xl bg-rose-500/10 hover:bg-rose-500/20 text-xs font-medium text-rose-300 border border-rose-500/20 transition-colors flex items-center gap-2"
      >
        <Power class="w-3.5 h-3.5" />
        <span>Reboot Router</span>
      </button>
    </div>
  </div>

  <!-- Safe Double-Confirm Reboot Modal -->
  <RebootModal isOpen={isRebootModalOpen} onClose={() => (isRebootModalOpen = false)} />

  <div class="grid grid-cols-1 lg:grid-cols-2 gap-6">
    <!-- Hardware Details Card -->
    <Card>
      <div class="flex items-center gap-2.5 pb-4 mb-4 border-b border-white/10">
        <Cpu class="w-5 h-5 text-indigo-400" />
        <h2 class="text-base font-bold text-white tracking-tight">Silicon & Architecture</h2>
      </div>

      <div class="flex flex-col divide-y divide-white/5 text-sm">
        <div class="flex items-center justify-between py-2.5">
          <span class="text-slate-400">Processor Model</span>
          <span class="font-semibold text-white">{$systemInfo?.chipModel || '—'}</span>
        </div>
        <div class="flex items-center justify-between py-2.5">
          <span class="text-slate-400">Silicon Revision</span>
          <span class="font-mono text-white">{$systemInfo?.chipRevision ? `v${$systemInfo.chipRevision}` : '—'}</span>
        </div>
        <div class="flex items-center justify-between py-2.5">
          <span class="text-slate-400">Core Architecture</span>
          <span class="text-white">{$systemInfo?.cpuCores ? `Xtensa LX7 32-bit (${ $systemInfo.cpuCores } Cores)` : '—'}</span>
        </div>
        <div class="flex items-center justify-between py-2.5">
          <span class="text-slate-400">CPU Clock Speed</span>
          <span class="font-mono text-indigo-400 font-semibold">{$systemInfo?.cpuFreqMHz ? `${$systemInfo.cpuFreqMHz} MHz` : '—'}</span>
        </div>
        <div class="flex items-center justify-between py-2.5">
          <span class="text-slate-400">Quad-SPI Flash Size</span>
          <span class="font-mono text-white">{$systemInfo?.flashSizeMB ? `${$systemInfo.flashSizeMB} MB` : '—'}</span>
        </div>
        <div class="flex items-center justify-between py-2.5">
          <span class="text-slate-400">Factory Base MAC</span>
          <span class="font-mono text-slate-300">{$systemInfo?.macAddress || '—'}</span>
        </div>
      </div>
    </Card>

    <!-- Firmware & Versioning Card -->
    <Card>
      <div class="flex items-center gap-2.5 pb-4 mb-4 border-b border-white/10">
        <ShieldCheck class="w-5 h-5 text-emerald-400" />
        <h2 class="text-base font-bold text-white tracking-tight">Firmware & Software Versioning</h2>
      </div>

      <div class="flex flex-col divide-y divide-white/5 text-sm">
        <div class="flex items-center justify-between py-2.5">
          <span class="text-slate-400">Firmware Version</span>
          <span class="font-mono font-bold text-emerald-400">v{$systemInfo?.fwVersion || $systemInfo?.firmwareVersion || '1.0.3'}</span>
        </div>
        <div class="flex items-center justify-between py-2.5">
          <span class="text-slate-400">Build Target</span>
          <span class="font-semibold text-white">{$systemInfo?.fwName || 'MicroRouter ESP32-S3'}</span>
        </div>
        <div class="flex items-center justify-between py-2.5">
          <span class="text-slate-400">ESP-IDF / Framework SDK</span>
          <span class="font-mono text-slate-300">{$systemInfo?.sdkVersion || '—'}</span>
        </div>
        <div class="flex items-center justify-between py-2.5">
          <span class="text-slate-400">Active Boot Partition</span>
          <span class="font-mono text-indigo-400 uppercase font-semibold">{$systemInfo?.partition || $systemInfo?.activePartition || 'ota_0'}</span>
        </div>
        <div class="flex items-center justify-between py-2.5">
          <span class="text-slate-400">Flash Bus Frequency</span>
          <span class="font-mono text-white">{$systemInfo?.flashSpeedMHz ? `${$systemInfo.flashSpeedMHz} MHz` : '—'}</span>
        </div>
        <div class="flex items-center justify-between py-2.5">
          <span class="text-slate-400">App Binary Footprint</span>
          <span class="font-mono text-slate-300">
            {$systemInfo?.sketchSize ? `${formatBytes($systemInfo.sketchSize)} (${sketchPercent}%)` : '—'}
          </span>
        </div>
      </div>
    </Card>

    <!-- Storage & Memory Meter Card -->
    <Card class="lg:col-span-2">
      <div class="flex items-center gap-2.5 pb-4 mb-4 border-b border-white/10">
        <HardDrive class="w-5 h-5 text-purple-400" />
        <h2 class="text-base font-bold text-white tracking-tight">Storage & Memory Footprint</h2>
      </div>

      <div class="grid grid-cols-1 md:grid-cols-2 gap-6">
        <!-- LittleFS Progress -->
        <div class="flex flex-col gap-2">
          <ProgressBar
            label="LittleFS Web Partition"
            valueText={$systemInfo ? `${formatBytes($systemInfo.fsUsedBytes)} / ${formatBytes($systemInfo.fsTotalBytes)} (${fsPercent}%)` : '—'}
            percent={fsPercent}
            color="indigo"
          />
          <span class="text-[11px] text-slate-500">Houses modern Svelte SPA bundle, assets, and configuration JSONs</span>
        </div>

        <!-- SRAM Progress -->
        <div class="flex flex-col gap-2">
          <ProgressBar
            label="SRAM Heap Allocation"
            valueText={$systemInfo ? `${formatBytes($totalHeap - $freeHeap)} / ${formatBytes($totalHeap)} (${100 - $heapPercent}%)` : '—'}
            percent={100 - $heapPercent}
            color="cyan"
          />
          <span class="text-[11px] text-slate-500">Active memory consumed by FreeRTOS tasks, TCP sockets, and buffers</span>
        </div>
      </div>

      <div class="grid grid-cols-1 sm:grid-cols-2 gap-4 mt-5">
        <div class="p-3 rounded-xl bg-white/[0.03] border border-white/5 flex items-center justify-between text-xs">
          <span class="text-slate-400">Lowest Free RAM Watermark</span>
          <span class="font-mono font-semibold text-emerald-400">
            {$systemInfo ? formatBytes($systemInfo.minFreeHeap) : '—'}
          </span>
        </div>

        <div class="p-3 rounded-xl bg-white/[0.03] border border-white/5 flex items-center justify-between text-xs">
          <span class="text-slate-400">PSRAM External Buffer</span>
          <span class="font-mono font-semibold {$systemInfo?.psramSize > 0 ? 'text-indigo-400' : 'text-slate-500'}">
            {$systemInfo?.psramSize > 0 ? `${formatBytes($systemInfo.psramFree)} free / ${formatBytes($systemInfo.psramSize)}` : 'Not Installed / Disabled'}
          </span>
        </div>
      </div>
    </Card>
  </div>
</div>
