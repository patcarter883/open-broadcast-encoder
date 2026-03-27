<template>
  <q-card>
    <q-card-section>
      <div class="text-h6">
        <q-icon name="input" class="q-mr-sm" />
        Input Source
      </div>
    </q-card-section>
    <q-separator />
    <q-card-section>
      <q-tabs v-model="inputTab" dense class="text-grey" active-color="primary" indicator-color="primary" align="justify">
        <q-tab name="ndi" label="NDI" icon="videocam" />
        <q-tab name="sdp" label="SDP/RTP" icon="settings_ethernet" />
        <q-tab name="mpegts" label="MPEG-TS" icon="tv" />
      </q-tabs>

      <q-tab-panels v-model="inputTab" animated>
        <q-tab-panel name="ndi" class="q-pa-none q-pt-md">
          <q-select
            v-model="ndiSource"
            :options="ndiSources"
            label="NDI Source"
            outlined
            dense
            clearable
            :loading="scanning"
          >
            <template v-slot:append>
              <q-btn round dense flat icon="refresh" @click="scanNdiSources" :loading="scanning">
                <q-tooltip>Scan for NDI sources</q-tooltip>
              </q-btn>
            </template>
          </q-select>
        </q-tab-panel>

        <q-tab-panel name="sdp" class="q-pa-none q-pt-md">
          <q-input v-model="sdpUrl" label="SDP URL" outlined dense placeholder="rtp://0.0.0.0:5004">
            <template v-slot:prepend>
              <q-icon name="link" />
            </template>
          </q-input>
        </q-tab-panel>

        <q-tab-panel name="mpegts" class="q-pa-none q-pt-md">
          <q-input v-model="mpegTsUrl" label="MPEG-TS URL" outlined dense placeholder="udp://0.0.0.0:1234">
            <template v-slot:prepend>
              <q-icon name="link" />
            </template>
          </q-input>
        </q-tab-panel>
      </q-tab-panels>
    </q-card-section>
  </q-card>
</template>

<script setup lang="ts">
import { ref } from 'vue'

const inputTab = ref('ndi')
const ndiSource = ref(null)
const sdpUrl = ref('')
const mpegTsUrl = ref('')
const scanning = ref(false)

const ndiSources = ref([
  'NDI Source 1',
  'NDI Source 2',
  'NDI Studio Monitor',
  'OBS NDI Output'
])

const scanNdiSources = () => {
  scanning.value = true
  setTimeout(() => {
    scanning.value = false
  }, 1500)
}
</script>
