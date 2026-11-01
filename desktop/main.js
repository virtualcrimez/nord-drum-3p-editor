'use strict';
const { app, BrowserWindow, Menu, ipcMain, session } = require('electron');
const fs = require('fs');
const path = require('path');

const PAGE = path.join(__dirname, 'app', 'index.html');
const MIDI_PERMISSIONS = ['midi', 'midiSysex'];
const isPage = url => url.startsWith('file:');
const delay = ms => new Promise(r => setTimeout(r, ms));

/* ---------- Window size and position ---------- */
const boundsFile = () => path.join(app.getPath('userData'), 'window.json');
function readBounds() {
  try { return JSON.parse(fs.readFileSync(boundsFile(), 'utf8')); } catch { return {}; }
}
function writeBounds(win) {
  try { fs.writeFileSync(boundsFile(), JSON.stringify({ ...win.getNormalBounds(), maximized: win.isMaximized() })); } catch { /* size just won't be remembered */ }
}

/* ---------- Settings ---------- */
const settingsFile = () => path.join(app.getPath('userData'), 'settings.json');
function readSettings() {
  try {
    const s = JSON.parse(fs.readFileSync(settingsFile(), 'utf8'));
    return Object.fromEntries(Object.entries(s).filter(([, v]) => typeof v === 'string'));
  } catch { return {}; }
}
function writeSettings(json) {
  if (typeof json !== 'string') return;
  try {
    const tmp = settingsFile() + '.tmp';                // a crash mid-write can't damage the real file
    fs.writeFileSync(tmp, json);
    fs.renameSync(tmp, settingsFile());
  } catch (e) { console.error('Could not save settings:', e); }
}
async function saveOnClose(win) {
  const page = win.webContents.executeJavaScript('saveSession()').catch(() => null);
  writeSettings(await Promise.race([page, delay(2000)]));
}

function createWindow() {
  const { x, y, width, height, maximized } = readBounds();
  const win = new BrowserWindow({
    x, y, width: width || 1440, height: height || 960, minWidth: 420, minHeight: 500,
    backgroundColor: '#a51c20',
    webPreferences: { contextIsolation: true, nodeIntegration: false, sandbox: true, preload: path.join(__dirname, 'preload.js') },
  });
  if (maximized) win.maximize();
  win.once('close', e => {
    e.preventDefault();
    writeBounds(win);
    saveOnClose(win).finally(() => win.destroy());
  });
  win.webContents.on('will-navigate', e => e.preventDefault());
  win.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  win.loadFile(PAGE);
}

function setMenu() {
  if (process.platform !== 'darwin') { Menu.setApplicationMenu(null); return; }
  // Undo is left out so Cmd+Z reaches the editor's own undo.
  Menu.setApplicationMenu(Menu.buildFromTemplate([
    { role: 'appMenu' },
    { label: 'Edit', submenu: [{ role: 'cut' }, { role: 'copy' }, { role: 'paste' }, { role: 'selectAll' }] },
    { role: 'windowMenu' },
  ]));
}

if (!app.requestSingleInstanceLock()) app.quit();
else {
  app.on('second-instance', () => {
    const win = BrowserWindow.getAllWindows()[0];
    if (win) { if (win.isMinimized()) win.restore(); win.focus(); }
  });
  ipcMain.on('settings-read', e => { e.returnValue = readSettings(); });
  ipcMain.on('settings-write', (e, json) => writeSettings(json));
  app.whenReady().then(() => {
    session.defaultSession.setPermissionRequestHandler((wc, permission, done) => done(MIDI_PERMISSIONS.includes(permission) && isPage(wc.getURL())));
    session.defaultSession.setPermissionCheckHandler((wc, permission) => MIDI_PERMISSIONS.includes(permission));
    setMenu();
    createWindow();
  });
  app.on('window-all-closed', () => app.quit());
}
