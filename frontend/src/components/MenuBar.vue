<template>
  <q-toolbar>
    <q-btn flat round dense icon="menu" class="q-mr-sm" />
    <q-toolbar-title>Open Broadcast Encoder</q-toolbar-title>
    <q-space />
    <q-btn flat round dense icon="settings" class="q-mr-sm">
      <q-tooltip>Settings</q-tooltip>
    </q-btn>
    <q-btn flat round dense icon="help" class="q-mr-sm">
      <q-tooltip>Help</q-tooltip>
    </q-btn>
    <q-btn flat round dense icon="dark_mode" @click="toggleDarkMode">
      <q-tooltip>Toggle Dark Mode</q-tooltip>
    </q-btn>
    <q-separator dark vertical class="q-mx-md" />
    <q-btn color="positive" icon="play_arrow" label="Start" class="q-mr-sm" @click="startEncoding" />
    <q-btn color="negative" icon="stop" label="Stop" @click="stopEncoding" />
  </q-toolbar>
</template>

<script setup lang="ts">
import { useQuasar } from 'quasar'

const $q = useQuasar()

const toggleDarkMode = () => {
  $q.dark.toggle()
}

const startEncoding = () => {
  $q.notify({
    type: 'positive',
    message: 'Encoding started',
    icon: 'play_arrow'
  })
}

const stopEncoding = () => {
  $q.notify({
    type: 'warning',
    message: 'Encoding stopped',
    icon: 'stop'
  })
}
</script>
