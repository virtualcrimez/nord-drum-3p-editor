Nord Drum 3P Editor VST3

Install
- Windows: copy "Nord Drum 3P Editor.vst3" to C:\Program Files\Common Files\VST3
- Mac: copy "Nord Drum 3P Editor.vst3" to /Library/Audio/Plug-Ins/VST3, then run this in Terminal,
  because the plugin isn't signed:
  xattr -cr "/Library/Audio/Plug-Ins/VST3/Nord Drum 3P Editor.vst3"

Then rescan plugins in your DAW. It loads as an instrument and makes no sound.

MIDI
- Through DAW: route the plugin's MIDI output to your interface in the DAW, and the unit's MIDI input
  to the plugin's track. Sys Ex often doesn't get through this way.
- A MIDI port: pick your interface in the plugin. The DAW must not be using that port, especially on Windows.

Track MIDI
Pick your MIDI port in the plugin and the track's MIDI goes straight to it, even with the window closed, with no second
track. It goes out on the Global channel you set in the plugin, so clips on channel 1 still play the unit. In individual
routing it keeps the track's own channels. If the unit's own MIDI also comes back through that track, notes can double,
so turn off monitoring.

Latency
A MIDI port goes straight to the unit. Through DAW waits for your DAW's audio buffer, so pads feel late.
The plugin picks a port with Nord in its name the first time it opens. If yours has another name, pick it yourself.

Your kit saves with the DAW project. The value map, kit library and MIDI setup are shared by every project.
Opening a project never sends anything to the unit, so press Send kit to unit when you want its sound back.

This plugin uses JUCE and is licensed under the GNU AGPL v3 (see LICENSE).
Source: https://github.com/virtualcrimez/nord-drum-3p-editor
