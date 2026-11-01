#include "Plugin.h"
#include <BinaryData.h>

using namespace juce;

static const String dawPort = "daw";

/* ---------- MidiRing ---------- */
void MidiRing::copyIn (const void* source, int bytes)
{
    const auto scope = fifo.write (bytes);
    auto* from = static_cast<const uint8*> (source);
    if (scope.blockSize1 > 0) std::memcpy (store.data() + scope.startIndex1, from, (size_t) scope.blockSize1);
    if (scope.blockSize2 > 0) std::memcpy (store.data() + scope.startIndex2, from + scope.blockSize1, (size_t) scope.blockSize2);
}

void MidiRing::copyOut (void* target, int bytes)
{
    const auto scope = fifo.read (bytes);
    auto* to = static_cast<uint8*> (target);
    if (scope.blockSize1 > 0) std::memcpy (to, store.data() + scope.startIndex1, (size_t) scope.blockSize1);
    if (scope.blockSize2 > 0) std::memcpy (to + scope.blockSize1, store.data() + scope.startIndex2, (size_t) scope.blockSize2);
}

bool MidiRing::push (double due, const uint8* data, int size)
{
    if (size <= 0 || size > maxMessage || fifo.getFreeSpace() < (int) sizeof (Header) + size)
        return false;
    const Header header { due, size };
    copyIn (&header, sizeof (header));
    copyIn (data, size);
    return true;
}

bool MidiRing::pop (double& due, uint8* data, int& size, int capacity)
{
    if (fifo.getNumReady() < (int) sizeof (Header))
        return false;
    Header header;
    copyOut (&header, sizeof (header));
    while (fifo.getNumReady() < header.size) {}          // the writer is between its two copies
    jassert (header.size <= capacity);
    copyOut (data, header.size);
    due = header.due;
    size = header.size;
    return true;
}

/* ---------- Settings shared by every copy of the plugin ---------- */
static File settingsFile()
{
    const auto base = File::getSpecialLocation (File::userApplicationDataDirectory);
   #if JUCE_MAC
    return base.getChildFile ("Application Support/Nord Drum 3P Editor/settings.json");
   #else
    return base.getChildFile ("Nord Drum 3P Editor/settings.json");
   #endif
}

static var readSettings()
{
    const auto parsed = JSON::parse (settingsFile().loadFileAsString());
    return parsed.isObject() ? parsed : var (new DynamicObject());
}

static void writeSetting (const String& key, const String& value)
{
    auto all = readSettings();                           // read first, so two open plugins don't undo each other
    all.getDynamicObject()->setProperty (Identifier (key), value);
    settingsFile().getParentDirectory().createDirectory();
    settingsFile().replaceWithText (JSON::toString (all, true));
}

/* The channel track MIDI goes out on: the unit's global channel, or the track's own in individual routing. */
static int trackChannelFrom (const var& all)
{
    if (JSON::fromString (all["routing"].toString()).toString() == "individual")
        return -1;
    const auto channel = JSON::fromString (all["globalCh"].toString());
    return channel.isVoid() ? 9 : jlimit (0, 15, (int) channel);
}

/* ---------- Processor ---------- */
Processor::Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      Thread ("Nord Drum 3P track MIDI") {}

Processor::~Processor() { stopThread (1000); }

void Processor::prepareToPlay (double, int)
{
    trackChannel = trackChannelFrom (readSettings());
    if (! isThreadRunning())
        startThread (Priority::highest);
    if (output == nullptr)                               // so track MIDI reaches a chosen port before the window opens
        selectOutputByName (JSON::fromString (readSettings()["outSel"].toString()).toString());
}

void Processor::selectOutput (const String& id)
{
    std::unique_ptr<MidiOutput> next;
    if (id.isNotEmpty() && id != dawPort)
        if ((next = MidiOutput::openDevice (id)) != nullptr)
            next->startBackgroundThread();

    const SpinLock::ScopedLockType lock (outputLock);
    std::swap (output, next);
    outputId = output != nullptr ? id : String();
    portOpen = output != nullptr;
}

void Processor::selectOutputByName (const String& name)
{
    for (const auto& d : MidiOutput::getAvailableDevices())
        if (d.name == name)
            return selectOutput (d.identifier);
    selectOutput ({});
}

