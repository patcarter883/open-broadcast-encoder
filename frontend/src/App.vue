<template>
  <q-layout view="hHh lpR fFf">
    <q-header elevated class="bg-dark text-white">
      <MenuBar />
    </q-header>

    <q-page-container>
      <q-page padding>
        <div class="row q-col-gutter-md">
          <div class="col-12 col-md-6 col-lg-4">
            <InputSelector />
          </div>
          <div class="col-12 col-md-6 col-lg-4">
            <CodecControls />
          </div>
          <div class="col-12 col-md-6 col-lg-4">
            <OutputManager />
          </div>
          <div class="col-12 col-md-6 col-lg-8">
            <StatsDashboard />
          </div>
          <div class="col-12 col-lg-4">
            <LogViewer />
          </div>
        </div>
      </q-page>
    </q-page-container>
  </q-layout>
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
body { margin: 0; }
</style>
