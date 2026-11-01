'use strict';
/* Plugin only. Loads before the editor's script and gives it the storage, Web MIDI and file saving
   it expects, backed by the plugin. seed.js comes from Plugin.cpp and holds the saved values and ports. */
(() => {
  const backend = window.__JUCE__.backend;
  const DAW = 'Through DAW';

  const values = new Map(Object.entries(window.pluginSeed.store));
  for (const k of ['outSel', 'inSel']) if (!values.has(k)) values.set(k, JSON.stringify(DAW));
  window.pluginStore = {
    get: k => values.has(k) ? values.get(k) : null,
    set(k, v) {
      if (values.get(k) === v) return;
      values.set(k, v);
      backend.emitEvent('ndStore', { key: k, value: v });
    },
  };

  // A Nord port beats Through DAW, once. Picking Through DAW after that sticks.
  if (!values.has('portsChecked')) {
    pluginStore.set('portsChecked', 'true');
    const { in: inputs, out: outputs } = window.pluginSeed.ports;
    for (const [k, ports] of [['outSel', outputs], ['inSel', inputs]]) {
      const nord = ports.find(p => /nord/i.test(p.name));
      if (nord) pluginStore.set(k, JSON.stringify(nord.name));
    }
  }

  window.pluginSave = (name, data) => {
    const bytes = typeof data === 'string' ? new TextEncoder().encode(data) : data;
    backend.emitEvent('ndSave', { name, bytes: Array.from(bytes) });
  };

  // Shows how long a pad click really takes here. The plugin answers after sending, so this is a little over.
  let pressAt = 0, ack = false;
  window.pluginPress = t => { pressAt = t; ack = true; };
  backend.addEventListener('ndSent', () => { $('midiText').textContent = 'Pad click to MIDI port: ' + Math.round(performance.now() - pressAt) + ' ms'; });

  const makeOutput = ({ id, name }) => ({
    id, name,
    send(bytes, at) {
      backend.emitEvent('ndSend', { port: id, bytes: Array.from(bytes), delay: Math.max(0, (at || 0) - performance.now()), ack });
      ack = false;
    },
  });
  const makeInput = ({ id, name }) => {
    let handler = null;
    return {
      id, name,
      get onmidimessage() { return handler; },
      set onmidimessage(fn) { handler = fn; backend.emitEvent('ndListen', { port: id, on: !!fn }); },
    };
  };

  const access = { sysexEnabled: true, inputs: new Map(), outputs: new Map(), onstatechange: null };
  function setPorts(ports) {
    access.inputs = new Map(ports.in.map(p => [p.id, makeInput(p)]));
    access.outputs = new Map(ports.out.map(p => [p.id, makeOutput(p)]));
  }
  setPorts(window.pluginSeed.ports);
  Object.defineProperty(navigator, 'requestMIDIAccess', { value: async () => access, configurable: true });

  backend.addEventListener('ndPorts', ports => { setPorts(ports); if (access.onstatechange) access.onstatechange(); });
  backend.addEventListener('ndMidi', ({ port, messages }) => {
    const input = access.inputs.get(port);
    if (input && input.onmidimessage) for (const m of messages) input.onmidimessage({ data: Uint8Array.from(m) });
  });
  backend.addEventListener('ndError', message => toast(message));
})();
