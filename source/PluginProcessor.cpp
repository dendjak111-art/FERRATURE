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
            for (int i = 0; i < numSamples; ++i)
            {
                const float ae = ampEnv.getNextSample();
                if (! ampEnv.isActive())
                {
                    clearCurrentNote();
                    return;
                }

                const float fe  = filtEnv.getNextSample();
                const float lfo = nextLfo (lfoInc, lfoShape);

                if ((ctrlCounter++ & 15) == 0)   // control rate: every 16 samples
                {
                    float wA = waveA, wB = waveB;
                    if (lfoDest == 1)
                    {
                        const float mod = lfo * lfoDepth * 0.5f;
                        wA += mod; wB += mod;
                    }
                    mA = WavetableBank::makeMorph (wA);
                    mB = WavetableBank::makeMorph (wB);

                    float octaves = envAmt * fe * 5.0f;
                    if (lfoDest == 0) octaves += lfo * lfoDepth * 3.0f;
                    const float cut = juce::jlimit (20.0f, (float) (0.49 * sr), cutoff * std::exp2 (octaves));
                    filter.setCutoffFrequency (cut);
                }

                float l = 0.0f, r = 0.0f;
                renderOsc (oscs[0], mA, l, r);
                renderOsc (oscs[1], mB, l, r);

                if (smpRun)
                {
                    if (smpPos >= (double) smpEnd)
                    {
                        if (smpLoop) smpPos = (double) smpStart + std::fmod (smpPos - (double) smpStart, (double) (smpEnd - smpStart));
                        else         { smpRun = false; smpActive = false; }
                    }

                    if (smpRun)
                    {
                        const int i0    = juce::jlimit (0, smpLen - 1, (int) smpPos);
                        const int i1    = juce::jmin (i0 + 1, smpLen - 1);
                        const float fr  = (float) (smpPos - (double) i0);
                        l += (smpL[i0] + fr * (smpL[i1] - smpL[i0])) * smpLevel;
                        r += (smpR[i0] + fr * (smpR[i1] - smpR[i0])) * smpLevel;
                        smpPos += smpStep;
                    }
                }

                if (useDrive)
                {
                    l = std::tanh (l * driveGain);
                    r = std::tanh (r * driveGain);
                }

                l = filter.processSample (0, l);
                r = filter.processSample (1, r);

                const float g = ae * vel * 0.3f;
                outL[i] += l * g;
                if (outR != nullptr) outR[i] += r * g;
            }
        }

    private:
        static constexpr int kMaxUni = 7;

        struct Osc
        {
            float phase[kMaxUni] {};
            float inc[kMaxUni] {};
            float gl[kMaxUni] {};
            float gr[kMaxUni] {};
            int   uni   = 1;
            int   table = 0;
            float level = 0.0f;
        };

        void setupOsc (Osc& o, float octave, int uni, float detuneCents, float pan, float level, double baseHz, double sr)
        {
            o.uni   = juce::jlimit (1, kMaxUni, uni);
            o.level = level;
            const float norm = 1.0f / std::sqrt ((float) o.uni);
            float maxF = 0.0f;

            for (int u = 0; u < o.uni; ++u)
            {
                const float pos   = o.uni > 1 ? ((float) u / (float) (o.uni - 1)) * 2.0f - 1.0f : 0.0f;
                const double f    = baseHz * std::pow (2.0, (double) octave + (double) (pos * detuneCents) / 1200.0);
                o.inc[u]          = (float) (f / sr);
                maxF              = juce::jmax (maxF, (float) f);

                const float p     = juce::jlimit (-1.0f, 1.0f, pan + pos * 0.75f);
                const float angle = (p + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                o.gl[u]           = std::cos (angle) * norm;
                o.gr[u]           = std::sin (angle) * norm;
            }
            o.table = ctx.bank->levelForFrequency (maxF, sr);
        }

        void renderOsc (Osc& o, const WavetableBank::Morph& m, float& l, float& r) noexcept
        {
            if (o.level < 1.0e-4f) return;

            float tl = 0.0f, tr = 0.0f;
            for (int u = 0; u < o.uni; ++u)
            {
                const float v = ctx.bank->read (m, o.table, o.phase[u]);
                tl += v * o.gl[u];
                tr += v * o.gr[u];
                o.phase[u] += o.inc[u];
                if (o.phase[u] >= 1.0f) o.phase[u] -= 1.0f;
            }
            l += tl * o.level;
            r += tr * o.level;
        }

        float nextLfo (float inc, int shape) noexcept
        {
            lfoPhase += inc;
            if (lfoPhase >= 1.0f)
            {
                lfoPhase -= 1.0f;
                shValue = rng.nextFloat() * 2.0f - 1.0f;
            }

            switch (shape)
            {
                case 0:  return std::sin (juce::MathConstants<float>::twoPi * lfoPhase);
                case 1:  return 2.0f * std::abs (2.0f * lfoPhase - 1.0f) - 1.0f;
                case 2:  return 2.0f * lfoPhase - 1.0f;
                case 3:  return lfoPhase < 0.5f ? 1.0f : -1.0f;
                default: return shValue;
            }
        }

        const VoiceContext& ctx;
        Osc oscs[2];
        juce::dsp::StateVariableTPTFilter<float> filter;
        juce::ADSR ampEnv, filtEnv;
        juce::Random rng;

        int   noteNumber = 60;
        float vel = 0.0f, pitchBend = 0.0f;
        float lfoPhase = 0.0f, shValue = 0.0f;
        int   ctrlCounter = 0;
        double smpPos = -1.0;
        bool   smpActive = false;
    };

    //==========================================================================
    // Pitch detection (YIN) used to auto-assign the sample's root note
    //==========================================================================
    int detectRootNote (const juce::AudioBuffer<float>& buf, double sr, bool& confident)
    {
        confident = false;
        const int len = buf.getNumSamples();
        if (sr <= 0.0 || len < 1024) return 60;

        const int maxLag = juce::jmin ((int) (sr / 50.0), len / 2);
        const int minLag = juce::jmax (2, (int) (sr / 2000.0));
        int W = 2048;
        int start = len / 8;
        if (start + W + maxLag >= len) start = 0;
        if (start + W + maxLag >= len) W = len - maxLag - 1;
        if (W < 256 || maxLag <= minLag + 2) return 60;

        const int segLen = W + maxLag + 1;
        std::vector<float> x ((size_t) segLen, 0.0f);
        const int chans = buf.getNumChannels();
        for (int c = 0; c < chans; ++c)
        {
            const float* src = buf.getReadPointer (c);
            for (int i = 0; i < segLen; ++i)
                x[(size_t) i] += src[start + i] / (float) chans;
        }

        std::vector<float> cmnd ((size_t) maxLag + 1, 1.0f);
        float running = 0.0f;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            float sum = 0.0f;
            for (int i = 0; i < W; ++i)
            {
                const float d = x[(size_t) i] - x[(size_t) (i + tau)];
                sum += d * d;
            }
            running += sum;
            cmnd[(size_t) tau] = running > 0.0f ? sum * (float) tau / running : 1.0f;
        }

        int best = -1;
        for (int tau = minLag; tau < maxLag; ++tau)
        {
            if (cmnd[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < maxLag && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
        {
            float mn = 1.0e9f;
            for (int tau = minLag; tau < maxLag; ++tau)
                if (cmnd[(size_t) tau] < mn) { mn = cmnd[(size_t) tau]; best = tau; }
        }
        if (best < 1 || cmnd[(size_t) best] > 0.4f) return 60;

        float tauF = (float) best;
        if (best > 1 && best < maxLag)
        {
            const float a = cmnd[(size_t) best - 1], b = cmnd[(size_t) best], c = cmnd[(size_t) best + 1];
            const float denom = a - 2.0f * b + c;
            if (std::abs (denom) > 1.0e-9f) tauF += 0.5f * (a - c) / denom;
        }

        const double freq = sr / (double) tauF;
        const double midi = 69.0 + 12.0 * std::log2 (freq / 440.0);
        confident = true;
        return juce::jlimit (0, 127, (int) std::lround (midi));
    }
}

//==============================================================================
// Processor
//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout FerratureAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto addF = [&] (const char* id, const juce::String& name, float lo, float hi, float def,
                     float skew = 1.0f, const juce::String& unit = {})
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> (lo, hi, 0.0f, skew), def,
            juce::AudioParameterFloatAttributes().withLabel (unit)));
    };
    auto addI = [&] (const char* id, const juce::String& name, int lo, int hi, int def)
    {
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, 1 }, name, lo, hi, def));
    };
    auto addC = [&] (const char* id, const juce::String& name, const juce::StringArray& choices, int def)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, choices, def));
    };
    auto addB = [&] (const char* id, const juce::String& name, bool def)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
    };

    // OSC A  (wave position: 0 Sine .. 0.25 Tri .. 0.5 Saw .. 0.75 Square .. 1 Harmonic)
    addF (ParamIDs::oscA_wave,   "A Wave",   0.0f, 1.0f, 0.5f);
    addI (ParamIDs::oscA_oct,    "A Octave", -3, 3, 0);
    addI (ParamIDs::oscA_uni,    "A Unison", 1, 7, 1);
    addF (ParamIDs::oscA_detune, "A Detune", 0.0f, 100.0f, 20.0f, 1.0f, "ct");
    addF (ParamIDs::oscA_pan,    "A Pan",   -1.0f, 1.0f, 0.0f);
    addF (ParamIDs::oscA_level,  "A Level",  0.0f, 1.0f, 0.8f);

    // OSC B
    addF (ParamIDs::oscB_wave,   "B Wave",   0.0f, 1.0f, 0.75f);
    addI (ParamIDs::oscB_oct,    "B Octave", -3, 3, 0);
    addI (ParamIDs::oscB_uni,    "B Unison", 1, 7, 1);
    addF (ParamIDs::oscB_detune, "B Detune", 0.0f, 100.0f, 20.0f, 1.0f, "ct");
    addF (ParamIDs::oscB_pan,    "B Pan",   -1.0f, 1.0f, 0.0f);
    addF (ParamIDs::oscB_level,  "B Level",  0.0f, 1.0f, 0.0f);

    // Sampler
    addF (ParamIDs::smp_level,   "Sampler Level", 0.0f, 1.0f, 0.8f);
    addF (ParamIDs::smp_start,   "Sample Start",  0.0f, 1.0f, 0.0f);
    addF (ParamIDs::smp_end,     "Sample End",    0.0f, 1.0f, 1.0f);
    addB (ParamIDs::smp_loop,    "Sample Loop",   false);
    addI (ParamIDs::smp_root,    "Sample Root",   0, 127, 60);

    // Filter
    addC (ParamIDs::flt_type,    "Filter Type", { "LowPass", "HighPass", "BandPass" }, 0);
    addF (ParamIDs::flt_cutoff,  "Cutoff",    20.0f, 20000.0f, 12000.0f, 0.3f, "Hz");
    addF (ParamIDs::flt_res,     "Resonance", 0.5f, 10.0f, 0.707f, 0.5f);
    addF (ParamIDs::flt_drive,   "Drive",     0.0f, 1.0f, 0.0f);
    addF (ParamIDs::flt_envamt,  "Env Amount", -1.0f, 1.0f, 0.0f);

    // Envelopes
    addF (ParamIDs::aenv_a, "Amp Attack",  0.001f, 5.0f, 0.005f, 0.35f, "s");
    addF (ParamIDs::aenv_d, "Amp Decay",   0.001f, 5.0f, 0.3f,   0.35f, "s");
    addF (ParamIDs::aenv_s, "Amp Sustain", 0.0f,   1.0f, 0.8f);
    addF (ParamIDs::aenv_r, "Amp Release", 0.001f, 8.0f, 0.2f,   0.35f, "s");
    addF (ParamIDs::fenv_a, "Mod Attack",  0.001f, 5.0f, 0.005f, 0.35f, "s");
    addF (ParamIDs::fenv_d, "Mod Decay",   0.001f, 5.0f, 0.4f,   0.35f, "s");
    addF (ParamIDs::fenv_s, "Mod Sustain", 0.0f,   1.0f, 0.3f);
    addF (ParamIDs::fenv_r, "Mod Release", 0.001f, 8.0f, 0.3f,   0.35f, "s");

    // LFO
    addC (ParamIDs::lfo_shape, "LFO Shape", { "Sine", "Triangle", "Saw", "Square", "S&H" }, 0);
    addF (ParamIDs::lfo_rate,  "LFO Rate",  0.05f, 20.0f, 2.0f, 0.4f, "Hz");
    addF (ParamIDs::lfo_depth, "LFO Depth", 0.0f, 1.0f, 0.0f);
    addC (ParamIDs::lfo_dest,  "LFO Dest",  { "Cutoff", "WT Position" }, 0);

    // FX
    addF (ParamIDs::fx_chorus_rate,  "Chorus Rate",  0.1f, 5.0f, 0.8f, 0.5f, "Hz");
    addF (ParamIDs::fx_chorus_depth, "Chorus Depth", 0.0f, 1.0f, 0.4f);
    addF (ParamIDs::fx_chorus_mix,   "Chorus Mix",   0.0f, 1.0f, 0.0f);
    addF (ParamIDs::fx_delay_time,   "Delay Time",   1.0f, 1500.0f, 350.0f, 0.5f, "ms");
    addF (ParamIDs::fx_delay_fb,     "Delay Feedback", 0.0f, 0.95f, 0.35f);
    addF (ParamIDs::fx_delay_mix,    "Delay Mix",    0.0f, 1.0f, 0.0f);
    addF (ParamIDs::fx_reverb_size,  "Reverb Size",  0.0f, 1.0f, 0.5f);
    addF (ParamIDs::fx_reverb_damp,  "Reverb Damp",  0.0f, 1.0f, 0.5f);
    addF (ParamIDs::fx_reverb_mix,   "Reverb Mix",   0.0f, 1.0f, 0.0f);

    addF (ParamIDs::master_gain, "Master", 0.0f, 1.5f, 0.7f);

    return layout;
}

