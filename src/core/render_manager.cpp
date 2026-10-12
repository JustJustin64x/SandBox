#include "render_manager.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

RenderManager::RenderManager(int virtualWidth, int virtualHeight)
    : virtualWidth_(virtualWidth),
      virtualHeight_(virtualHeight) {
}

RenderManager::~RenderManager() {
    Shutdown();
}

void RenderManager::InitWindowAndContext(const char* title) {
    if (isInitialized_) return;

    // 1. Fenster-Konfigurationsflags setzen:
    //    - FLAG_WINDOW_RESIZABLE: Erlaubt Vergrößern/Verkleinern und Maximieren
    //    - FLAG_WINDOW_HIGHDPI: Unterstützt Retina- und HiDPI-Displays nativ
    //    - FLAG_VSYNC_HINT: Tearing-freie Bildwiederholrate
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_VSYNC_HINT);

    // 2. Temporäres Basis-Fenster öffnen, um Monitor-Informationen abzufragen
    InitWindow(1280, 720, title);

    // 3. Monitor-Eigenschaften des aktuellen Bildschirms ermitteln
    int monitor = GetCurrentMonitor();
    int monW = GetMonitorWidth(monitor);
    int monH = GetMonitorHeight(monitor);
    Vector2 monPos = GetMonitorPosition(monitor);

    // 4. Ideale Startgröße berechnen:
    //    Wenn der Monitor groß genug für 1920x1080 + Fensterdekorationen ist, nutzen wir 1080p.
    //    Andernfalls (z. B. MacBook 13"-16" Retina mit 1440x900 bis 1728x1117 Points)
    //    skalieren wir das Fenster proportional herunter (ca. 85 % Monitor-Abdeckung),
    //    damit das Fenster zentriert und ohne Ecken-Versatz startet.
    const float fitScale = std::min({1.0f,
        monW * 0.85f / virtualWidth_, monH * 0.85f / virtualHeight_});
    const int initW = std::max(1, (int)(virtualWidth_ * fitScale));
    const int initH = std::max(1, (int)(virtualHeight_ * fitScale));

    SetWindowSize(initW, initH);
    SetWindowPosition((int)(monPos.x + (monW - initW) / 2),
                      (int)(monPos.y + (monH - initH) / 2));
    SetWindowMinSize(640, 360);

    // 5. Offscreen-Render-Target für die feste virtuelle Auflösung (1920x1080)
    target_ = LoadRenderTexture(virtualWidth_, virtualHeight_);
    SetTextureFilter(target_.texture, TEXTURE_FILTER_BILINEAR);

    // OpenGL invertiert FBO-Texturen in Y-Richtung
    sourceRect_ = { 0.0f, 0.0f, (float)virtualWidth_, -(float)virtualHeight_ };

    // 6. Erstes Letterbox-Setup
    CalculateLetterbox();
    isInitialized_ = true;
}

void RenderManager::Shutdown() {
    if (!isInitialized_) return;

    if (target_.id > 0) {
        UnloadRenderTexture(target_);
        target_ = {};
    }

    CloseWindow();
    isInitialized_ = false;
}

void RenderManager::CalculateLetterbox() {
    int winW = GetScreenWidth();
    int winH = GetScreenHeight();

    // Verhindert Division durch 0 bei minimiertem Fenster
    if (winW <= 0) winW = 1;
    if (winH <= 0) winH = 1;

    // Uniforme Skalierung berechnen (16:9 beibehalten)
    scale_ = std::min((float)winW / (float)virtualWidth_,
                      (float)winH / (float)virtualHeight_);
    if (scale_ < 0.0001f) scale_ = 0.0001f;

    float renderW = (float)virtualWidth_ * scale_;
    float renderH = (float)virtualHeight_ * scale_;

    // Schwarze Ränder berechnen (Letterboxing oben/unten oder Pillarboxing links/rechts)
    offsetX_ = ((float)winW - renderW) * 0.5f;
    offsetY_ = ((float)winH - renderH) * 0.5f;

    destRect_ = { offsetX_, offsetY_, renderW, renderH };

    // Raylib-internes Mauseingabe-System auf virtuelle Koordinaten kalibrieren.
    // GetMousePosition() returns virtual coordinates; GetMouseDelta() stays unscaled.
    SetMouseOffset((int)std::round(-offsetX_), (int)std::round(-offsetY_));
    SetMouseScale(1.0f / scale_, 1.0f / scale_);
}

void RenderManager::Update() {
    // Tastenkürzel für Vollbild: F11 oder Alt+Enter
    if (IsKeyPressed(KEY_F11) || ((IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) && IsKeyPressed(KEY_ENTER))) {
        ToggleFullscreenMode();
    }

    CalculateLetterbox();
}

void RenderManager::ToggleFullscreenMode() {
    // Borderless Windowed ist stabiler als exklusives Umschalten der Monitor-Wiederholrate
    ToggleBorderlessWindowed();
}

void RenderManager::BeginVirtual() {
    BeginTextureMode(target_);
    ClearBackground(BLACK);
}

void RenderManager::EndVirtual() {
    EndTextureMode();
}

void RenderManager::DrawLetterbox() const {
    DrawTexturePro(target_.texture, sourceRect_, destRect_, { 0.0f, 0.0f }, 0.0f, WHITE);
}

void RenderManager::Present() {
    BeginDrawing();
        // raylib 5.5 retains its initial DPI modelview scale after a resize on
        // Windows. Our destination rectangle is already in window coordinates:
        // map those coordinates to framebuffer pixels exactly once, including
        // Retina framebuffers whose pixel size differs from the window size.
        rlDrawRenderBatchActive();
        rlViewport(0, 0, GetRenderWidth(), GetRenderHeight());
        rlMatrixMode(RL_PROJECTION);
        rlLoadIdentity();
        rlOrtho(0, std::max(1, GetScreenWidth()), std::max(1, GetScreenHeight()), 0, 0, 1);
        rlMatrixMode(RL_MODELVIEW);
        rlLoadIdentity();
        ClearBackground(BLACK); // Zeichnet schwarze Balken (Letterbox / Pillarbox)
        DrawLetterbox();
    EndDrawing();
}

Vector2 RenderManager::ScreenToVirtual(Vector2 screenPos) const {
    return {
        (screenPos.x - offsetX_) / scale_,
        (screenPos.y - offsetY_) / scale_
    };
}

Vector2 RenderManager::VirtualToScreen(Vector2 virtualPos) const {
    return {
        virtualPos.x * scale_ + offsetX_,
        virtualPos.y * scale_ + offsetY_
    };
}

Vector2 RenderManager::GetVirtualMousePosition() const {
    return GetMousePosition();
}

Vector2 RenderManager::GetClampedVirtualMousePosition() const {
    Vector2 m = GetMousePosition();
    return {
        std::clamp(m.x, 0.0f, (float)virtualWidth_),
        std::clamp(m.y, 0.0f, (float)virtualHeight_)
    };
}

bool RenderManager::IsMouseOnVirtualScreen() const {
    Vector2 m = GetMousePosition();
    return m.x >= 0.0f && m.x <= (float)virtualWidth_ &&
           m.y >= 0.0f && m.y <= (float)virtualHeight_;
}

