#ifndef WORLD_H
#define WORLD_H

#include "common.h"

typedef enum {
    TILE_DEEP_WATER = 0,
    TILE_SHALLOW_WATER,
    TILE_SAND,
    TILE_GRASS,
    TILE_FOREST,
    TILE_MOUNTAIN,
    TILE_TYPE_COUNT
} TileType;

typedef struct {
    const char* name;
    Color color;
    bool isWalkable;
    bool isLiquid;
} TileProperty;

typedef struct {
    TileType type;
} Tile;

typedef struct World {
    int width;
    int height;
    Tile* tiles;
} World;

World* World_Create(int width, int height);
void World_Destroy(World* world);

bool World_IsInBounds(const World* world, int x, int y);
Tile* World_GetTile(World* world, int x, int y);
void World_SetTile(World* world, int x, int y, TileType type);

const TileProperty* World_GetProperty(TileType type);
void World_Draw(const World* world);

#endif // WORLD_H

