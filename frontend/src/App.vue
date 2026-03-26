<template>
  <div role="main">
    <MenuBar />
    <div class="content">
      <InputSelector />
      <CodecControls />
      <OutputManager />
      <StatsDashboard />
      <LogViewer />
    </div>
  </div>
</template>

<script setup lang="ts">
import MenuBar from './components/MenuBar.vue'
import InputSelector from './components/InputSelector.vue'
import CodecControls from './components/CodecControls.vue'
import OutputManager from './components/OutputManager.vue'
import StatsDashboard from './components/StatsDashboard.vue'
import LogViewer from './components/LogViewer.vue'
</script>

<style>
body { margin: 0; font-family: system-ui, sans-serif; }
#app { display: flex; flex-direction: column; height: 100vh; }
</style>
