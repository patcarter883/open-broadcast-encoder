import { defineStore } from 'pinia'
import { ref } from 'vue'

export const useEncoderStore = defineStore('encoder', () => {
  const isRunning = ref(false)
  const isInitialized = ref(false)

  return { isRunning, isInitialized }
})
