/* Loads the built VST3 the way a DAW does, opens its real page and runs test/selftest.js inside it.
   Checks MIDI both ways through the plugin and that a saved project restores in a new copy. */
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

using namespace juce;

class Test final : public JUCEApplication, private MidiInputCallback, private Timer
{
public:
    const String getApplicationName() override     { return "Plugin Test"; }
    const String getApplicationVersion() override  { return "1.0.0"; }

    void initialise (const String& args) override
    {
        path = args.unquoted();
        results = File (SystemStats::getEnvironmentVariable ("ND3P_TEST_RESULT", {}));
        results.deleteFile();
        audio.setSize (2, 480);
        testPort = MidiInput::createNewDevice ("Test Port", this);      // null on Windows, which has no virtual ports
        if (testPort != nullptr)
            testPort->start();
        open ({});
        startTimer (10);
    }

    void shutdown() override { testPort.reset(); close(); }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const String&) override {}

private:
    void open (const MemoryBlock& state)
    {
        OwnedArray<PluginDescription> types;
        VST3PluginFormat format;
        format.findAllTypesForFile (types, path);
        if (types.isEmpty())
            return finish ("FAIL no plugin found in " + path);

        String error;
        plugin = format.createInstanceFromDescription (*types[0], 48000.0, 480, error);
        if (plugin == nullptr)
            return finish ("FAIL couldn't load the plugin: " + error);

        plugin->setRateAndBufferSizeDetails (48000.0, 480);
        plugin->prepareToPlay (48000.0, 480);
        if (! state.isEmpty())
            plugin->setStateInformation (state.getData(), (int) state.getSize());

        window = std::make_unique<DocumentWindow> ("Plugin Test", Colours::black, 0);
        window->setContentOwned (plugin->createEditorIfNeeded(), true);
        window->setVisible (true);
        started = Time::getMillisecondCounter();
        read = 0;
    }

    void handleIncomingMidiMessage (MidiInput*, const MidiMessage& m) override
    {
        const ScopedLock lock (portLock);
        const auto* d = m.getRawData();
        if (m.getRawDataSize() != 3)
            return;
        if ((d[0] & 0xf0) == 0x90 && d[1] == 1 && d[2] > 0)
            closedStatus = d[0];
        else if ((d[0] & 0xf0) == 0x90 && d[1] == 60 && d[2] > 0)
            trackStatus = d[0], trackNotes.add (Time::currentTimeMillis());
        else if ((d[0] & 0xf0) == 0x90 && d[2] > 0)
            portNotes.add (Time::currentTimeMillis());
        else if ((d[0] & 0xf0) == 0xb0 && d[1] != 70)
            portKnobs.add (Time::currentTimeMillis());
    }

    void reportTiming (const String& name, const Array<int64>& from, const Array<int64>& to)
    {
        report ((! from.isEmpty() && from.size() == to.size() ? "PASS " : "FAIL ") + name + " reaches the port :: "
                + String (to.size()) + " of " + String (from.size()));
        Array<int64> times;
        for (int i = 0; i < jmin (from.size(), to.size()); ++i)
            times.add (to[i] - from[i]);
        times.sort();
        const auto middle = times[times.size() / 2];
        report ((middle < 50 ? "PASS " : "FAIL ") + name + " to port ms :: middle " + String (middle)
                + ", most " + String (times.getLast()));
    }

    void close()
    {
        window.reset();
        if (plugin != nullptr) plugin->releaseResources();
        plugin.reset();
    }

    void finish (const String& line)
    {
        if (line.isNotEmpty()) report (line);
        setApplicationReturnValue (failed ? 1 : 0);
        stopTimer();
        quit();
    }

    void report (const String& line)
    {
        std::cout << "host " << line << std::endl;
        if (line.startsWith ("FAIL")) failed = true;
    }

    void timerCallback() override
    {
        if (plugin == nullptr)
            return;

        audio.clear();
        plugin->processBlock (audio, midi);
        for (const auto m : midi)
        {
            ++received;
            if (m.numBytes == 3 && (m.data[0] & 0xf0) == 0x90 && m.data[2] > 0)
                notes.add (Time::currentTimeMillis());
            if (m.numBytes == 2 && m.data[0] == 0xc9 && m.data[1] == 7) passedThrough = true;
            if (m.numBytes == 3 && m.data[0] == 0x90 && m.data[1] == 60) closedNote = true;
            if (m.numBytes == 3 && m.data[0] == 0xbb && m.data[1] == 70) ++selects;
        }
        midi.clear();

        if (round == 2)
            return finish ((closedNote ? "PASS " : "FAIL ") + String ("track MIDI passes through with the window closed"));

        StringArray lines;
        results.readLines (lines);
        lines.removeEmptyStrings();                      // the file ends in a newline, which reads as one
        for (; read < lines.size() && ! reopen; ++read)
            onLine (lines[read]);

        if (std::exchange (reopen, false))
        {
            MemoryBlock state;
            plugin->getStateInformation (state);
            report ((state.getSize() > 100 ? "PASS " : "FAIL ") + String ("project state saved :: ") + String (state.getSize()));
            close();
            results.deleteFile();
            received = 0;
            open (state);
        }

        if (plugin != nullptr &&Time::getMillisecondCounter() - started > 90000)
            finish ("FAIL timed out");
    }

    void onLine (const String& line)
    {
        if (line.isEmpty()) return;
        std::cout << "page " << line << std::endl;
        if (line.startsWith ("FAIL")) failed = true;

        if (line == "READY")
        {
            const uint8 dump[] = { 0xf0, 0x7d, 0x01, 0xf7 };
            midi.addEvent (MidiMessage::programChange (10, 7), 0);
            midi.addEvent (dump, 4, 1);
            received = selects = 0;
        }
        else if (line.startsWith ("HIT "))
        {
            hits.add (line.fromFirstOccurrenceOf (" ", false, false).getLargeIntValue());
        }
        else if (line == "SENDING")
        {
            report ((! hits.isEmpty() && notes.size() == hits.size() ? "PASS " : "FAIL ") + String ("every pad press plays :: ")
                    + String (notes.size()) + " of " + String (hits.size()));
            int64 most = 0, total = 0;
            for (int i = 0; i < jmin (hits.size(), notes.size()); ++i)
            {
                most = jmax (most, notes[i] - hits[i]);
                total += notes[i] - hits[i];
            }
            report ((most < 250 ? "PASS " : "FAIL ") + String ("pad press to plugin output ms (test ticks every 10 ms) :: average ")
                    + String (total / jmax (1, hits.size())) + ", most " + String (most));
            report ((passedThrough ? "PASS " : "FAIL ") + String ("track MIDI passes through"));
            received = selects = 0;
        }
        else if (line.startsWith ("SENT "))
        {
            const auto sent = line.fromFirstOccurrenceOf (" ", false, false).getIntValue();
            report ((sent > 100 && received == sent ? "PASS " : "FAIL ") + String ("every message reaches the DAW :: ")
                    + String (received) + " of " + String (sent));
            report ((selects == 6 + 1 ? "PASS " : "FAIL ") + String ("channel selects kept :: ") + String (selects));
        }
        else if (line.startsWith ("PORTHIT "))
        {
            portHits.add (line.fromFirstOccurrenceOf (" ", false, false).getLargeIntValue());
        }
        else if (line.startsWith ("PORTKNOB "))
        {
            knobMoves.add (line.fromFirstOccurrenceOf (" ", false, false).getLargeIntValue());
        }
        else if (line == "PORTTRACK")
        {
            // Runs the block here, because the 10 ms timer can run late when the page is busy.
            MidiBuffer note;
            note.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0);
            trackSent.add (Time::currentTimeMillis());
            plugin->processBlock (audio, note);
        }
        else if (line == "PORTCLOSED")
        {
            // A new copy with no window, like a project that opens with the plugin closed.
            OwnedArray<PluginDescription> types;
            VST3PluginFormat format;
            format.findAllTypesForFile (types, path);
            String error;
            auto copy = format.createInstanceFromDescription (*types[0], 48000.0, 480, error);
            copy->setRateAndBufferSizeDetails (48000.0, 480);
            copy->prepareToPlay (48000.0, 480);
            AudioBuffer<float> silence (2, 480);
            MidiBuffer note;
            note.addEvent (MidiMessage::noteOn (1, 1, (uint8) 100), 0);
            copy->processBlock (silence, note);
            Thread::sleep (200);
            copy->releaseResources();
        }
        else if (line == "PORTDONE")
        {
            const ScopedLock lock (portLock);
            reportTiming ("pad press", portHits, portNotes);
            reportTiming ("knob move", knobMoves, portKnobs);
            report ((closedStatus == 0x9b ? "PASS " : "FAIL ") + String ("saved port plays track MIDI with no window :: ")
                    + String::toHexString (closedStatus));
            report ((trackStatus == 0x9b ? "PASS " : "FAIL ") + String ("track notes use the unit's channel, no second track :: ")
                    + String::toHexString ((int) trackStatus));
            reportTiming ("track note", trackSent, trackNotes);
        }
        else if (line == "DONE" && round == 0)
        {
            round = 1;
            reopen = true;
        }
        else if (line == "DONE")
        {
            report ((received == 0 ? "PASS " : "FAIL ") + String ("reopening sends nothing :: ") + String (received));
            window.reset();
            midi.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0);
            round = 2;
        }
    }

    String path;
    File results;
    std::unique_ptr<AudioPluginInstance> plugin;
    std::unique_ptr<DocumentWindow> window;
    AudioBuffer<float> audio;
    MidiBuffer midi;
    int read = 0, round = 0, received = 0, selects = 0;
    uint32 started = 0;
    bool failed = false, reopen = false, passedThrough = false, closedNote = false;
    Array<int64> hits, notes, portHits, knobMoves, portNotes, portKnobs, trackSent, trackNotes;
    std::unique_ptr<MidiInput> testPort;
    CriticalSection portLock;
    int trackStatus = 0, closedStatus = 0;
};

START_JUCE_APPLICATION (Test)
