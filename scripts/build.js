const { execSync } = require('child_process');
const path = require('path');

const desktopDir = path.join(__dirname, '..');

console.log('Building TypeScript...');
try {
  execSync(`bunx tsc -p "${path.join(desktopDir, 'tsconfig.json')}"`, {
    stdio: 'inherit',
    cwd: desktopDir,
  });
} catch {
  process.exit(1);
}

// Copy assets
require('./copy-assets.js');
