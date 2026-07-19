import { contextBridge, ipcRenderer } from 'electron';

interface PickerSource {
  id: string;
  name: string;
  thumbnail: string;
  appIcon: string | null;
  isScreen: boolean;
}

contextBridge.exposeInMainWorld('screenPicker', {
  getSources: () =>
    ipcRenderer.invoke('screen-picker:sources') as Promise<PickerSource[]>,
  choose: (id: string) => ipcRenderer.send('screen-picker:choose', id),
  cancel: () => ipcRenderer.send('screen-picker:choose', null)
});
