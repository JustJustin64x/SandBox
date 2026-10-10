#ifndef SIMULATION_H
#define SIMULATION_H

#include "common.h"
#include "world.h"
#include "entity.h"
#include "camera.h"

typedef struct {
    World* world;
    EntityManager entityManager;
    Camera2D camera;
    bool isPaused;
    TileType selectedTile;
    int brushRadius;
} Simulation;

Simulation* Simulation_Create(int worldWidth, int worldHeight, int screenWidth, int screenHeight);
void Simulation_Destroy(Simulation* sim);

void Simulation_Update(Simulation* sim, float deltaTime);
void Simulation_DrawWorld(const Simulation* sim);
void Simulation_DrawUI(const Simulation* sim);

#endif // SIMULATION_H

