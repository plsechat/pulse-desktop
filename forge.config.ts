import type { ForgeConfig } from '@electron-forge/shared-types';
import { MakerZIP } from '@electron-forge/maker-zip';
import { MakerDMG } from '@electron-forge/maker-dmg';
import { MakerSquirrel } from '@electron-forge/maker-squirrel';
import { MakerDeb } from '@electron-forge/maker-deb';
import { MakerRpm } from '@electron-forge/maker-rpm';
import { existsSync } from 'fs';
import { execFileSync } from 'child_process';
import path from 'path';

// macOS audio driver + helper — only include if built
const hasNativeDriver = existsSync('./native/audio-driver/build/PulseAudio.driver');
const hasAudioHelper = existsSync('./native/audio-driver/build/pulse-audio-helper');

const config: ForgeConfig = {
  hooks: {
    // Ad-hoc sign the packaged macOS bundle when no signing identity is
    // configured. This must happen here, between package and make: the
    // ZIP/DMG makers archive out/<name>/Pulse.app, so anything signed
    // after `make` never reaches the artifacts users download. An
    // unsigned Electron bundle fails `codesign --verify` ("code has no
    // resources but signature indicates they must be present"), which
    // makes macOS re-prompt for mic/camera/screen access on every launch.
    // With APPLE_IDENTITY set, osxSign handles signing and this is a no-op.
    postPackage: async (_config, { platform, outputPaths }) => {
      if (platform !== 'darwin' || process.env.APPLE_IDENTITY) return;
      for (const dir of outputPaths) {
        const appPath = path.join(dir, 'Pulse.app');
        if (!existsSync(appPath)) continue;
        console.log(`[forge] ad-hoc signing ${appPath}`);
        execFileSync('codesign', ['--force', '--deep', '--sign', '-', appPath], {
          stdio: 'inherit',
        });
        execFileSync('codesign', ['--verify', '--deep', '--strict', appPath], {
          stdio: 'inherit',
        });
      }
    },
  },
  packagerConfig: {
    asar: {
      unpack: '**/*.node', // Native addons must be outside asar
    },
    name: 'Pulse',
    executableName: 'pulse-desktop',
    appBundleId: 'com.pulse.desktop',
    icon: './assets/icon',
    ignore: [
      /^\/node_modules/,
      /^\/src/,
      /^\/scripts/,
      /^\/tsconfig\.json/,
      /^\/forge\.config\.ts/,
    ],
    // Ship the audio driver + helper in app resources (macOS only)
    ...(hasNativeDriver && {
      extraResource: [
        './native/audio-driver/build/PulseAudio.driver',
        ...(hasAudioHelper ? ['./native/audio-driver/build/pulse-audio-helper'] : []),
      ],
    }),
    extendInfo: {
      NSMicrophoneUsageDescription: 'Pulse needs microphone access for voice chat.',
      NSCameraUsageDescription: 'Pulse needs camera access for video calls.',
      NSScreenCaptureUsageDescription: 'Pulse needs screen capture access for screen sharing.',
      // Register the pulse:// deep-link scheme on macOS.
      CFBundleURLTypes: [
        {
          CFBundleURLName: 'Pulse',
          CFBundleURLSchemes: ['pulse'],
        },
      ],
    },
    // macOS code signing (requires APPLE_IDENTITY env var)
    ...(process.env.APPLE_IDENTITY && {
      osxSign: {
        identity: process.env.APPLE_IDENTITY,
        entitlements: './entitlements.plist',
        'entitlements-inherit': './entitlements.plist',
        'hardened-runtime': true,
      },
    }),
    // macOS notarization (requires APPLE_ID + APPLE_PASSWORD + APPLE_TEAM_ID env vars)
    ...(process.env.APPLE_ID && process.env.APPLE_PASSWORD && process.env.APPLE_TEAM_ID && {
      osxNotarize: {
        appleId: process.env.APPLE_ID,
        appleIdPassword: process.env.APPLE_PASSWORD,
        teamId: process.env.APPLE_TEAM_ID,
      },
    }),
  },
  makers: [
    new MakerDMG({
      format: 'ULFO',
    }),
    // Windows installer + Squirrel.Windows auto-update feed (RELEASES +
    // *.nupkg + Setup.exe). `name` is the Squirrel app id and MUST stay
    // stable across releases or updates will not apply.
    new MakerSquirrel({
      name: 'pulse-desktop',
      authors: 'Pulse',
      description: 'Pulse Desktop',
      setupExe: 'Pulse-Setup.exe',
      setupIcon: './assets/icon.ico',
    }),
    // macOS auto-update (Squirrel.Mac) consumes the darwin .zip; the win32
    // .zip is a portable, non-installer download.
    new MakerZIP({}, ['darwin', 'win32']),
    new MakerDeb({
      options: {
        name: 'pulse-desktop',
        bin: 'pulse-desktop',
        productName: 'Pulse',
        icon: './assets/icon.png',
        categories: ['Network', 'Chat'],
      },
    }),
    new MakerRpm({
      options: {
        name: 'pulse-desktop',
        bin: 'pulse-desktop',
        productName: 'Pulse',
        icon: './assets/icon.png',
        categories: ['Network', 'Chat'],
      },
    }),
  ],
};

export default config;
