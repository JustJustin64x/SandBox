#include "core/camera.h"
#include <algorithm>

CameraController::CameraController(int screenWidth, int screenHeight, Vector2 focus, float zoom) {
    camera_ = Camera2D{};
    camera_.target = focus;
    camera_.offset = { (float)screenWidth / 2.0f, (float)screenHeight / 2.0f };
    camera_.rotation = 0.0f;
    camera_.zoom = zoom;
}

void CameraController::Update(float deltaTime) {
    // 1. WASD & Pfeiltasten
    const float moveSpeed = 450.0f / camera_.zoom;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    camera_.target.y -= moveSpeed * deltaTime;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  camera_.target.y += moveSpeed * deltaTime;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  camera_.target.x -= moveSpeed * deltaTime;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) camera_.target.x += moveSpeed * deltaTime;

    // 2. Ziehen mit mittlerer Maustaste
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        // raylib does not apply SetMouseScale() to GetMouseDelta().
        const float displayScale = std::max(0.0001f, std::min(
            (float)GetScreenWidth() / SCREEN_WIDTH,
            (float)GetScreenHeight() / SCREEN_HEIGHT));
        const Vector2 delta = Vector2Scale(GetMouseDelta(), -1.0f / (camera_.zoom * displayScale));
        camera_.target = Vector2Add(camera_.target, delta);
    }

    // 3. Mausrad-Zoom zentriert auf Mauszeiger
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        Vector2 mouse = GetMousePosition();
        // Zoom nur ausführen, wenn die Maus innerhalb des virtuellen Bildschirms liegt
        if (mouse.x >= 0.0f && mouse.x <= SCREEN_WIDTH && mouse.y >= 0.0f && mouse.y <= SCREEN_HEIGHT) {
            const Vector2 mouseWorldPos = GetScreenToWorld2D(mouse, camera_);
            camera_.offset = mouse;
            camera_.target = mouseWorldPos;

            camera_.zoom += 0.25f * wheel;
            if (camera_.zoom < 0.25f) camera_.zoom = 0.25f;
            if (camera_.zoom > 8.0f)  camera_.zoom = 8.0f;
        }
    }
}

