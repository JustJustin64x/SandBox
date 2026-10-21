#include "gfx/sprite_animator.h"
#include "gfx/asset_manager.h"
#include <cmath>
#include <algorithm>

SpriteAnimator& SpriteAnimator::Instance() {
    static SpriteAnimator sInst;
    return sInst;
}

SpriteAnimator::SpriteAnimator(int frameWidth, int frameHeight, int cols, int rows)
    : frameWidth_(frameWidth), frameHeight_(frameHeight), cols_(cols), rows_(rows) {
}

Rectangle SpriteAnimator::GetFrameRect(AnimState state, float animTime, bool facingLeft) const {
    (void)facingLeft;
    int row = static_cast<int>(state);
    if (row < 0 || row >= rows_) row = 0;

    int col = 0;
    switch (state) {
        case AnimState::Idle:
            col = (int)(animTime * 2.0f) % std::min(2, cols_);
            break;
        case AnimState::Walking:
            col = (int)(animTime * fps_) % cols_;
            break;
        case AnimState::Working:
            col = (int)(animTime * (fps_ * 1.5f)) % cols_;
            break;
        case AnimState::Sleeping:
            col = 0;
            break;
    }

    return Rectangle{
        (float)(col * frameWidth_),
        (float)(row * frameHeight_),
        (float)frameWidth_,
        (float)frameHeight_
    };
}

void SpriteAnimator::DrawLayeredCharacter(const std::string& baseKey,
                                          AnimState state,
                                          float animTime,
                                          Vector2 worldPos,
                                          bool facingLeft,
                                          Color bodyTint,
                                          float scale,
                                          const std::string& clothingKey,
                                          Color clothingTint,
                                          const std::string& itemKey,
                                          Color itemTint) const {
    Rectangle srcRect = GetFrameRect(state, animTime, facingLeft);
    float drawW = (float)frameWidth_ * scale;
    float drawH = (float)frameHeight_ * scale;
    Rectangle destRect = {
        worldPos.x - drawW * 0.5f,
        worldPos.y - drawH + 4.0f,
        drawW,
        drawH
    };

    // -------------------------------------------------------------------------
    // SCHICHT 1: Basis-Sprite (Nackter Körper mit Hautfarbe / Geschlecht)
    // -------------------------------------------------------------------------
    AssetManager::Get().DrawRegion(baseKey, srcRect, destRect, bodyTint, facingLeft);

    // -------------------------------------------------------------------------
    // SCHICHT 2: Kleidung / Rüstung (Layering)
    // -------------------------------------------------------------------------
    if (!clothingKey.empty()) {
        AssetManager::Get().DrawRegion(clothingKey, srcRect, destRect, clothingTint, facingLeft);
    }

    // -------------------------------------------------------------------------
    // SCHICHT 3: Waffe / Werkzeug in der Hand
    // -------------------------------------------------------------------------
    if (!itemKey.empty() && state != AnimState::Sleeping) {
        float handOffsetX = (facingLeft ? -7.0f : 7.0f) * scale;
        float handOffsetY = -10.0f * scale;
        if (state == AnimState::Working) {
            float swing = std::sin(animTime * 14.0f) * 4.0f;
            handOffsetY += swing * scale;
        }
        Vector2 handPos = { worldPos.x + handOffsetX, worldPos.y + handOffsetY };
        AssetManager::Get().DrawSpriteCentered(itemKey, handPos, 16.0f * scale, 16.0f * scale, itemTint);
    }
}

void SpriteAnimator::DrawAnimal(const std::string& animalKey,
                                Vector2 worldPos,
                                float animTime,
                                bool isMoving,
                                bool facingLeft,
                                Color tint,
                                float scale) const {
    AnimState state = isMoving ? AnimState::Walking : AnimState::Idle;
    Rectangle srcRect = GetFrameRect(state, animTime, facingLeft);
    float drawW = (float)frameWidth_ * scale;
    float drawH = (float)frameHeight_ * scale;
    Rectangle destRect = {
        worldPos.x - drawW * 0.5f,
        worldPos.y - drawH + 3.0f,
        drawW,
        drawH
    };

    AssetManager::Get().DrawRegion(animalKey, srcRect, destRect, tint, facingLeft);
}

