#include "raylib.h"

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600

int main(void) {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "WorldBox Sandbox - C Prototype");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        // Update Logik hier

        // Zeichnen
        BeginDrawing();
            ClearBackground(BLACK);
            DrawText("WorldBox C Prototype", 20, 20, 20, RAYWHITE);
            DrawText("Druecke ESC zum Beenden", 20, 50, 16, GRAY);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
