#include "PluginEditor.h"

namespace Colors
{
    const juce::Colour bgDark      = juce::Colour (0xff12141a);
    const juce::Colour panelBg     = juce::Colour (0xff1b1f29);
    const juce::Colour panelBorder = juce::Colour (0xff2c3344);
    const juce::Colour accent      = juce::Colour (0xff38bdf8);
    const juce::Colour accentAlt   = juce::Colour (0xffa855f7);
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

    g.setColour (Colors::bgDark);
    g.fillEllipse (centre.x - radius + 4.0f, centre.y - radius + 4.0f, (radius - 4.0f) * 2.0f, (radius - 4.0f) * 2.0f);

    juce::Path backgroundArc;
    backgroundArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (Colors::panelBorder);
    g.strokePath (backgroundArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (sliderPos > 0.0f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, toAngle, true);
        g.setColour (Colors::accent);
        g.strokePath (valueArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

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
// SimpleMidiKeyboard
//==============================================================================
SimpleMidiKeyboard::SimpleMidiKeyboard (juce::MidiKeyboardState& state)
    : keyboardState (state)
{
}

void SimpleMidiKeyboard::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const int numWhiteKeys = numOctaves * 7;
    const float whiteW = bounds.getWidth() / (float) numWhiteKeys;
    const int whiteOffsets[7] = { 0, 2, 4, 5, 7, 9, 11 };

    // Белые клавиши
    for (int i = 0; i < numWhiteKeys; ++i)
    {
        int note = startNote + (i / 7) * 12 + whiteOffsets[i % 7];
        auto keyRect = juce::Rectangle<float> (i * whiteW, 0.0f, whiteW, bounds.getHeight()).reduced (0.5f);
        g.setColour (note == activeNote ? Colors::accent : juce::Colour (0xffe2e8f0));
        g.fillRoundedRectangle (keyRect, 2.0f);
        g.setColour (Colors::bgDark);
        g.drawRoundedRectangle (keyRect, 2.0f, 1.0f);
    }

    // Черные клавиши
    const int blackSemis[5] = { 1, 3, 6, 8, 10 };
    const float blackPos[5] = { 0.65f, 1.65f, 3.65f, 4.65f, 5.65f };
    const float blackW = whiteW * 0.62f;
    const float blackH = bounds.getHeight() * 0.62f;

    for (int oct = 0; oct < numOctaves; ++oct)
    {
        for (int b = 0; b < 5; ++b)
        {
            int note = startNote + oct * 12 + blackSemis[b];
            float x = (oct * 7 + blackPos[b]) * whiteW;
            auto keyRect = juce::Rectangle<float> (x, 0.0f, blackW, blackH);
            g.setColour (note == activeNote ? Colors::accentAlt : juce::Colour (0xff0f172a));
            g.fillRoundedRectangle (keyRect, 2.0f);
            g.setColour (Colors::panelBorder);
            g.drawRoundedRectangle (keyRect, 2.0f, 1.0f);
        }
    }
}

int SimpleMidiKeyboard::noteAtPosition (juce::Point<float> pos) const
{
    auto bounds = getLocalBounds().toFloat();
    if (! bounds.contains (pos))
        return -1;

    const int numWhiteKeys = numOctaves * 7;
    const float whiteW = bounds.getWidth() / (float) numWhiteKeys;
    const float blackW = whiteW * 0.62f;
    const float blackH = bounds.getHeight() * 0.62f;
    const int blackSemis[5] = { 1, 3, 6, 8, 10 };
    const float blackPos[5] = { 0.65f, 1.65f, 3.65f, 4.65f, 5.65f };

    if (pos.y <= blackH)
    {
        for (int oct = 0; oct < numOctaves; ++oct)
        {
            for (int b = 0; b < 5; ++b)
            {
                float x = (oct * 7 + blackPos[b]) * whiteW;
                if (pos.x >= x && pos.x <= x + blackW)
                    return startNote + oct * 12 + blackSemis[b];
            }
        }
    }

    const int whiteOffsets[7] = { 0, 2, 4, 5, 7, 9, 11 };
    int whiteIdx = juce::jlimit (0, numWhiteKeys - 1, (int) (pos.x / whiteW));
    return startNote + (whiteIdx / 7) * 12 + whiteOffsets[whiteIdx % 7];
}

void SimpleMidiKeyboard::mouseDown (const juce::MouseEvent& e)
{
    int note = noteAtPosition (e.position);
    if (note >= 0)
    {
        activeNote = note;
        keyboardState.noteOn (1, activeNote, 0.9f);
        repaint();
    }
}

void SimpleMidiKeyboard::mouseDrag (const juce::MouseEvent& e)
{
    int note = noteAtPosition (e.position);
    if (note != activeNote)
    {
        if (activeNote >= 0)
            keyboardState.noteOff (1, activeNote, 0.0f);
        activeNote = note;
        if (activeNote >= 0)
            keyboardState.noteOn (1, activeNote, 0.9f);
