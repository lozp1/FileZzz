#pragma once
#include <SDL.h>

enum AppTheme {
    THEME_DARK = 0,
    THEME_OLED,
    THEME_WHITE,
    THEME_COUNT
};

struct ThemeColors {
    const char* name;
    SDL_Color BgBase;
    SDL_Color BgSurface;
    SDL_Color BgCard;
    SDL_Color BgSelected;

    SDL_Color BorderSubtle;
    SDL_Color BorderFocused;

    SDL_Color TextPrimary;
    SDL_Color TextSecondary;
    SDL_Color TextMuted;

    SDL_Color AccentCyan;
    SDL_Color AccentEmerald;
    SDL_Color AccentBlue;
    SDL_Color AccentIndigo;
    SDL_Color AccentViolet;
    SDL_Color AccentAmber;
    SDL_Color AccentRed;
};

inline const ThemeColors THEMES[THEME_COUNT] = {
    // 0: Dark Obsidian (Andromeda Official)
    {
        "Dark",
        { 11, 17, 32, 255 },    // #0B1120
        { 6, 9, 19, 255 },      // #060913
        { 14, 21, 38, 255 },    // #0E1526
        { 29, 78, 216, 255 },   // #1D4ED8
        { 30, 41, 59, 255 },    // #1E293B
        { 56, 189, 248, 255 },  // #38BDF8
        { 255, 255, 255, 255 }, // Pure White
        { 148, 163, 184, 255 }, // #94A3B8
        { 100, 116, 139, 255 }, // #64748B
        { 56, 189, 248, 255 },  // #38BDF8
        { 16, 231, 97, 255 },   // #10E761
        { 59, 130, 246, 255 },  // #3B82F6
        { 99, 102, 241, 255 },  // #6366F1
        { 168, 85, 247, 255 },  // #A855F7
        { 245, 158, 11, 255 },  // #F59E0B
        { 239, 68, 68, 255 }    // #EF4444
    },
    // 1: OLED (Pure Black for Switch OLED)
    {
        "OLED",
        { 0, 0, 0, 255 },       // Pure Black
        { 0, 0, 0, 255 },       // Pure Black
        { 10, 14, 26, 255 },    // #0A0E1A
        { 29, 78, 216, 255 },   // #1D4ED8
        { 30, 41, 59, 255 },    // #1E293B
        { 56, 189, 248, 255 },  // #38BDF8
        { 255, 255, 255, 255 }, // Pure White
        { 148, 163, 184, 255 }, // #94A3B8
        { 100, 116, 139, 255 }, // #64748B
        { 56, 189, 248, 255 },  // #38BDF8
        { 16, 231, 97, 255 },   // #10E761
        { 59, 130, 246, 255 },  // #3B82F6
        { 99, 102, 241, 255 },  // #6366F1
        { 168, 85, 247, 255 },  // #A855F7
        { 245, 158, 11, 255 },  // #F59E0B
        { 239, 68, 68, 255 }    // #EF4444
    },
    // 2: White (Light Theme)
    {
        "White",
        { 241, 245, 249, 255 }, // #F1F5F9
        { 255, 255, 255, 255 }, // #FFFFFF
        { 226, 232, 240, 255 }, // #E2E8F0
        { 59, 130, 246, 255 },  // #3B82F6
        { 203, 213, 225, 255 }, // #CBD5E1
        { 2, 132, 199, 255 },   // #0284C7
        { 15, 23, 42, 255 },    // #0F172A
        { 71, 85, 105, 255 },   // #475569
        { 148, 163, 184, 255 }, // #94A3B8
        { 2, 132, 199, 255 },   // #0284C7
        { 22, 163, 74, 255 },   // #16A34A
        { 37, 99, 235, 255 },   // #2563EB
        { 79, 70, 229, 255 },   // #4F46E5
        { 147, 51, 234, 255 },  // #9333EA
        { 217, 119, 6, 255 },   // #D97706
        { 220, 38, 38, 255 }    // #DC2626
    }
};

inline AppTheme currentTheme = THEME_DARK;

inline const ThemeColors& curTheme() {
    return THEMES[currentTheme];
}

inline void nextTheme() {
    currentTheme = (AppTheme)((currentTheme + 1) % THEME_COUNT);
}

inline void setTheme(int idx) {
    if (idx >= 0 && idx < THEME_COUNT)
        currentTheme = (AppTheme)idx;
}

inline int getThemeCount() { return (int)THEME_COUNT; }
inline int currentThemeIdx() { return (int)currentTheme; }

struct TerminalTheme {
    const char* name;
    SDL_Color bg;
    SDL_Color fg;
    SDL_Color accent;
    SDL_Color selectBg;
};

const TerminalTheme TERM_THEMES[] = {
    { "Hacker",    {0,0,0,255},       {255,255,255,255}, {0,255,0,255},   {0,0,170,255} },
    { "Dracula",   {40,42,54,255},    {248,248,242,255}, {255,121,198,255}, {68,71,90,255} },
    { "Nord",      {46,52,64,255},    {216,222,233,255}, {136,192,208,255}, {67,76,94,255} },
    { "Oceanic",   {27,43,52,255},    {216,222,233,255}, {249,145,87,255},  {79,91,102,255} },
    { "Solarized", {253,246,227,255}, {101,123,131,255}, {181,137,0,255},   {238,232,213,255} },
    { "Synthwave", {36,27,47,255},    {255,255,255,255}, {255,116,184,255}, {73,84,149,255} }
};

inline int currentTermThemeIdx = 0;
inline const TerminalTheme& curTermTheme() { return TERM_THEMES[currentTermThemeIdx]; }
inline int getTermThemeCount() { return 6; }
