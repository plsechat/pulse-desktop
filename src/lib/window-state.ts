import type { BrowserWindow } from 'electron';
import type { Store, WindowState } from './store';

const DEFAULT_STATE: WindowState = {
  width: 1280,
  height: 800,
  isMaximized: false,
};

export function loadWindowState(store: Store): WindowState {
  return store.get('windowState') ?? { ...DEFAULT_STATE };
}

let saveTimeout: ReturnType<typeof setTimeout> | null = null;

export function saveWindowState(win: BrowserWindow, store: Store): void {
  if (saveTimeout) clearTimeout(saveTimeout);

  saveTimeout = setTimeout(() => {
    if (win.isDestroyed()) return;

    const isMaximized = win.isMaximized();
    const bounds = win.getBounds();

    store.set('windowState', {
      x: bounds.x,
      y: bounds.y,
      width: bounds.width,
      height: bounds.height,
      isMaximized,
    });
  }, 500);
}

export function trackWindowState(win: BrowserWindow, store: Store): void {
  const save = () => saveWindowState(win, store);
  win.on('resize', save);
  win.on('move', save);
  win.on('maximize', save);
  win.on('unmaximize', save);
}
