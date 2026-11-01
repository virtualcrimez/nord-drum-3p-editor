/* Runs inside the plugin's page, driven by test/host.cpp. The first open makes changes and the
   second, in a new copy of the plugin restored from the first one's state, checks they came back. */
addEventListener('load', () => setTimeout(async () => {
  const say = line => window.__JUCE__.backend.emitEvent('ndTest', line);
  const check = (name, ok, got) => say((ok ? 'PASS ' : 'FAIL ') + name + (got === undefined ? '' : ' :: ' + got));
  const wait = ms => new Promise(r => setTimeout(r, ms));
  const until = async test => { for (let i = 0; i < 200 && !test(); i++) await wait(50); return test(); };
  const set = (id, value, event) => { const e = $(id); e.value = value; e.dispatchEvent(new Event(event, { bubbles: true })); };

  if ($('kitName').value === 'Project kit') {
    check('kit restored from the project', true);
    check('settings restored from the shared file', $('globalCh').value === '12', $('globalCh').value);
    check('port restored', $('outSel').selectedOptions[0].textContent === 'Through DAW');
    say('DONE');
    return;
  }

  const font = document.fonts.ready.then(() => [...document.fonts].some(f => f.status === 'loaded'));
  check('title', document.title === 'Nord Drum 3P Editor', document.title);
  check('bundled fonts load', await font);
  check('Through DAW selected by default', $('outSel').selectedOptions[0].textContent === 'Through DAW' && $('inSel').selectedOptions[0].textContent === 'Through DAW');
  check('linked both ways', $('midiText').textContent.startsWith('Linked through your DAW'), $('midiText').textContent);

  say('READY');
  check('program change from the DAW reaches the page', await until(() => $('prog').value === '8'), $('prog').value);
  check('Sys Ex from the DAW reaches the page', await until(() => sysex.length === 1), sysex.length);

  set('kitName', 'Project kit', 'input');
  set('globalCh', '12', 'change');
  await wait(600);

  const pad = padEls[1].pad;
  for (let i = 0; i < 30; i++) {
    const at = Date.now();
    pad.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, clientY: 1 }));
    await wait(200);
    say('HIT ' + at);
    await wait(100);
  }

  say('SENDING');
  await wait(300);
  let sent = 0;
  const send = out.send;
  out.send = (bytes, at) => { sent++; send(bytes, at); };
  $('sendKit').click();
  await wait(4000);
  out.send = send;
  say('SENT ' + sent);
  await wait(1000);

  const port = [...$('outSel').options].find(o => o.textContent === 'Test Port');
  if (port) {                                          // only where the test host can make a virtual port
    const daw = $('outSel').value;
    set('outSel', port.value, 'change');
    await wait(500);
    for (let i = 0; i < 30; i++) {
      say('PORTTRACK');
      await wait(100);
    }
    say('PORTCLOSED');
    await wait(1000);
    for (let i = 0; i < 30; i++) {
      const at = Date.now();
      pad.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, clientY: 1 }));
      await wait(150);
      say('PORTHIT ' + at);
    }
    check('click time shown', $('midiText').textContent.startsWith('Pad click to MIDI port'), $('midiText').textContent);
    const knob = CH_PARAMS.find(p => !p.pitch && !p.kit);
    for (let i = 0; i < 30; i++) {
      await wait(100);
      const at = Date.now();
      queueSend(knob, 1, i);
      await wait(100);
      say('PORTKNOB ' + at);
    }
    say('PORTDONE');
    set('outSel', daw, 'change');
    await wait(300);
  }
  say('DONE');
}, 1000));
