import { desktopCapturer, systemPreferences, dialog, shell } from 'electron';
import type { BrowserWindow } from 'electron';
import { pickScreenSource } from './screen-picker';
import type { Store } from './store';

/** Request macOS system-level mic/camera/screen access. */
export async function requestMediaAccess(store: Store): Promise<void> {
  if (process.platform !== 'darwin') return;

  // Mic + Camera — check status first, only prompt if not already granted
  const micCurrent = systemPreferences.getMediaAccessStatus('microphone');
  const camCurrent = systemPreferences.getMediaAccessStatus('camera');

  if (micCurrent !== 'granted') {
    await systemPreferences.askForMediaAccess('microphone');
  }
  if (camCurrent !== 'granted') {
    await systemPreferences.askForMediaAccess('camera');
  }

  console.log(`[permissions] microphone: ${micCurrent}, camera: ${camCurrent}`);

  // Screen Recording — no askForMediaAccess API, must be triggered by usage.
  // Check current status and prompt if needed.
  const screenStatus = systemPreferences.getMediaAccessStatus('screen');
  console.log(`[permissions] screen recording: ${screenStatus}`);

  if (screenStatus !== 'granted') {
    // Trigger the macOS permission prompt by attempting a screen capture.
    // On first run, this causes macOS to show "Pulse would like to record
    // the screen" dialog (on macOS 14+) or add it to System Settings.
    try {
      await desktopCapturer.getSources({ types: ['screen'], thumbnailSize: { width: 1, height: 1 } });
    } catch {
      // Expected to fail if permission not yet granted
    }

    // Re-check after the attempt
    const updatedStatus = systemPreferences.getMediaAccessStatus('screen');
    console.log(`[permissions] screen recording (after trigger): ${updatedStatus}`);

    // Guide the user to System Settings — but only once. The OS prompt
    // above is one-shot; after that only the Settings pane can grant it,
    // and the client's screen-share button surfaces the failure in context.
    if (updatedStatus !== 'granted' && !store.get('screenPermissionGuidanceShown')) {
      store.set('screenPermissionGuidanceShown', true);
      const { response } = await dialog.showMessageBox({
        type: 'warning',
        title: 'Screen Recording Permission Required',
        message: 'Pulse needs Screen & System Audio Recording permission for screen sharing and system audio capture.',
        detail: 'Please grant access in System Settings > Privacy & Security > Screen & System Audio Recording, then restart Pulse.',
        buttons: ['Open System Settings', 'Later'],
        defaultId: 0,
      });

      if (response === 0) {
        // Open the Screen Recording pane in System Settings
        shell.openExternal('x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture');
      }
    }
  }
}

export function setupPermissions(win: BrowserWindow): void {
  const session = win.webContents.session;

  // Grant all permission requests from the loaded Pulse server.
  // Electron's default without a handler is to auto-approve, but we set one
  // explicitly to ensure media, display-capture, etc. are always granted.
  const ALLOWED_PERMISSIONS = new Set([
    'media', 'display-capture', 'mediaKeySystem',
    'midi', 'midiSysex', 'notifications', 'fullscreen'
  ]);

  session.setPermissionRequestHandler((_webContents, permission, callback) => {
    const granted = ALLOWED_PERMISSIONS.has(permission);
    console.log(`[permissions] request: ${permission} -> ${granted ? 'granted' : 'denied'}`);
    callback(granted);
  });

  session.setPermissionCheckHandler((_webContents, permission) => {
    const granted = ALLOWED_PERMISSIONS.has(permission);
    console.log(`[permissions] check: ${permission} -> ${granted ? 'granted' : 'denied'}`);
    return granted;
  });

  // Required for getDisplayMedia() — screen sharing in voice channels.
  //
  // A handler MUST be registered on every platform: with a null handler
  // Electron's JS layer skips the `useSystemPicker` wrapper entirely (see
  // lib/browser/api/session.ts) and getDisplayMedia rejects "Not supported".
  //
  // macOS: ask for the native OS picker (SCContentSharingPicker, macOS 15+).
  // When it is available Electron never invokes our handler; otherwise the
  // in-app picker below is the fallback. System audio on macOS comes from
  // the Pulse virtual audio device (the client swaps it in), not from here.
  // Windows: no loopback-capable OS picker exists, so always show our own
  // and forward the chosen source with system (loopback) audio.
  // Linux: in-app picker, video only.
  session.setDisplayMediaRequestHandler(
    async (_request, callback) => {
      const source = await pickScreenSource(win);
      if (source) {
        callback({
          video: source,
          ...(process.platform === 'win32' && { audio: 'loopback' as const }),
        });
      } else {
        // Cancelled — deny so the renderer's getDisplayMedia rejects cleanly
        // instead of hanging forever.
        callback({} as { video: Electron.DesktopCapturerSource });
      }
    },
    { useSystemPicker: process.platform === 'darwin' }
  );
}
