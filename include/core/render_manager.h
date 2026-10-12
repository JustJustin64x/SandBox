#pragma once

#include "core/common.h"

// ============================================================================
//  RenderManager
//  Trennt die logische (virtuelle) Spielauflösung (1920x1080) von der realen
//  Fenster- und Displayauflösung (inklusive HiDPI / Retina-Skalierung).
//
//  Funktionen:
//  - Zentrierter und stabiler Fensterstart auf macOS und Windows
//  - Resizable Window mit Erhaltung des 16:9-Seitenverhältnisses (Aspect Ratio)
//  - Letterboxing / Pillarboxing über ein Offscreen-Render-Target (RenderTexture2D)
//  - Vollautomatische sowie explizite Rückrechnung aller Mauskoordinaten
//  - Vollbild-Umschaltung via F11 oder Alt+Enter
// ============================================================================
class RenderManager {
public:
    RenderManager(int virtualWidth = SCREEN_WIDTH, int virtualHeight = SCREEN_HEIGHT);
    ~RenderManager();

    RenderManager(const RenderManager&) = delete;
    RenderManager& operator=(const RenderManager&) = delete;

    // Erstellt das Fenster angepasst an den aktuellen Monitor und initialisiert das Render-Target
    void InitWindowAndContext(const char* title);

    // Beendet Window und gibt GPU-Ressourcen frei
    void Shutdown();

    // Wird zu Beginn jedes Frame-Durchlaufs aufgerufen (passt Skalierung & Maus-Offset an)
    void Update();

    // Beginnt das Zeichnen in die interne 1920x1080 Render-Texture
    void BeginVirtual();

    // Beendet das Zeichnen in die Render-Texture
    void EndVirtual();

    // Zeichnet die skalierte Render-Texture mit Letterboxing auf den Bildschirm
    void DrawLetterbox() const;

    // Präsentiert den Frame vollständig auf dem Bildschirm:
    // BeginDrawing() -> ClearBackground(BLACK) -> DrawLetterbox() -> EndDrawing()
    void Present();

    // Vollbild umschalten (z. B. F11 oder Alt+Enter)
    void ToggleFullscreenMode();

    // Koordinaten-Umrechnung
    Vector2 ScreenToVirtual(Vector2 screenPos) const;
    Vector2 VirtualToScreen(Vector2 virtualPos) const;
    Vector2 GetVirtualMousePosition() const;
    Vector2 GetClampedVirtualMousePosition() const;
    bool IsMouseOnVirtualScreen() const;

    // Getter
    int VirtualWidth() const { return virtualWidth_; }
    int VirtualHeight() const { return virtualHeight_; }
    float Scale() const { return scale_; }
    Vector2 Offset() const { return { offsetX_, offsetY_ }; }
    Rectangle DestinationRect() const { return destRect_; }
    const RenderTexture2D& Target() const { return target_; }
    bool IsInitialized() const { return isInitialized_; }

private:
    void CalculateLetterbox();

    int virtualWidth_;
    int virtualHeight_;

    RenderTexture2D target_{};
    bool isInitialized_ = false;

    float scale_ = 1.0f;
    float offsetX_ = 0.0f;
    float offsetY_ = 0.0f;
    Rectangle destRect_{};
    Rectangle sourceRect_{};
};

