import { invoke } from '@tauri-apps/api/core';
import type { InputConfig, EncodeConfig } from '../types/config';

export async function initEncoder(): Promise<void> {
  return invoke('init_encoder');
}

export async function destroyEncoder(): Promise<void> {
  return invoke('destroy_encoder');
}

export async function setInputConfig(config: InputConfig): Promise<void> {
  return invoke('set_input_config', { config });
}

export async function setEncodeConfig(config: EncodeConfig): Promise<void> {
  return invoke('set_encode_config', { config });
}

export async function startEncoder(): Promise<void> {
  return invoke('start_encoder');
}

export async function stopEncoder(): Promise<void> {
  return invoke('stop_encoder');
}

export async function setBitrate(bitrate: number): Promise<void> {
  return invoke('set_bitrate', { bitrate });
}

export async function getStats(): Promise<{
  bitrate: number;
  quality: number;
  rtt: number;
}> {
  return invoke('get_stats');
}

export async function openSdpFile(): Promise<string | null> {
  return invoke('open_sdp_file');
}
