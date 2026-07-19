import { app } from 'electron';
import { spawn } from 'child_process';
import path from 'path';
import { DEEP_LINK_SCHEME } from './deep-link';

/**
 * Squirrel.Windows integration. Setup.exe (and later Update.exe) launch the
 * app once per lifecycle event with a `--squirrel-*` argument; the app is
 * responsible for creating/removing its own shortcuts via Update.exe and
 * must exit without showing UI. Without this handling, "installing" looks
 * like the app just launched once: files land in %LocalAppData% but no
 * Start Menu / Desktop shortcuts ever appear.
 *
 * Dependency-free port of electron-squirrel-startup — the forge `ignore`
 * rule strips node_modules from the package, so a runtime dep can't be used
 * (same reason updater.ts uses the built-in autoUpdater).
 *
 * @returns true when this launch is a Squirrel event and normal startup
 *          must be skipped (the app quits once the shortcut work finishes).
 */
export function handleSquirrelStartup(): boolean {
  if (process.platform !== 'win32') return false;

  const cmd = process.argv[1];
  if (!cmd || !cmd.startsWith('--squirrel-')) return false;

  // %LocalAppData%\pulse-desktop\Update.exe — one level above app-<version>\
  const updateExe = path.resolve(
    path.dirname(process.execPath),
    '..',
    'Update.exe'
  );
  const exeName = path.basename(process.execPath);

  const runUpdate = (args: string[]): void => {
    try {
      spawn(updateExe, args, { detached: true })
        .on('close', () => app.quit())
        .on('error', () => app.quit());
    } catch {
      app.quit();
    }
    // Safety net: never linger as a ghost process if Update.exe hangs.
    setTimeout(() => app.quit(), 15000);
  };

  switch (cmd) {
    case '--squirrel-install':
    case '--squirrel-updated':
      runUpdate([`--createShortcut=${exeName}`]);
      return true;
    case '--squirrel-uninstall':
      // Also drop the pulse:// registration so it doesn't point at a
      // deleted exe after uninstall.
      app.removeAsDefaultProtocolClient(DEEP_LINK_SCHEME);
      runUpdate([`--removeShortcut=${exeName}`]);
      return true;
    case '--squirrel-obsolete':
      app.quit();
      return true;
    default:
      // --squirrel-firstrun: the real post-install launch — run normally.
      return false;
  }
}
