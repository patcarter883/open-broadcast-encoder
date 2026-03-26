export interface InputConfig {
  mode: number
  url: string
  ndi_name: string
  port: number
}

export interface EncodeConfig {
  codec: number
  encoder: number
  width: number
  height: number
  bitrate_kbps: number
  framerate_num: number
  framerate_den: number
  preset: string
}

export interface StreamConfig {
  stream_id: string
  destination_url: string
  protocol: number
  port: number
  sdp_file: string
}

export interface StreamStats {
  stream_id: string
  packets_sent: number
  bytes_sent: number
  packets_lost: number
  rtt_us: number
  bitrate_kbps: number
}

export interface CumulativeStats {
  num_streams: number
  streams: StreamStats[]
  uptime_ms: number
}
