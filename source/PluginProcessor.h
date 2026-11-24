#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

//==============================================================================
// Parameter IDs
//==============================================================================
namespace ParamIDs
{
    // OSC A
    inline constexpr const char* oscA_wave   = "oscA_wave";
    inline constexpr const char* oscA_oct    = "oscA_oct";
    inline constexpr const char* oscA_uni    = "oscA_uni";
    inline constexpr const char* oscA_detune = "oscA_detune";
    inline constexpr const char* oscA_pan    = "oscA_pan";
    inline constexpr const char* oscA_level  = "oscA_level";
    // OSC B
    inline constexpr const char* oscB_wave   = "oscB_wave";
    inline constexpr const char* oscB_oct    = "oscB_oct";
    inline constexpr const char* oscB_uni    = "oscB_uni";
    inline constexpr const char* oscB_detune = "oscB_detune";
    inline constexpr const char* oscB_pan    = "oscB_pan";
    inline constexpr const char* oscB_level  = "oscB_level";
    // Sampler
    inline constexpr const char* smp_level   = "smp_level";
    inline constexpr const char* smp_start   = "smp_start";
    inline constexpr const char* smp_end     = "smp_end";
    inline constexpr const char* smp_loop    = "smp_loop";
    inline constexpr const char* smp_root    = "smp_root";
    // Filter
    inline constexpr const char* flt_type    = "flt_type";
    inline constexpr const char* flt_cutoff  = "flt_cutoff";
    inline constexpr const char* flt_res     = "flt_res";
    inline constexpr const char* flt_drive   = "flt_drive";
    inline constexpr const char* flt_envamt  = "flt_envamt";
    // Amp envelope
    inline constexpr const char* aenv_a      = "aenv_a";
    inline constexpr const char* aenv_d      = "aenv_d";
    inline constexpr const char* aenv_s      = "aenv_s";
    inline constexpr const char* aenv_r      = "aenv_r";
    // Filter / mod envelope
    inline constexpr const char* fenv_a      = "fenv_a";
    inline constexpr const char* fenv_d      = "fenv_d";
    inline constexpr const char* fenv_s      = "fenv_s";
    inline constexpr const char* fenv_r      = "fenv_r";
    // LFO
    inline constexpr const char* lfo_shape   = "lfo_shape";
    inline constexpr const char* lfo_rate    = "lfo_rate";
    inline constexpr const char* lfo_depth   = "lfo_depth";
    inline constexpr const char* lfo_dest    = "lfo_dest";
    // FX
    inline constexpr const char* fx_chorus_rate  = "fx_chorus_rate";
    inline constexpr const char* fx_chorus_depth = "fx_chorus_depth";
    inline constexpr const char* fx_chorus_mix   = "fx_chorus_mix";
    inline constexpr const char* fx_delay_time   = "fx_delay_time";
    inline constexpr const char* fx_delay_fb     = "fx_delay_fb";
    inline constexpr const char* fx_delay_mix    = "fx_delay_mix";
    inline constexpr const char* fx_reverb_size  = "fx_reverb_size";
    inline constexpr const char* fx_reverb_damp  = "fx_reverb_damp";
    inline constexpr const char* fx_reverb_mix   = "fx_reverb_mix";
    // Master
    inline constexpr const char* master_gain = "master_gain";
}

//==============================================================================
// Band-limited wavetable bank (5 waves x 10 mip levels), shared by all voices.
// Waves: 0 Sine, 1 Triangle, 2 Saw, 3 Square, 4 Harmonic (rich additive).
//==============================================================================
class WavetableBank
{
public:
    static constexpr int tableSize = 2048;
    static constexpr int numWaves  = 5;
    static constexpr int numLevels = 10;

    struct Morph { int w0 = 0; int w1 = 1; float frac = 0.0f; };

    WavetableBank();

    /** pos in 0..1 -> pair of neighbouring waves + interpolation factor. */
    static Morph makeMorph (float pos) noexcept
    {
        pos = juce::jlimit (0.0f, 1.0f, pos) * (float) (numWaves - 1);
        const int w0 = juce::jmin ((int) pos, numWaves - 2);
        return { w0, w0 + 1, pos - (float) w0 };
    }

    /** Picks the mip level whose harmonics stay below Nyquist. */
    int levelForFrequency (float freqHz, double sampleRate) const noexcept
    {
        const float maxHarmonics = (float) (0.5 * sampleRate) / juce::jmax (1.0f, freqHz);
        for (int l = 0; l < numLevels; ++l)
            if ((float) (1024 >> l) <= maxHarmonics)
                return l;
        return numLevels - 1;
    }

    /** phase in 0..1 */
    float read (const Morph& m, int level, float phase) const noexcept
    {
        const float x  = phase * (float) tableSize;
        int i0         = (int) x;
        const float fr = x - (float) i0;
        i0 &= (tableSize - 1);

        const float* a = table (m.w0, level);
        const float* b = table (m.w1, level);
        const float va = a[i0] + fr * (a[i0 + 1] - a[i0]);
        const float vb = b[i0] + fr * (b[i0 + 1] - b[i0]);
        return va + m.frac * (vb - va);
    }