FerratureAudioProcessor::FerratureAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FERRATURE_STATE", createParameterLayout())
{
    formatManager.registerBasicFormats();

    ctx.p.bind (apvts);
    ctx.bank = &bank;

    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new FerratureVoice (ctx));
    synth.addSound (new FerratureSound());

    for (auto& s : scopeData) s.store (0.0f);

    apvts.state.setProperty ("presetName", "Init", nullptr);
}

FerratureAudioProcessor::~FerratureAudioProcessor() {}

bool FerratureAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void FerratureAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2u };

    chorus.prepare (spec);
    chorus.setCentreDelay (7.0f);
    chorus.setFeedback (0.0f);
    chorus.reset();

    delayLine.setMaximumDelayInSamples ((int) (sampleRate * 2.0) + 4);
    delayLine.prepare (spec);
    delayLine.reset();
    delaySamplesSmoothed.reset (sampleRate, 0.05);
    delaySamplesSmoothed.setCurrentAndTargetValue (ctx.p.fx_delay_time->load() * 0.001f * (float) sampleRate);

    reverb.prepare (spec);
    reverb.reset();

    lastMasterGain = ctx.p.master_gain->load();
}

void FerratureAudioProcessor::releaseResources() {}

void FerratureAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();

    // Pick up a newly loaded sample (never blocks the audio thread)
    {
        juce::SpinLock::ScopedTryLockType tl (sampleLock);
        if (tl.isLocked() && audioSample != currentSample)
            audioSample = currentSample;
    }
    ctx.sample = audioSample.get();

    keyboardState.processNextMidiBuffer (midi, 0, n, true);
    synth.renderNextBlock (buffer, midi, 0, n);

    const int numCh = juce::jmin (2, buffer.getNumChannels());

    // --- Chorus ---------------------------------------------------------------
    const float chorusMix = ctx.p.fx_chorus_mix->load();
    if (chorusMix > 0.001f && numCh == 2)
    {
        chorus.setRate  (ctx.p.fx_chorus_rate->load());
        chorus.setDepth (ctx.p.fx_chorus_depth->load());
        chorus.setMix   (chorusMix);
        juce::dsp::AudioBlock<float> block (buffer);
        chorus.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    // --- Delay ----------------------------------------------------------------
    const float delayMix = ctx.p.fx_delay_mix->load();
    if (delayMix > 0.001f)
    {
        const float fb = ctx.p.fx_delay_fb->load();
        delaySamplesSmoothed.setTargetValue (juce::jlimit (1.0f, (float) (currentSampleRate * 2.0),
                                                           ctx.p.fx_delay_time->load() * 0.001f * (float) currentSampleRate));
        float* ch[2] = { buffer.getWritePointer (0), numCh > 1 ? buffer.getWritePointer (1) : nullptr };

        for (int i = 0; i < n; ++i)
        {
            const float d = delaySamplesSmoothed.getNextValue();
            for (int c = 0; c < numCh; ++c)
            {
                const float in      = ch[c][i];
                const float delayed = delayLine.popSample (c, d, true);
                delayLine.pushSample (c, in + delayed * fb);
                ch[c][i] = in + delayed * delayMix;
            }
        }
    }

    // --- Reverb ---------------------------------------------------------------
    const float revMix = ctx.p.fx_reverb_mix->load();
    if (revMix > 0.001f && numCh == 2)
    {
        juce::dsp::Reverb::Parameters rp;
        rp.roomSize   = ctx.p.fx_reverb_size->load();
        rp.damping    = ctx.p.fx_reverb_damp->load();
        rp.wetLevel   = revMix;
        rp.dryLevel   = 1.0f - revMix * 0.5f;
        rp.width      = 1.0f;
        rp.freezeMode = 0.0f;
        reverb.setParameters (rp);
        juce::dsp::AudioBlock<float> block (buffer);
        reverb.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    // --- Master + safety limiter ------------------------------------------------
    const float g = ctx.p.master_gain->load();
    buffer.applyGainRamp (0, n, lastMasterGain, g);
    lastMasterGain = g;

    for (int c = 0; c < buffer.getNumChannels(); ++c)
    {
        float* d = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i)
            d[i] = juce::jlimit (-1.0f, 1.0f, d[i]);
    }

    // --- Scope feed -----------------------------------------------------------
    int w = scopeWritePos.load (std::memory_order_relaxed);
    const float* l = buffer.getReadPointer (0);
    const float* r = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : l;
    for (int i = 0; i < n; ++i)
    {
        scopeData[(size_t) w].store (0.5f * (l[i] + r[i]), std::memory_order_relaxed);
        w = (w + 1) & (scopeSize - 1);
    }
    scopeWritePos.store (w, std::memory_order_release);
}

