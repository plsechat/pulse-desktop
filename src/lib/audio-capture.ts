import { execSync } from 'child_process';
import { existsSync } from 'fs';
import { app } from 'electron';
import path from 'path';

// State for the current capture session
let savedDefaultDeviceId: string | null = null;

/** Locate the pulse-audio-helper binary */
function getHelperPath(): string {
  // Packaged app: resources/pulse-audio-helper
  const resourcePath = path.join(process.resourcesPath || app.getAppPath(), 'pulse-audio-helper');
  if (existsSync(resourcePath)) return resourcePath;

  // Dev fallback: built alongside the driver
  const devPath = path.join(app.getAppPath(), 'native', 'audio-driver', 'build', 'pulse-audio-helper');
  if (existsSync(devPath)) return devPath;

  return '';
}

/** Run the helper binary and return stdout, or null on failure */
function runHelper(...args: string[]): string | null {
  const helperPath = getHelperPath();
  if (!helperPath) {
    console.warn('[audio-capture] pulse-audio-helper not found');
    return null;
  }

  try {
    const result = execSync(`"${helperPath}" ${args.join(' ')}`, {
      timeout: 10000,
      encoding: 'utf-8',
    });
    return result.trim();
  } catch (err) {
    console.error('[audio-capture] helper command failed:', args.join(' '), err);
    return null;
  }
}

/** Check if system audio capture is available (macOS + driver active) */
export function canCaptureSystemAudio(): boolean {
  if (process.platform !== 'darwin') return false;

  const helperPath = getHelperPath();
  if (!helperPath) return false;

  try {
    execSync(`"${helperPath}" detect`, { timeout: 5000 });
    return true; // exit code 0 = driver found
  } catch {
    return false; // exit code 1 = not found
  }
}

/**
 * Start system audio capture for screen sharing.
 *
 * Uses the `start-capture` command which performs the full flow in a single
 * process to avoid device ID staleness between separate CLI invocations:
 * 1. Save the current default output device
 * 2. Destroy any leftover aggregate
 * 3. Create a multi-output aggregate device (real output + Pulse Audio)
 * 4. Set the aggregate as default output
 * 5. Return the saved device ID and name
 */
export function startSystemAudioCapture(): { pulseDeviceUID: string; realOutputDeviceName: string } | null {
  try {
    const result = runHelper('start-capture');
    if (!result) {
      console.error('[audio-capture] start-capture failed');
      return null;
    }

    // Output format: "savedDeviceId|realOutputName"
    const [deviceIdStr, deviceName] = result.split('|');
    savedDefaultDeviceId = deviceIdStr;

    console.log(`[audio-capture] Capture started, saved device: ${deviceName} (ID: ${deviceIdStr})`);

    return {
      pulseDeviceUID: 'com.pulse.audio.device',
      realOutputDeviceName: deviceName,
    };
  } catch (err) {
    console.error('[audio-capture] Failed to start capture:', err);
    stopSystemAudioCapture();
    return null;
  }
}

/**
 * Stop system audio capture and restore the original output device.
 *
 * Uses the `stop-capture` command which destroys the aggregate device
 * and restores the saved default output device.
 */
export function stopSystemAudioCapture(): void {
  try {
    if (savedDefaultDeviceId !== null) {
      console.log(`[audio-capture] Stopping capture, restoring device ID: ${savedDefaultDeviceId}`);
      runHelper('stop-capture', savedDefaultDeviceId);
      savedDefaultDeviceId = null;
    } else {
      // No saved device — just destroy the aggregate
      runHelper('destroy-aggregate');
    }
  } catch (err) {
    console.error('[audio-capture] Error during cleanup:', err);
    savedDefaultDeviceId = null;
  }
}
