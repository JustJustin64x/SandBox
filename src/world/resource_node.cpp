#include "resource_node.h"
#include "config.h"
#include "world.h"

#include <algorithm>

const char* ResourceTypeName(ResourceType type) {
    switch (type) {
        case ResourceType::Tree:   return "Baum";
        case ResourceType::Bush:   return "Busch";
        case ResourceType::Rock:   return "Fels";
        case ResourceType::Animal: return "Wildtier";
        default:                   return "?";
    }
}

// ============================================================================
//  Platzierung
// ============================================================================
void ResourceManager::SpawnNode(ResourceType type, Vector2 position) {
    ResourceNode n;
    n.type = type;
    n.position = position;
    switch (type) {
        case ResourceType::Tree: n.maxAmount = GetRandomValue(6, 9);  break;
        case ResourceType::Bush: n.maxAmount = 5;                     break;
        case ResourceType::Rock: n.maxAmount = GetRandomValue(8, 12); break;
        case ResourceType::Animal:
            n.maxAmount = cfg::kAnimalMeat;
            n.hp = cfg::kAnimalHp;
            n.wanderTarget = position;
            n.wanderTimer = (float)GetRandomValue(1, 4);
            break;
        default: break;
    }
    n.amount = n.maxAmount;
    nodes_.push_back(n);
}

void ResourceManager::Populate(const World& world, Vector2 campPos) {
    nodes_.clear();
    const float campClear = 5.0f * TILE_SIZE;
    auto farFromCamp = [&](Vector2 p, float minDist) { return Vector2Distance(p, campPos) >= minDist; };

    // Bäume im Wald (Fallback: Gras)
    for (int i = 0; i < cfg::kTreeCount; ++i) {
        Vector2 p;
        bool ok = world.FindRandomTile([&](int x, int y) {
            return world.GetTile(x, y) == TILE_FOREST && farFromCamp(World::TileCenter(x, y), campClear);
        }, p);
        if (!ok) ok = world.FindRandomTile([&](int x, int y) { return world.GetTile(x, y) == TILE_GRASS; }, p);
        if (ok) SpawnNode(ResourceType::Tree, p);
    }

    // Büsche auf Gras – die Hälfte bewusst in Lagernähe
    for (int i = 0; i < cfg::kBushCount; ++i) {
        Vector2 p;
        bool ok = false;
        if (i % 2 == 0) {
            ok = world.FindRandomWalkablePosition(campPos, 25.0f * TILE_SIZE, p, 60, TILE_GRASS) &&
                 farFromCamp(p, campClear);
        }
        if (!ok) ok = world.FindRandomTile([&](int x, int y) { return world.GetTile(x, y) == TILE_GRASS; }, p);
        if (ok) SpawnNode(ResourceType::Bush, p);
    }

    // Felsen bevorzugt am Fuß der Berge
    for (int i = 0; i < cfg::kRockCount; ++i) {
        Vector2 p;
        bool ok = world.FindRandomTile([&](int x, int y) {
            return world.IsWalkableTile(x, y) && world.HasNeighbor(x, y, TILE_MOUNTAIN);
        }, p, 1500);
        if (!ok) ok = world.FindRandomTile([&](int x, int y) {
            return world.IsWalkableTile(x, y) && farFromCamp(World::TileCenter(x, y), campClear);
        }, p);
        if (ok) SpawnNode(ResourceType::Rock, p);
    }

    // Wildtiere auf Gras, mit Abstand zum Lager
    for (int i = 0; i < cfg::kAnimalCount; ++i) {
        Vector2 p;
        if (world.FindRandomTile([&](int x, int y) {
                return world.GetTile(x, y) == TILE_GRASS && farFromCamp(World::TileCenter(x, y), 12.0f * TILE_SIZE);
            }, p)) {
            SpawnNode(ResourceType::Animal, p);
        }
    }
}

int ResourceManager::SpawnAnimal(const World& world, Vector2 position) {
    if (!world.IsWalkable(position)) return -1;
    SpawnNode(ResourceType::Animal, position);
    return (int)nodes_.size() - 1;
}

// ============================================================================
//  Zugriff
// ============================================================================
ResourceNode* ResourceManager::Get(int index) {
    if (index < 0 || index >= (int)nodes_.size()) return nullptr;
    return &nodes_[(size_t)index];
}

const ResourceNode* ResourceManager::Get(int index) const {
    if (index < 0 || index >= (int)nodes_.size()) return nullptr;
    return &nodes_[(size_t)index];
}

bool ResourceManager::IsValidTarget(int index, unsigned int generation) const {
    const ResourceNode* n = Get(index);
    return n && n->generation == generation && n->IsAvailable();
}

