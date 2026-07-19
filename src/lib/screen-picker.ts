import { BrowserWindow, desktopCapturer, ipcMain } from 'electron';
import type { DesktopCapturerSource } from 'electron';
import { SCREEN_PICKER_PATH, SCREEN_PICKER_PRELOAD_PATH } from './constants';

/**
 * Windows has no OS picker that also captures loopback audio, so getDisplayMedia
 * would otherwise auto-grab the first screen. This shows an in-app modal that
 * lets the user choose which screen/window to share, and resolves with the
 * chosen source (or null if cancelled).
 */
export async function pickScreenSource(
  parent: BrowserWindow
): Promise<DesktopCapturerSource | null> {
  const sources = await desktopCapturer.getSources({
    types: ['screen', 'window'],
    thumbnailSize: { width: 320, height: 180 },
    fetchWindowIcons: true
  });

  const serialized = sources.map((s) => ({
    id: s.id,
    name: s.name,
    thumbnail: s.thumbnail.toDataURL(),
    appIcon:
      s.appIcon && !s.appIcon.isEmpty() ? s.appIcon.toDataURL() : null,
    isScreen: s.id.startsWith('screen:')
  }));

  return new Promise((resolve) => {
    const picker = new BrowserWindow({
      parent,
      modal: true,
      width: 780,
      height: 580,
      resizable: false,
      minimizable: false,
      maximizable: false,
      fullscreenable: false,
      title: 'Choose what to share',
      backgroundColor: '#313338',
      autoHideMenuBar: true,
      webPreferences: {
        preload: SCREEN_PICKER_PRELOAD_PATH,
        contextIsolation: true,
        nodeIntegration: false
      }
    });

    let settled = false;
    const finish = (result: DesktopCapturerSource | null): void => {
      if (settled) return;
      settled = true;
      ipcMain.removeHandler('screen-picker:sources');
      ipcMain.removeAllListeners('screen-picker:choose');
      if (!picker.isDestroyed()) picker.close();
      resolve(result);
    };

    // Renderer pulls the source list once the window has loaded.
    ipcMain.handleOnce('screen-picker:sources', () => serialized);

    // Renderer sends the chosen id, or null to cancel.
    ipcMain.once('screen-picker:choose', (_event, sourceId: string | null) => {
      const chosen = sourceId
        ? (sources.find((s) => s.id === sourceId) ?? null)
        : null;
      finish(chosen);
    });

    // Closing the window (X / Esc) counts as cancel.
    picker.on('closed', () => finish(null));

    void picker.loadFile(SCREEN_PICKER_PATH);
  });
}
