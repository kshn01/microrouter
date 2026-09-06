<script>
  import { toasts, dismissToast } from '../../services/toast.service.js'
  import { CheckCircle2, AlertCircle, AlertTriangle, Info, X } from '@lucide/svelte'

  const icons = {
    success: CheckCircle2,
    error: AlertCircle,
    warning: AlertTriangle,
    info: Info,
  }

  const borderStyles = {
    success: 'border-emerald-500/40 text-emerald-300 bg-emerald-950/40',
    error: 'border-rose-500/40 text-rose-300 bg-rose-950/40',
    warning: 'border-amber-500/40 text-amber-300 bg-amber-950/40',
    info: 'border-indigo-500/40 text-indigo-300 bg-indigo-950/40',
  }
</script>

<div class="fixed bottom-5 right-5 z-50 flex flex-col gap-2.5 max-w-sm w-full pointer-events-none px-4 sm:px-0">
  {#each $toasts as toast (toast.id)}
    {@const IconComponent = icons[toast.type] || Info}
    <div class="pointer-events-auto flex items-center justify-between gap-3 p-4 rounded-xl border backdrop-blur-xl shadow-2xl transition-all duration-300 transform translate-y-0 opacity-100 {borderStyles[toast.type] || borderStyles.info}">
      <div class="flex items-center gap-2.5">
        <IconComponent class="w-5 h-5 shrink-0" />
        <span class="text-xs font-medium leading-relaxed">{toast.message}</span>
      </div>

      <button
        onclick={() => dismissToast(toast.id)}
        class="p-1 rounded-lg hover:bg-white/10 text-slate-400 hover:text-white transition-colors"
        aria-label="Dismiss"
      >
        <X class="w-4 h-4" />
      </button>
    </div>
  {/each}
</div>
