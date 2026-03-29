export interface LogEvent {
  timestamp: string;
  level: 'debug' | 'info' | 'warn' | 'error';
  source: 'encoder' | 'transport' | 'ndi' | 'system';
  message: string;
}

export interface StatsEvent {
  timestamp: string;
  bitrate: number;
  currentBitrate: number;
  quality: number;
  rtt: number;
  bandwidth: number;
  buffer: number;
}

export interface StreamStatusEvent {
  streamId: string;
  status: 'idle' | 'connecting' | 'streaming' | 'error';
  error?: string;
}

export interface NDIDevice {
  name: string;
  url: string;
}

export interface NDIDevicesEvent {
  devices: NDIDevice[];
}

export interface EncoderStatusEvent {
  status: 'idle' | 'initializing' | 'running' | 'stopping' | 'error';
  error?: string;
}
