import { app } from 'electron';
import { execSync } from 'child_process';
import { existsSync } from 'fs';
import path from 'path';

const DRIVER_NAME = 'PulseAudio.driver';
const SYSTEM_DRIVER_PATH = `/Library/Audio/Plug-Ins/HAL/${DRIVER_NAME}`;

/** Path to the driver bundled inside the app's resources */
function getBundledDriverPath(): string {
  // In packaged app: resources/PulseAudio.driver
  // In dev: native/audio-driver/build/PulseAudio.driver
  const resourcePath = path.join(process.resourcesPath || app.getAppPath(), DRIVER_NAME);
  if (existsSync(resourcePath)) return resourcePath;

  // Dev fallback
  const devPath = path.join(app.getAppPath(), 'native', 'audio-driver', 'build', DRIVER_NAME);
  if (existsSync(devPath)) return devPath;

  return '';
}

/** Locate the pulse-audio-helper binary */
export function getHelperPath(): string {
  // Packaged app: resources/pulse-audio-helper
  const resourcePath = path.join(process.resourcesPath || app.getAppPath(), 'pulse-audio-helper');
  if (existsSync(resourcePath)) return resourcePath;

  // Dev fallback
  const devPath = path.join(app.getAppPath(), 'native', 'audio-driver', 'build', 'pulse-audio-helper');
  if (existsSync(devPath)) return devPath;

  return '';
}

/** Check if the driver bundle exists in /Library/Audio/Plug-Ins/HAL/ */
export function isDriverFileInstalled(): boolean {
  return existsSync(SYSTEM_DRIVER_PATH);
}

/** Check if the driver is active (device visible to CoreAudio) */
export function isDriverActive(): boolean {
  if (process.platform !== 'darwin') return false;

  const helperPath = getHelperPath();
  if (!helperPath) return false;

  try {
    execSync(`"${helperPath}" detect`, { timeout: 5000 });
    return true; // exit code 0 = found
  } catch {
    return false; // exit code 1 = not found
  }
}

/** Install the driver to /Library/Audio/Plug-Ins/HAL/ with admin privileges */
export async function installDriver(): Promise<{ success: boolean; error?: string }> {
  if (process.platform !== 'darwin') {
    return { success: false, error: 'Only supported on macOS' };
  }

  const sourcePath = getBundledDriverPath();
  if (!sourcePath) {
    return { success: false, error: 'Driver bundle not found in app resources' };
  }

  try {
    // Use osascript to get admin privileges for the copy
    const script = `
      do shell script "rm -rf '${SYSTEM_DRIVER_PATH}' && cp -R '${sourcePath}' '${SYSTEM_DRIVER_PATH}' && codesign --force --sign - '${SYSTEM_DRIVER_PATH}' && killall coreaudiod 2>/dev/null || true" with administrator privileges
    `;
    execSync(`osascript -e '${script}'`, { timeout: 30000 });

    // Wait for coreaudiod to restart and detect the driver
    await new Promise(resolve => setTimeout(resolve, 2000));

    return { success: true };
  } catch (err) {
    const message = err instanceof Error ? err.message : 'Installation failed';
    // User cancelled the admin dialog
    if (message.includes('User canceled') || message.includes('-128')) {
      return { success: false, error: 'Installation cancelled by user' };
    }
    return { success: false, error: message };
  }
}

/** Uninstall the driver from /Library/Audio/Plug-Ins/HAL/ with admin privileges */
export async function uninstallDriver(): Promise<{ success: boolean; error?: string }> {
  if (process.platform !== 'darwin') {
    return { success: false, error: 'Only supported on macOS' };
  }

  if (!isDriverFileInstalled()) {
    return { success: true }; // Already uninstalled
  }

  try {
    const script = `
      do shell script "rm -rf '${SYSTEM_DRIVER_PATH}' && killall coreaudiod 2>/dev/null || true" with administrator privileges
    `;
    execSync(`osascript -e '${script}'`, { timeout: 30000 });

    await new Promise(resolve => setTimeout(resolve, 2000));

    return { success: true };
  } catch (err) {
    const message = err instanceof Error ? err.message : 'Uninstallation failed';
    if (message.includes('User canceled') || message.includes('-128')) {
      return { success: false, error: 'Uninstallation cancelled by user' };
    }
    return { success: false, error: message };
  }
}

/** Get the full driver status */
export function getDriverStatus(): { supported: boolean; fileInstalled: boolean; active: boolean } {
  const supported = process.platform === 'darwin';
  return {
    supported,
    fileInstalled: supported ? isDriverFileInstalled() : false,
    active: supported ? isDriverActive() : false,
  };
}
