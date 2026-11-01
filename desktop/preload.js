'use strict';
/* The settings file is the real copy. It's loaded into storage here, before the editor reads it. */
const { contextBridge, ipcRenderer } = require('electron');

const saved = ipcRenderer.sendSync('settings-read');
for (const k in saved) localStorage.setItem(k, saved[k]);

contextBridge.exposeInMainWorld('desktop', {
  saveSettings: json => ipcRenderer.send('settings-write', json),
});
