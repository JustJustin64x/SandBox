#ifndef ENTITY_H
#define ENTITY_H

#include "common.h"

struct World;

typedef enum {
    ENTITY_HUMAN = 0,
    ENTITY_ANIMAL,
    ENTITY_TYPE_COUNT
} EntityType;

typedef struct {
    bool active;
    EntityType type;
    Vector2 position;   // Weltkoordinaten in Pixeln
    int hp;
    int maxHp;
    float moveTimer;
    Vector2 moveDir;
} Entity;

#define MAX_ENTITIES 512

typedef struct {
    Entity entities[MAX_ENTITIES];
    int count;
} EntityManager;

void EntityManager_Init(EntityManager* em);
Entity* EntityManager_Spawn(EntityManager* em, EntityType type, Vector2 worldPos);
void EntityManager_Update(EntityManager* em, struct World* world, float deltaTime);
void EntityManager_Draw(const EntityManager* em);

#endif // ENTITY_H

