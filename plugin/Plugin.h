#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

/* Passes MIDI between threads without locks or allocation, so the audio thread can use it. */
class MidiRing
{
public:
    explicit MidiRing (int bytes) : fifo (bytes), store ((size_t) bytes) {}

    bool push (double due, const juce::uint8* data, int size);
    bool pop (double& due, juce::uint8* data, int& size, int capacity);

    static constexpr int maxMessage = 65536;

private:
    struct Header { double due; int size; };

    void copyIn (const void* source, int bytes);
    void copyOut (void* target, int bytes);

    juce::AbstractFifo fifo;
    std::vector<juce::uint8> store;
};

class Processor final : public juce::AudioProcessor,
                        private juce::Thread
{
public:
    Processor();
    ~Processor() override;

    const juce::String getName() const override                     { return JucePlugin_Name; }
    void prepareToPlay (double, int) override;
    void releaseResources() override                                {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    double getTailLengthSeconds() const override                    { return 0.0; }
    bool acceptsMidi() const override                               { return true; }
    bool producesMidi() const override                              { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                                 { return true; }

    int getNumPrograms() override                                   { return 1; }
    int getCurrentProgram() override                                { return 0; }
    void setCurrentProgram (int) override                           {}
    const juce::String getProgramName (int) override                { return {}; }
    void changeProgramName (int, const juce::String&) override      {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    void selectOutput (const juce::String& id);
    void selectOutputByName (const juce::String& name);
    bool sendDirect (const juce::String& id, double due, const juce::uint8* data, int size);

    juce::String getSession() const;
    void setSession (const juce::String&);

    MidiRing toHost { 1 << 18 }, fromHost { 1 << 18 };
    std::atomic<bool> listening { false };
    std::atomic<int> restored { 0 };                    // goes up when the host loads a project
    std::atomic<int> trackChannel { 9 };                // zero-based, or -1 to leave the track's channels alone

private:
    void run() override;

    juce::SpinLock outputLock;
    std::unique_ptr<juce::MidiOutput> output;
    juce::String outputId;
    double lastQueued = 0.0;

    // Track MIDI on its way to a chosen port. JUCE's own output thread was far too slow to wake for this.
    MidiRing toPort { 1 << 18 };
    std::atomic<bool> portOpen { false };
    std::array<juce::uint8, MidiRing::maxMessage> sending;

    juce::CriticalSection sessionLock;
    juce::String session;

    bool holding = false;
    double heldDue = 0.0;
    int heldSize = 0;
    std::array<juce::uint8, MidiRing::maxMessage> held;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};
