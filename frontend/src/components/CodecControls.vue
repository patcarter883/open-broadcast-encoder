<template>
  <q-card>
    <q-card-section>
      <div class="text-h6">
        <q-icon name="tune" class="q-mr-sm" />
        Encoder Settings
      </div>
    </q-card-section>
    <q-separator />
    <q-card-section>
      <div class="row q-col-gutter-md">
        <div class="col-12">
          <q-select
            v-model="codec"
            :options="codecOptions"
            label="Codec"
            outlined
            dense
            emit-value
            map-options
          />
        </div>
        <div class="col-12">
          <q-select
            v-model="encoder"
            :options="encoderOptions"
            label="Encoder"
            outlined
            dense
            emit-value
            map-options
          />
        </div>
        <div class="col-6">
          <q-input v-model.number="width" label="Width" outlined dense type="number" />
        </div>
        <div class="col-6">
          <q-input v-model.number="height" label="Height" outlined dense type="number" />
        </div>
        <div class="col-12">
          <q-item class="q-px-none">
            <q-item-section>
              <q-item-label>Bitrate</q-item-label>
              <q-slider v-model="bitrate" :min="1000" :max="50000" :step="500" label color="primary" />
            </q-item-section>
            <q-item-section side>
              <q-badge color="primary">{{ bitrate }} kbps</q-badge>
            </q-item-section>
          </q-item>
        </div>
        <div class="col-6">
          <q-select
            v-model="framerateNum"
            :options="[24, 25, 30, 50, 60]"
            label="FPS Num"
            outlined
            dense
          />
        </div>
        <div class="col-6">
          <q-select
            v-model="framerateDen"
            :options="[1]"
            label="FPS Den"
            outlined
            dense
          />
        </div>
        <div class="col-12">
          <q-select
            v-model="preset"
            :options="presetOptions"
            label="Quality Preset"
            outlined
            dense
            emit-value
            map-options
          />
        </div>
      </div>
    </q-card-section>
  </q-card>
</template>

<script setup lang="ts">
import { ref } from 'vue'

const codec = ref('h264')
const encoder = ref('x264')
const width = ref(1920)
const height = ref(1080)
const bitrate = ref(6000)
const framerateNum = ref(30)
const framerateDen = ref(1)
const preset = ref('medium')

const codecOptions = [
  { label: 'H.264 / AVC', value: 'h264' },
  { label: 'H.265 / HEVC', value: 'h265' },
  { label: 'AV1', value: 'av1' }
]

const encoderOptions = [
  { label: 'x264 (Software)', value: 'x264' },
  { label: 'NVIDIA NVENC', value: 'nvenc' },
  { label: 'Intel QSV', value: 'qsv' },
  { label: 'AMD AMF', value: 'amf' }
]

const presetOptions = [
  { label: 'Ultra Fast', value: 'ultrafast' },
  { label: 'Super Fast', value: 'superfast' },
  { label: 'Very Fast', value: 'veryfast' },
  { label: 'Faster', value: 'faster' },
  { label: 'Fast', value: 'fast' },
  { label: 'Medium', value: 'medium' },
  { label: 'Slow', value: 'slow' },
  { label: 'Slower', value: 'slower' },
  { label: 'Very Slow', value: 'veryslow' }
]
</script>