int ResourceManager::FindBest(ResourceType type, Vector2 from) const {
    int best = -1;
    float bestScore = 0.0f;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const ResourceNode& n = nodes_[i];
        if (n.type != type || !n.IsAvailable()) continue;
        // Distanz, bestraft durch bereits zugewiesene Arbeiter -> verteilt die Leute
        const float score = Vector2Distance(from, n.position) * (1.0f + 0.75f * (float)n.claims);
        if (best < 0 || score < bestScore) {
            best = (int)i;
            bestScore = score;
        }
    }
    return best;
}

void ResourceManager::Claim(int index) {
    if (ResourceNode* n = Get(index)) n->claims++;
}

void ResourceManager::Release(int index) {
    if (ResourceNode* n = Get(index)) {
        n->claims = std::max(0, n->claims - 1);
        if (n->claims == 0) n->engaged = false;
    }
}

int ResourceManager::Harvest(int index, int wanted) {
    ResourceNode* n = Get(index);
    if (!n || !n->IsAvailable() || n->type == ResourceType::Animal) return 0;
    const int taken = std::min(n->amount, std::max(1, wanted));
    n->amount -= taken;
    return taken;
}

int ResourceManager::KillAnimal(int index) {
    ResourceNode* n = Get(index);
    if (!n || n->type != ResourceType::Animal || !n->alive) return 0;
    const int meat = n->amount;
    n->alive = false;
    n->amount = 0;
    n->engaged = false;
    n->respawnTimer = cfg::kAnimalRespawnTime;
    n->generation++;
    return meat;
}

int ResourceManager::CountAvailable(ResourceType type) const {
    int count = 0;
    for (const ResourceNode& n : nodes_)
        if (n.type == type && n.IsAvailable()) ++count;
    return count;
}

// ============================================================================
//  Update: Nachwachsen & Tierverhalten
// ============================================================================
void ResourceManager::Update(const World& world, Vector2 campPos, float dt) {
    for (ResourceNode& n : nodes_) {
        if (n.type == ResourceType::Animal) {
            UpdateAnimal(n, world, campPos, dt);
            continue;
        }
        if (n.amount >= n.maxAmount) {
            n.regrowTimer = 0.0f;
            continue;
        }
        const float regrowTime = (n.type == ResourceType::Tree) ? cfg::kTreeRegrowTime
                               : (n.type == ResourceType::Bush) ? cfg::kBushRegrowTime
                                                                 : cfg::kRockRegrowTime;
        n.regrowTimer += dt;
        if (n.regrowTimer >= regrowTime) {
            n.regrowTimer -= regrowTime;
            n.amount++;
        }
    }
}

void ResourceManager::UpdateAnimal(ResourceNode& n, const World& world, Vector2 campPos, float dt) {
    if (!n.alive) {
        n.respawnTimer -= dt;
        if (n.respawnTimer <= 0.0f) RespawnAnimal(n, world, campPos);
        return;
    }
    if (n.engaged) return;   // wird gerade bekämpft -> steht still

    n.wanderTimer -= dt;
    if (n.wanderTimer <= 0.0f || Vector2Distance(n.position, n.wanderTarget) < 2.0f) {
        Vector2 target;
        if (world.FindRandomWalkablePosition(n.position, 5.0f * TILE_SIZE, target, 10)) n.wanderTarget = target;
        n.wanderTimer = (float)GetRandomValue(30, 70) / 10.0f;
    }

    const Vector2 delta = Vector2Subtract(n.wanderTarget, n.position);
    const float dist = Vector2Length(delta);
    if (dist < 1.0f) return;

    const Vector2 dir = Vector2Scale(delta, 1.0f / dist);
    const Vector2 next = Vector2Add(n.position, Vector2Scale(dir, std::min(dist, cfg::kAnimalSpeed * dt)));
    if (world.IsWalkable(next)) {
        n.position = next;
        n.animTime += dt;
        if (std::fabs(dir.x) > 0.1f) n.facingLeft = dir.x < 0.0f;
    } else {
        n.wanderTimer = 0.0f;   // Hindernis -> neues Ziel
    }
}

void ResourceManager::RespawnAnimal(ResourceNode& n, const World& world, Vector2 campPos) {
    Vector2 p;
    if (!world.FindRandomTile([&](int x, int y) {
            return world.GetTile(x, y) == TILE_GRASS &&
                   Vector2Distance(World::TileCenter(x, y), campPos) >= 15.0f * TILE_SIZE;
        }, p)) {
        n.respawnTimer = 5.0f;   // später erneut versuchen
        return;
    }
    n.position = p;
    n.wanderTarget = p;
    n.alive = true;
    n.hp = cfg::kAnimalHp;
    n.amount = n.maxAmount;
    n.engaged = false;
    n.generation++;
}

