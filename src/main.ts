import { app, BrowserWindow, ipcMain, shell, net, Menu } from 'electron';
import path from 'path';
import { Store } from './lib/store';
import { loadWindowState, trackWindowState } from './lib/window-state';
import { setupPermissions, requestMediaAccess } from './lib/permissions';
import { createTray, destroyTray } from './lib/tray';
import { APP_NAME, PRELOAD_PATH, SERVER_SELECTOR_PATH } from './lib/constants';
import { getDriverStatus, installDriver, uninstallDriver } from './lib/audio-driver';
import { canCaptureSystemAudio, startSystemAudioCapture, stopSystemAudioCapture } from './lib/audio-capture';
import { initAutoUpdates } from './lib/updater';
import { setUnreadBadge } from './lib/badge';
import { parseDeepLink, targetUrl, DEEP_LINK_SCHEME } from './lib/deep-link';
import { handleSquirrelStartup } from './lib/squirrel-events';

// Squirrel.Windows install/update/uninstall launches — create/remove
// shortcuts and exit instead of starting the real app. MUST run before any
// other startup work (single-instance lock, protocol registration).
const isSquirrelStartup = handleSquirrelStartup();

// GPU acceleration: force-enable the paths Chromium sometimes leaves off
// on desktop GPUs (raster + zero-copy upload, and don't let a stale
// driver blocklist entry silently drop the app to software compositing —
// the symptom is whole-app sluggishness). Must run before app ready.
app.commandLine.appendSwitch('enable-gpu-rasterization');
app.commandLine.appendSwitch('enable-zero-copy');
app.commandLine.appendSwitch('ignore-gpu-blocklist');

const store = new Store();
let mainWindow: BrowserWindow | null = null;
let isQuitting = false;
// Chrome style of the CURRENT window (set at creation; needs a window
// rebuild to change, see navigateToServer).
let windowIsFrameless = false;

// Frameless chrome needs the web client to provide its own drag regions —
// shipped with server 0.2.5. Older servers keep the native title bar so the
// window stays movable (a frameless window with no drag region is stuck).
const MIN_FRAMELESS_SERVER = [0, 2, 5] as const;

function serverSupportsFrameless(version: string | undefined): boolean {
  if (!version) return false;
  const parts = version.split('.').map((n) => parseInt(n, 10));
  if (parts.length < 3 || parts.some(Number.isNaN)) return false;
  for (let i = 0; i < 3; i++) {
    if (parts[i] > MIN_FRAMELESS_SERVER[i]) return true;
    if (parts[i] < MIN_FRAMELESS_SERVER[i]) return false;
  }
  return true; // exactly the minimum
}

/**
 * Frameless (overlay window controls) is Windows-only for now: macOS needs
 * a layout rework first (traffic lights would cover the client's top-left
 * home button), and Linux lacks reliable overlay support. The server
 * selector is our own page (always drag-ready), so no-server also qualifies.
 */
function wantsFramelessChrome(): boolean {
  if (process.platform !== 'win32') return false;
  const serverUrl = store.get('serverUrl');
  if (!serverUrl) return true;
  return serverSupportsFrameless(store.get('serverVersion'));
}

function disconnectServer(): void {
  store.delete('serverUrl');
  store.delete('serverName');
  if (mainWindow && !mainWindow.isDestroyed()) {
    mainWindow.loadFile(SERVER_SELECTOR_PATH);
  }
}

// Deep link (pulse://) queued when it arrives before the window exists.
let pendingDeepLink: string | null = null;

/**
 * Load a server page into the window, rebuilding the window first when the
 * desired chrome (frameless vs native) changed — titleBarStyle is fixed at
 * creation time. The tray is rebuilt too (its menu closes over the window).
 */
function navigateToServer(pageUrl: string): void {
  if (!mainWindow || mainWindow.isDestroyed()) return;

  if (wantsFramelessChrome() !== windowIsFrameless) {
    const old = mainWindow;
    mainWindow = createWindow();
    destroyTray();
    createTray(mainWindow, store, disconnectServer);
    // Destroy AFTER the new window exists so window-all-closed can't fire
    // an app.quit in the gap. destroy() skips the close event, so the
    // minimize-to-tray close handler doesn't swallow it.
    old.destroy();
    // createWindow already started loading the bare serverUrl; honor a more
    // specific page (e.g. an invite deep link).
    if (pageUrl !== store.get('serverUrl')) {
      mainWindow.loadURL(pageUrl);
    }
  } else {
    mainWindow.loadURL(pageUrl);
  }
}

