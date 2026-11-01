'use strict';
/* Desktop app only. Runs after the editor's own script and shares its variables.
   Restoring never sends MIDI, so opening the app can't change the sound on the unit. */
(() => {
  const SESSION = 'session';

  // A missing port shouldn't erase the choice: the interface may just be unplugged.
  const setStored = store.set;
  store.set = (k, v) => {
    if (v == null && (k === 'outSel' || k === 'inSel')) {
      const ports = access ? [...(k === 'outSel' ? access.outputs : access.inputs).values()] : [];
      if (!ports.some(p => p.name === store.get(k))) return;
    }
    setStored(k, v);
  };

  const pick = (sel, value) => { if ([...sel.options].some(o => o.value === value)) sel.value = value; };
  function restore(s) {
    if (!s) return;
    if (s.kit) kit = normaliseKit(s.kit);
    if (Number.isInteger(s.focus)) focus = clamp(s.focus, 0, 5);
    if (typeof s.editAll === 'boolean' && s.editAll !== editAll) $('editAll').click();
    if (typeof s.audition === 'boolean' && s.audition !== audition) $('audition').click();
    if (Number.isInteger(s.bank)) $('bank').selectedIndex = clamp(s.bank, 0, 3);
    if (Number.isInteger(s.prog)) $('prog').value = clamp(s.prog, 1, 50);
    pick($('starter'), s.starter);
    pick($('scale'), s.scale);
    if (typeof s.tab === 'string' && /^tab-\w+$/.test(s.tab) && $(s.tab)) $(s.tab).click();
    if (Array.isArray(s.sysex)) { sysex = s.sysex; updateSysex(); }
    afterKitLoad();
  }

  const snapshot = () => ({
    kit, focus, editAll, audition,
    bank: $('bank').selectedIndex, prog: +$('prog').value,
    starter: $('starter').value, scale: $('scale').value,
    tab: document.querySelector('[role=tab][aria-selected=true]').id,
    sysex,
  });
  // Covers everything the editor stores: this session, the kit library, the value map and MIDI setup.
  const everything = () => { store.set(SESSION, snapshot()); return JSON.stringify(localStorage); };
  let saved;
  function save() {
    const now = everything();
    if (now !== saved) { saved = now; window.desktop.saveSettings(now); }
    return now;
  }

  restore(store.get(SESSION));
  saved = everything();
  setInterval(save, 1000);
  window.saveSession = save;                           // main.js calls this before closing
  connect();
})();
