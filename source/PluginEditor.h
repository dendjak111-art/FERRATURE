#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

#include <array>
#include <memory>
#include <vector>

//==============================================================================
// Кастомный стиль (LookAndFeel)
//==============================================================================
class FerratureLookAndFeel : public juce::LookAndFeel_V4
{
public:
    FerratureLookAndFeel();

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override;

    void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox& box) override;
};

//==============================================================================
// Превью формы волны для OSC A и OSC B
//==============================================================================
class WavePreview : public juce::Component
{
public:
    WavePreview (const WavetableBank& bank, std::atomic<float>* waveParam);
    void paint (juce::Graphics& g) override;

private:
    const WavetableBank& wavetableBank;
    std::atomic<float>* wavePos = nullptr;
};

//==============================================================================
// Живой осциллограф
//==============================================================================
class OscilloscopeView : public juce::Component
{
public:
    explicit OscilloscopeView (FerratureAudioProcessor& p);
    void paint (juce::Graphics& g) override;

private:
    FerratureAudioProcessor& processor;
    std::array<float, 512> buffer {};
};

//==============================================================================
// Встроенная экранная MIDI-клавиатура (не требует дополнительных модулей JUCE)
//==============================================================================
class SimpleMidiKeyboard : public juce::Component
{
public:
    explicit SimpleMidiKeyboard (juce::MidiKeyboardState& state);
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

private:
    int noteAtPosition (juce::Point<float> pos) const;
    juce::MidiKeyboardState& keyboardState;
    int activeNote = -1;
    static constexpr int startNote = 48; // C3
    static constexpr int numOctaves = 3; // 3 октавы (C3 - B5)
};

//==============================================================================
// Виджет ручки с подписью
//==============================================================================
struct KnobControl
{
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    void init (juce::Component& parent,
               juce::AudioProcessorValueTreeState& apvts,
               const juce::String& paramID,
               const juce::String& name);

    void setBounds (juce::Rectangle<int> area);
};

//==============================================================================
// Виджет выпадающего списка с подписью
//==============================================================================
struct BoxControl
{
    juce::ComboBox box;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;

    void init (juce::Component& parent,
               juce::AudioProcessorValueTreeState& apvts,
               const juce::String& paramID,
               const juce::String& name);

    void setBounds (juce::Rectangle<int> area);
};

//==============================================================================
// Главный редактор плагина Ferrature
//==============================================================================
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

    FerratureAudioProcessor& audioProcessor;
    FerratureLookAndFeel customLookAndFeel;

    // Верхняя панель
    juce::ComboBox presetBox;
    juce::TextButton savePresetBtn { "Save" };
    juce::TextButton initPresetBtn { "Init" };
    juce::TextButton loadSampleBtn { "Load Sample" };
    juce::TextButton clearSampleBtn { "X" };
    juce::Label sampleNameLabel;
    OscilloscopeView scope;

    // Превью волн
    WavePreview oscAPreview;
    WavePreview oscBPreview;

    // OSC A
    KnobControl oscAWave, oscAOct, oscAUni, oscADetune, oscAPan, oscALevel;

    // OSC B
    KnobControl oscBWave, oscBOct, oscBUni, oscBDetune, oscBPan, oscBLevel;

    // Sampler
    KnobControl smpLevel, smpStart, smpEnd, smpRoot;
    juce::ToggleButton smpLoopBtn { "Loop" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> smpLoopAtt;

    // Filter
    BoxControl fltType;
    KnobControl fltCutoff, fltRes, fltDrive, fltEnvAmt;

    // Amp Envelope
    KnobControl aenvA, aenvD, aenvS, aenvR;

    // Filter Envelope
    KnobControl fenvA, fenvD, fenvS, fenvR;

    // LFO
    BoxControl lfoShape, lfoDest;
    KnobControl lfoRate, lfoDepth;

    // FX
    KnobControl chorusRate, chorusDepth, chorusMix;
    KnobControl delayTime, delayFb, delayMix;
    KnobControl reverbSize, reverbDamp, reverbMix;

    // Master
    KnobControl masterGain;

    // Клавиатура
    SimpleMidiKeyboard midiKeyboard;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FerratureAudioProcessorEditor)
};