bool Processor::sendDirect (const String& id, double due, const uint8* data, int size)
{
    if (outputId != id)
        selectOutput (id);

    // Only this thread changes the port once it's open, so no lock, which also leaves the audio thread free.
    if (output == nullptr)
        return false;

    // Nothing is waiting in the queue, so this goes straight out. Spaced messages like a kit send stay in order.
    const auto now = Time::getMillisecondCounterHiRes();
    if (due <= now + 3.0 && lastQueued < now)
    {
        output->sendMessageNow (MidiMessage (data, size));
        return true;
    }

    lastQueued = due;
    MidiBuffer message;
    message.addEvent (data, size, 0);
    output->sendBlockOfMessages (message, due, 44100.0);
    return true;
}

void Processor::run()
{
    while (! threadShouldExit())
    {
        double due;
        int size;
        if (! toPort.pop (due, sending.data(), size, (int) sending.size()))
        {
            Thread::sleep (1);
            continue;
        }

        while (! threadShouldExit())
        {
            const auto left = due - Time::getMillisecondCounterHiRes();
            if (left <= 0.0)
                break;
            if (left > 2.0)
                Thread::sleep (1);
            else
                Thread::yield();
        }

        const SpinLock::ScopedLockType lock (outputLock);
        if (output != nullptr)
            output->sendMessageNow (MidiMessage (sending.data(), size));
    }
}

void Processor::processBlock (AudioBuffer<float>& audio, MidiBuffer& midi)
{
    audio.clear();

    if (listening.load())
        for (const auto m : midi)
            fromHost.push (0.0, m.data, m.numBytes);

    const auto samples = audio.getNumSamples();
    const auto rate = getSampleRate();
    if (samples <= 0 || rate <= 0.0)
        return;

    const auto now = Time::getMillisecondCounterHiRes();
    const auto blockMs = samples * 1000.0 / rate;

    // The track's MIDI stays in the buffer, so it carries on to the plugin's output. It also goes to a chosen port,
    // on the unit's channel, because clips are normally on channel 1.
    if (portOpen.load())
    {
        const auto channel = trackChannel.load();
        for (const auto m : midi)
        {
            const auto due = now + m.samplePosition * 1000.0 / rate;
            if (channel >= 0 && m.numBytes <= 3 && m.data[0] >= 0x80 && m.data[0] < 0xf0)
            {
                uint8 bytes[3] {};
                std::memcpy (bytes, m.data, (size_t) m.numBytes);
                bytes[0] = (uint8) ((bytes[0] & 0xf0) | channel);
                toPort.push (due, bytes, m.numBytes);
            }
            else
            {
                toPort.push (due, m.data, m.numBytes);
            }
        }
    }

    while (holding || (holding = toHost.pop (heldDue, held.data(), heldSize, (int) held.size())))
    {
        if (heldDue >= now + blockMs)
            break;
        midi.addEvent (held.data(), heldSize, jlimit (0, samples - 1, (int) ((heldDue - now) / blockMs * samples)));
        holding = false;
    }
}

juce::String Processor::getSession() const { const ScopedLock l (sessionLock); return session; }
void Processor::setSession (const String& s) { const ScopedLock l (sessionLock); session = s; }

void Processor::getStateInformation (MemoryBlock& dest)
{
    const auto text = getSession().toRawUTF8();
    dest.replaceAll (text, std::strlen (text));
}

void Processor::setStateInformation (const void* data, int size)
{
    setSession (String::fromUTF8 (static_cast<const char*> (data), size));
    ++restored;
}

