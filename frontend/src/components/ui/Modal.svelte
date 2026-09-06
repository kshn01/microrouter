<script>
  import { X } from '@lucide/svelte'

  let {
    isOpen = false,
    title = '',
    onClose = () => {},
    children,
  } = $props()

  function handleBackdropClick(e) {
    if (e.target === e.currentTarget) {
      onClose()
    }
  }

  function handleKeydown(e) {
    if (e.key === 'Escape') {
      onClose()
    }
  }
</script>

{#if isOpen}
  <div
    class="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/70 backdrop-blur-md transition-opacity animate-in fade-in duration-200"
    onclick={handleBackdropClick}
    onkeydown={handleKeydown}
    role="dialog"
    tabindex="-1"
  >
    <div class="glass-panel w-full max-w-md rounded-2xl p-6 shadow-2xl border border-white/10 transform transition-transform animate-in zoom-in-95 duration-200">
      <div class="flex items-center justify-between pb-4 mb-4 border-b border-white/10">
        <h3 class="text-lg font-bold text-white tracking-tight">{title}</h3>
        <button
          onclick={onClose}
          class="p-1 rounded-lg text-slate-400 hover:text-white hover:bg-white/5 transition-colors"
          aria-label="Close"
        >
          <X class="w-5 h-5" />
        </button>
      </div>

      <div>
        {@render children?.()}
      </div>
    </div>
  </div>
{/if}
