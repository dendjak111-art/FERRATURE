#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class FerratureLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Палитра Ferrature (Cyberpunk / Industrial)
    static inline const juce::Colour bgDark      = juce::Colour (0xff121316);
    static inline const juce::Colour panelBg     = juce::Colour (0xff1a1c21);
    static inline const juce::Colour panelBorder = juce::Colour (0xff2b2f38);
    static inline const juce::Colour accentOrange = juce::Colour (0xffff5500);
    static inline const juce::Colour accentCyan   = juce::Colour (0xff00e5ff);
    static inline const juce::Colour textMain     = juce::Colour (0xffe8eaed);
    static inline const juce::Colour textMuted    = juce::Colour (0xff8a92a0);

    FerratureLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, bgDark);
        setColour (juce::Label::textColourId, textMain);
        setColour (juce::ComboBox::backgroundColourId, panelBg);
        setColour (juce::ComboBox::outlineColourId, panelBorder);
        setColour (juce::ComboBox::textColourId, textMain);
        setColour (juce::ComboBox::arrowColourId, accentCyan);
        setColour (juce::PopupMenu::backgroundColourId, panelBg);
        setColour (juce::PopupMenu::textColourId, textMain);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accentOrange.withAlpha (0.25f));
        setColour (juce::PopupMenu::highlightedTextColourId, accentOrange);
        setColour (juce::TextButton::buttonColourId, panelBg);
        setColour (juce::TextButton::buttonOnColourId, accentOrange.withAlpha (0.3f));
        setColour (juce::TextButton::textColourOffId, textMain);
        setColour (juce::TextButton::textColourOnId, accentOrange);
    }

    // Отрисовка современных плоских крутилок со светящейся дугой
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, const float rotaryStartAngle,
                           const float rotaryEndAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const float toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const float lineW = juce::jmax (2.5f, radius * 0.14f);
        const float arcRadius = radius - lineW * 0.6f;
        const auto centre = bounds.getCentre();

        // Определяем цвет акцента (по умолчанию оранжевый, если задано свойство "cyan" — циановый)
        const bool isCyan = slider.getProperties().getWithDefault ("cyan", false);
        const auto activeColour = isCyan ? accentCyan : accentOrange;

        // Фоновая дуга (трек)
        juce::Path backgroundArc;
        backgroundArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius,
                                     0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (panelBorder);
        g.strokePath (backgroundArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));

        // Активная светящаяся дуга значения
        if (slider.isEnabled())
        {
            juce::Path valueArc;
            valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius,
                                    0.0f, rotaryStartAngle, toAngle, true);

            // Мягкое свечение (glow)
            g.setColour (activeColour.withAlpha (0.25f));
            g.strokePath (valueArc, juce::PathStrokeType (lineW * 2.2f, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));

            // Основная яркая линия
            g.setColour (activeColour);
            g.strokePath (valueArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
        }

        // Внутренний корпус ручки
        const float innerRadius = arcRadius - lineW * 1.1f;
        if (innerRadius > 4.0f)
        {
            g.setColour (juce::Colour (0xff20232a));
            g.fillEllipse (centre.x - innerRadius, centre.y - innerRadius,
                           innerRadius * 2.0f, innerRadius * 2.0f);

            g.setColour (panelBorder.brighter (0.15f));
            g.drawEllipse (centre.x - innerRadius, centre.y - innerRadius,
                           innerRadius * 2.0f, innerRadius * 2.0f, 1.0f);

            // Указатель (риска) на ручке
            juce::Path pointer;
            const float pointerLength = innerRadius * 0.65f;
            const float pointerThickness = 2.0f;
            pointer.addRoundedRectangle (-pointerThickness * 0.5f, -innerRadius + 2.0f,
                                         pointerThickness, pointerLength, 1.0f);
            pointer.applyTransform (juce::AffineTransform::rotation (toAngle).translated (centre.x, centre.y));

            g.setColour (textMain);
            g.fillPath (pointer);
        }
    }

    // Стилизация кнопок (Save, Load, Init, Load Sample, Loop)
    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const bool isToggled = button.getToggleState();
        const bool isCyan = button.getProperties().getWithDefault ("cyan", false);
        const auto accent = isCyan ? accentCyan : accentOrange;

        auto baseCol = isToggled ? accent.withAlpha (0.22f) : backgroundColour;
        if (shouldDrawButtonAsDown)
            baseCol = baseCol.brighter (0.15f);
        else if (shouldDrawButtonAsHighlighted)
            baseCol = baseCol.brighter (0.08f);

        g.setColour (baseCol);
        g.fillRoundedRectangle (bounds, 4.0f);

        g.setColour (isToggled ? accent : (shouldDrawButtonAsHighlighted ? accent.withAlpha (0.6f) : panelBorder));
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
    }

    // Стилизация выпадающих списков (выбор фильтра, LFO, пресетов)
    void drawComboBox (juce::Graphics& g, int width, int height, bool,
                       int, int, int, int, juce::ComboBox& box) override
    {
        const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (bounds, 4.0f);

        g.setColour (box.hasKeyboardFocus (true) ? accentCyan : panelBorder);
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

        // Стрелочка справа
        juce::Rectangle<int> arrowZone (width - 22, 0, 16, height);
        juce::Path path;
        path.startNewSubPath ((float) arrowZone.getX() + 3.0f, (float) arrowZone.getCentreY() - 2.0f);
        path.lineTo ((float) arrowZone.getCentreX(), (float) arrowZone.getCentreY() + 3.0f);
        path.lineTo ((float) arrowZone.getRight() - 3.0f, (float) arrowZone.getCentreY() - 2.0f);

        g.setColour (accentCyan);
        g.strokePath (path, juce::PathStrokeType (1.8f));
    }
};

