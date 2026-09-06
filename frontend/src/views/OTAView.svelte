<script>
  import { ArrowUpRight, ShieldCheck, Layers, AlertCircle, UploadCloud } from '@lucide/svelte'
  import Card from '../components/ui/Card.svelte'
  import { activePartition, fwVersion, fwValidated, bootCount } from '../stores/telemetry.store.js'
  import { openOtaPortal } from '../stores/ota.store.js'
</script>

<div class="flex flex-col gap-8 animate-in fade-in duration-300">
  <!-- Page Header -->
  <div class="flex flex-col gap-1">
    <h1 class="text-2xl lg:text-3xl font-bold tracking-tight text-white">Over-The-Air (OTA) Updates</h1>
    <p class="text-sm text-slate-400">Dual-bank A/B wireless firmware updates with hardware rollback protection</p>
  </div>

  <!-- Dual-Bank Status & Upload Trigger -->
  <div class="grid grid-cols-1 lg:grid-cols-2 gap-6">
    <!-- Current Image Properties -->
    <Card>
      <div class="flex items-center gap-2.5 pb-4 mb-4 border-b border-white/10">
        <ShieldCheck class="w-5 h-5 text-emerald-400" />
        <h2 class="text-base font-bold text-white tracking-tight">Active Image Health</h2>
      </div>

      <div class="flex flex-col divide-y divide-white/5 text-sm">
        <div class="flex items-center justify-between py-2.5">
          <span class="text-slate-400">Firmware Build</span>
          <span class="font-mono font-bold text-white">v{$fwVersion}</span>
        </div>
        <div class="flex items-center justify-between py-2.5">
          <span class="text-slate-400">Active Flash Slot</span>
          <span class="font-mono font-semibold text-indigo-400 uppercase">{$activePartition}</span>
        </div>
        <div class="flex items-center justify-between py-2.5">
          <span class="text-slate-400">Integrity Validation</span>
          <span class="flex items-center gap-1.5 text-xs font-semibold px-2 py-0.5 rounded-full {$fwValidated ? 'bg-emerald-500/10 text-emerald-300 border border-emerald-500/20' : 'bg-amber-500/10 text-amber-300 border border-amber-500/20'}">
            {$fwValidated ? '✓ Verified Stable' : '⏳ Validating Boot...'}
          </span>
        </div>
        <div class="flex items-center justify-between py-2.5">
          <span class="text-slate-400">Successful Boot Cycles</span>
          <span class="font-mono text-white">{$bootCount}</span>
        </div>
      </div>
    </Card>

    <!-- Upload Portal Zone -->
    <Card class="flex flex-col justify-between">
      <div class="flex items-center gap-2.5 pb-4 mb-3 border-b border-white/10">
        <UploadCloud class="w-5 h-5 text-indigo-400" />
        <h2 class="text-base font-bold text-white tracking-tight">Wireless Update Portal</h2>
      </div>

      <p class="text-xs text-slate-400 mb-4 leading-relaxed">
        Upload compiled <code class="font-mono text-indigo-300 px-1 py-0.5 rounded bg-white/5">firmware.bin</code> or filesystem <code class="font-mono text-indigo-300 px-1 py-0.5 rounded bg-white/5">littlefs.bin</code> directly through the built-in ElegantOTA upload manager.
      </p>

      <button
        onclick={openOtaPortal}
        class="w-full py-4 rounded-xl bg-gradient-to-r from-indigo-500 to-purple-500 hover:from-indigo-600 hover:to-purple-600 text-white font-semibold text-sm shadow-[0_0_25px_rgba(99,102,241,0.5)] transition-all flex items-center justify-center gap-2 group"
      >
        <span>Open ElegantOTA Upload Manager</span>
        <ArrowUpRight class="w-4 h-4 transition-transform group-hover:translate-x-0.5 group-hover:-translate-y-0.5" />
      </button>

      <span class="text-[11px] text-center text-slate-500 mt-3">
        Opens <span class="font-mono text-slate-400">/update</span> in a secure management session
      </span>
    </Card>
  </div>

  <!-- A/B Partition Architecture Explained -->
  <Card>
    <div class="flex items-center gap-2.5 pb-4 mb-4 border-b border-white/10">
      <Layers class="w-5 h-5 text-indigo-400" />
      <h2 class="text-base font-bold text-white tracking-tight">Dual-Bank Rollback Protection Architecture</h2>
    </div>

    <div class="grid grid-cols-1 md:grid-cols-2 gap-4">
      <div class="p-4 rounded-xl bg-white/[0.02] border border-white/5 flex flex-col gap-2">
        <div class="flex items-center justify-between">
          <span class="font-mono font-bold text-sm text-white">Bank 0: ota_0 (3.0 MB)</span>
          <span class="text-[10px] uppercase font-bold px-2 py-0.5 rounded {$activePartition === 'ota_0' ? 'bg-indigo-500/20 text-indigo-300 border border-indigo-500/30' : 'bg-slate-800 text-slate-400'}">
            {$activePartition === 'ota_0' ? 'Current Boot' : 'Standby'}
          </span>
        </div>
        <p class="text-xs text-slate-400 leading-relaxed">
          Contains verified production firmware. If an update to Bank 1 fails during boot, the hardware bootloader automatically falls back here.
        </p>
      </div>

      <div class="p-4 rounded-xl bg-white/[0.02] border border-white/5 flex flex-col gap-2">
        <div class="flex items-center justify-between">
          <span class="font-mono font-bold text-sm text-white">Bank 1: ota_1 (3.0 MB)</span>
          <span class="text-[10px] uppercase font-bold px-2 py-0.5 rounded {$activePartition === 'ota_1' ? 'bg-indigo-500/20 text-indigo-300 border border-indigo-500/30' : 'bg-slate-800 text-slate-400'}">
            {$activePartition === 'ota_1' ? 'Current Boot' : 'Standby'}
          </span>
        </div>
        <p class="text-xs text-slate-400 leading-relaxed">
          Receives newly uploaded firmware payloads wirelessly. Marked as tentative until validated via software watchdog.
        </p>
      </div>
    </div>
  </Card>
</div>
