#ifndef SIMULATION_H
#define SIMULATION_H

// --- Core ---
#include "core/common.h"
#include "core/camera.h"
#include "core/event_log.h"

// --- World & Environment ---
#include "world/world.h"
#include "world/resource_node.h"

// --- Entities & Tribe ---
#include "entities/caveman.h"
#include "entities/tribe.h"

// --- Graphics ---
#include "gfx/sprite_sheet.h"

#include <vector>
#include <memory>

class Simulation {
public:
    Simulation(int worldWidth, int worldHeight, int screenWidth, int screenHeight);
    ~Simulation() = default;

    void Update(float dt);
    void DrawWorld() const;
    void DrawUI() const;

    bool IsPaused() const { return isPaused_; }
    void SetPaused(bool paused) { isPaused_ = paused; }

private:
    void HandleInput();
    void SpawnCaveman(Role role, Vector2 worldPos);
    void SpawnAnimal(Vector2 worldPos);
    int GetAliveCount() const;

    World world_;
    CameraController camera_;
    Tribe tribe_;
    ResourceManager resources_;
    std::vector<Caveman> cavemen_;
    SpriteSheet spriteSheet_;
    EventLog eventLog_;

    bool isPaused_ = false;
    float timeSpeed_ = 1.0f;
    float totalSimTime_ = 0.0f;
    int nextCavemanId_ = 1;

    // Kachel-Pinsel
    TileType selectedTile_ = TILE_GRASS;
    int brushRadius_ = 0;

    // Selektion / Inspektor
    int selectedCavemanId_ = -1;
};

#endif // SIMULATION_H
