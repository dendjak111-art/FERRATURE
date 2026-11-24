#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// WavetableBank
//==============================================================================
WavetableBank::WavetableBank()
{
    data.assign ((size_t) numWaves * numLevels * (tableSize + 1), 0.0f);

    // Integer-indexed sine table => no sin() calls in the inner loop
    std::vector<float> sinTab (tableSize);
    for (int i = 0; i < tableSize; ++i)
        sinTab[(size_t) i] = std::sin (juce::MathConstants<float>::twoPi * (float) i / (float) tableSize);

    auto harmonicAmp = [] (int wave, int h) -> float
    {
        const float fh = (float) h;
        switch (wave)
        {
            case 0:  return h == 1 ? 1.0f : 0.0f;                                   // sine
            case 1:  if ((h & 1) == 0) return 0.0f;                                 // triangle
                     return (((h - 1) / 2) % 2 == 0 ? 1.0f : -1.0f) / (fh * fh);
            case 2:  return -1.0f / fh;                                             // saw
            case 3:  return (h & 1) ? 1.0f / fh : 0.0f;                             // square
            default: return ((h & 1) ? 1.0f : 0.6f) / std::pow (fh, 0.75f);         // harmonic-rich
        }
    };

    for (int w = 0; w < numWaves; ++w)
    {
        float scale = 1.0f;

        for (int l = 0; l < numLevels; ++l)
        {
            const int maxHarm = juce::jmax (1, 1024 >> l);
            float* t = data.data() + (size_t) (w * numLevels + l) * (size_t) (tableSize + 1);

            for (int i = 0; i < tableSize; ++i)
            {
                float acc = 0.0f;
                for (int h = 1; h <= maxHarm; ++h)
                {
                    const float a = harmonicAmp (w, h);
                    if (a == 0.0f) continue;
                    const int phaseOff = (w == 4 && (h % 3) == 0) ? tableSize / 4 : 0;
                    acc += a * sinTab[(size_t) (((i * h) + phaseOff) & (tableSize - 1))];
                }
                t[i] = acc;
            }

            if (l == 0)
            {
                float peak = 0.0f;
                for (int i = 0; i < tableSize; ++i) peak = juce::jmax (peak, std::abs (t[i]));
                scale = peak > 1.0e-6f ? 1.0f / peak : 1.0f;
            }

            for (int i = 0; i < tableSize; ++i) t[i] *= scale;
            t[tableSize] = t[0];   // guard point for interpolation
        }
    }
}

//==============================================================================
// ParamPtrs
//==============================================================================
void ParamPtrs::bind (juce::AudioProcessorValueTreeState& apvts)
{
   #define FER_BIND(x) x = apvts.getRawParameterValue (ParamIDs::x)
    FER_BIND (oscA_wave); FER_BIND (oscA_oct); FER_BIND (oscA_uni); FER_BIND (oscA_detune); FER_BIND (oscA_pan); FER_BIND (oscA_level);
    FER_BIND (oscB_wave); FER_BIND (oscB_oct); FER_BIND (oscB_uni); FER_BIND (oscB_detune); FER_BIND (oscB_pan); FER_BIND (oscB_level);
    FER_BIND (smp_level); FER_BIND (smp_start); FER_BIND (smp_end); FER_BIND (smp_loop); FER_BIND (smp_root);
    FER_BIND (flt_type); FER_BIND (flt_cutoff); FER_BIND (flt_res); FER_BIND (flt_drive); FER_BIND (flt_envamt);
    FER_BIND (aenv_a); FER_BIND (aenv_d); FER_BIND (aenv_s); FER_BIND (aenv_r);
    FER_BIND (fenv_a); FER_BIND (fenv_d); FER_BIND (fenv_s); FER_BIND (fenv_r);
    FER_BIND (lfo_shape); FER_BIND (lfo_rate); FER_BIND (lfo_depth); FER_BIND (lfo_dest);
    FER_BIND (fx_chorus_rate); FER_BIND (fx_chorus_depth); FER_BIND (fx_chorus_mix);
    FER_BIND (fx_delay_time); FER_BIND (fx_delay_fb); FER_BIND (fx_delay_mix);
    FER_BIND (fx_reverb_size); FER_BIND (fx_reverb_damp); FER_BIND (fx_reverb_mix);
    FER_BIND (master_gain);
   #undef FER_BIND
}

