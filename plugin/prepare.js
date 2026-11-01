'use strict';
/* Builds the plugin page from the hosted index.html and the desktop app's memory.js, so all three stay in step.
   Each swap must match, so a wording change fails the build instead of shipping stale text. */
const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');
const out = path.join(__dirname, 'web');

function swap(text, swaps) {
  for (const [from, to] of swaps) {
    if (typeof from === 'string' ? !text.includes(from) : !from.test(text)) throw new Error('Source no longer contains: ' + from);
    text = text.replace(from, () => to);
  }
  return text;
}

const PAGE = [
  [/<link rel="stylesheet" href="https:\/\/fonts\.googleapis\.com[^>]*>/, '<link rel="stylesheet" href="fonts.css">'],
  ['</title>', '</title>\n<script src="seed.js"></script>\n<script src="bridge.js"></script>'],
  [/<li>Open this page in Chrome or Edge and allow MIDI when asked\.[^<]*<\/li>/, '<li>Choose <b>Through DAW</b> to use your DAW\'s MIDI routing, or pick a MIDI port to talk to the unit directly. Sys Ex usually needs a direct port.</li>'],
  ['\'" in this browser.\'', '\'" in this plugin.\''],
  ['MIDI is blocked here. Open the saved .html file in Chrome or Edge.', 'MIDI could not start. Reopen the plugin window.'],
  ['localStorage.getItem(\'nd3p.\' + k)', 'pluginStore.get(k)'],
  ['localStorage.setItem(\'nd3p.\' + k, JSON.stringify(v))', 'pluginStore.set(k, JSON.stringify(v))'],
  [/function download\(name, data, type\) \{[\s\S]*?\n\}/, 'function download(name, data) { pluginSave(name, data); }'],
  ['pad.addEventListener(\'click\', hit);', 'pad.addEventListener(\'pointerdown\', e => { if (!e.target.closest(\'.mono\')) hit(e); });'],
  // The note goes out first, because redrawing the page can take longer than the note should.
  [/selectChannel\(c\);\s+if \(!audition\) return;\s+const r = pad\.getBoundingClientRect\(\);\s+const y = .*\s+trigger\(c, .*\);/,
   'const r = pad.getBoundingClientRect();\n      const y = e && e.clientY ? (r.bottom - e.clientY) / r.height : .7;\n      if (audition) { pluginPress(e ? e.timeStamp : performance.now()); const note = NOTES[c]; raw([0x90 | globalCh, note, clamp(Math.round(30 + y * 97), 1, 127)]); setTimeout(() => raw([0x80 | globalCh, note, 0]), 120); }\n      setTimeout(() => { selectChannel(c); if (audition) flashPad(c); });'],
  ["out && inp ? 'Linked both ways'", "out && inp ? (out.id === 'daw' ? 'Linked through your DAW. Pick a MIDI port for instant pads' : 'Linked both ways')"],
  // The first knob move goes straight out. Later ones are grouped briefly so a fast turn can't flood the port.
  [/if \(flushQueued\) return;\s+flushQueued = true;\s+setTimeout\(\(\) => \{ flushQueued = false; for \(const f of pending\.values\(\)\) f\(\); pending\.clear\(\); \}, 16\);/,
   'if (flushQueued) return;\n  const flush = () => { for (const f of pending.values()) f(); pending.clear(); };\n  flushQueued = true;\n  flush();\n  setTimeout(() => { flushQueued = false; flush(); }, 8);'],
  ['</body>', '<script src="memory.js"></script>\n</body>'],
];

const MEMORY = [
  ['Desktop app only.', 'Plugin only, built from the desktop app\'s memory.js.'],
  ['return JSON.stringify(localStorage);', 'return \'\';'],
  ['window.desktop.saveSettings(now);', ''],
  ['setInterval(save, 1000)', 'setInterval(save, 250)'],
];

fs.rmSync(out, { recursive: true, force: true });
fs.mkdirSync(out);
fs.writeFileSync(path.join(out, 'index.html'), swap(fs.readFileSync(path.join(root, 'index.html'), 'utf8'), PAGE));
fs.writeFileSync(path.join(out, 'memory.js'), swap(fs.readFileSync(path.join(root, 'desktop', 'memory.js'), 'utf8'), MEMORY));
fs.copyFileSync(path.join(__dirname, 'bridge.js'), path.join(out, 'bridge.js'));
fs.copyFileSync(path.join(root, 'desktop', 'fonts.css'), path.join(out, 'fonts.css'));
fs.cpSync(path.join(root, 'desktop', 'fonts'), out, { recursive: true });
