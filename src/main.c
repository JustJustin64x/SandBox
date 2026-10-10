#include "common.h"
#include "simulation.h"

int main(void) {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "WorldBox Sandbox - C Prototype");
    SetTargetFPS(TARGET_FPS);

    Simulation* sim = Simulation_Create(
        DEFAULT_WORLD_WIDTH,
        DEFAULT_WORLD_HEIGHT,
        SCREEN_WIDTH,
        SCREEN_HEIGHT
    );

    if (!sim) {
        CloseWindow();
        return 1;
    }

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        Simulation_Update(sim, dt);

        BeginDrawing();
            ClearBackground(BLACK);
            Simulation_DrawWorld(sim);
            Simulation_DrawUI(sim);
        EndDrawing();
    }

    Simulation_Destroy(sim);
    CloseWindow();

    return 0;
}