void FerratureAudioProcessor::copyScope (float* dest, int num) const
{
    num = juce::jlimit (1, scopeSize, num);
    const int w = scopeWritePos.load (std::memory_order_acquire);
    for (int i = 0; i < num; ++i)
        dest[i] = scopeData[(size_t) ((w - num + i + scopeSize) & (scopeSize - 1))].load (std::memory_order_relaxed);
}

juce::AudioProcessorEditor* FerratureAudioProcessor::createEditor()
{
    return new FerratureAudioProcessorEditor (*this);
}

//==============================================================================
// State
//==============================================================================
void FerratureAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void FerratureAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            loadStateFromXml (*xml);
}

void FerratureAudioProcessor::loadStateFromXml (const juce::XmlElement& xml)
{
    apvts.replaceState (juce::ValueTree::fromXml (xml));
    restoreSampleFromState();

    const auto name = apvts.state.getProperty ("presetName").toString();
    currentPresetName = name.isNotEmpty() ? name : juce::String ("Init");
}

//==============================================================================
// Sampler loading
//==============================================================================
void FerratureAudioProcessor::publishSample (std::shared_ptr<const SampleData> newSample)
{
    {
        const juce::SpinLock::ScopedLockType sl (sampleLock);
        if (currentSample != nullptr)
            retiredSamples.push_back (currentSample);
        currentSample = std::move (newSample);
    }

    // Free retired samples that the audio thread no longer references (never on the audio thread)
    retiredSamples.erase (std::remove_if (retiredSamples.begin(), retiredSamples.end(),
                                          [] (const auto& p) { return p.use_count() == 1; }),
                          retiredSamples.end());
}

