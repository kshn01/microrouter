<script>
  let { class: className = '', children, hover = false } = $props()
</script>

<div class="glass-card rounded-2xl p-6 transition-all duration-300 {hover ? 'glass-panel-hover' : ''} {className}">
  {@render children?.()}
</div>
