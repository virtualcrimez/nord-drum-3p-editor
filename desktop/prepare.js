'use strict';
/* Builds the desktop page from the hosted index.html, so the two can't drift apart.
   Each swap must match, so a wording change in index.html fails the build instead of shipping stale text. */
const fs = require('fs');
const path = require('path');

const SWAPS = [
  [/<link rel="stylesheet" href="https:\/\/fonts\.googleapis\.com[^>]*>/, '<link rel="stylesheet" href="fonts.css">'],
  [/<li>Open this page in Chrome or Edge and allow MIDI when asked\.[^<]*<\/li>/, '<li>The app connects to your MIDI ports when it opens. If you plug in your interface later, it picks it up, or press Rescan.</li>'],
  ['\'" in this browser.\'', '\'" on this computer.\''],
  ['MIDI is blocked here. Open the saved .html file in Chrome or Edge.', 'MIDI could not start. Check your interface and press Connect MIDI.'],
  ['</body>', '<script src="memory.js"></script>\n</body>'],
];

const out = path.join(__dirname, 'app');
fs.rmSync(out, { recursive: true, force: true });
fs.mkdirSync(out);

let html = fs.readFileSync(path.join(__dirname, '..', 'index.html'), 'utf8');
for (const [from, to] of SWAPS) {
  if (!html.match(from)) throw new Error('index.html no longer contains: ' + from);
  html = html.replace(from, to);
}
fs.writeFileSync(path.join(out, 'index.html'), html);
fs.copyFileSync(path.join(__dirname, 'memory.js'), path.join(out, 'memory.js'));
fs.copyFileSync(path.join(__dirname, 'fonts.css'), path.join(out, 'fonts.css'));
fs.cpSync(path.join(__dirname, 'fonts'), path.join(out, 'fonts'), { recursive: true });