//==============================================================================
// Synth sound + voice
//==============================================================================
namespace
{
    class FerratureSound : public juce::SynthesiserSound
    {
    public:
        bool appliesToNote (int) override    { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    class FerratureVoice : public juce::SynthesiserVoice
    {
    public:
        explicit FerratureVoice (const VoiceContext& c) : ctx (c) {}

        bool canPlaySound (juce::SynthesiserSound* s) override
        {
            return dynamic_cast<FerratureSound*> (s) != nullptr;
        }

        void setCurrentPlaybackSampleRate (double newRate) override
        {
            juce::SynthesiserVoice::setCurrentPlaybackSampleRate (newRate);
            const double sr = newRate > 0.0 ? newRate : 44100.0;

            juce::dsp::ProcessSpec spec { sr, 512u, 2u };
            filter.prepare (spec);
            filter.reset();
            ampEnv.setSampleRate (sr);
            filtEnv.setSampleRate (sr);
        }

        void startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int pitchWheel) override
        {
            noteNumber = midiNoteNumber;
            vel        = velocity;
            pitchBend  = ((float) pitchWheel - 8192.0f) / 8192.0f;

            for (auto& o : oscs)
                for (int u = 0; u < kMaxUni; ++u)
                    o.phase[u] = rng.nextFloat();

            lfoPhase = 0.0f;
            shValue  = rng.nextFloat() * 2.0f - 1.0f;
            smpPos   = -1.0;
            smpActive = true;
            ctrlCounter = 0;

            filter.reset();
            ampEnv.noteOn();
            filtEnv.noteOn();
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff)
            {
                ampEnv.noteOff();
                filtEnv.noteOff();
            }
            else
            {
                ampEnv.reset();
                filtEnv.reset();
                clearCurrentNote();
            }
        }

        void pitchWheelMoved (int newValue) override { pitchBend = ((float) newValue - 8192.0f) / 8192.0f; }
        void controllerMoved (int, int) override {}

        void renderNextBlock (juce::AudioBuffer<float>& out, int startSample, int numSamples) override
        {
            if (! isVoiceActive() || out.getNumChannels() < 1)
                return;

            const auto& P = ctx.p;
            const double sr = getSampleRate();
            auto ld = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed); };

            // --- read parameters once per block --------------------------------
            const int   fltType   = (int) ld (P.flt_type);
            const float cutoff    = ld (P.flt_cutoff);
            const float resonance = ld (P.flt_res);
            const float drive     = ld (P.flt_drive);
            const float envAmt    = ld (P.flt_envamt);
            const int   lfoShape  = (int) ld (P.lfo_shape);
            const float lfoRate   = ld (P.lfo_rate);
            const float lfoDepth  = ld (P.lfo_depth);
            const int   lfoDest   = (int) ld (P.lfo_dest);
            const float waveA     = ld (P.oscA_wave);
            const float waveB     = ld (P.oscB_wave);

            ampEnv.setParameters  ({ ld (P.aenv_a), ld (P.aenv_d), ld (P.aenv_s), ld (P.aenv_r) });
            filtEnv.setParameters ({ ld (P.fenv_a), ld (P.fenv_d), ld (P.fenv_s), ld (P.fenv_r) });

            filter.setType (fltType == 1 ? juce::dsp::StateVariableTPTFilterType::highpass
                          : fltType == 2 ? juce::dsp::StateVariableTPTFilterType::bandpass
                                         : juce::dsp::StateVariableTPTFilterType::lowpass);
            filter.setResonance (resonance);

            const float driveGain = 1.0f + drive * 9.0f;
            const bool  useDrive  = drive > 0.001f;

            // --- oscillators --------------------------------------------------
            const double baseHz = juce::MidiMessage::getMidiNoteInHertz (noteNumber)
                                  * std::pow (2.0, (double) (pitchBend * 2.0f) / 12.0);

            setupOsc (oscs[0], ld (P.oscA_oct), (int) ld (P.oscA_uni), ld (P.oscA_detune), ld (P.oscA_pan), ld (P.oscA_level), baseHz, sr);
            setupOsc (oscs[1], ld (P.oscB_oct), (int) ld (P.oscB_uni), ld (P.oscB_detune), ld (P.oscB_pan), ld (P.oscB_level), baseHz, sr);

            // --- sampler ------------------------------------------------------
            const SampleData* smp = ctx.sample;
            const float smpLevel  = ld (P.smp_level);
            const bool  smpLoop   = ld (P.smp_loop) > 0.5f;
            bool smpRun = smp != nullptr && smp->buffer.getNumSamples() > 2 && smpActive && smpLevel > 1.0e-4f;

            int smpLen = 0, smpStart = 0, smpEnd = 0;
            double smpStep = 0.0;
            const float *smpL = nullptr, *smpR = nullptr;

            if (smpRun)
            {
                smpLen   = smp->buffer.getNumSamples();
                smpStart = juce::jlimit (0, smpLen - 2, (int) (ld (P.smp_start) * (float) smpLen));
                smpEnd   = juce::jlimit (smpStart + 1, smpLen, (int) (ld (P.smp_end) * (float) smpLen));
                const double semis = (double) noteNumber + (double) (pitchBend * 2.0f) - (double) ld (P.smp_root);
                smpStep  = (smp->sampleRate / sr) * std::pow (2.0, semis / 12.0);
                smpL     = smp->buffer.getReadPointer (0);
                smpR     = smp->buffer.getNumChannels() > 1 ? smp->buffer.getReadPointer (1) : smpL;
                if (smpPos < 0.0) smpPos = (double) smpStart;
            }

            float* outL = out.getWritePointer (0, startSample);
            float* outR = out.getNumChannels() > 1 ? out.getWritePointer (1, startSample) : nullptr;

            WavetableBank::Morph mA, mB;
            const float lfoInc = lfoRate / (float) sr;

            // --- per-sample loop ---------------------------------------------
