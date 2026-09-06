<script>
  import { onMount } from 'svelte'
  import Router from 'svelte-spa-router'
  import Sidebar from './components/layout/Sidebar.svelte'
  import Topbar from './components/layout/Topbar.svelte'
  import ToastContainer from './components/ui/ToastContainer.svelte'
  import DashboardView from './views/DashboardView.svelte'
  import DnsView from './views/DnsView.svelte'
  import DevicesView from './views/DevicesView.svelte'
  import ParentalView from './views/ParentalView.svelte'
  import WiFiView from './views/WiFiView.svelte'
  import SystemView from './views/SystemView.svelte'
  import OTAView from './views/OTAView.svelte'
  import InsightsView from './views/InsightsView.svelte'

  import { initTelemetryConnection } from './services/websocket.service.js'
  import { updateTelemetry } from './stores/telemetry.store.js'
  import { setConnectionStatus } from './stores/connection.store.js'

  const routes = {
    '/': DashboardView,
    '/dns': DnsView,
    '/devices': DevicesView,
    '/insights': InsightsView,
    '/parental': ParentalView,
    '/wifi': WiFiView,
    '/system': SystemView,
    '/ota': OTAView,
    '*': DashboardView,
  }

  let currentHash = $state(window.location.hash || '#/')
  let mobileOpen = $state(false)

  onMount(() => {
    // Initialize telemetry manager (auto-connects to ESP32 with simulation fallback)
    initTelemetryConnection(
      (data) => updateTelemetry(data),
      (status) => setConnectionStatus(status)
    )

    const onHashChange = () => {
      currentHash = window.location.hash || '#/'
      mobileOpen = false
    }

    window.addEventListener('hashchange', onHashChange)
    return () => window.removeEventListener('hashchange', onHashChange)
  })
</script>

<div class="min-h-screen bg-slate-950 text-slate-100 flex flex-col antialiased selection:bg-indigo-500 selection:text-white">
  <!-- Sidebar Navigation -->
  <Sidebar
    {currentHash}
    {mobileOpen}
    onCloseMobile={() => (mobileOpen = false)}
  />

  <!-- Mobile Overlay Backdrop -->
  {#if mobileOpen}
    <div
      class="fixed inset-0 z-30 bg-black/60 backdrop-blur-sm lg:hidden"
      onclick={() => (mobileOpen = false)}
      aria-hidden="true"
    ></div>
  {/if}

  <!-- Main Viewport Area -->
  <div class="flex-1 flex flex-col min-w-0 lg:pl-64">
    <Topbar onToggleMobile={() => (mobileOpen = !mobileOpen)} />

    <main class="flex-1 p-4 sm:p-6 lg:p-8 max-w-7xl w-full mx-auto">
      <Router {routes} />
    </main>
  </div>

  <!-- Global Toasts -->
  <ToastContainer />
</div>
