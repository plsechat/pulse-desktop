#!/usr/bin/env node

/**
 * Build native modules for macOS audio driver support.
 * - Builds the CoreAudio HAL plugin (PulseAudio.driver) via CMake
 * - Builds the N-API addon (pulse-coreaudio.node) via cmake-js
 * - Optionally codesigns the driver bundle
 *
 * Usage:
 *   node scripts/build-native.js [--sign]
 *
 * Skips entirely on non-macOS platforms.
 */

import { execSync } from 'child_process';
import { existsSync, mkdirSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const desktopRoot = join(__dirname, '..');
const nativeDir = join(desktopRoot, 'native');

const args = process.argv.slice(2);
const shouldSign = args.includes('--sign');

function run(cmd, cwd) {
  console.log(`> ${cmd}`);
  execSync(cmd, { cwd, stdio: 'inherit' });
}

function main() {
  if (process.platform !== 'darwin') {
    console.log('[build-native] Skipping — not macOS');
    return;
  }

  // 1. Build HAL plugin
  console.log('\n=== Building CoreAudio HAL Plugin ===\n');
  const driverDir = join(nativeDir, 'audio-driver');
  const driverBuildDir = join(driverDir, 'build');

  if (!existsSync(driverBuildDir)) {
    mkdirSync(driverBuildDir, { recursive: true });
  }

  run('cmake .. -DCMAKE_BUILD_TYPE=Release', driverBuildDir);
  run('cmake --build . --config Release', driverBuildDir);

  const driverOutput = join(driverBuildDir, 'PulseAudio.driver');
  if (!existsSync(driverOutput)) {
    console.error('ERROR: PulseAudio.driver not found after build');
    process.exit(1);
  }
  console.log(`Driver built: ${driverOutput}`);

  // Optional codesigning
  if (shouldSign) {
    const identity = process.env.CODESIGN_IDENTITY || '-';
    console.log(`\nSigning driver with identity: ${identity}`);
    run(`codesign --force --sign "${identity}" --timestamp "${driverOutput}"`, driverDir);
  }

  // 2. Build N-API addon
  console.log('\n=== Building N-API Addon ===\n');
  const addonDir = join(nativeDir, 'coreaudio-addon');

  // Install addon dependencies
  run('npm install', addonDir);

  // Detect Electron version for native build targeting
  let electronVersion;
  try {
    const electronPkg = join(desktopRoot, 'node_modules', 'electron', 'package.json');
    const pkg = JSON.parse(require('fs').readFileSync(electronPkg, 'utf8'));
    electronVersion = pkg.version;
  } catch {
    // Fallback: try to read from desktop's package.json
    try {
      const desktopPkg = JSON.parse(require('fs').readFileSync(join(desktopRoot, 'package.json'), 'utf8'));
      const electronSpec = desktopPkg.devDependencies?.electron || '';
      electronVersion = electronSpec.replace(/[\^~>=<]/g, '');
    } catch {
      console.warn('WARNING: Could not detect Electron version, building for Node instead');
    }
  }

  if (electronVersion) {
    console.log(`Building for Electron ${electronVersion}`);
    run(`npx cmake-js compile --runtime=electron --runtime-version=${electronVersion} --arch=${process.arch}`, addonDir);
  } else {
    run('npx cmake-js compile', addonDir);
  }

  const addonOutput = join(addonDir, 'build', 'Release', 'pulse-coreaudio.node');
  if (!existsSync(addonOutput)) {
    console.error('ERROR: pulse-coreaudio.node not found after build');
    process.exit(1);
  }
  console.log(`Addon built: ${addonOutput}`);

  console.log('\n=== Native build complete ===\n');
}

main();
