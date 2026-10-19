#include "font_manager.h"
#include <vector>
#include <iostream>

namespace {
Font g_uiFont{};
bool g_isLoaded = false;
}

void FontManager::Init() {
    if (g_isLoaded) return;

    // Mögliche Font-Pfade prüfen (primär unser gebündeltes assets/ui_font.ttf)
    const char* candidatePaths[] = {
        "assets/ui_font.ttf",
        "assets/cavemen.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Verdana.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf"
    };

    const char* chosenPath = nullptr;
    for (const char* path : candidatePaths) {
        if (FileExists(path)) {
            chosenPath = path;
            break;
        }
    }

    if (!chosenPath) {
        std::cout << "[FontManager] Kein externer TTF-Font gefunden. Nutze Raylib-Fallback." << std::endl;
        g_uiFont = GetFontDefault();
        g_isLoaded = true;
        return;
    }

    // Zeichensatz zusammenstellen: ASCII + deutsche Umlaute (ä, ö, ü, Ä, Ö, Ü, ß)
    std::vector<int> codepoints;
    codepoints.reserve(256);
    for (int i = 32; i <= 126; ++i) codepoints.push_back(i);   // Standard ASCII
    for (int i = 160; i <= 255; ++i) codepoints.push_back(i); // Latin-1 Supplement (ä, ö, ü, etc.)
    codepoints.push_back(0x2022); // • Bullet
    codepoints.push_back(0x2013); // – En-dash
    codepoints.push_back(0x2014); // — Em-dash
    codepoints.push_back(0x2192); // → Pfeil

    // Hochauflösende Rasterung (64 Pixel Basishöhe für extrem scharfe Skalierung bei allen Größen)
    g_uiFont = LoadFontEx(chosenPath, 64, codepoints.data(), static_cast<int>(codepoints.size()));

    if (g_uiFont.texture.id > 0) {
        // Bilineares Filtering aktivieren für weiche Kanten ohne Block-Pixel
        SetTextureFilter(g_uiFont.texture, TEXTURE_FILTER_BILINEAR);
        g_isLoaded = true;
        std::cout << "[FontManager] HD-Schriftart erfolgreich geladen: " << chosenPath
                  << " (64px Atlas mit bilinearem Filter)" << std::endl;
    } else {
        std::cout << "[FontManager] Warnung: Konnte " << chosenPath << " nicht laden. Fallback auf Default." << std::endl;
        g_uiFont = GetFontDefault();
        g_isLoaded = true;
    }
}

void FontManager::Shutdown() {
    if (g_isLoaded && g_uiFont.texture.id > 0 && g_uiFont.texture.id != GetFontDefault().texture.id) {
        UnloadFont(g_uiFont);
        g_uiFont = {};
    }
    g_isLoaded = false;
}

bool FontManager::IsLoaded() {
    return g_isLoaded;
}

Font FontManager::GetFont() {
    if (!g_isLoaded) Init();
    return g_uiFont;
}

void FontManager::DrawText(const char* text, float x, float y, float fontSize, Color color) {
    if (!text || text[0] == '\0') return;
    if (!g_isLoaded) Init();

    DrawTextEx(g_uiFont, text, Vector2{ x, y }, fontSize, 1.0f, color);
}

void FontManager::DrawText(const std::string& text, float x, float y, float fontSize, Color color) {
    DrawText(text.c_str(), x, y, fontSize, color);
}

void FontManager::DrawTextShadow(const char* text, float x, float y, float fontSize, Color color, Color shadowCol) {
    if (!text || text[0] == '\0') return;
    if (!g_isLoaded) Init();

    // 1 Pixel dezenter Schlagschatten für optimalen Kontrast
    DrawTextEx(g_uiFont, text, Vector2{ x + 1.2f, y + 1.2f }, fontSize, 1.0f, shadowCol);
    DrawTextEx(g_uiFont, text, Vector2{ x, y }, fontSize, 1.0f, color);
}

void FontManager::DrawTextShadow(const std::string& text, float x, float y, float fontSize, Color color, Color shadowCol) {
    DrawTextShadow(text.c_str(), x, y, fontSize, color, shadowCol);
}

void FontManager::DrawTextCentered(const char* text, float centerX, float y, float fontSize, Color color) {
    if (!text || text[0] == '\0') return;
    float w = MeasureText(text, fontSize);
    DrawText(text, centerX - w * 0.5f, y, fontSize, color);
}

void FontManager::DrawTextCenteredShadow(const char* text, float centerX, float y, float fontSize, Color color, Color shadowCol) {
    if (!text || text[0] == '\0') return;
    float w = MeasureText(text, fontSize);
    DrawTextShadow(text, centerX - w * 0.5f, y, fontSize, color, shadowCol);
}

float FontManager::MeasureText(const char* text, float fontSize) {
    if (!text || text[0] == '\0') return 0.0f;
    if (!g_isLoaded) Init();
    return MeasureTextEx(text, fontSize).x;
}

Vector2 FontManager::MeasureTextEx(const char* text, float fontSize) {
    if (!text || text[0] == '\0') return Vector2{ 0.0f, 0.0f };
    if (!g_isLoaded) Init();
    return ::MeasureTextEx(g_uiFont, text, fontSize, 1.0f);
}

