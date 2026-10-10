#include "entity.h"
#include "world.h"

void EntityManager_Init(EntityManager* em) {
    if (!em) return;
    em->count = 0;
    for (int i = 0; i < MAX_ENTITIES; ++i) {
        em->entities[i].active = false;
    }
}

Entity* EntityManager_Spawn(EntityManager* em, EntityType type, Vector2 worldPos) {
    if (!em) return NULL;

    for (int i = 0; i < MAX_ENTITIES; ++i) {
        if (!em->entities[i].active) {
            Entity* e = &em->entities[i];
            e->active = true;
            e->type = type;
            e->position = worldPos;
            e->moveTimer = (float)GetRandomValue(5, 20) / 10.0f;
            e->moveDir = (Vector2){ 0.0f, 0.0f };

            if (type == ENTITY_HUMAN) {
                e->hp = 100;
                e->maxHp = 100;
            } else {
                e->hp = 50;
                e->maxHp = 50;
            }

            em->count++;
            return e;
        }
    }
    return NULL;
}

void EntityManager_Update(EntityManager* em, struct World* world, float deltaTime) {
    if (!em) return;

    for (int i = 0; i < MAX_ENTITIES; ++i) {
        Entity* e = &em->entities[i];
        if (!e->active) continue;

        e->moveTimer -= deltaTime;
        if (e->moveTimer <= 0.0f) {
            e->moveTimer = (float)GetRandomValue(10, 30) / 10.0f;

            // Zufällige Richtung wählen (-1, 0, 1)
            int r = GetRandomValue(0, 4);
            switch (r) {
                case 0: e->moveDir = (Vector2){  1.0f,  0.0f }; break;
                case 1: e->moveDir = (Vector2){ -1.0f,  0.0f }; break;
                case 2: e->moveDir = (Vector2){  0.0f,  1.0f }; break;
                case 3: e->moveDir = (Vector2){  0.0f, -1.0f }; break;
                default: e->moveDir = (Vector2){ 0.0f,  0.0f }; break; // Rastpause
            }
        }

        // Bewegen (Geschwindigkeit: z. B. 25 Pixel pro Sekunde)
        float speed = (e->type == ENTITY_HUMAN) ? 30.0f : 40.0f;
        Vector2 nextPos = Vector2Add(e->position, Vector2Scale(e->moveDir, speed * deltaTime));

        // Kollision mit Weltkacheln prüfen
        if (world) {
            int tileX = (int)(nextPos.x / TILE_SIZE);
            int tileY = (int)(nextPos.y / TILE_SIZE);

            if (World_IsInBounds(world, tileX, tileY)) {
                Tile* tile = World_GetTile(world, tileX, tileY);
                const TileProperty* prop = World_GetProperty(tile->type);
                if (prop->isWalkable) {
                    e->position = nextPos;
                } else {
                    // An Hindernis gestoßen: Richtung umkehren
                    e->moveDir = Vector2Scale(e->moveDir, -1.0f);
                }
            }
        } else {
            e->position = nextPos;
        }
    }
}

void EntityManager_Draw(const EntityManager* em) {
    if (!em) return;

    for (int i = 0; i < MAX_ENTITIES; ++i) {
        const Entity* e = &em->entities[i];
        if (!e->active) continue;

        if (e->type == ENTITY_HUMAN) {
            DrawCircleV(e->position, 4.0f, RED);
            DrawCircleLines((int)e->position.x, (int)e->position.y, 4.0f, MAROON);
        } else {
            DrawCircleV(e->position, 3.0f, ORANGE);
            DrawCircleLines((int)e->position.x, (int)e->position.y, 3.0f, BROWN);
        }
    }
}
