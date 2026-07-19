const fs = require('fs');
const path = require('path');

const src = path.join(__dirname, '..', 'src');
const dist = path.join(__dirname, '..', 'dist');

const assets = ['server-selector.html', 'server-selector.css'];

for (const file of assets) {
  fs.copyFileSync(path.join(src, file), path.join(dist, file));
}

console.log(`Copied assets to dist/: ${assets.join(', ')}`);
