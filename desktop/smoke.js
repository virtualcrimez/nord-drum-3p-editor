'use strict';
/* Opens the packaged app twice on a build machine: checks it starts offline, that MIDI
   permission is granted, and that a change survives a real quit and relaunch. */
const { spawn } = require('child_process');
const path = require('path');

const PORT = 9333;
const NAME = 'Nord Drum 3P Editor';
const APP = process.platform === 'darwin'
  ? path.join(__dirname, 'dist', 'mac-universal', NAME + '.app', 'Contents', 'MacOS', NAME)
  : path.join(__dirname, 'dist', 'win-unpacked', NAME + '.exe');
const wait = ms => new Promise(r => setTimeout(r, ms));
const check = (name, ok, got) => {
  console.log((ok ? 'PASS ' : 'FAIL ') + name + (got !== undefined ? ' :: ' + JSON.stringify(got) : ''));
  if (!ok) process.exitCode = 1;
};

async function open() {
  const proc = spawn(APP, ['--remote-debugging-port=' + PORT], { stdio: 'ignore' });
  const exited = new Promise(r => proc.on('exit', r));
  let target;
  for (let i = 0; i < 120 && !target; i++) {
    await wait(500);
    try {
      const list = await (await fetch(`http://127.0.0.1:${PORT}/json/list`)).json();
      target = list.find(t => t.type === 'page' && t.url.startsWith('file:'));
    } catch { /* not listening yet */ }
  }
  if (!target) throw new Error('The app never opened a page');
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
  let id = 0;
  const waiting = new Map();
  ws.onmessage = e => { const m = JSON.parse(e.data); if (waiting.has(m.id)) { waiting.get(m.id)(m); waiting.delete(m.id); } };
  const ev = expression => new Promise(res => {
    waiting.set(++id, m => res(m.result && m.result.result ? m.result.result.value : m));
    ws.send(JSON.stringify({ id, method: 'Runtime.evaluate', params: { expression, awaitPromise: true, returnByValue: true } }));
  });
  const close = async () => {
    // The same requests as the window's X button on Windows and Cmd+Q on a Mac.
    if (process.platform === 'win32') spawn('taskkill', ['/PID', String(proc.pid)], { stdio: 'ignore' });
    else proc.kill('SIGTERM');
    const code = await Promise.race([exited, wait(20000).then(() => 'timed out')]);
    check('quits cleanly', code === 0, code);
    if (code === 'timed out') proc.kill();
    await wait(1000);
  };
  return { ev, close };
}

(async () => {
  const a = await open();
  await wait(2000);
  check('title', await a.ev('document.title') === NAME);
  check('no web fonts link', await a.ev('!document.querySelector(\'link[href*="googleapis"]\')'));
  const fonts = await a.ev('document.fonts.ready.then(() => [...document.fonts].map(f => f.family + " " + f.weight + " " + f.status))');
  check('bundled fonts load', Array.isArray(fonts) && fonts.some(f => f.endsWith('loaded')) && !fonts.some(f => f.endsWith('error')), fonts);
  check('MIDI with Sys Ex allowed', await a.ev('navigator.requestMIDIAccess({ sysex: true }).then(m => m.sysexEnabled, e => String(e))') === true);
  check('connected on launch', await a.ev('document.getElementById("connect").textContent') === 'Rescan');
  let app = a;
  for (const round of [1, 2, 3]) {
    await app.ev(`(() => {
      const n = document.getElementById('kitName'); n.value = 'Smoke kit ${round}'; n.dispatchEvent(new Event('input', { bubbles: true }));
      document.querySelectorAll('.pad')[${round + 1}].click();
      document.getElementById('prog').value = ${20 + round};
      document.getElementById('tab-log').click();
    })()`);
    await app.close();
    app = await open();
    await wait(2000);
    check(`round ${round}: kit name restored`, await app.ev('document.getElementById("kitName").value') === `Smoke kit ${round}`);
    check(`round ${round}: channel restored`, await app.ev(`document.querySelectorAll(".pad")[${round + 1}].classList.contains("sel")`) === true);
    check(`round ${round}: kit number restored`, await app.ev('document.getElementById("prog").value') === String(20 + round));
    check(`round ${round}: tab restored`, await app.ev('document.getElementById("pane-log").hidden') === false);
  }
  await app.close();
})().catch(e => { console.log('FAIL ' + e.stack); process.exitCode = 1; }).finally(() => process.exit());
