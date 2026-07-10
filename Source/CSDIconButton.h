/*
  ==============================================================================

    CSDIconButton.h
    Small icon button for a 3D view's camera-control cluster: a magnifying
    glass (zoom in / out) or a directional arrow (pan up/down/left/right).

    Shared by every orbitable 3D view - the CSD waterfall (CSDView) and the
    sweep-capture surfaces (SweepView, SweepCurveDisplay) - so the zoom
    buttons and D-pad panner look and behave identically across modes.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class CSDIconButton  : public juce::Button
{
public:
    enum class Icon { ZoomIn, ZoomOut, Up, Down, Left, Right };

    explicit CSDIconButton (Icon i) : juce::Button ({}), icon (i) {}

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto b = getLocalBounds().toFloat();
        const float corner = b.getWidth() * 0.18f;
        g.setColour (juce::Colour (down ? 0xff3c4049 : over ? 0xff2d3038 : 0xff202329));
        g.fillRoundedRectangle (b, corner);
        g.setColour (juce::Colour (0xff44484f));
        g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);

        g.setColour (juce::Colours::whitesmoke.withAlpha (0.85f));
        const auto  c = b.getCentre();
        const float s = juce::jmin (b.getWidth(), b.getHeight());

        if (icon == Icon::ZoomIn || icon == Icon::ZoomOut)
        {
            const float r = s * 0.22f;
            const juce::Point<float> gc (c.x - s * 0.06f, c.y - s * 0.06f);
            g.drawEllipse (gc.x - r, gc.y - r, r * 2.0f, r * 2.0f, s * 0.08f);
            const float h = r * 0.70710678f;
            g.drawLine (gc.x + h, gc.y + h,
                        gc.x + h + s * 0.20f, gc.y + h + s * 0.20f, s * 0.10f);
            g.drawLine (gc.x - r * 0.5f, gc.y, gc.x + r * 0.5f, gc.y, s * 0.08f);
            if (icon == Icon::ZoomIn)
                g.drawLine (gc.x, gc.y - r * 0.5f, gc.x, gc.y + r * 0.5f, s * 0.08f);
        }
        else
        {
            const float a = s * 0.26f;
            juce::Path p;
            if      (icon == Icon::Up)    p.addTriangle (c.x, c.y - a, c.x - a, c.y + a * 0.7f, c.x + a, c.y + a * 0.7f);
            else if (icon == Icon::Down)  p.addTriangle (c.x, c.y + a, c.x - a, c.y - a * 0.7f, c.x + a, c.y - a * 0.7f);
            else if (icon == Icon::Left)  p.addTriangle (c.x - a, c.y, c.x + a * 0.7f, c.y - a, c.x + a * 0.7f, c.y + a);
            else                          p.addTriangle (c.x + a, c.y, c.x - a * 0.7f, c.y - a, c.x - a * 0.7f, c.y + a);
            g.fillPath (p);
        }
    }

private:
    Icon icon;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CSDIconButton)
};