bool FerratureAudioProcessor::loadSample (const juce::File& file, bool applyDetectedRoot)
{
    if (! file.existsAsFile())
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return false;

    const int maxLen = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 60.0));
    const int chans  = (int) juce::jmin (2u, reader->numChannels);

    auto data = std::make_shared<SampleData>();
    data->buffer.setSize (chans, maxLen);
    data->buffer.clear();

    if (! reader->read (&data->buffer, 0, maxLen, 0, true, chans > 1))
        return false;

    data->sampleRate = reader->sampleRate;
    data->name       = file.getFileName();

    bool confident = false;
    data->detectedRoot = detectRootNote (data->buffer, data->sampleRate, confident);

    const int root = data->detectedRoot;
    publishSample (std::move (data));

    loadedSamplePath = file.getFullPathName();
    apvts.state.setProperty ("samplePath", loadedSamplePath, nullptr);

    if (applyDetectedRoot)
        if (auto* p = apvts.getParameter (ParamIDs::smp_root))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) root));

    return true;
}

void FerratureAudioProcessor::clearSample()
{
    publishSample (nullptr);
    loadedSamplePath.clear();
    apvts.state.removeProperty ("samplePath", nullptr);
}

void FerratureAudioProcessor::restoreSampleFromState()
{
    const juce::String path = apvts.state.getProperty ("samplePath").toString();

    if (path.isEmpty())
    {
        if (currentSample != nullptr) clearSample();
        return;
    }

    if (path == loadedSamplePath && currentSample != nullptr)
        return;

    if (! loadSample (juce::File (path), false))
        clearSample();
}

