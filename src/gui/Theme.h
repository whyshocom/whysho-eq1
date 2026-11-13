#pragma once
#include <juce_graphics/juce_graphics.h>
#include <array>

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
            juce::Colour (0xff6E6857), // textDim
            juce::Colour (0xffC5542F), // accent (terracotta)
            juce::Colour (0xff8E6FD6), // accent2 (purple)
            juce::Colour (0xff3F8F56), // good
            juce::Colour (0xffC27C2B), // warn
            juce::Colour (0xffC0392B), // bad
            juce::Colour (0xff14110B), // curve (near-black)
            juce::Colour (0x3C5B86B4), // specPre
            juce::Colour (0x665B86B4), // specPost (blue fill)
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
            juce::Colour (0x5563718A), juce::Colour (0x884CC2FF),
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
            juce::Colour (0x55707078), juce::Colour (0x88F2A33C),
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
