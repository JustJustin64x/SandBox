#ifndef CAMERA_H
#define CAMERA_H

#if __has_include("core/common.h")
    #include "core/common.h"
#else
    #include "common.h"
#endif

// Steuert die 2D-Kamera: WASD/Pfeiltasten, Ziehen mit mittlerer Maustaste,
// Mausrad-Zoom zentriert auf den Mauszeiger.
class CameraController {
public:
    CameraController(int screenWidth, int screenHeight, Vector2 focus, float zoom = 2.0f);

    void Update(float deltaTime);
    const Camera2D& Get() const { return camera_; }
    Vector2 ScreenToWorld(Vector2 screenPos) const { return GetScreenToWorld2D(screenPos, camera_); }

private:
    Camera2D camera_;
    [[maybe_unused]] int screenWidth_ = 1920;
    [[maybe_unused]] int screenHeight_ = 1080;
};

#endif // CAMERA_H