/** Handle a pulse:// deep link — switch to the server and open the invite. */
function handleDeepLink(rawUrl: string): void {
  const parsed = parseDeepLink(rawUrl);
  if (!parsed) return;

  store.set('serverUrl', parsed.serverUrl);
  const target = targetUrl(parsed);

  // Resolve the target server's version first (it gates window chrome);
  // tolerate failure — unknown version just means native chrome.
  void app
    .whenReady()
    .then(() =>
      net.fetch(`${parsed.serverUrl}/info`, {
        signal: AbortSignal.timeout(5000),
      })
    )
    .then((r) => (r.ok ? (r.json() as Promise<{ version?: string }>) : null))
    .catch(() => null)
    .then((info) => {
      store.set('serverVersion', info?.version ?? '');
      if (mainWindow && !mainWindow.isDestroyed()) {
        if (mainWindow.isMinimized()) mainWindow.restore();
        mainWindow.show();
        mainWindow.focus();
        navigateToServer(target);
      } else {
        // Cold start — the window doesn't exist yet; consume once built.
        pendingDeepLink = target;
      }
    });
}

function createWindow(): BrowserWindow {
  const windowState = loadWindowState(store);
  const frameless = wantsFramelessChrome();
  windowIsFrameless = frameless;

  const win = new BrowserWindow({
    title: APP_NAME,
    width: windowState.width,
    height: windowState.height,
    x: windowState.x,
    y: windowState.y,
    minWidth: 940,
    minHeight: 600,
    backgroundColor: '#313338',
    show: false,
    autoHideMenuBar: true,
    // Frameless: no title bar strip — the client's top bar doubles as the
    // drag region and the OS draws min/max/close floating over it. The 48px
    // band matches the client's h-12 top bar; the client re-colors the
    // controls to its theme via the titlebar:set-overlay IPC.
    ...(frameless && {
      titleBarStyle: 'hidden' as const,
      titleBarOverlay: {
        color: '#16161a',
        symbolColor: '#a1a1aa',
        height: 48,
      },
    }),
    webPreferences: {
      preload: PRELOAD_PATH,
      contextIsolation: true,
      nodeIntegration: false,
      autoplayPolicy: 'no-user-gesture-required',
      // A voice/chat client must keep ticking while minimized to tray;
      // throttled timers also make the window feel unresponsive right
      // after restore.
      backgroundThrottling: false,
    },
    icon: path.join(__dirname, '..', 'assets', 'icon.png'),
  });

  if (windowState.isMaximized) {
    win.maximize();
  }

  // Show window when ready
  win.once('ready-to-show', () => {
    win.show();
  });

  // Track window state changes
  trackWindowState(win, store);

  // Setup media permissions (also installs the Windows screen-share picker)
  setupPermissions(win);

  // Handle external links — open in default browser
  win.webContents.setWindowOpenHandler(({ url }) => {
    // Block dangerous URL schemes
    const lower = url.toLowerCase().trim();
    if (lower.startsWith('javascript:') || lower.startsWith('data:') || lower.startsWith('vbscript:')) {
      console.warn('[security] Blocked dangerous URL:', url);
      return { action: 'deny' };
    }

    try {
      const linkUrl = new URL(url);
      const serverUrl = store.get('serverUrl');

      // Allow same-origin navigation (e.g. OAuth popups)
      if (serverUrl) {
        const server = new URL(serverUrl);
        if (linkUrl.origin === server.origin) {
          return { action: 'allow' };
        }
      }

      // Only allow http/https external links
      if (linkUrl.protocol === 'http:' || linkUrl.protocol === 'https:') {
        shell.openExternal(url);
      }
    } catch {
      console.warn('[security] Blocked invalid URL:', url);
    }

    return { action: 'deny' };
  });

  // If the saved server can't be reached, don't strand the user on a raw
  // Chromium error page — fall back to the server selector with the reason.
  win.webContents.on(
    'did-fail-load',
    (_event, errorCode, errorDescription, validatedURL, isMainFrame) => {
      // -3 (ABORTED) fires on normal redirects/navigations; ignore it and
      // any sub-frame failure.
      if (!isMainFrame || errorCode === -3) return;

      const serverUrl = store.get('serverUrl');
      if (!serverUrl) return; // already on the selector

      // Only react to the SERVER load failing, never the local selector file
      // (guards against a reload loop).
      try {
        if (!validatedURL.startsWith(new URL(serverUrl).origin)) return;
      } catch {
        return;
      }

      console.warn(
        `[load] server unreachable (${errorCode} ${errorDescription}) for ${validatedURL}; returning to selector`
      );
      win.loadFile(SERVER_SELECTOR_PATH, {
        search: `error=${encodeURIComponent(`Couldn't reach ${serverUrl} — it may be offline.`)}`
      });
    }
  );

  // Minimize to tray on close instead of quitting (opt-in)
  win.on('close', (event) => {
    if (isQuitting) return;

    const minimizeToTray = store.get('minimizeToTray');
    if (minimizeToTray) {
      event.preventDefault();
      win.hide();
    }
  });

  // Load server or selector
  const serverUrl = store.get('serverUrl');
  if (serverUrl) {
    win.loadURL(serverUrl);
  } else {
    win.loadFile(SERVER_SELECTOR_PATH);
  }

  return win;
}

