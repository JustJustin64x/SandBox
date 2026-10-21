#pragma once

#include "core/common.h"
#include <string>

// ============================================================================
//  SpriteAnimator (AUFTRAG PHASE 4)
//  Animiert Entitäten (Menschen & Wildtiere) anhand von Utility-AI-Zuständen
//  und steuert das mehrschichtige Rendering (Basis-Körper -> Kleidung -> Waffe/Item).
// ============================================================================
enum class AnimState {
    Idle = 0,     // Zeile 0: Stehen / Warten / Reden
    Walking = 1,  // Zeile 1: Laufen / Bewegung
    Working = 2,  // Zeile 2: Arbeiten (Holzhacken, Schlagen, Ernten, Bauen)
    Sleeping = 3  // Zeile 3: Schlafen / Liegen / Tot
};

class SpriteAnimator {
public:
    static SpriteAnimator& Instance();
    static SpriteAnimator& Get() { return Instance(); }

    SpriteAnimator(int frameWidth = 32, int frameHeight = 32, int cols = 4, int rows = 4);

    // Ermittelt Zeile und Spalte basierend auf Zustand und Animationszeit
    Rectangle GetFrameRect(AnimState state, float animTime, bool facingLeft) const;

    // Mehrschichtiges Zeichnen einer Entität:
    // 1. Basis-Sprite (Körper / Nackt)
    // 2. Kleidungsschicht (Fell, Tunika, Rüstung)
    // 3. Waffe / Werkzeug in der Hand
    void DrawLayeredCharacter(const std::string& baseKey,
                              AnimState state,
                              float animTime,
                              Vector2 worldPos,
                              bool facingLeft,
                              Color bodyTint = WHITE,
                              float scale = 1.0f,
                              const std::string& clothingKey = "",
                              Color clothingTint = WHITE,
                              const std::string& itemKey = "",
                              Color itemTint = WHITE) const;

    // Zeichnen von Wildtieren
    void DrawAnimal(const std::string& animalKey,
                    Vector2 worldPos,
                    float animTime,
                    bool isMoving,
                    bool facingLeft,
                    Color tint = WHITE,
                    float scale = 1.0f) const;

private:
    int frameWidth_ = 32;
    int frameHeight_ = 32;
    int cols_ = 4;
    int rows_ = 4;
    float fps_ = 8.0f;
};

