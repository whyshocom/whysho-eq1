#pragma once
#include <juce_graphics/juce_graphics.h>
#include <juce_events/juce_events.h>
#include <array>
#include <cmath>

namespace wsho::gui
{
// A complete, swappable palette. Field names match the old `col::` tokens so
// every component simply reads `theme().<name>`.
struct Theme
{
    juce::String name;
    bool   light      = false;   // paper-style rendering (black curve, fills)
    bool   curveGlow  = true;    // neon glow under the curve (dark themes)
    bool   mono       = false;   // monospace editorial typography

    juce::Colour bg, panel, panel2, grid, text, textDim,
                 accent, accent2, good, warn, bad, curve, specPre, specPost;

    std::array<juce::Colour, 8> nodePalette; // per-band node colours
};

// ---- The theme catalogue --------------------------------------------------
inline const std::array<Theme, 3>& themes()
{
    static const std::array<Theme, 3> list = {
        // 0 — Paper (default): warm editorial light look
        Theme{
            "Paper", /*light*/ true, /*glow*/ false, /*mono*/ true,
            juce::Colour (0xffF2EEE3), // bg
            juce::Colour (0xffEAE5D7), // panel
            juce::Colour (0xffFBF9F2), // panel2 (fields)
            juce::Colour (0xffCBC4B0), // grid
            juce::Colour (0xff1A1813), // text
            juce::Colour (0xff5E5949), // textDim (5.6:1 on panel)
            juce::Colour (0xffC5542F), // accent (terracotta)
            juce::Colour (0xff8E6FD6), // accent2 (purple)
            juce::Colour (0xff3F8F56), // good
            juce::Colour (0xffC27C2B), // warn
            juce::Colour (0xffB03226), // bad (5:1 on panel)
            juce::Colour (0xff14110B), // curve (near-black)
            juce::Colour (0xA06F6A5A), // specPre  (spectrum line; the fill under it is derived, fainter)
            juce::Colour (0xE02E6CA8), // specPost (blue)
            { juce::Colour (0xffE4572E), juce::Colour (0xffF2932C), juce::Colour (0xff3FA34D),
              juce::Colour (0xff8E6FD6), juce::Colour (0xff3C78C8), juce::Colour (0xff2BA6A0),
              juce::Colour (0xffD7599B), juce::Colour (0xffC9A227) }
        },
        // 1 — Midnight: dark cyan (the original)
        Theme{
            "Midnight", false, true, false,
            juce::Colour (0xff0E0F13), juce::Colour (0xff15171E), juce::Colour (0xff1C1F29),
            juce::Colour (0xff2A2E3A), juce::Colour (0xffE6E9F0), juce::Colour (0xff8A90A0),
            juce::Colour (0xff4CC2FF), juce::Colour (0xffB46BFF), juce::Colour (0xff57D9A3),
            juce::Colour (0xffFFB454), juce::Colour (0xffFF6B6B), juce::Colour (0xff4CC2FF),
            juce::Colour (0xA07C8AA5), juce::Colour (0xC04CC2FF),
            { juce::Colour (0xff4CC2FF), juce::Colour (0xff6BE3B8), juce::Colour (0xffB46BFF),
              juce::Colour (0xffFFB454), juce::Colour (0xffFF6B9D), juce::Colour (0xff7CF2E0),
              juce::Colour (0xffFFD166), juce::Colour (0xff8AA8FF) }
        },
        // 2 — Graphite: dark neutral, amber accent
        Theme{
            "Graphite", false, false, false,
            juce::Colour (0xff17171A), juce::Colour (0xff1F1F23), juce::Colour (0xff27272C),
            juce::Colour (0xff343439), juce::Colour (0xffEDEDED), juce::Colour (0xff908F96),
            juce::Colour (0xffF2A33C), juce::Colour (0xffCE6BF2), juce::Colour (0xff5BD98C),
            juce::Colour (0xffF2C14E), juce::Colour (0xffF2655B), juce::Colour (0xffF2A33C),
            juce::Colour (0xA085858E), juce::Colour (0xC0F2A33C),
            { juce::Colour (0xffF2A33C), juce::Colour (0xff5BD98C), juce::Colour (0xffCE6BF2),
              juce::Colour (0xff5BB8D9), juce::Colour (0xffF2655B), juce::Colour (0xffE0C341),
              juce::Colour (0xffCE6BF2), juce::Colour (0xff9AD95B) }
        }
    };
    return list;
}

// ---- Active theme ---------------------------------------------------------
namespace detail { inline int& activeThemeIndex() { static int i = 0; return i; } }

inline const Theme& theme()
{
    const auto& list = themes();
    int i = detail::activeThemeIndex();
    if (i < 0 || i >= (int) list.size()) i = 0;
    return list[(size_t) i];
}

inline int  currentThemeIndex() { return detail::activeThemeIndex(); }
inline void setThemeIndex (int i) { detail::activeThemeIndex() = (i >= 0 && i < (int) themes().size()) ? i : 0; }

// The theme is one preference for the whole computer (not part of a project or
// preset): every open EQ1 window follows it, and it is remembered between sessions.
inline juce::ChangeBroadcaster& themeBroadcaster() { static juce::ChangeBroadcaster b; return b; }

inline juce::File themePreferenceFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("Whysho").getChildFile ("EQ1").getChildFile ("theme.txt");
}

// Reads the saved theme the first time it is called in this process.
inline void loadThemePreference()
{
    static bool loaded = false;
    if (std::exchange (loaded, true)) return;
    const auto name = themePreferenceFile().loadFileAsString().trim();
    for (int i = 0; i < (int) themes().size(); ++i)
        if (themes()[(size_t) i].name == name) setThemeIndex (i);
}

// The user picked a theme: remember it and restyle every open editor.
inline void chooseTheme (int index)
{
    setThemeIndex (index);
    auto f = themePreferenceFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (theme().name);
    themeBroadcaster().sendSynchronousChangeMessage();
}

inline juce::StringArray themeNames()
{
    juce::StringArray n;
    for (auto& t : themes()) n.add (t.name);
    return n;
}

inline juce::Colour bandColour (int band)
{
    const auto& pal = theme().nodePalette;
    return pal[(size_t) (band % (int) pal.size())];
}

// Readable text colour (near-black or near-white) for drawing on top of `fill`.
inline juce::Colour onColour (juce::Colour fill)
{
    auto lin = [] (float c) { return c <= 0.03928f ? c / 12.92f : std::pow ((c + 0.055f) / 1.055f, 2.4f); };
    const float lum = 0.2126f * lin (fill.getFloatRed()) + 0.7152f * lin (fill.getFloatGreen())
                    + 0.0722f * lin (fill.getFloatBlue());
    // Whichever of the two gives the higher WCAG contrast ratio.
    return (1.05f / (lum + 0.05f)) > ((lum + 0.05f) / 0.05f) ? juce::Colour (0xffFFFFFF) : juce::Colour (0xff14110B);
}

// Typography: monospace editorial face for Paper, clean sans for dark themes.
inline juce::Font themeFont (float height, bool bold = false)
{
    if (theme().mono)
        return juce::Font (juce::Font::getDefaultMonospacedFontName(), height,
                           bold ? juce::Font::bold : juce::Font::plain);
    return juce::Font (height, bold ? juce::Font::bold : juce::Font::plain);
}

// All-caps label with light tracking (used for section / header text).
inline juce::String caps (const juce::String& s) { return s.toUpperCase(); }

} // namespace wsho::gui
