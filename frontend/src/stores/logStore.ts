import { ref, computed } from 'vue';
import { defineStore } from 'pinia';
import type { LogEvent } from '../types/events';

const MAX_BUFFER_SIZE = 10000;

export const useLogStore = defineStore('logs', () => {
  const logs = ref<LogEvent[]>([]);
  const filterLevel = ref<LogEvent['level'] | null>(null);
  const filterSource = ref<LogEvent['source'] | null>(null);

  const filteredLogs = computed(() => {
    return logs.value.filter((log) => {
      if (filterLevel.value && log.level !== filterLevel.value) {
        return false;
      }
      if (filterSource.value && log.source !== filterSource.value) {
        return false;
      }
      return true;
    });
  });

  function addLog(log: LogEvent): void {
    logs.value.push(log);

    if (logs.value.length > MAX_BUFFER_SIZE) {
      logs.value.shift();
    }
  }

  function clearLogs(): void {
    logs.value = [];
  }

  function setFilterLevel(level: LogEvent['level'] | null): void {
    filterLevel.value = level;
  }

  function setFilterSource(source: LogEvent['source'] | null): void {
    filterSource.value = source;
  }

  function getLogsBySource(source: LogEvent['source']): LogEvent[] {
    return logs.value.filter((log) => log.source === source);
  }

  return {
    logs,
    filteredLogs,
    filterLevel,
    filterSource,
    addLog,
    clearLogs,
    setFilterLevel,
    setFilterSource,
    getLogsBySource,
  };
});
