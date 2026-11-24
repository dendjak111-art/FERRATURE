#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "CustomLookAndFeel.h"

class FerratureScope : public juce::Component
{
public:
    explicit FerratureScope (FerratureAudioProcessor& p) : processor (p) {}
    void paint (juce::Graphics& g) override;

private:
    FerratureAudioProcessor& processor;
    std::array<float, 512> buffer {};
};

class FerratureAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      private juce::Timer
{
public:
    explicit FerratureAudioProcessorEditor (FerratureAudioProcessor&);
    ~FerratureAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshPresetBox();
    void loadSampleDialog();
    void savePresetDialog();
    void loadPresetDialog();

    FerratureAudioProcessor& audioProcessor;
    FerratureLookAndFeel customLnf;

    FerratureScope scopeView;
    juce::MidiKeyboardComponent midiKeyboard;

    juce::ComboBox presetBox;
    juce::TextButton savePresetBtn { "SAVE" };
    juce::TextButton loadPresetBtn { "LOAD" };
    juce::TextButton initPresetBtn { "INIT" };

    juce::TextButton loadSampleBtn { "LOAD SAMPLE" };
    juce::TextButton clearSampleBtn { "X" };
    juce::Label sampleNameLabel;
    juce::ToggleButton smpLoopBtn { "LOOP" };

    juce::ComboBox fltTypeBox;
    juce::ComboBox lfoShapeBox;
    juce::ComboBox lfoDestBox;

    struct KnobWithLabel
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    KnobWithLabel oscAWave, oscAOct, oscAUni, oscADetune, oscAPan, oscALevel;
    KnobWithLabel oscBWave, oscBOct, oscBUni, oscBDetune, oscBPan, oscBLevel;
    KnobWithLabel smpLevel, smpStart, smpEnd, smpRoot;
    KnobWithLabel fltCutoff, fltRes, fltDrive, fltEnvAmt;
    KnobWithLabel aenvA, aenvD, aenvS, aenvR;
    KnobWithLabel fenvA, fenvD, fenvS, fenvR;
    KnobWithLabel lfoRate, lfoDepth;
    KnobWithLabel chorusRate, chorusDepth, chorusMix;
    KnobWithLabel delayTime, delayFb, delayMix;
    KnobWithLabel reverbSize, reverbDamp, reverbMix;
    KnobWithLabel masterGain;

    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<ButtonAttachment> smpLoopAtt;
    std::unique_ptr<ComboAttachment>  fltTypeAtt;
    std::unique_ptr<ComboAttachment>  lfoShapeAtt;
    std::unique_ptr<ComboAttachment>  lfoDestAtt;

    std::unique_ptr<juce::FileChooser> fileChooser;

    void setupKnob (KnobWithLabel& k, const juce::String& name, const char* paramID, bool isCyan = false);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FerratureAudioProcessorEditor)
};
