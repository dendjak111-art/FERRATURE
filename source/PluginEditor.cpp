#include "PluginEditor.h"

namespace Colors
{
    const juce::Colour bgDark      = juce::Colour (0xff12141a);
    const juce::Colour panelBg     = juce::Colour (0xff1b1f29);
    const juce::Colour panelBorder = juce::Colour (0xff2c3344);
    const juce::Colour accent      = juce::Colour (0xff38bdf8); // Голубой неон
    const juce::Colour accentAlt   = juce::Colour (0xffa855f7); // Фиолетовый акцент
    const juce::Colour textMain    = juce::Colour (0xfff1f5f9);
    const juce::Colour textDim     = juce::Colour (0xff94a3b8);
}

//==============================================================================
// FerratureLookAndFeel
//==============================================================================
FerratureLookAndFeel::FerratureLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Colors::bgDark);
    setColour (juce::Label::textColourId, Colors::textMain);
    setColour (juce::ComboBox::backgroundColourId, Colors::bgDark);
    setColour (juce::ComboBox::textColourId, Colors::textMain);
    setColour (juce::ComboBox::outlineColourId, Colors::panelBorder);
    setColour (juce::PopupMenu::backgroundColourId, Colors::panelBg);
    setColour (juce::PopupMenu::textColourId, Colors::textMain);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colors::accent.withAlpha (0.3f));
    setColour (juce::TextButton::buttonColourId, Colors::panelBorder);
    setColour (juce::TextButton::textColourOffId, Colors::textMain);
}

void FerratureLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPos, float rotaryStartAngle,
                                             float rotaryEndAngle, juce::Slider&)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    auto lineW = 3.0f;
    auto arcRadius = radius - lineW * 0.5f;
    auto centre = bounds.getCentre();

    // Внутренний круг ручки
    g.setColour (Colors::bgDark);
    g.fillEllipse (centre.x - radius + 4.0f, centre.y - radius + 4.0f, (radius - 4.0f) * 2.0f, (radius - 4.0f) * 2.0f);

    // Фоновая дуга
    juce::Path backgroundArc;
    backgroundArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (Colors::panelBorder);
    g.strokePath (backgroundArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Активная дуга
    if (sliderPos > 0.0f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, toAngle, true);
        g.setColour (Colors::accent);
        g.strokePath (valueArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Указатель на ручке
    juce::Path thumb;
    auto thumbLength = radius * 0.55f;
    thumb.addRoundedRectangle (-1.5f, -radius + 5.0f, 3.0f, thumbLength, 1.5f);
    thumb.applyTransform (juce::AffineTransform::rotation (toAngle).translated (centre.x, centre.y));
    g.setColour (Colors::textMain);
    g.fillPath (thumb);
}

void FerratureLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                       int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat();
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 4.0f);

    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    juce::Path arrow;
    auto arrowZone = bounds.removeFromRight (20.0f).reduced (6.0f, 8.0f);
    arrow.addTriangle (arrowZone.getX(), arrowZone.getY(),
                       arrowZone.getRight(), arrowZone.getY(),
                       arrowZone.getCentreX(), arrowZone.getBottom());
    g.setColour (Colors::textDim);
    g.fillPath (arrow);
}

//==============================================================================
// WavePreview
//==============================================================================
WavePreview::WavePreview (const WavetableBank& bank, std::atomic<float>* waveParam)
    : wavetableBank (bank), wavePos (waveParam)
{
}

void WavePreview::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (Colors::bgDark);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (Colors::panelBorder);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    const float pos = (wavePos != nullptr) ? wavePos->load() : 0.0f;
    juce::Path wavePath;
    const int numPoints = 64;

    for (int i = 0; i <= numPoints; ++i)
    {
        float phase = (float) i / (float) numPoints;
        float sample = wavetableBank.previewSample (pos, phase);
        float px = bounds.getX() + 4.0f + phase * (bounds.getWidth() - 8.0f);
        float py = bounds.getCentreY() - sample * (bounds.getHeight() * 0.38f);

        if (i == 0)
            wavePath.startNewSubPath (px, py);
        else
            wavePath.lineTo (px, py);
    }

    g.setColour (Colors::accent);
    g.strokePath (wavePath, juce::PathStrokeType (1.8f));
}

//==============================================================================
// OscilloscopeView
//==============================================================================
OscilloscopeView::OscilloscopeView (FerratureAudioProcessor& p)
    : processor (p)
{
}

void OscilloscopeView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (Colors::bgDark);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (Colors::panelBorder);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    processor.copyScope (buffer.data(), (int) buffer.size());

    juce::Path scopePath;
    for (size_t i = 0; i < buffer.size(); ++i)
    {
        float normX = (float) i / (float) (buffer.size() - 1);
        float px = bounds.getX() + 4.0f + normX * (bounds.getWidth() - 8.0f);
        float clamped = juce::jlimit (-1.0f, 1.0f, buffer[i]);
        float py = bounds.getCentreY() - clamped * (bounds.getHeight() * 0.42f);

        if (i == 0)
            scopePath.startNewSubPath (px, py);
        else
            scopePath.lineTo (px, py);
    }

    g.setColour (Colors::accentAlt);
    g.strokePath (scopePath, juce::PathStrokeType (1.6f));
}

//==============================================================================
// KnobControl & BoxControl
//==============================================================================
void KnobControl::init (juce::Component& parent,
                        juce::AudioProcessorValueTreeState& apvts,
                        const juce::String& paramID,
                        const juce::String& name)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 14);
    slider.setColour (juce::Slider::textBoxTextColourId, Colors::textDim);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    parent.addAndMakeVisible (slider);

    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::Font (11.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, Colors::textMain);
    parent.addAndMakeVisible (label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);
}

void KnobControl::setBounds (juce::Rectangle<int> area)
{
    label.setBounds (area.removeFromTop (15));
    slider.setBounds (area);
}

void BoxControl::init (juce::Component& parent,
                       juce::AudioProcessorValueTreeState& apvts,
                       const juce::String& paramID,
                       const juce::String& name)
{
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (paramID)))
    {
        box.addItemList (param->choices, 1);
    }
    parent.addAndMakeVisible (box);

    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::Font (11.0f, juce::Font::bold));
    parent.addAndMakeVisible (label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, paramID, box);
}

void BoxControl::setBounds (juce::Rectangle<int> area)
{
    label.setBounds (area.removeFromTop (15));
    box.setBounds (area.reduced (4, 6));
}

//==============================================================================
// FerratureAudioProcessorEditor
//==============================================================================
FerratureAudioProcessorEditor::FerratureAudioProcessorEditor (FerratureAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      scope (p),
      oscAPreview (p.getWavetableBank(), p.apvts.getRawParameterValue (ParamIDs::oscA_wave)),
      oscBPreview (p.getWavetableBank(), p.apvts.getRawParameterValue (ParamIDs::oscB_wave)),
      midiKeyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&customLookAndFeel);

    auto& apvts = audioProcessor.apvts;

    // Верхняя панель
    addAndMakeVisible (presetBox);
    presetBox.onChange = [this]
    {
        if (presetBox.getSelectedId() > 0)
            audioProcessor.loadPreset (presetBox.getText());
    };
    refreshPresetBox();

    addAndMakeVisible (savePresetBtn);
    savePresetBtn.onClick = [this] { savePresetDialog(); };

    addAndMakeVisible (initPresetBtn);
    initPresetBtn.onClick = [this]
    {
