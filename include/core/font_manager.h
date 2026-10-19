#pragma once

#include "common.h"
#include <string>

// ============================================================================
//  FontManager
//  Lädt hochauflösende TrueType-Schriftarten (inkl. deutscher Umlaute) mit
//  bilinearem Texture-Filtering, um verpixelte Raylib-Bitmap-Schriften vollständig
//  zu eliminieren und Text überall sauber, scharf und lesbar zu rendern.
// ============================================================================
class FontManager {
public:
    static void Init();
    static void Shutdown();
    static bool IsLoaded();
    static Font GetFont();

    // Zeichnet sauberen, geglätteten Text
    static void DrawText(const char* text, float x, float y, float fontSize, Color color);
    static void DrawText(const std::string& text, float x, float y, float fontSize, Color color);

    // Zeichnet sauberen Text mit feinem Kontrastschatten (für optimale Lesbarkeit auf hellem/dunklem Grund)
    static void DrawTextShadow(const char* text, float x, float y, float fontSize, Color color,
                               Color shadowCol = Color{ 0, 0, 0, 210 });
    static void DrawTextShadow(const std::string& text, float x, float y, float fontSize, Color color,
                               Color shadowCol = Color{ 0, 0, 0, 210 });

    // Zentrierten Text zeichnen
    static void DrawTextCentered(const char* text, float centerX, float y, float fontSize, Color color);
    static void DrawTextCenteredShadow(const char* text, float centerX, float y, float fontSize, Color color,
                                       Color shadowCol = Color{ 0, 0, 0, 210 });

    // Textabmessungen messen
    static float MeasureText(const char* text, float fontSize);
    static Vector2 MeasureTextEx(const char* text, float fontSize);
};

