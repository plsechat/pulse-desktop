import { contextBridge, ipcRenderer } from 'electron';

contextBridge.exposeInMainWorld('pulseDesktop', {
  isElectron: true,
  platform: process.platform,
  connectToServer: (url: string) => ipcRenderer.invoke('connect-to-server', url),
  disconnectServer: () => ipcRenderer.invoke('disconnect-server'),
  getSettings: () => ipcRenderer.invoke('get-settings'),
  updateSetting: (key: string, value: unknown) => ipcRenderer.invoke('update-setting', key, value),

  // Unread count on the dock / taskbar
  setBadgeCount: (count: number) => ipcRenderer.invoke('badge:set', count),

  // Window chrome: 'overlay' = frameless with floating window controls (the
  // client must provide drag regions), 'native' = OS title bar.
  getWindowChrome: () => ipcRenderer.invoke('chrome:get') as Promise<'overlay' | 'native'>,
  setTitleBarOverlay: (overlay: { color?: string; symbolColor?: string }) =>
    ipcRenderer.invoke('titlebar:set-overlay', overlay),

  // Launch Pulse automatically at system login
  getStartupEnabled: () => ipcRenderer.invoke('startup:get') as Promise<boolean>,
  setStartupEnabled: (enabled: boolean) => ipcRenderer.invoke('startup:set', enabled) as Promise<void>,

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
