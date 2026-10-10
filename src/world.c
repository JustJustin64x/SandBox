#include "world.h"

// Definition der Kachel-Eigenschaften (später leicht erweiterbar)
static const TileProperty s_tileProperties[TILE_TYPE_COUNT] = {
    [TILE_DEEP_WATER]    = { .name = "Tiefwasser",    .color = (Color){ 28, 70, 140, 255 },  .isWalkable = false, .isLiquid = true },
    [TILE_SHALLOW_WATER] = { .name = "Flachwasser",   .color = (Color){ 45, 115, 185, 255 }, .isWalkable = false, .isLiquid = true },
    [TILE_SAND]          = { .name = "Sand",          .color = (Color){ 215, 195, 120, 255 }, .isWalkable = true,  .isLiquid = false },
    [TILE_GRASS]         = { .name = "Gras",          .color = (Color){ 80, 160, 60, 255 },   .isWalkable = true,  .isLiquid = false },
    [TILE_FOREST]        = { .name = "Wald",          .color = (Color){ 45, 110, 40, 255 },   .isWalkable = true,  .isLiquid = false },
    [TILE_MOUNTAIN]      = { .name = "Gebirge",       .color = (Color){ 130, 125, 120, 255 }, .isWalkable = false, .isLiquid = false },
};

const TileProperty* World_GetProperty(TileType type) {
    if (type >= 0 && type < TILE_TYPE_COUNT) {
        return &s_tileProperties[type];
    }
    return &s_tileProperties[TILE_DEEP_WATER];
}

bool World_IsInBounds(const World* world, int x, int y) {
    if (!world) return false;
    return (x >= 0 && x < world->width && y >= 0 && y < world->height);
}

Tile* World_GetTile(World* world, int x, int y) {
    if (!World_IsInBounds(world, x, y)) return NULL;
    return &world->tiles[y * world->width + x];
}

void World_SetTile(World* world, int x, int y, TileType type) {
    if (!World_IsInBounds(world, x, y)) return;
    world->tiles[y * world->width + x].type = type;
}

World* World_Create(int width, int height) {
    if (width <= 0 || height <= 0) return NULL;

    World* world = (World*)malloc(sizeof(World));
    if (!world) return NULL;

    world->width = width;
    world->height = height;
    world->tiles = (Tile*)malloc(sizeof(Tile) * (size_t)(width * height));
    if (!world->tiles) {
        free(world);
        return NULL;
    }

    // Einfache Anfangsinsel generieren
    int centerX = width / 2;
    int centerY = height / 2;
    int islandRadius = (width < height ? width : height) / 3;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float dx = (float)(x - centerX);
            float dy = (float)(y - centerY);
            float dist = sqrtf(dx * dx + dy * dy);

            TileType type;
            if (dist > islandRadius + 6) {
                type = TILE_DEEP_WATER;
            } else if (dist > islandRadius) {
                type = TILE_SHALLOW_WATER;
            } else if (dist > islandRadius - 3) {
                type = TILE_SAND;
            } else if (dist > islandRadius - 8) {
                type = TILE_GRASS;
            } else if (dist > islandRadius - 14) {
                type = TILE_FOREST;
            } else {
                type = TILE_MOUNTAIN;
            }

            World_SetTile(world, x, y, type);
        }
    }

    return world;
}

void World_Destroy(World* world) {
    if (!world) return;
    if (world->tiles) {
        free(world->tiles);
        world->tiles = NULL;
    }
    free(world);
}

void World_Draw(const World* world) {
    if (!world || !world->tiles) return;

    for (int y = 0; y < world->height; ++y) {
        for (int x = 0; x < world->width; ++x) {
            TileType type = world->tiles[y * world->width + x].type;
            const TileProperty* prop = World_GetProperty(type);

            Rectangle rect = {
                (float)(x * TILE_SIZE),
                (float)(y * TILE_SIZE),
                (float)TILE_SIZE,
                (float)TILE_SIZE
            };
            DrawRectangleRec(rect, prop->color);
        }
    }

    // Weltbegrenzung zeichnen
    DrawRectangleLines(0, 0, world->width * TILE_SIZE, world->height * TILE_SIZE, DARKGRAY);
}