// ============================================================================
//  Zeichnen (einfache Primitive, damit keine weiteren Assets nötig sind)
// ============================================================================
void ResourceManager::Draw(const Camera2D& camera) const {
    const Vector2 tl = GetScreenToWorld2D({ 0, 0 }, camera);
    const Vector2 br = GetScreenToWorld2D({ (float)GetScreenWidth(), (float)GetScreenHeight() }, camera);
    const Rectangle view = { tl.x - 32, tl.y - 32, br.x - tl.x + 64, br.y - tl.y + 64 };

    for (const ResourceNode& n : nodes_) {
        if (!CheckCollisionPointRec(n.position, view)) continue;
        const float x = n.position.x, y = n.position.y;

        switch (n.type) {
            case ResourceType::Tree:
                DrawEllipse((int)x, (int)y, 6, 2, Fade(BLACK, 0.25f));
                if (n.amount > 0) {
                    DrawRectangleRec({ x - 1.5f, y - 7.0f, 3.0f, 7.0f }, { 110, 70, 35, 255 });
                    DrawCircleV({ x, y - 12.0f }, 6.5f, { 30, 90, 30, 255 });
                    DrawCircleV({ x - 3.5f, y - 9.0f }, 4.5f, { 40, 115, 40, 255 });
                    DrawCircleV({ x + 3.5f, y - 9.5f }, 4.5f, { 50, 125, 45, 255 });
                } else {
                    DrawRectangleRec({ x - 2.0f, y - 3.0f, 4.0f, 3.0f }, { 120, 80, 40, 255 });   // Baumstumpf
                }
                break;

            case ResourceType::Bush: {
                DrawCircleV({ x, y - 3.0f }, 5.0f, { 35, 105, 35, 255 });
                DrawCircleV({ x - 3.0f, y - 2.0f }, 3.5f, { 45, 125, 45, 255 });
                static const Vector2 kBerryOffsets[5] = { { -2, -5 }, { 2, -4 }, { 0, -1 }, { -4, -2 }, { 3, -1 } };
                for (int i = 0; i < n.amount && i < 5; ++i)
                    DrawCircleV({ x + kBerryOffsets[i].x, y + kBerryOffsets[i].y }, 1.2f, { 200, 30, 50, 255 });
                break;
            }

            case ResourceType::Rock:
                if (n.amount > 0) {
                    DrawCircleV({ x, y - 3.0f }, 5.0f, { 110, 108, 105, 255 });
                    DrawCircleV({ x + 2.5f, y - 2.0f }, 3.5f, { 135, 132, 128, 255 });
                    DrawCircleV({ x - 1.5f, y - 5.0f }, 1.5f, { 170, 168, 165, 255 });
                } else {
                    DrawCircleV({ x, y - 1.0f }, 1.5f, { 120, 118, 115, 255 });
                }
                break;

            case ResourceType::Animal: {
                if (!n.alive) break;
                const float dir = n.facingLeft ? -1.0f : 1.0f;
                const float legSwing = n.engaged ? 0.0f : std::sin(n.animTime * 12.0f) * 1.5f;
                const Color fur = { 125, 85, 50, 255 };
                DrawEllipse((int)x, (int)y, 6, 2, Fade(BLACK, 0.25f));
                DrawLineEx({ x - 3.0f, y - 3.0f }, { x - 3.0f + legSwing, y }, 1.2f, { 80, 55, 30, 255 });
                DrawLineEx({ x + 3.0f, y - 3.0f }, { x + 3.0f - legSwing, y }, 1.2f, { 80, 55, 30, 255 });
                DrawEllipse((int)x, (int)(y - 4.0f), 6, 3, fur);
                DrawCircleV({ x + 6.0f * dir, y - 6.0f }, 2.5f, fur);
                DrawLineEx({ x + 6.0f * dir, y - 8.0f }, { x + 8.0f * dir, y - 10.5f }, 0.8f, { 230, 220, 200, 255 });
                if (n.hp < cfg::kAnimalHp) {   // Lebensbalken während der Jagd
                    DrawRectangleRec({ x - 6.0f, y - 13.0f, 12.0f, 1.5f }, Fade(BLACK, 0.6f));
                    DrawRectangleRec({ x - 6.0f, y - 13.0f, 12.0f * std::max(0.0f, n.hp) / cfg::kAnimalHp, 1.5f }, RED);
                }
                break;
            }

            default: break;
        }
    }
}

