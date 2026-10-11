#include "camera.h"

Camera2D Camera_Create(int screenWidth, int screenHeight, int worldPixelWidth, int worldPixelHeight) {
    Camera2D camera = { 0 };
    camera.target = (Vector2){ (float)worldPixelWidth / 2.0f, (float)worldPixelHeight / 2.0f };
    camera.offset = (Vector2){ (float)screenWidth / 2.0f, (float)screenHeight / 2.0f };
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;
    return camera;
}s

void Camera_Update(Camera2D* camera, float deltaTime) {
    if (!camera) return;

    // 1. WASD & Pfeiltasten
    float moveSpeed = 450.0f / camera->zoom;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    camera->target.y -= moveSpeed * deltaTime;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  camera->target.y += moveSpeed * deltaTime;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  camera->target.x -= moveSpeed * deltaTime;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) camera->target.x += moveSpeed * deltaTime;

    // 2. Drag mit mittlerer Maustaste
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        Vector2 delta = GetMouseDelta();
        delta = Vector2Scale(delta, -1.0f / camera->zoom);
        camera->target = Vector2Add(camera->target, delta);
    }

    // 3. Mausrad-Zoom zentriert auf Mauszeiger
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        Vector2 mouseWorldPos = GetScreenToWorld2D(GetMousePosition(), *camera);
        camera->offset = GetMousePosition();
        camera->target = mouseWorldPos;

        float zoomIncrement = 0.125f * wheel;
        camera->zoom += zoomIncrement;

        if (camera->zoom < 0.2f) camera->zoom = 0.2f;
        if (camera->zoom > 4.0f) camera->zoom = 4.0f;
    }
}

