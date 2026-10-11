#include "common.h"
#include "simulation.h"

int main() {
    // Raylib-Fenster initialisieren
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Stone Age 2D Sandbox - Primitive Survival & Evolution");
    SetTargetFPS(TARGET_FPS);

    // Hauptsimulation erzeugen
    Simulation sim(
        DEFAULT_WORLD_WIDTH,
        DEFAULT_WORLD_HEIGHT,
        SCREEN_WIDTH,
        SCREEN_HEIGHT
    );

    // Spiel-Loop
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        sim.Update(dt);

        BeginDrawing();
            ClearBackground(BLACK);
            sim.DrawWorld();
            sim.DrawUI();
        EndDrawing();
    }

    CloseWindow();
    return 0;
}

