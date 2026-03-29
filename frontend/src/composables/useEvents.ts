import { onMounted, onUnmounted } from 'vue';
import { listen, type UnlistenFn } from '@tauri-apps/api/event';
import { useLogStore } from '../stores/logStore';
import { useNdiStore } from '../stores/ndiStore';
import { useEncoderStore } from '../stores/encoderStore';
import type {
  LogEvent,
  StatsEvent,
  StreamStatusEvent,
  NDIDevicesEvent,
  EncoderStatusEvent,
} from '../types/events';

export function useEvents() {
  const logStore = useLogStore();
  const ndiStore = useNdiStore();
  const encoderStore = useEncoderStore();

  let unlisteners: UnlistenFn[] = [];

  onMounted(async () => {
    const logUnlisten = await listen<LogEvent>('log', (event) => {
      logStore.addLog(event.payload);
    });

    const statsUnlisten = await listen<StatsEvent>('stats', (event) => {
      encoderStore.updateStats(event.payload);
    });

    const streamStatusUnlisten = await listen<StreamStatusEvent>(
      'stream-status',
      (event) => {
        encoderStore.updateStreamStatus(event.payload);
      }
    );

    const ndiDevicesUnlisten = await listen<NDIDevicesEvent>(
      'ndi-devices',
      (event) => {
        ndiStore.setDevices(event.payload.devices);
      }
    );

    const encoderStatusUnlisten = await listen<EncoderStatusEvent>(
      'encoder-status',
      (event) => {
        encoderStore.setStatus(event.payload.status, event.payload.error);
      }
    );

    unlisteners = [
      logUnlisten,
      statsUnlisten,
      streamStatusUnlisten,
      ndiDevicesUnlisten,
      encoderStatusUnlisten,
    ];
  });

  onUnmounted(() => {
    unlisteners.forEach((unlisten) => unlisten());
    unlisteners = [];
  });

  return {
    logStore,
    ndiStore,
    encoderStore,
  };
}
