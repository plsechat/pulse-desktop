import { contextBridge, ipcRenderer } from 'electron';

contextBridge.exposeInMainWorld('pulseDesktop', {
  isElectron: true,
  platform: process.platform,
  connectToServer: (url: string) => ipcRenderer.invoke('connect-to-server', url),
  disconnectServer: () => ipcRenderer.invoke('disconnect-server'),
  getSettings: () => ipcRenderer.invoke('get-settings'),
  updateSetting: (key: string, value: unknown) => ipcRenderer.invoke('update-setting', key, value),

  // macOS audio driver management
  audioDriver: {
    getStatus: () => ipcRenderer.invoke('audio-driver:status') as Promise<{ supported: boolean; fileInstalled: boolean; active: boolean }>,
    install: () => ipcRenderer.invoke('audio-driver:install') as Promise<{ success: boolean; error?: string }>,
    uninstall: () => ipcRenderer.invoke('audio-driver:uninstall') as Promise<{ success: boolean; error?: string }>,
  },

  // macOS system audio capture for screen sharing
  audioCapture: {
    isAvailable: () => ipcRenderer.invoke('audio-capture:available') as Promise<boolean>,
    start: () => ipcRenderer.invoke('audio-capture:start') as Promise<{ pulseDeviceUID: string; realOutputDeviceName: string } | null>,
    stop: () => ipcRenderer.invoke('audio-capture:stop') as Promise<void>,
  },
});
