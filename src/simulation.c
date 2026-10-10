#include "simulation.h"

Simulation* Simulation_Create(int worldWidth, int worldHeight, int screenWidth, int screenHeight) {
    Simulation* sim = (Simulation*)malloc(sizeof(Simulation));
    if (!sim) return NULL;

    sim->world = World_Create(worldWidth, worldHeight);
    EntityManager_Init(&sim->entityManager);

    sim->camera = Camera_Create(screenWidth, screenHeight, worldWidth * TILE_SIZE, worldHeight * TILE_SIZE);
    sim->isPaused = false;
    sim->selectedTile = TILE_GRASS;
    sim->brushRadius = 0;

    // Ein paar Anfangswesen in der Mitte platzieren
    int centerX = worldWidth / 2;
    int centerY = worldHeight / 2;
    for (int i = 0; i < 5; ++i) {
        Vector2 pos = {
            (float)((centerX + GetRandomValue(-3, 3)) * TILE_SIZE + TILE_SIZE / 2),
            (float)((centerY + GetRandomValue(-3, 3)) * TILE_SIZE + TILE_SIZE / 2)
        };
        EntityManager_Spawn(&sim->entityManager, ENTITY_HUMAN, pos);
    }
    for (int i = 0; i < 5; ++i) {
        Vector2 pos = {
            (float)((centerX + GetRandomValue(-5, 5)) * TILE_SIZE + TILE_SIZE / 2),
            (float)((centerY + GetRandomValue(-5, 5)) * TILE_SIZE + TILE_SIZE / 2)
        };
        EntityManager_Spawn(&sim->entityManager, ENTITY_ANIMAL, pos);
    }

    return sim;
}

void Simulation_Destroy(Simulation* sim) {
    if (!sim) return;
    if (sim->world) {
        World_Destroy(sim->world);
        sim->world = NULL;
    }
    free(sim);
}

void Simulation_Update(Simulation* sim, float deltaTime) {
    if (!sim) return;

    // 1. Pause umschalten
    if (IsKeyPressed(KEY_SPACE)) {
        sim->isPaused = !sim->isPaused;
    }

    // 2. Kamera aktualisieren
    Camera_Update(&sim->camera, deltaTime);

    // 3. Kachel-Werkzeug wählen (1 bis 6)
    if (IsKeyPressed(KEY_ONE))   sim->selectedTile = TILE_DEEP_WATER;
    if (IsKeyPressed(KEY_TWO))   sim->selectedTile = TILE_SHALLOW_WATER;
    if (IsKeyPressed(KEY_THREE)) sim->selectedTile = TILE_SAND;
    if (IsKeyPressed(KEY_FOUR))  sim->selectedTile = TILE_GRASS;
    if (IsKeyPressed(KEY_FIVE))  sim->selectedTile = TILE_FOREST;
    if (IsKeyPressed(KEY_SIX))   sim->selectedTile = TILE_MOUNTAIN;

    // Pinselgröße (Bilder auf/ab oder +/-)
    if (IsKeyPressed(KEY_KP_ADD) || IsKeyPressed(KEY_EQUAL)) {
        if (sim->brushRadius < 3) sim->brushRadius++;
    }
    if (IsKeyPressed(KEY_KP_SUBTRACT) || IsKeyPressed(KEY_MINUS)) {
        if (sim->brushRadius > 0) sim->brushRadius--;
    }

    Vector2 mouseWorld = GetScreenToWorld2D(GetMousePosition(), sim->camera);

    // 4. Terrain malen mit Linksklick
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        int tx = (int)(mouseWorld.x / TILE_SIZE);
        int ty = (int)(mouseWorld.y / TILE_SIZE);

        for (int dy = -sim->brushRadius; dy <= sim->brushRadius; ++dy) {
            for (int dx = -sim->brushRadius; dx <= sim->brushRadius; ++dx) {
                World_SetTile(sim->world, tx + dx, ty + dy, sim->selectedTile);
            }
        }
    }

    // 5. Entität spawnen mit Rechtsklick (Shift + Rechtsklick = Tier, sonst Mensch)
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        EntityType spawnType = IsKeyDown(KEY_LEFT_SHIFT) ? ENTITY_ANIMAL : ENTITY_HUMAN;
        EntityManager_Spawn(&sim->entityManager, spawnType, mouseWorld);
    }

    // 6. Simulations-Tick
    if (!sim->isPaused) {
        EntityManager_Update(&sim->entityManager, sim->world, deltaTime);
    }
}

void Simulation_DrawWorld(const Simulation* sim) {
    if (!sim) return;

    BeginMode2D(sim->camera);

    World_Draw(sim->world);
    EntityManager_Draw(&sim->entityManager);

    // Pinsel-Vorschau an der Mausposition
    Vector2 mouseWorld = GetScreenToWorld2D(GetMousePosition(), sim->camera);
    int tx = (int)(mouseWorld.x / TILE_SIZE);
    int ty = (int)(mouseWorld.y / TILE_SIZE);

    int startX = (tx - sim->brushRadius) * TILE_SIZE;
    int startY = (ty - sim->brushRadius) * TILE_SIZE;
    int size = (sim->brushRadius * 2 + 1) * TILE_SIZE;
    DrawRectangleLines(startX, startY, size, size, WHITE);

    EndMode2D();
}

void Simulation_DrawUI(const Simulation* sim) {
    if (!sim) return;

    // Oberer Informationsbalken
    DrawRectangle(0, 0, GetScreenWidth(), 45, Fade(BLACK, 0.75f));

    DrawText("WorldBox C Prototype", 15, 12, 20, RAYWHITE);

    // Pause-Status
    if (sim->isPaused) {
        DrawText("[PAUSIERT]", 260, 14, 18, RED);
    } else {
        DrawText("[LÄUFT]", 260, 14, 18, GREEN);
    }

    // Ausgewähltes Werkzeug
    const TileProperty* prop = World_GetProperty(sim->selectedTile);
    DrawText(TextFormat("Pinsel: %s (Größe: %d)", prop->name, sim->brushRadius + 1), 370, 14, 18, prop->color);

    // Einheiten-Zahl & FPS
    DrawText(TextFormat("Wesen: %d/%d", sim->entityManager.count, MAX_ENTITIES), 650, 14, 18, RAYWHITE);
    DrawFPS(GetScreenWidth() - 90, 14);

    // Untere Hilfeleiste
    int bottomY = GetScreenHeight() - 28;
    DrawRectangle(0, bottomY - 4, GetScreenWidth(), 32, Fade(BLACK, 0.75f));
    DrawText("1-6: Kachel wählen | Linksklick: Malen | Rechtsklick: Mensch (Shift: Tier) | Leertaste: Pause | WASD/MMB: Kamera | Rad: Zoom",
             15, bottomY, 15, LIGHTGRAY);
}

