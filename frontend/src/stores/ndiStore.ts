import { ref, computed } from 'vue';
import { defineStore } from 'pinia';
import type { NDIDevice } from '../types/events';

export const useNdiStore = defineStore('ndi', () => {
  const devices = ref<NDIDevice[]>([]);
  const selectedDevice = ref<NDIDevice | null>(null);
  const isPreviewing = ref(false);
  const isRefreshing = ref(false);
  const error = ref<string | null>(null);

  const deviceNames = computed(() =>
    devices.value.map((device) => device.name)
  );

  const hasDevices = computed(() => devices.value.length > 0);

  function setDevices(newDevices: NDIDevice[]): void {
    devices.value = newDevices;
  }

  function selectDevice(deviceName: string): void {
    const device = devices.value.find((d) => d.name === deviceName);
    selectedDevice.value = device || null;
  }

  function clearSelection(): void {
    selectedDevice.value = null;
  }

  function setPreviewing(previewing: boolean): void {
    isPreviewing.value = previewing;
  }

  function setRefreshing(refreshing: boolean): void {
    isRefreshing.value = refreshing;
  }

  function setError(err: string | null): void {
    error.value = err;
  }

  return {
    devices,
    selectedDevice,
    isPreviewing,
    isRefreshing,
    error,
    deviceNames,
    hasDevices,
    setDevices,
    selectDevice,
    clearSelection,
    setPreviewing,
    setRefreshing,
    setError,
  };
});
