import { invoke } from '@tauri-apps/api/core';
import type { StreamConfig } from '../types/config';

export async function addStream(config: StreamConfig): Promise<string> {
  return invoke('add_stream', { config });
}

export async function removeStream(streamId: string): Promise<void> {
  return invoke('remove_stream', { streamId });
}

export async function getStreams(): Promise<StreamConfig[]> {
  return invoke('get_streams');
}
