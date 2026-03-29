import { invoke } from '@tauri-apps/api/core';
import type { NDIDevice } from '../types/events';

export async function refreshDevices(): Promise<NDIDevice[]> {
  return invoke('refresh_ndi_devices');
}

export async function startPreview(deviceName: string): Promise<void> {
  return invoke('start_ndi_preview', { deviceName });
}

export async function stopPreview(): Promise<void> {
  return invoke('stop_ndi_preview');
}