// IPC Handlers
function setupIpcHandlers(): void {
  ipcMain.handle('connect-to-server', async (_event, url: string) => {
    // Normalize URL
    let serverUrl = url.trim();
    if (!serverUrl.startsWith('http://') && !serverUrl.startsWith('https://')) {
      serverUrl = `https://${serverUrl}`;
    }
    // Remove trailing slash
    serverUrl = serverUrl.replace(/\/+$/, '');

    // Validate by fetching /info (bounded so the button can't hang forever)
    try {
      const response = await net.fetch(`${serverUrl}/info`, {
        signal: AbortSignal.timeout(10000)
      });
      if (!response.ok) {
        return { success: false, error: `Server returned ${response.status}` };
      }

      const data = (await response.json()) as { name?: string; version?: string };

      store.set('serverUrl', serverUrl);
      store.set('serverName', data.name ?? 'Pulse Server');
      store.set('serverVersion', data.version ?? '');

      navigateToServer(serverUrl);

      return { success: true, name: data.name, version: data.version };
    } catch (err) {
      if (
        err instanceof Error &&
        (err.name === 'TimeoutError' || err.name === 'AbortError')
      ) {
        return { success: false, error: 'Server did not respond (timed out)' };
      }
      const message = err instanceof Error ? err.message : 'Connection failed';
      return { success: false, error: message };
    }
  });

  ipcMain.handle('disconnect-server', () => {
    disconnectServer();
  });

  ipcMain.handle('get-settings', () => {
    return store.getAll();
  });

  ipcMain.handle('update-setting', (_event, key: string, value: unknown) => {
    if (key === 'minimizeToTray' && typeof value === 'boolean') {
      store.set('minimizeToTray', value);
    }
  });

  // Audio driver management (macOS)
  ipcMain.handle('audio-driver:status', () => getDriverStatus());
  ipcMain.handle('audio-driver:install', () => installDriver());
  ipcMain.handle('audio-driver:uninstall', () => uninstallDriver());

  // Audio capture lifecycle (macOS)
  ipcMain.handle('audio-capture:available', () => canCaptureSystemAudio());
  ipcMain.handle('audio-capture:start', () => startSystemAudioCapture());
  ipcMain.handle('audio-capture:stop', () => {
    stopSystemAudioCapture();
  });

  // Unread count on the dock / taskbar
  ipcMain.handle('badge:set', (_event, count: number) => {
    setUnreadBadge(mainWindow, count);
  });

  // Window chrome — lets the client know it must provide drag regions, and
  // lets it recolor the overlay window controls to match its theme.
  ipcMain.handle('chrome:get', () =>
    windowIsFrameless ? 'overlay' : 'native'
  );
  ipcMain.handle(
    'titlebar:set-overlay',
    (_event, overlay: { color?: string; symbolColor?: string }) => {
      if (process.platform !== 'win32' || !windowIsFrameless) return;
      if (!mainWindow || mainWindow.isDestroyed()) return;
      try {
        mainWindow.setTitleBarOverlay({
          ...(typeof overlay?.color === 'string' && { color: overlay.color }),
          ...(typeof overlay?.symbolColor === 'string' && {
            symbolColor: overlay.symbolColor,
          }),
        });
      } catch (err) {
        console.warn('[chrome] setTitleBarOverlay failed:', err);
      }
    }
  );

  // Launch Pulse at system login
  ipcMain.handle('startup:get', () => app.getLoginItemSettings().openAtLogin);
  ipcMain.handle('startup:set', (_event, enabled: boolean) => {
    app.setLoginItemSettings({ openAtLogin: !!enabled });
  });
}

