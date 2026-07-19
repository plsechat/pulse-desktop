import { app, nativeImage } from 'electron';
import type { BrowserWindow, NativeImage } from 'electron';

/**
 * Unread indicator on the dock / taskbar.
 *
 *   - macOS + Linux (Unity): `app.setBadgeCount(n)` shows the actual number.
 *   - Windows: has no numeric app badge, so we set a small red-dot taskbar
 *     overlay icon when there's anything unread, and clear it otherwise.
 */

let redDot: NativeImage | null = null;

/** A 16×16 red circle on a transparent background, built once at runtime. */
function getRedDot(): NativeImage {
  if (redDot) return redDot;

  const size = 16;
  const buf = Buffer.alloc(size * size * 4);
  const center = (size - 1) / 2;
  const radius = size / 2 - 0.5;

  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const i = (y * size + x) * 4;
      const inside =
        (x - center) ** 2 + (y - center) ** 2 <= radius * radius;
      // createFromBitmap expects BGRA; pure red is B=0 G=0 R=255.
      buf[i] = 0;
      buf[i + 1] = 0;
      buf[i + 2] = 255;
      buf[i + 3] = inside ? 255 : 0;
    }
  }

  redDot = nativeImage.createFromBitmap(buf, { width: size, height: size });
  return redDot;
}

export function setUnreadBadge(
  win: BrowserWindow | null,
  count: number
): void {
  const n = Number.isFinite(count) && count > 0 ? Math.floor(count) : 0;

  if (process.platform === 'win32') {
    if (!win || win.isDestroyed()) return;
    win.setOverlayIcon(n > 0 ? getRedDot() : null, n > 0 ? `${n} unread` : '');
    return;
  }

  // macOS + Linux: numeric badge (0 clears it).
  app.setBadgeCount(n);
}
