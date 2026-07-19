import { app, autoUpdater, dialog } from 'electron';
import { UPDATE_REPO } from './constants';

/**
 * Self-update via Electron's built-in Squirrel autoUpdater, fed by the free
 * update.electronjs.org service (public GitHub repos only).
 *
 * Platform support mirrors Squirrel:
 *   - macOS  (Squirrel.Mac)      REQUIRES a code-signed .app; unsigned builds
 *                                throw at setFeedURL and are skipped silently.
 *   - Windows (Squirrel.Windows) needs the MakerSquirrel installer artifacts.
 *   - Linux                      unsupported; distro packages handle updates.
 *
 * No-ops in development (unpackaged) so `bun run dev` never phones home.
 */
const SIX_HOURS = 6 * 60 * 60 * 1000;

export function initAutoUpdates(): void {
  if (!app.isPackaged) return; // dev / bare `electron .`
  if (process.platform === 'linux') return; // Squirrel has no Linux backend

  const feedUrl = `https://update.electronjs.org/${UPDATE_REPO}/${process.platform}/${app.getVersion()}`;

  try {
    autoUpdater.setFeedURL({ url: feedUrl });
  } catch (err) {
    // macOS throws here when the app isn't code-signed — nothing we can do,
    // so stay silent rather than nagging the user on every launch.
    console.warn(
      '[updater] disabled:',
      err instanceof Error ? err.message : err
    );
    return;
  }

  autoUpdater.on('error', (err) => {
    console.error('[updater] error:', err);
  });

  autoUpdater.on('update-downloaded', (_event, _notes, releaseName) => {
    void dialog
      .showMessageBox({
        type: 'info',
        buttons: ['Restart now', 'Later'],
        defaultId: 0,
        cancelId: 1,
        title: 'Update ready',
        message: releaseName || 'A new version of Pulse is available',
        detail: 'It has been downloaded. Restart to finish updating.'
      })
      .then(({ response }) => {
        if (response === 0) autoUpdater.quitAndInstall();
      });
  });

  const check = (): void => {
    try {
      autoUpdater.checkForUpdates();
    } catch (err) {
      console.warn('[updater] check failed:', err);
    }
  };

  check(); // on launch
  setInterval(check, SIX_HOURS); // and periodically thereafter
}