/* ---------- Editor ---------- */
class Editor final : public AudioProcessorEditor,
                     private MidiInputCallback,
                     private Timer
{
public:
    explicit Editor (Processor& p) : AudioProcessorEditor (p), processor (p), seenRestore (p.restored.load())
    {
        addAndMakeVisible (browser);
        setResizable (true, true);
        setResizeLimits (640, 480, 2400, 1800);
        setSize (1240, 860);
        browser.goToURL (WebBrowserComponent::getResourceProviderRoot());
        startTimerHz (100);
    }

    ~Editor() override
    {
        stopTimer();
        processor.listening = false;
    }

    void resized() override { browser.setBounds (getLocalBounds()); }

private:
    /* Keeps the page from leaving the editor if something inside it follows a link. */
    struct Page final : public WebBrowserComponent
    {
        using WebBrowserComponent::WebBrowserComponent;
        bool pageAboutToLoad (const String& url) override { return url.startsWith (getResourceProviderRoot()); }
    };

    using Options = WebBrowserComponent::Options;

    Options browserOptions()
    {
        return Options{}
            .withBackend (Options::Backend::webview2)
            .withWinWebView2Options (Options::WinWebView2{}
                                         .withUserDataFolder (File::getSpecialLocation (File::tempDirectory).getChildFile ("Nord Drum 3P Editor"))
                                         .withBackgroundColour (Colour (0xff161616)))
            .withNativeIntegrationEnabled()
            .withKeepPageLoadedWhenBrowserIsHidden()
            .withEventListener ("ndStore",  [this] (var e) { onStore (e); })
            .withEventListener ("ndSend",   [this] (var e) { onSend (e); })
            .withEventListener ("ndListen", [this] (var e) { onListen (e); })
            .withEventListener ("ndSave",   [this] (var e) { onSave (e); })
            .withEventListener ("ndTest",   [] (var e) { onTest (e); })
            .withResourceProvider ([this] (const String& url) { return resource (url); });
    }

    /* ---------- Page files ---------- */
    std::optional<WebBrowserComponent::Resource> resource (const String& url)
    {
        const auto name = url == "/" ? String ("index.html") : url.fromLastOccurrenceOf ("/", false, false);

        if (name == "seed.js")
            return text (seedScript(), "text/javascript");

        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            if (name != BinaryData::originalFilenames[i])
                continue;
            int size = 0;
            const auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
            const auto* bytes = reinterpret_cast<const std::byte*> (data);
            return WebBrowserComponent::Resource { { bytes, bytes + size }, mimeType (name) };
        }
        return std::nullopt;
    }

    static WebBrowserComponent::Resource text (const String& s, const String& mime)
    {
        const auto* bytes = reinterpret_cast<const std::byte*> (s.toRawUTF8());
        return { { bytes, bytes + s.getNumBytesAsUTF8() }, mime };
    }

    static String mimeType (const String& name)
    {
        const auto ext = name.fromLastOccurrenceOf (".", false, true);
        return ext == "html" ? "text/html" : ext == "js" ? "text/javascript" : ext == "css" ? "text/css"
             : ext == "woff2" ? "font/woff2" : "text/plain";
    }

    String seedScript() const
    {
        auto values = readSettings();
        if (const auto session = processor.getSession(); session.isNotEmpty())
            values.getDynamicObject()->setProperty ("session", session);

        auto* seed = new DynamicObject();
        seed->setProperty ("store", values);
        seed->setProperty ("ports", portList());
        auto script = "window.pluginSeed = " + JSON::toString (var (seed), true) + ";\n";

        // Lets the test host run a script in the page. Never set outside the build's own test.
        const File test (SystemStats::getEnvironmentVariable ("ND3P_TEST_SCRIPT", {}));
        if (SystemStats::getEnvironmentVariable ("ND3P_TEST_SCRIPT", {}).isNotEmpty())
            script << test.loadFileAsString();
        return script;
    }

    static var portList()
    {
        const auto list = [] (const Array<MidiDeviceInfo>& devices)
        {
            Array<var> ports;
            const auto add = [&ports] (const String& id, const String& name)
            {
                auto* port = new DynamicObject();
                port->setProperty ("id", id);
                port->setProperty ("name", name);
                ports.add (var (port));
            };
            add (dawPort, "Through DAW");
            for (const auto& d : devices)
                add (d.identifier, d.name);
            return ports;
        };
        auto* ports = new DynamicObject();
        ports->setProperty ("in", list (MidiInput::getAvailableDevices()));
        ports->setProperty ("out", list (MidiOutput::getAvailableDevices()));
        return var (ports);
    }

    /* ---------- Messages from the page ---------- */
    void onStore (const var& e)
    {
        const auto key = e["key"].toString();
        if (key == "session")
        {
            processor.setSession (e["value"].toString());
            return;
        }
        writeSetting (key, e["value"].toString());
        if (key == "outSel")
            processor.selectOutputByName (JSON::fromString (e["value"].toString()).toString());
        if (key == "globalCh" || key == "routing")
            processor.trackChannel = trackChannelFrom (readSettings());
    }

    void onSend (const var& e)
    {
        std::vector<uint8> bytes;
        if (const auto* list = e["bytes"].getArray())
            for (const auto& b : *list)
                bytes.push_back ((uint8) (int) b);
        if (bytes.empty())
            return;

        const auto port = e["port"].toString();
        const auto due = Time::getMillisecondCounterHiRes() + (double) e["delay"];

        if (port == dawPort)
        {
            if (! processor.toHost.push (due, bytes.data(), (int) bytes.size()))
                reportError ("Too much MIDI at once. Is the DAW's audio running?");
            return;
        }

        if (! processor.sendDirect (port, due, bytes.data(), (int) bytes.size()))
        {
            processor.toHost.push (due, bytes.data(), (int) bytes.size());
            reportError ("Couldn't open that MIDI port, so this went through your DAW. Your DAW may be using the port, so turn it off there.");
        }
        else if ((bool) e["ack"])
        {
            browser.emitEventIfBrowserIsVisible ("ndSent", var (true));
        }
    }

    void onListen (const var& e)
    {
        const auto port = e["port"].toString();
        const auto on = (bool) e["on"];

        if (port == dawPort)
        {
            processor.listening = on;
            return;
        }
        if (input != nullptr)
            input.reset();
        inputId = {};
        if (! on)
            return;

        input = MidiInput::openDevice (port, this);
        if (input == nullptr)
        {
            reportError ("Couldn't open that MIDI port. Your DAW may be using it, so turn it off there or use Through DAW.");
            return;
        }
        inputId = port;
        input->start();
    }

    void onSave (const var& e)
    {
        MemoryBlock data;
        if (const auto* list = e["bytes"].getArray())
            for (const auto& b : *list)
            {
                const auto byte = (uint8) (int) b;
                data.append (&byte, 1);
            }

        const auto start = File::getSpecialLocation (File::userDocumentsDirectory)
                               .getChildFile (File::createLegalFileName (e["name"].toString()));
        chooser = std::make_unique<FileChooser> ("Save", start);
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting,
                              [this, data] (const FileChooser& c)
                              {
                                  if (const auto file = c.getResult(); file != File{} && ! file.replaceWithData (data.getData(), data.getSize()))
                                      reportError ("Couldn't save that file.");
                              });
    }

    static void onTest (const var& e)
    {
        const File file (SystemStats::getEnvironmentVariable ("ND3P_TEST_RESULT", {}));
        if (SystemStats::getEnvironmentVariable ("ND3P_TEST_RESULT", {}).isNotEmpty())
            file.appendText (e.toString() + "\n");
    }

    void reportError (const String& message) { browser.emitEventIfBrowserIsVisible ("ndError", message); }

    /* ---------- Messages to the page ---------- */
    void handleIncomingMidiMessage (MidiInput*, const MidiMessage& m) override
    {
        direct.push (0.0, m.getRawData(), m.getRawDataSize());
    }

    void drain (MidiRing& ring, const String& port)
    {
        Array<var> messages;
        double due;
        int size;
        while (ring.pop (due, buffer.data(), size, (int) buffer.size()))
        {
            if (buffer[0] >= 0xf8)                           // clock and active sensing
                continue;
            Array<var> bytes;
            for (int i = 0; i < size; ++i)
                bytes.add ((int) buffer[(size_t) i]);
            messages.add (bytes);
        }
        if (messages.isEmpty())
            return;
        auto* event = new DynamicObject();
        event->setProperty ("port", port);
        event->setProperty ("messages", messages);
        browser.emitEventIfBrowserIsVisible ("ndMidi", var (event));
    }

    void timerCallback() override
    {
        drain (processor.fromHost, dawPort);
        drain (direct, inputId);

        if (const auto r = processor.restored.load(); r != seenRestore)
        {
            seenRestore = r;
            browser.goToURL (WebBrowserComponent::getResourceProviderRoot());
        }
    }

    Processor& processor;
    int seenRestore;

    Page browser { browserOptions() };

    MidiRing direct { 1 << 18 };
    std::array<uint8, MidiRing::maxMessage> buffer;
    std::unique_ptr<MidiInput> input;
    String inputId;
    std::unique_ptr<FileChooser> chooser;
    MidiDeviceListConnection deviceWatch = MidiDeviceListConnection::make ([this]
    {
        browser.emitEventIfBrowserIsVisible ("ndPorts", portList());
    });

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Processor(); }