// Single-instance lock — a chat/voice client must not run twice (a second
// window means a second WebSocket + voice connection to the same server).
// The second launch hands focus to the existing window and exits. Skipped
// during Squirrel events so the install-time run doesn't hold the lock
// against the real post-install (--squirrel-firstrun) launch.
const gotSingleInstanceLock =
  isSquirrelStartup || app.requestSingleInstanceLock();
if (!gotSingleInstanceLock) {
  app.quit();
}

app.on('second-instance', (_event, argv) => {
  // Windows/Linux deliver a pulse:// deep link as an argv entry on re-launch.
  const link = argv.find((a) => a.startsWith(`${DEEP_LINK_SCHEME}://`));
  if (link) {
    handleDeepLink(link);
    return;
  }
  if (mainWindow && !mainWindow.isDestroyed()) {
    if (mainWindow.isMinimized()) mainWindow.restore();
    if (!mainWindow.isVisible()) mainWindow.show();
    mainWindow.focus();
  }
});

// macOS delivers deep links via open-url (can fire before the app is ready).
app.on('open-url', (event, url) => {
  event.preventDefault();
  handleDeepLink(url);
});

// Register pulse:// so the OS routes those links to this app. Re-running on
// every launch keeps the registration pointing at the current exe across
// Squirrel updates (the versioned app-<version> path changes each release).
if (!isSquirrelStartup) {
  app.setAsDefaultProtocolClient(DEEP_LINK_SCHEME);
}

// App lifecycle
app.on('before-quit', () => {
  isQuitting = true;
  destroyTray();
  // Ensure audio capture is cleaned up (restore default output device)
  stopSystemAudioCapture();
});

app.whenReady().then(async () => {
  // Squirrel event runs and losing second instances quit — don't build UI.
  if (isSquirrelStartup || !gotSingleInstanceLock) return;

  // Request macOS system-level mic/camera access BEFORE creating the window
  await requestMediaAccess();

  // Application menu (ensures Cmd+Q works on macOS)
  const template: Electron.MenuItemConstructorOptions[] = [
    {
      label: APP_NAME,
      submenu: [
        { role: 'about' },
        { type: 'separator' },
        { role: 'hide' },
        { role: 'hideOthers' },
        { role: 'unhide' },
        { type: 'separator' },
        { role: 'quit' },
      ],
    },
    { role: 'editMenu' },
    { role: 'viewMenu' },
    { role: 'windowMenu' },
  ];
  Menu.setApplicationMenu(Menu.buildFromTemplate(template));

  setupIpcHandlers();
  mainWindow = createWindow();
  createTray(mainWindow, store, disconnectServer);

  // Self-update (packaged mac/win only; no-ops in dev and on Linux)
  initAutoUpdates();

  // Consume a cold-start deep link: Windows/Linux pass it in argv; macOS may
  // have queued it via open-url before the window existed.
  const argvLink = process.argv.find((a) =>
    a.startsWith(`${DEEP_LINK_SCHEME}://`)
  );
  if (argvLink) {
    handleDeepLink(argvLink);
  } else if (pendingDeepLink && mainWindow && !mainWindow.isDestroyed()) {
    mainWindow.loadURL(pendingDeepLink);
    pendingDeepLink = null;
  }

  app.on('activate', () => {
    // macOS: re-create window when dock icon clicked
    if (BrowserWindow.getAllWindows().length === 0) {
      mainWindow = createWindow();
    } else if (mainWindow && !mainWindow.isDestroyed()) {
      mainWindow.show();
    }
  });
});

app.on('window-all-closed', () => {
  app.quit();
});