//==============================================================================
// Presets
//==============================================================================
juce::File FerratureAudioProcessor::getPresetDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Ferrature").getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

juce::StringArray FerratureAudioProcessor::getPresetNames() const
{
    juce::StringArray names;
    for (const auto& f : getPresetDirectory().findChildFiles (juce::File::findFiles, false, "*.ferrature"))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

bool FerratureAudioProcessor::savePreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return false;
    return saveToFile (getPresetDirectory().getChildFile (clean + ".ferrature"), clean);
}

bool FerratureAudioProcessor::loadPreset (const juce::String& name)
{
    return loadFromFile (getPresetDirectory().getChildFile (name + ".ferrature"));
}

bool FerratureAudioProcessor::saveToFile (const juce::File& file, const juce::String& presetName)
{
    const auto name = presetName.isNotEmpty() ? presetName : file.getFileNameWithoutExtension();
    apvts.state.setProperty ("presetName", name, nullptr);

    auto xml = apvts.copyState().createXml();
    if (xml == nullptr || ! xml->writeTo (file))
        return false;

    currentPresetName = name;
    return true;
}

bool FerratureAudioProcessor::loadFromFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return false;

    loadStateFromXml (*xml);

    if (apvts.state.getProperty ("presetName").toString().isEmpty())
    {
        currentPresetName = file.getFileNameWithoutExtension();
        apvts.state.setProperty ("presetName", currentPresetName, nullptr);
    }
    return true;
}

void FerratureAudioProcessor::initPreset()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());

    clearSample();
    currentPresetName = "Init";
    apvts.state.setProperty ("presetName", currentPresetName, nullptr);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FerratureAudioProcessor();
}