    const float* table (int wave, int level) const noexcept
    {
        return data.data() + (size_t) (wave * numLevels + level) * (size_t) (tableSize + 1);
    }

    /** For UI previews: one morphed cycle sample, phase 0..1, pos 0..1. */
    float previewSample (float pos, float phase) const noexcept
    {
        return read (makeMorph (pos), 3, phase);
    }

private:
    std::vector<float> data;
};

//==============================================================================
// Loaded sample (immutable once published to the audio thread)
//==============================================================================
struct SampleData
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 44100.0;
    int detectedRoot  = 60;
    juce::String name;
};

//==============================================================================
// Cached raw parameter pointers (lock-free reads on the audio thread)
//==============================================================================
struct ParamPtrs
{
    std::atomic<float> *oscA_wave {}, *oscA_oct {}, *oscA_uni {}, *oscA_detune {}, *oscA_pan {}, *oscA_level {};
    std::atomic<float> *oscB_wave {}, *oscB_oct {}, *oscB_uni {}, *oscB_detune {}, *oscB_pan {}, *oscB_level {};
    std::atomic<float> *smp_level {}, *smp_start {}, *smp_end {}, *smp_loop {}, *smp_root {};
    std::atomic<float> *flt_type {}, *flt_cutoff {}, *flt_res {}, *flt_drive {}, *flt_envamt {};
    std::atomic<float> *aenv_a {}, *aenv_d {}, *aenv_s {}, *aenv_r {};
    std::atomic<float> *fenv_a {}, *fenv_d {}, *fenv_s {}, *fenv_r {};
    std::atomic<float> *lfo_shape {}, *lfo_rate {}, *lfo_depth {}, *lfo_dest {};
    std::atomic<float> *fx_chorus_rate {}, *fx_chorus_depth {}, *fx_chorus_mix {};
    std::atomic<float> *fx_delay_time {}, *fx_delay_fb {}, *fx_delay_mix {};
    std::atomic<float> *fx_reverb_size {}, *fx_reverb_damp {}, *fx_reverb_mix {};
    std::atomic<float> *master_gain {};

    void bind (juce::AudioProcessorValueTreeState& apvts);
};

/** Everything a voice needs; owned by the processor, outlives the voices. */
struct VoiceContext
{
    ParamPtrs p;
    const WavetableBank* bank = nullptr;
    const SampleData* sample  = nullptr;   // set by the audio thread once per block
};

//==============================================================================
// Processor
//==============================================================================
class FerratureAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int numVoices = 16;
    static constexpr int scopeSize = 2048;

    FerratureAudioProcessor();
    ~FerratureAudioProcessor() override;

    //== AudioProcessor ========================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Ferrature"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //== Parameters ============================================================
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //== Sampler OSC (message thread) ==========================================
    /** Loads wav / aif / mp3 (mp3 needs JUCE_USE_MP3AUDIOFORMAT=1). If applyDetectedRoot is true,
        the detected pitch is written into the "smp_root" parameter. */
    bool loadSample (const juce::File& file, bool applyDetectedRoot = true);
    void clearSample();
    std::shared_ptr<const SampleData> getCurrentSample() const { return currentSample; }
    juce::String getLoadedSampleName() const { return currentSample != nullptr ? currentSample->name : juce::String(); }

    //== Preset manager (message thread) =======================================
    static juce::File getPresetDirectory();
static juce::StringArray getPresetNames();
    bool savePreset (const juce::String& name);
    bool loadPreset (const juce::String& name);
    bool saveToFile (const juce::File& file, const juce::String& presetName = {});
    bool loadFromFile (const juce::File& file);
    void initPreset();
    juce::String getCurrentPresetName() const { return currentPresetName; }

    //== Visual / UI state =====================================================
    juce::MidiKeyboardState keyboardState;
    const WavetableBank& getWavetableBank() const noexcept { return bank; }
    void copyScope (float* dest, int num) const;

private:
    void loadStateFromXml (const juce::XmlElement& xml);
    void publishSample (std::shared_ptr<const SampleData> newSample);
    void restoreSampleFromState();

    WavetableBank bank;
    VoiceContext ctx;
    juce::Synthesiser synth;

    juce::AudioFormatManager formatManager;
    juce::SpinLock sampleLock;
    std::shared_ptr<const SampleData> currentSample;
    std::shared_ptr<const SampleData> audioSample;
    std::vector<std::shared_ptr<const SampleData>> retiredSamples;
    juce::String loadedSamplePath;
    juce::String currentPresetName { "Init" };

    double currentSampleRate = 44100.0;
    float lastMasterGain = 0.7f;

    juce::dsp::Chorus<float> chorus;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine;
    juce::SmoothedValue<float> delaySamplesSmoothed;
    juce::dsp::Reverb reverb;

    std::array<std::atomic<float>, scopeSize> scopeData {};
    std::atomic<int> scopeWritePos { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FerratureAudioProcessor)
};
