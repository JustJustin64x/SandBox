#include "world/resource_node.h"
#include "core/config.h"
#include "world/world.h"
#include "world/environmental_system.h"

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

    // Bäume im Wald (Fallback: Gras) – keine Bäume auf Klippen oder Hochgebirge
    for (int i = 0; i < cfg::kTreeCount; ++i) {
        Vector2 p;
        bool ok = world.FindRandomTile([&](int x, int y) {
            const TileData& td = world.GetTileData(x, y);
            if (td.isCliff || td.heightLevel >= 3) return false;
            return world.GetTile(x, y) == TILE_FOREST && farFromCamp(World::TileCenter(x, y), campClear);
        }, p);
        if (!ok) ok = world.FindRandomTile([&](int x, int y) {
            const TileData& td = world.GetTileData(x, y);
            if (td.isCliff || td.heightLevel >= 3) return false;
            return world.GetTile(x, y) == TILE_GRASS;
        }, p);
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
        if (!ok) ok = world.FindRandomTile([&](int x, int y) {
            const TileData& td = world.GetTileData(x, y);
            if (td.isCliff || td.heightLevel >= 3) return false;
            return world.GetTile(x, y) == TILE_GRASS;
        }, p);
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

    // Wildtiere auf flachem Gras oder Wald, mit Abstand zum Lager
    for (int i = 0; i < cfg::kAnimalCount; ++i) {
        Vector2 p;
        if (world.FindRandomTile([&](int x, int y) {
                const TileData& td = world.GetTileData(x, y);
                if (td.isCliff || td.heightLevel >= 3) return false;
                int t = world.GetTile(x, y);
                return (t == TILE_GRASS || t == TILE_FOREST) && farFromCamp(World::TileCenter(x, y), 12.0f * TILE_SIZE);
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

int ResourceManager::FindBest(ResourceType type, Vector2 from, const World* world, int actorTribeId, bool allowNeutralGrenz, const std::function<bool(int)>& isBlacklisted) const {
    int best = -1;
    float bestScore = 0.0f;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const ResourceNode& n = nodes_[i];
        if (n.type != type || !n.IsAvailable()) continue;
        if (isBlacklisted && isBlacklisted((int)i)) continue;

        // Territoriale Beschränkung (Phase 2):
        if (world && actorTribeId >= 0) {
            int tx = World::ToTile(n.position.x);
            int ty = World::ToTile(n.position.y);
            int ownerId = world->GetOwnerTribeId(tx, ty);

            // Fremdes Clan-Territorium ist für friedliche Alltagsarbeit strikt tabu!
            if (ownerId >= 0 && ownerId != actorTribeId) continue;

            // Neutrales Land:
            if (ownerId == -1) {
                // Wildtiere dürfen in neutralem Land gejagt werden.
                // Rohstoffe (Holz/Stein/Beeren) nur bei Mangel oder Grenz-Pionieren:
                if (type != ResourceType::Animal && !allowNeutralGrenz) {
                    continue;
                }
            }
        }

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
        // Verkohlte Bäume/Büsche brauchen länger, um sich aus der Asche zu erholen
        if (n.charred) {
            const int tx = World::ToTile(n.position.x);
            const int ty = World::ToTile(n.position.y);
            if (world.Env() && world.Env()->GetState(tx, ty).isBurning) {
                n.regrowTimer = 0.0f;
                continue;
            }
            n.regrowTimer += dt;
            if (n.regrowTimer >= cfg::kTreeRegrowTime * 2.2f) {
                n.charred = false;
                n.regrowTimer = 0.0f;
                n.amount = 1;
                n.generation++;
            }
            continue;
        }
        if (n.amount >= n.maxAmount) {
            n.regrowTimer = 0.0f;
            continue;
        }
        const float regrowTime = (n.type == ResourceType::Tree) ? cfg::kTreeRegrowTime
                               : (n.type == ResourceType::Bush) ? cfg::kBushRegrowTime
                                                                 : cfg::kRockRegrowTime;
        float growRate = 1.0f;
        // Fruchtbarkeit an Flussauen: +40% Nachwachsen im Umkreis von 5 Kacheln um Süßwasser
        int tx = World::ToTile(n.position.x);
        int ty = World::ToTile(n.position.y);
        if (world.IsNearFreshwater(tx, ty, 5)) {
            growRate = 1.40f;
        }
        n.regrowTimer += dt * growRate;
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

    n.panicTimer = std::max(0.0f, n.panicTimer - dt);

    // Prüfen, ob Feuer oder Strom in der Nähe ist -> Panische Flucht!
    if (world.Env()) {
        Vector2 hazardCenter = { 0.0f, 0.0f };
        if (world.Env()->IsHazardNear(n.position, 44.0f, hazardCenter)) {
            n.engaged = false;
            n.panicTimer = 2.4f;
            Vector2 away = Vector2Subtract(n.position, hazardCenter);
            if (Vector2Length(away) < 0.5f) away = { 1.0f, 0.0f };
            away = Vector2Normalize(away);
            Vector2 escapeTarget = Vector2Add(n.position, Vector2Scale(away, 55.0f));
            if (world.IsWalkable(escapeTarget)) {
                n.wanderTarget = escapeTarget;
            } else {
                world.FindRandomWalkablePosition(n.position, 55.0f, n.wanderTarget, 12);
            }
            n.wanderTimer = 1.2f;
        }
    }

    if (n.engaged && n.panicTimer <= 0.0f) return;   // wird gerade bekämpft -> steht still

    n.wanderTimer -= dt;
    if (n.wanderTimer <= 0.0f || Vector2Distance(n.position, n.wanderTarget) < 2.0f) {
        Vector2 target;
        if (world.FindRandomWalkablePosition(n.position, 5.0f * TILE_SIZE, target, 10)) n.wanderTarget = target;
        n.wanderTimer = (float)GetRandomValue(30, 70) / 10.0f;
    }

    const Vector2 delta = Vector2Subtract(n.wanderTarget, n.position);
    const float dist = Vector2Length(delta);
    if (dist < 1.0f) return;

    const float speedMult = (n.panicTimer > 0.0f) ? 1.75f : 1.0f;
    const Vector2 dir = Vector2Scale(delta, 1.0f / dist);
    const Vector2 next = Vector2Add(n.position, Vector2Scale(dir, std::min(dist, cfg::kAnimalSpeed * speedMult * dt)));
    if (world.IsWalkable(next)) {
        n.position = next;
        n.animTime += dt * speedMult;
        if (std::fabs(dir.x) > 0.1f) n.facingLeft = dir.x < 0.0f;
    } else {
        n.wanderTimer = 0.0f;   // Hindernis -> neues Ziel
    }
}

void ResourceManager::RespawnAnimal(ResourceNode& n, const World& world, Vector2 campPos) {
    Vector2 p;
    if (!world.FindRandomTile([&](int x, int y) {
            int t = world.GetTile(x, y);
            return (t == TILE_GRASS || t == TILE_FOREST) &&
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
    n.charred = false;
    n.panicTimer = 0.0f;
    n.generation++;
}

// ============================================================================
//  Zeichnen (Phase 4: 2.5D Elevation-Offset, plastische 3D-Bäume & Drop-Shadows)
// ============================================================================
void ResourceManager::Draw(const Camera2D& camera, const World* world) const {
    const Vector2 tl = GetScreenToWorld2D({ 0, 0 }, camera);
    const Vector2 br = GetScreenToWorld2D({ (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT }, camera);
    const Rectangle view = { tl.x - 32, tl.y - 48, br.x - tl.x + 64, br.y - tl.y + 80 };

    for (const ResourceNode& n : nodes_) {
        if (!CheckCollisionPointRec(n.position, view)) continue;
        const float x = n.position.x;
        const float visualY = n.position.y + (world ? world->GetVisualYOffset(n.position) : 0.0f);

        switch (n.type) {
            case ResourceType::Tree:
                // Runder 3D-Boden-Schlagschatten
                DrawEllipse((int)x, (int)visualY, 7, 3, Fade(BLACK, 0.30f));
                if (n.charred) {
                    // Verkohlter Baumstumpf mit Glutresten
                    DrawRectangleRec({ x - 2.5f, visualY - 5.0f, 5.0f, 5.0f }, { 38, 32, 30, 255 });
                    DrawLineEx({ x - 2.0f, visualY - 5.0f }, { x - 3.5f, visualY - 8.5f }, 1.5f, { 30, 25, 22, 255 });
                    DrawLineEx({ x + 1.5f, visualY - 5.0f }, { x + 3.0f, visualY - 7.5f }, 1.3f, { 30, 25, 22, 255 });
                    DrawCircleV({ x, visualY - 2.5f }, 1.2f, { 235, 95, 30, 200 }); // Glutpunkt
                } else if (n.amount > 0) {
                    // Plastischer 3D-Baum mit mehrstufig abgestufter Kugelkrone
                    DrawRectangleRec({ x - 1.5f, visualY - 8.0f, 3.0f, 8.0f }, { 102, 64, 32, 255 });
                    // Dunkelgrüne Basis-Krone
                    DrawCircleV({ x, visualY - 14.0f }, 7.5f, { 24, 76, 26, 255 });
                    // Mittlere Schattierungsebenen
                    DrawCircleV({ x - 3.5f, visualY - 11.0f }, 5.2f, { 35, 102, 36, 255 });
                    DrawCircleV({ x + 3.5f, visualY - 11.5f }, 5.2f, { 42, 114, 40, 255 });
                    // Sonnen-Highlights auf der Oberseite (plastische Plastizität)
                    DrawCircleV({ x - 1.2f, visualY - 16.0f }, 3.8f, { 64, 148, 54, 255 });
                    DrawCircleV({ x + 2.2f, visualY - 14.2f }, 2.8f, { 80, 168, 64, 255 });
                } else {
                    DrawRectangleRec({ x - 2.0f, visualY - 3.0f, 4.0f, 3.0f }, { 120, 80, 40, 255 });   // Baumstumpf
                }
                break;

            case ResourceType::Bush: {
                DrawEllipse((int)x, (int)visualY, 5, 2, Fade(BLACK, 0.22f));
                if (n.charred) {
                    DrawCircleV({ x, visualY - 2.0f }, 3.5f, { 45, 40, 38, 255 });
                    break;
                }
                DrawCircleV({ x, visualY - 3.0f }, 5.0f, { 35, 105, 35, 255 });
                DrawCircleV({ x - 3.0f, visualY - 2.0f }, 3.5f, { 45, 125, 45, 255 });
                DrawCircleV({ x + 1.0f, visualY - 5.0f }, 2.8f, { 60, 142, 52, 255 });
                static const Vector2 kBerryOffsets[5] = { { -2, -5 }, { 2, -4 }, { 0, -1 }, { -4, -2 }, { 3, -1 } };
                for (int i = 0; i < n.amount && i < 5; ++i)
                    DrawCircleV({ x + kBerryOffsets[i].x, visualY + kBerryOffsets[i].y }, 1.2f, { 200, 30, 50, 255 });
                break;
            }

            case ResourceType::Rock:
                DrawEllipse((int)x, (int)visualY, 6, 2, Fade(BLACK, 0.26f));
                if (n.amount > 0) {
                    DrawCircleV({ x, visualY - 3.0f }, 5.0f, { 110, 108, 105, 255 });
                    DrawCircleV({ x + 2.5f, visualY - 2.0f }, 3.5f, { 135, 132, 128, 255 });
                    DrawCircleV({ x - 1.5f, visualY - 5.0f }, 1.8f, { 172, 170, 166, 255 });
                } else {
                    DrawCircleV({ x, visualY - 1.0f }, 1.5f, { 120, 118, 115, 255 });
                }
                break;

            case ResourceType::Animal: {
                if (!n.alive) break;
                const float dir = n.facingLeft ? -1.0f : 1.0f;
                const float legSwing = n.engaged ? 0.0f : std::sin(n.animTime * 12.0f) * 1.5f;
                const Color fur = { 125, 85, 50, 255 };
                DrawEllipse((int)x, (int)visualY, 6, 2, Fade(BLACK, 0.25f));
                DrawLineEx({ x - 3.0f, visualY - 3.0f }, { x - 3.0f + legSwing, visualY }, 1.2f, { 80, 55, 30, 255 });
                DrawLineEx({ x + 3.0f, visualY - 3.0f }, { x + 3.0f - legSwing, visualY }, 1.2f, { 80, 55, 30, 255 });
                DrawEllipse((int)x, (int)(visualY - 4.0f), 6, 3, fur);
                DrawCircleV({ x + 6.0f * dir, visualY - 6.0f }, 2.5f, fur);
                DrawLineEx({ x + 6.0f * dir, visualY - 8.0f }, { x + 8.0f * dir, visualY - 10.5f }, 0.8f, { 230, 220, 200, 255 });
                if (n.panicTimer > 0.0f) {
                    DrawText("!", (int)x - 1, (int)(visualY - 16.0f), 10, ORANGE);
                }
                if (n.hp < cfg::kAnimalHp) {   // Lebensbalken während der Jagd
                    DrawRectangleRec({ x - 6.0f, visualY - 13.0f, 12.0f, 1.5f }, Fade(BLACK, 0.6f));
                    DrawRectangleRec({ x - 6.0f, visualY - 13.0f, 12.0f * std::max(0.0f, n.hp) / cfg::kAnimalHp, 1.5f }, RED);
                }
                break;
            }

            default: break;
        }
    }
}

