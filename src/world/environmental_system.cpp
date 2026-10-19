#include "world/environmental_system.h"
#include "world/world.h"
#include "world/resource_node.h"
#include "buildings/building.h"
#include "entities/caveman.h"
#include "core/event_log.h"
#include "core/config.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace {
constexpr float kEnvTickInterval = 0.15f;   // 6.6 Ticks/Sekunde für den 160x100 Kachel-Automaten
constexpr int   kMaxParticles    = 900;
}

const char* WeatherTypeName(WeatherType type) {
    switch (type) {
        case WeatherType::Clear: return "KLAR";
        case WeatherType::Rain:  return "REGEN";
        case WeatherType::Storm: return "STURM";
        case WeatherType::Fog:   return "NEBEL";
    }
    return "KLAR";
}

Color WeatherTypeColor(WeatherType type) {
    switch (type) {
        case WeatherType::Clear: return GOLD;
        case WeatherType::Rain:  return SKYBLUE;
        case WeatherType::Storm: return { 180, 140, 255, 255 };
        case WeatherType::Fog:   return LIGHTGRAY;
    }
    return WHITE;
}

// ============================================================================
//  Konstruktion & Initialisierung
// ============================================================================
EnvironmentalSystem::EnvironmentalSystem(int width, int height)
    : width_(width), height_(height),
      grid_((size_t)(width * height)),
      nextGrid_((size_t)(width * height)) {
    particles_.reserve(kMaxParticles);
}

void EnvironmentalSystem::InitDemoPhenomena(World& world) {
    (void)world;
    // Keine automatischen Kristalle oder Krater beim Start spawnen.
}

// ============================================================================
//  Kachel-Zugriff & Abfragen für KI / Pathfinding
// ============================================================================
const TileEnvState& EnvironmentalSystem::GetState(int tx, int ty) const {
    static const TileEnvState kDefault{};
    if (tx < 0 || tx >= width_ || ty < 0 || ty >= height_) return kDefault;
    return grid_[(size_t)(ty * width_ + tx)];
}

TileEnvState& EnvironmentalSystem::GetStateMutable(int tx, int ty) {
    static TileEnvState kDummy{};
    if (tx < 0 || tx >= width_ || ty < 0 || ty >= height_) return kDummy;
    return grid_[(size_t)(ty * width_ + tx)];
}

bool EnvironmentalSystem::IsFlammableTile(const World& world, int tx, int ty) const {
    if (!world.IsInBounds(tx, ty)) return false;
    TileType t = world.GetTile(tx, ty);
    const TileEnvState& st = GetState(tx, ty);
    if (st.scorched && st.fuel <= 0.05f) return false;
    return (t == TILE_FOREST || t == TILE_GRASS);
}

bool EnvironmentalSystem::IsTileDangerous(int tx, int ty) const {
    const TileEnvState& st = GetState(tx, ty);
    return (st.isBurning && st.fireIntensity > 0.05f) || (st.electrifiedTimer > 0.0f);
}

bool EnvironmentalSystem::IsHazardNear(Vector2 worldPos, float radiusPx, Vector2& outHazardCenter) const {
    const int cx = World::ToTile(worldPos.x);
    const int cy = World::ToTile(worldPos.y);
    const int rTiles = std::max(1, (int)std::ceil(radiusPx / (float)TILE_SIZE));

    float bestDistSq = radiusPx * radiusPx;
    bool found = false;

    for (int dy = -rTiles; dy <= rTiles; ++dy) {
        for (int dx = -rTiles; dx <= rTiles; ++dx) {
            int nx = cx + dx, ny = cy + dy;
            if (!IsTileDangerous(nx, ny)) continue;
            Vector2 tc = World::TileCenter(nx, ny);
            float dSq = Vector2DistanceSqr(worldPos, tc);
            if (dSq <= bestDistSq) {
                bestDistSq = dSq;
                outHazardCenter = tc;
                found = true;
            }
        }
    }
    return found;
}

float EnvironmentalSystem::GetTileTraversalCost(int tx, int ty) const {
    const TileEnvState& st = GetState(tx, ty);
    // Brennende oder elektrifizierte Kacheln werden vom A* extrem stark gemieden
    if (st.isBurning || st.electrifiedTimer > 0.0f) return 45.0f;
    float cost = 1.0f;
    if (st.frozen || st.frost > 0.35f) cost += 0.75f;
    if (st.scorched) cost += 0.25f;
    return cost;
}

float EnvironmentalSystem::GetMovementSpeedMultiplier(Vector2 worldPos) const {
    const int tx = World::ToTile(worldPos.x);
    const int ty = World::ToTile(worldPos.y);
    const TileEnvState& st = GetState(tx, ty);

    float mult = 1.0f;
    // Glätte / Rutschen auf Eis oder starkem Bodenfrost
    if (st.frozen || st.frost > 0.4f) mult *= 0.75f;
    if (weather_ == WeatherType::Storm) mult *= 0.88f;
    return mult;
}

float EnvironmentalSystem::GetEnvironmentalWarmthDelta(Vector2 worldPos) const {
    const int tx = World::ToTile(worldPos.x);
    const int ty = World::ToTile(worldPos.y);
    const TileEnvState& st = GetState(tx, ty);

    float delta = 0.0f;
    // Nässe und Regen entziehen Körperwärme
    if (weather_ == WeatherType::Rain)  delta -= 1.2f;
    if (weather_ == WeatherType::Storm) delta -= 2.2f;
    if (st.frost > 0.3f || st.frozen)   delta -= 1.8f;

    // Nahes Feuer strahlt kräftige Hitze ab
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            if (dx == 0 && dy == 0) continue;
            const TileEnvState& nst = GetState(tx + dx, ty + dy);
            if (nst.isBurning) {
                delta += 4.5f * nst.fireIntensity;
            }
        }
    }
    return std::clamp(delta, -5.0f, 12.0f);
}

float EnvironmentalSystem::GetVisionMultiplier(float timeOfDay, bool hasTorch) const {
    float mult = 1.0f;
    if (weather_ == WeatherType::Fog)   mult *= 0.45f;
    if (weather_ == WeatherType::Storm) mult *= 0.70f;

    bool isNight = (timeOfDay > 0.72f || timeOfDay < 0.22f);
    if (isNight && !hasTorch) {
        mult *= 0.55f;
    }
    return mult;
}

// ============================================================================
//  Spieler-Eingriffe & Sandbox-Aktionen
// ============================================================================
void EnvironmentalSystem::IgniteTile(World& world, int tx, int ty, float intensity) {
    if (!world.IsInBounds(tx, ty)) return;
    TileType t = world.GetTile(tx, ty);

    // Wenn Wasser getroffen wird: sofort Dampf erzeugen!
    if (World::GetProperty(t).isLiquid) {
        Vector2 c = World::TileCenter(tx, ty);
        for (int i = 0; i < 4; ++i) {
            SpawnParticle(EnvParticleType::Steam,
                          { c.x + (float)GetRandomValue(-4, 4), c.y + (float)GetRandomValue(-4, 4) },
                          { windDir_.x * 8.0f, -14.0f - (float)GetRandomValue(0, 8) },
                          1.4f, 3.5f, Fade(RAYWHITE, 0.75f));
        }
        return;
    }

    if (t == TILE_MOUNTAIN) return;

    TileEnvState& st = GetStateMutable(tx, ty);
    st.isBurning = true;
    st.fireIntensity = std::clamp(intensity, 0.3f, 1.0f);
    st.fireTimer = (t == TILE_FOREST) ? 14.0f : 7.5f;
    st.fuel = (t == TILE_FOREST) ? 1.0f : 0.55f;
    st.wetness = 0.0f;
    st.frost = 0.0f;
    st.frozen = false;
}

void EnvironmentalSystem::IgniteRadius(World& world, Vector2 worldPos, float radiusPx, EventLog& log) {
    int cx = World::ToTile(worldPos.x);
    int cy = World::ToTile(worldPos.y);
    int r = std::max(1, (int)std::ceil(radiusPx / (float)TILE_SIZE));
    int ignitedCount = 0;

    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy <= r * r) {
                int tx = cx + dx, ty = cy + dy;
                if (world.IsInBounds(tx, ty) && !World::GetProperty(world.GetTile(tx, ty)).isLiquid) {
                    IgniteTile(world, tx, ty, 0.9f);
                    ignitedCount++;
                }
            }
        }
    }

    if (ignitedCount > 0 && wildfireLogCooldown_ <= 0.0f) {
        log.Add("Ein Lauffeuer wurde entfacht! Wind treibt die Flammen voran.", { 255, 110, 40, 255 });
        wildfireLogCooldown_ = 5.0f;
    }
}

void EnvironmentalSystem::SpawnAntigravityAnomaly(Vector2 worldPos, float radiusPx, float duration, bool permanent, EventLog& log) {
    AntigravityZone z;
    z.position = worldPos;
    z.radius = radiusPx;
    z.duration = duration;
    z.permanent = permanent;
    antigravZones_.push_back(z);

    // Ein paar lose Steine/Holzstücke erzeugen, die sofort zu schweben beginnen
    for (int i = 0; i < 4; ++i) {
        Vector2 p = { worldPos.x + (float)GetRandomValue(-14, 14), worldPos.y + (float)GetRandomValue(-14, 14) };
        SpawnGroundDrop((i % 2 == 0) ? DropItemType::StoneChunk : DropItemType::WoodLog, p, 1);
    }

    log.Add("Gravitations-Anomalie aktiv! Trümmer und Ressourcen schweben schwerelos.", { 150, 130, 255, 255 });
}

void EnvironmentalSystem::TriggerLightningStrike(World& world,
                                                 ResourceManager& resources,
                                                 BuildingManager& buildings,
                                                 std::vector<Caveman>& cavemen,
                                                 Vector2 worldPos,
                                                 EventLog& log) {
    (void)buildings;
    (void)cavemen;
    int tx = World::ToTile(worldPos.x);
    int ty = World::ToTile(worldPos.y);
    if (!world.IsInBounds(tx, ty)) return;

    TileType t = world.GetTile(tx, ty);
    if (World::GetProperty(t).isLiquid) {
        // Blitz schlägt ins Wasser ein -> Elektrifiziert verbundene Wasserfläche!
        GetStateMutable(tx, ty).electrifiedTimer = 5.0f;
        GetStateMutable(tx, ty).electricIntensity = 1.0f;
        PropagateElectricity(world);
        log.Add("Blitzeinschlag im Gewässer! Das Wasser steht unter Strom!", { 110, 220, 255, 255 });
    } else {
        // Blitz schlägt an Land ein -> Entzündet Wald/Gras und hinterlässt kleinen Einschlag
        IgniteRadius(world, worldPos, 18.0f, log);
        for (ResourceNode& n : resources.NodesMutable()) {
            if (n.type == ResourceType::Tree && Vector2Distance(n.position, worldPos) < 16.0f) {
                n.charred = true;
                n.amount = 0;
                SpawnGroundDrop(DropItemType::CharredWood, n.position, 2);
            }
        }
        log.Add("Blitzeinschlag hat ein Feuer ausgelöst!", { 255, 200, 90, 255 });
    }

    for (int i = 0; i < 18; ++i) {
        SpawnParticle(EnvParticleType::ElectricArc, worldPos,
                      { (float)GetRandomValue(-45, 45), (float)GetRandomValue(-45, 45) },
                      0.45f, 2.5f, { 160, 235, 255, 255 });
    }
}

void EnvironmentalSystem::SetWeather(WeatherType weather, EventLog* log) {
    weather_ = weather;
    weatherTimer_ = (float)GetRandomValue(55, 95);
    switch (weather_) {
        case WeatherType::Clear:
            windStrength_ = 0.25f;
            break;
        case WeatherType::Rain:
            windStrength_ = 0.50f;
            break;
        case WeatherType::Storm:
            windStrength_ = 0.92f;
            break;
        case WeatherType::Fog:
            windStrength_ = 0.15f;
            break;
    }
    if (log) {
        log->Add(TextFormat("Wetterumschwung: %s (Windstärke %.0f%%)", WeatherTypeName(weather_), windStrength_ * 100.0f),
                 WeatherTypeColor(weather_));
    }
}

void EnvironmentalSystem::CycleWeather(EventLog& log) {
    int next = ((int)weather_ + 1) % 4;
    SetWeather((WeatherType)next, &log);
}

// ============================================================================
//  Haupt-Update & Kachel-Automat (Cellular Automata)
// ============================================================================
void EnvironmentalSystem::Update(World& world,
                                 ResourceManager& resources,
                                 BuildingManager& buildings,
                                 std::vector<Caveman>& cavemen,
                                 EventLog& log,
                                 float timeOfDay,
                                 float dt) {
    wildfireLogCooldown_   = std::max(0.0f, wildfireLogCooldown_ - dt);
    collateralLogCooldown_ = std::max(0.0f, collateralLogCooldown_ - dt);

    // 1. Dynamischer Wind & Wetter-Zyklus
    weatherTimer_ -= dt;
    if (weatherTimer_ <= 0.0f) {
        int r = GetRandomValue(0, 99);
        WeatherType nextW = WeatherType::Clear;
        if (r < 40)      nextW = WeatherType::Clear;
        else if (r < 70) nextW = WeatherType::Rain;
        else if (r < 88) nextW = WeatherType::Storm;
        else             nextW = WeatherType::Fog;
        if (nextW != weather_) SetWeather(nextW, &log);
        else weatherTimer_ = 60.0f;
    }

    windAngle_ += std::sin(timeOfDay * 6.28f) * 0.08f * dt;
    windDir_ = { std::cos(windAngle_), std::sin(windAngle_) };

    // Globale Temperatur (Tag/Nacht + Wetter)
    bool isNight = (timeOfDay > 0.72f || timeOfDay < 0.22f);
    float targetGlobalTemp = isNight ? 3.0f : 21.0f;
    if (weather_ == WeatherType::Rain)  targetGlobalTemp -= 4.0f;
    if (weather_ == WeatherType::Storm) targetGlobalTemp -= 6.0f;
    globalTemp_ = Lerp(globalTemp_, targetGlobalTemp, dt * 0.5f);

    // Bei Sturm gelegentliche Blitzeinschläge
    if (weather_ == WeatherType::Storm) {
        lightningCooldown_ -= dt;
        if (lightningCooldown_ <= 0.0f) {
            lightningCooldown_ = (float)GetRandomValue(10, 20);
            Vector2 strikePos;
            if (world.FindRandomTile([&](int x, int y) {
                    return Vector2Distance(World::TileCenter(x, y), world.CenterPosition()) > 95.0f;
                }, strikePos, 100)) {
                TriggerLightningStrike(world, resources, buildings, cavemen, strikePos, log);
            }
        }
    }

    // 2. Getakteter Kachel-Automat (alle 0.15s für hohe Performance auf 160x100)
    gridTickAccumulator_ += dt;
    if (gridTickAccumulator_ >= kEnvTickInterval) {
        float tickDt = gridTickAccumulator_;
        gridTickAccumulator_ = 0.0f;
        UpdateGridCellularAutomata(world, resources, buildings, cavemen, log, timeOfDay, tickDt);
    }

    // 3. Kontinuierliche Partikel & Physik-Drops (60 FPS)
    UpdateGroundDrops(dt);
    UpdateParticles(world, dt);
}

void EnvironmentalSystem::PropagateElectricity(const World& world) {
    // BFS Flood-Fill von Blitzeinschlägen über zusammenhängende Wasser-Kacheln
    std::queue<std::pair<int, int>> q;
    std::vector<int> distGrid((size_t)(width_ * height_), -1);
    constexpr int kMaxElectricReach = 12;

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            size_t idx = (size_t)(y * width_ + x);
            if (grid_[idx].electrifiedTimer > 0.0f && grid_[idx].electricIntensity >= 0.95f) {
                distGrid[idx] = 0;
                q.push({ x, y });
            }
        }
    }

    static const int kDx[4] = { 1, -1, 0, 0 };
    static const int kDy[4] = { 0, 0, 1, -1 };

    while (!q.empty()) {
        auto [x, y] = q.front();
        q.pop();
        int d = distGrid[(size_t)(y * width_ + x)];
        if (d >= kMaxElectricReach) continue;

        for (int k = 0; k < 4; ++k) {
            int nx = x + kDx[k], ny = y + kDy[k];
            if (!world.IsInBounds(nx, ny)) continue;
            size_t nIdx = (size_t)(ny * width_ + nx);
            if (distGrid[nIdx] != -1) continue;

            TileType tt = world.GetTile(nx, ny);
            if (World::GetProperty(tt).isLiquid || grid_[nIdx].wetness > 0.85f) {
                distGrid[nIdx] = d + 1;
                float falloff = 1.0f - (float)(d + 1) / (float)(kMaxElectricReach + 1);
                grid_[nIdx].electrifiedTimer = std::max(grid_[nIdx].electrifiedTimer, 0.45f);
                grid_[nIdx].electricIntensity = std::max(grid_[nIdx].electricIntensity, falloff);
                q.push({ nx, ny });
            }
        }
    }
}

void EnvironmentalSystem::UpdateGridCellularAutomata(World& world,
                                                     ResourceManager& resources,
                                                     BuildingManager& buildings,
                                                     std::vector<Caveman>& cavemen,
                                                     EventLog& log,
                                                     float timeOfDay,
                                                     float tickDt) {
    (void)timeOfDay;
    PropagateElectricity(world);
    nextGrid_ = grid_;

    const bool isRaining = (weather_ == WeatherType::Rain || weather_ == WeatherType::Storm);
    static const int kDx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
    static const int kDy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const size_t idx = (size_t)(y * width_ + x);
            const TileEnvState& cur = grid_[idx];
            TileEnvState& nxt = nextGrid_[idx];
            const TileType tileType = world.GetTile(x, y);
            const bool isLiquid = World::GetProperty(tileType).isLiquid;

            // --- A) Nässe & Regen ---
            if (isLiquid) {
                nxt.wetness = 1.0f;
            } else if (isRaining) {
                nxt.wetness = std::min(1.0f, cur.wetness + 0.12f * tickDt);
            } else {
                nxt.wetness = std::max(0.0f, cur.wetness - 0.04f * tickDt);
            }

            // --- B) Strom-Abklingen ---
            if (cur.electrifiedTimer > 0.0f) {
                nxt.electrifiedTimer = std::max(0.0f, cur.electrifiedTimer - tickDt);
                if (nxt.electrifiedTimer <= 0.0f) nxt.electricIntensity = 0.0f;
                else if (GetRandomValue(0, 100) < 12) {
                    Vector2 tc = World::TileCenter(x, y);
                    SpawnParticle(EnvParticleType::ElectricArc, tc,
                                  { (float)GetRandomValue(-18, 18), (float)GetRandomValue(-18, 18) },
                                  0.25f, 2.0f, { 150, 235, 255, 255 });
                }
            }

            // --- C) Lokale Temperatur & Eis/Frost ---
            float localTargetTemp = globalTemp_;
            if (tileType == TILE_MOUNTAIN || world.HasNeighbor(x, y, TILE_MOUNTAIN)) {
                localTargetTemp -= 9.5f; // Gebirgsregionen sind deutlich kälter
            }
            if (cur.isBurning) {
                localTargetTemp += 60.0f * cur.fireIntensity;
            }
            nxt.temperature = Lerp(cur.temperature, localTargetTemp, tickDt * 1.5f);

            if (nxt.temperature < -1.0f && !cur.isBurning) {
                if (tileType == TILE_SHALLOW_WATER) {
                    nxt.frozen = true;
                }
                nxt.frost = std::min(1.0f, cur.frost + 0.15f * tickDt);
            } else if (nxt.temperature > 2.0f || cur.isBurning) {
                if (cur.frozen) {
                    nxt.frozen = false;
                    nxt.wetness = 1.0f;
                }
                nxt.frost = std::max(0.0f, cur.frost - 0.25f * tickDt);
            }

            // --- D) Feuer-Logik, Wasser-Löschung (Dampf) & Wind-Ausbreitung ---
            if (cur.isBurning) {
                // 1. Wenn Kachel zu Wasser wurde -> sofort löschen + Dampf!
                if (isLiquid) {
                    nxt.isBurning = false;
                    nxt.fireIntensity = 0.0f;
                    nxt.fireTimer = 0.0f;
                    Vector2 tc = World::TileCenter(x, y);
                    SpawnParticle(EnvParticleType::Steam, tc, { windDir_.x * 10.0f, -18.0f }, 1.3f, 4.0f, Fade(WHITE, 0.8f));
                    continue;
                }

                // 2. Angrenzendes Wasser erzeugt Dampf und dämpft das Feuer am Ufer
                bool nearWater = world.HasNeighbor(x, y, TILE_SHALLOW_WATER) || world.HasNeighbor(x, y, TILE_DEEP_WATER);
                if (nearWater && GetRandomValue(0, 100) < 45) {
                    Vector2 tc = World::TileCenter(x, y);
                    SpawnParticle(EnvParticleType::Steam,
                                  { tc.x + (float)GetRandomValue(-5, 5), tc.y + (float)GetRandomValue(-5, 5) },
                                  { windDir_.x * 12.0f, -16.0f - (float)GetRandomValue(0, 8) },
                                  1.4f, 3.8f, Fade(RAYWHITE, 0.75f));
                }

                // 3. Regen reduziert Feuer-Intensität und erzeugt Dampf
                float extinguishRate = isRaining ? 0.32f : 0.0f;
                if (nearWater) extinguishRate += 0.18f;

                nxt.fireTimer = cur.fireTimer - tickDt * (1.0f + extinguishRate * 2.5f);
                nxt.fuel      = std::max(0.0f, cur.fuel - tickDt * 0.075f);
                nxt.fireIntensity = std::clamp(cur.fireIntensity - extinguishRate * tickDt, 0.2f, 1.0f);

                if (isRaining && GetRandomValue(0, 100) < 20) {
                    Vector2 tc = World::TileCenter(x, y);
                    SpawnParticle(EnvParticleType::Steam, tc, { windDir_.x * 10.0f, -15.0f }, 1.1f, 3.0f, Fade(LIGHTGRAY, 0.65f));
                }

                // Rauch- und Funkenpartikel über brennender Kachel
                if (GetRandomValue(0, 100) < 38) {
                    Vector2 tc = World::TileCenter(x, y);
                    SpawnParticle(EnvParticleType::Smoke,
                                  { tc.x + (float)GetRandomValue(-4, 4), tc.y - 4.0f },
                                  { windDir_.x * 16.0f * windStrength_, -14.0f - (float)GetRandomValue(0, 8) },
                                  1.6f, (float)GetRandomValue(3, 5), Fade(Color{ 55, 55, 60, 255 }, 0.7f));
                }
                if (GetRandomValue(0, 100) < 22) {
                    Vector2 tc = World::TileCenter(x, y);
                    SpawnParticle(EnvParticleType::FlameSpark,
                                  { tc.x + (float)GetRandomValue(-3, 3), tc.y - 3.0f },
                                  { windDir_.x * 28.0f * windStrength_, -22.0f },
                                  0.65f, 2.0f, GOLD);
                }

                // 4. Feuer-Ausbreitung auf Nachbarkacheln (beeinflusst durch Windrichtung!)
                for (int k = 0; k < 8; ++k) {
                    int nx = x + kDx[k], ny = y + kDy[k];
                    if (!IsFlammableTile(world, nx, ny)) continue;

                    TileEnvState& neighborNext = nextGrid_[(size_t)(ny * width_ + nx)];
                    if (neighborNext.isBurning || neighborNext.scorched) continue;

                    // Schmelze Eis/Frost auf Nachbarkacheln
                    neighborNext.frozen = false;
                    neighborNext.frost = 0.0f;

                    Vector2 stepDir = Vector2Normalize({ (float)kDx[k], (float)kDy[k] });
                    float windDot = stepDir.x * windDir_.x + stepDir.y * windDir_.y; // -1 .. +1
                    float windFactor = std::max(0.15f, 1.0f + windDot * windStrength_ * 1.8f);

                    TileType nType = world.GetTile(nx, ny);
                    float baseChance = (nType == TILE_FOREST) ? 0.14f : 0.055f;
                    float moisturePenalty = 1.0f - neighborNext.wetness * 0.75f;
                    float spreadProb = baseChance * cur.fireIntensity * windFactor * moisturePenalty;

                    if ((float)GetRandomValue(0, 1000) / 1000.0f < spreadProb) {
                        neighborNext.isBurning = true;
                        neighborNext.fireIntensity = (nType == TILE_FOREST) ? 0.95f : 0.70f;
                        neighborNext.fireTimer = (nType == TILE_FOREST) ? 13.0f : 7.0f;
                        neighborNext.fuel = (nType == TILE_FOREST) ? 1.0f : 0.5f;
                        neighborNext.wetness = 0.0f;

                        if (nType == TILE_FOREST && wildfireLogCooldown_ <= 0.0f) {
                            log.Add("Ein Lauffeuer hat sich auf den Wald ausgebreitet!", { 255, 100, 35, 255 });
                            wildfireLogCooldown_ = 9.0f;
                        }
                    }
                }

                // 5. Ausbrennen -> Verkohlter Boden / Asche
                if (nxt.fireTimer <= 0.0f || nxt.fuel <= 0.0f) {
                    nxt.isBurning = false;
                    nxt.fireIntensity = 0.0f;
                    nxt.fireTimer = 0.0f;
                    nxt.scorched = true;
                    nxt.scorchedRecovery = 55.0f;
                    if (tileType == TILE_FOREST) {
                        world.SetTile(x, y, TILE_GRASS);
                    }
                }
            } else if (cur.scorched) {
                // Verkohlter Boden erholt sich langsam wieder
                nxt.scorchedRecovery = cur.scorchedRecovery - tickDt;
                if (GetRandomValue(0, 100) < 4) {
                    Vector2 tc = World::TileCenter(x, y);
                    SpawnParticle(EnvParticleType::Smoke, tc, { windDir_.x * 6.0f, -8.0f }, 1.2f, 2.5f, Fade(GRAY, 0.4f));
                }
                if (nxt.scorchedRecovery <= 0.0f) {
                    nxt.scorched = false;
                    nxt.fuel = 0.6f;
                }
            }
        }
    }

    grid_.swap(nextGrid_);

    // --- E) Einwirkung auf Ressourcen (Bäume verkohlen, Tiere fliehen) ---
    for (ResourceNode& n : resources.NodesMutable()) {
        int tx = World::ToTile(n.position.x);
        int ty = World::ToTile(n.position.y);
        const TileEnvState& st = GetState(tx, ty);

        if (st.isBurning) {
            if ((n.type == ResourceType::Tree || n.type == ResourceType::Bush) && !n.charred) {
                n.charred = true;
                n.amount = 0;
                n.generation++;
                if (n.type == ResourceType::Tree && GetRandomValue(0, 100) < 45) {
                    SpawnGroundDrop(DropItemType::CharredWood, n.position, 1);
                }
            } else if (n.type == ResourceType::Animal && n.alive) {
                n.hp -= 12.0f * tickDt;
                if (n.hp <= 0.0f) {
                    n.alive = false;
                    n.respawnTimer = cfg::kAnimalRespawnTime;
                    n.generation++;
                }
            }
        } else if (st.electrifiedTimer > 0.0f && n.type == ResourceType::Animal && n.alive) {
            n.hp -= 18.0f * tickDt;
            if (n.hp <= 0.0f) {
                n.alive = false;
                n.respawnTimer = cfg::kAnimalRespawnTime;
                n.generation++;
            }
        }
    }

    // --- F) Einwirkung auf Gebäude (Feuer-Kollateralschaden) ---
    for (Building& b : buildings.All()) {
        int tx = World::ToTile(b.position.x);
        int ty = World::ToTile(b.position.y);
        bool fireNear = false;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (GetState(tx + dx, ty + dy).isBurning) fireNear = true;
            }
        }
        if (fireNear) {
            b.hp = std::max(10.0f, b.hp - 6.5f * tickDt);
            if (collateralLogCooldown_ <= 0.0f) {
                log.Add(TextFormat("Kollateralschaden: %s durch Flammen beschädigt!", BuildingTypeName(b.type, b.tier)),
                        { 255, 130, 50, 255 });
                collateralLogCooldown_ = 8.0f;
            }
        }
    }

    // --- G) Einwirkung auf Figuren (Feuer- & Stromschaden) ---
    for (Caveman& c : cavemen) {
        if (c.IsDead()) continue;
        int tx = World::ToTile(c.Position().x);
        int ty = World::ToTile(c.Position().y);
        const TileEnvState& st = GetState(tx, ty);
        if (st.isBurning || st.electrifiedTimer > 0.0f) {
            Vector2 p = c.Position();
            SpawnParticle(st.isBurning ? EnvParticleType::FlameSpark : EnvParticleType::ElectricArc,
                          p, { (float)GetRandomValue(-20, 20), -25.0f }, 0.4f, 2.5f,
                          st.isBurning ? ORANGE : SKYBLUE);
        }
    }
}

// ============================================================================
//  Physische Boden-Drops & Antigravitations-Physik
// ============================================================================
void EnvironmentalSystem::SpawnGroundDrop(DropItemType type, Vector2 pos, int amount) {
    if (drops_.size() >= 140) return;
    GroundDrop d;
    d.type = type;
    d.position = pos;
    d.velocity = { (float)GetRandomValue(-22, 22), (float)GetRandomValue(-22, 22) };
    d.amount = std::max(1, amount);
    d.lifeTimer = 120.0f;
    d.spiralPhase = (float)GetRandomValue(0, 628) / 100.0f;
    drops_.push_back(d);
}

int EnvironmentalSystem::FindNearestGroundDrop(Vector2 from, float maxRadiusPx) const {
    int best = -1;
    float bestDist = maxRadiusPx;
    for (size_t i = 0; i < drops_.size(); ++i) {
        float d = Vector2Distance(from, drops_[i].position);
        if (d < bestDist) {
            bestDist = d;
            best = (int)i;
        }
    }
    return best;
}

bool EnvironmentalSystem::CollectGroundDrop(int index, DropItemType& outType, int& outAmount) {
    if (index < 0 || index >= (int)drops_.size()) return false;
    outType = drops_[(size_t)index].type;
    outAmount = drops_[(size_t)index].amount;
    drops_.erase(drops_.begin() + index);
    return true;
}

void EnvironmentalSystem::UpdateGroundDrops(float dt) {
    // Antigravitations-Zonen aktualisieren
    for (size_t i = 0; i < antigravZones_.size(); ) {
        AntigravityZone& z = antigravZones_[i];
        z.elapsed += dt;
        if (!z.permanent && z.elapsed >= z.duration) {
            antigravZones_.erase(antigravZones_.begin() + (ptrdiff_t)i);
            continue;
        }
        // Schwebende bläulich-violette Antigravitations-Partikel spawnen
        if (GetRandomValue(0, 100) < 35) {
            float ang = (float)GetRandomValue(0, 360) * DEG2RAD;
            float r = (float)GetRandomValue(2, (int)z.radius);
            Vector2 p = { z.position.x + std::cos(ang) * r, z.position.y + std::sin(ang) * r };
            SpawnParticle(EnvParticleType::AntigravOrb, p,
                          { -std::sin(ang) * 10.0f, -14.0f - (float)GetRandomValue(0, 10) },
                          1.6f, (float)GetRandomValue(2, 4), { 130, 180, 255, 230 });
        }
        ++i;
    }

    // Boden-Drops bewegen & Schwerelosigkeit berechnen
    for (size_t i = 0; i < drops_.size(); ) {
        GroundDrop& d = drops_[i];
        d.lifeTimer -= dt;
        if (d.lifeTimer <= 0.0f) {
            drops_.erase(drops_.begin() + (ptrdiff_t)i);
            continue;
        }

        d.position = Vector2Add(d.position, Vector2Scale(d.velocity, dt));
        d.velocity = Vector2Scale(d.velocity, std::max(0.0f, 1.0f - 4.0f * dt));

        // Prüfen, ob sich der Drop in einer Antigravitations-Zone befindet
        bool inZone = false;
        Vector2 zoneCenter = d.position;
        for (const auto& z : antigravZones_) {
            if (Vector2Distance(d.position, z.position) <= z.radius) {
                inZone = true;
                zoneCenter = z.position;
                break;
            }
        }

        d.inZeroG = inZone;
        if (inZone) {
            d.spiralPhase += dt * 2.6f;
            float targetH = 8.0f + std::sin(d.spiralPhase) * 4.5f;
            d.floatHeight = Lerp(d.floatHeight, targetH, dt * 3.0f);
            // Leichte Spiraldrift um das Anomalie-Zentrum
            Vector2 tangent = { -(d.position.y - zoneCenter.y), (d.position.x - zoneCenter.x) };
            if (Vector2Length(tangent) > 0.1f) {
                tangent = Vector2Normalize(tangent);
                d.position = Vector2Add(d.position, Vector2Scale(tangent, 5.5f * dt));
            }
        } else {
            d.floatHeight = std::max(0.0f, d.floatHeight - 18.0f * dt);
        }
        ++i;
    }
}

void EnvironmentalSystem::SpawnParticle(EnvParticleType type, Vector2 pos, Vector2 vel, float life, float size, Color col) {
    if ((int)particles_.size() >= kMaxParticles) return;
    EnvParticle p;
    p.type = type;
    p.position = pos;
    p.velocity = vel;
    p.life = life;
    p.maxLife = life;
    p.size = size;
    p.color = col;
    particles_.push_back(p);
}

void EnvironmentalSystem::UpdateParticles(const World& world, float dt) {
    // Windböen-Wirbel & fliegendes Laub spawnen
    if (GetRandomValue(0, 100) < (int)(windStrength_ * 45.0f)) {
        Vector2 p = { (float)GetRandomValue(0, world.Width() * TILE_SIZE),
                      (float)GetRandomValue(0, world.Height() * TILE_SIZE) };
        SpawnParticle(EnvParticleType::WindSwirl, p,
                      { windDir_.x * (55.0f + 45.0f * windStrength_), windDir_.y * 35.0f },
                      0.9f, 2.0f, Fade(WHITE, 0.45f));
    }

    for (size_t i = 0; i < particles_.size(); ) {
        EnvParticle& p = particles_[i];
        p.life -= dt;
        if (p.life <= 0.0f) {
            particles_[i] = particles_.back();
            particles_.pop_back();
            continue;
        }
        p.position = Vector2Add(p.position, Vector2Scale(p.velocity, dt));
        if (p.type == EnvParticleType::Smoke || p.type == EnvParticleType::Steam) {
            p.size += 1.4f * dt;
        }
        ++i;
    }
}

// ============================================================================
//  Rendering: Kachel-Zustände, Feuer, Blitze & Wetter
// ============================================================================
void EnvironmentalSystem::DrawTileOverlays(const World& world, const Camera2D& camera, float simTime) const {
    const Vector2 topLeft = GetScreenToWorld2D({ 0.0f, 0.0f }, camera);
    const Vector2 bottomRight = GetScreenToWorld2D({ (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT }, camera);

    const int x0 = std::max(0, World::ToTile(topLeft.x) - 1);
    const int y0 = std::max(0, World::ToTile(topLeft.y) - 1);
    const int x1 = std::min(width_ - 1, World::ToTile(bottomRight.x) + 1);
    const int y1 = std::min(height_ - 1, World::ToTile(bottomRight.y) + 1);

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const TileEnvState& st = GetState(x, y);
            const int px = x * TILE_SIZE;
            const int py = y * TILE_SIZE;

            // 1. Ausgebrannter Ascheboden
            if (st.scorched) {
                DrawRectangle(px, py, TILE_SIZE, TILE_SIZE, Color{ 45, 42, 40, 210 });
            }

            // 2. Gefrorenes Wasser (Eisschicht) & Bodenfrost
            if (st.frozen) {
                DrawRectangle(px, py, TILE_SIZE, TILE_SIZE, Color{ 165, 220, 245, 215 });
                DrawLine(px + 2, py + 3, px + TILE_SIZE - 3, py + TILE_SIZE - 2, Fade(WHITE, 0.6f));
            } else if (st.frost > 0.25f) {
                DrawRectangle(px, py, TILE_SIZE, TILE_SIZE, Fade(Color{ 200, 235, 255, 255 }, st.frost * 0.45f));
            }

            // 3. Nässe-Glanz bei Regen
            if (st.wetness > 0.2f && !World::GetProperty(world.GetTile(x, y)).isLiquid && !st.isBurning) {
                DrawRectangle(px, py, TILE_SIZE, TILE_SIZE, Fade(Color{ 15, 25, 45, 255 }, st.wetness * 0.22f));
            }

            // 4. Elektrifiziertes Wasser (bei Blitzeinschlag im Sturm)
            if (st.electrifiedTimer > 0.0f) {
                float pulse = 0.5f + 0.5f * std::sin(simTime * 18.0f + (float)(x * 3 + y * 5));
                DrawRectangle(px, py, TILE_SIZE, TILE_SIZE,
                              Fade(Color{ 90, 200, 255, 255 }, 0.32f * st.electricIntensity * pulse));

                if (((x + y + (int)(simTime * 12.0f)) % 3) == 0) {
                    Vector2 c = World::TileCenter(x, y);
                    Vector2 mid = { c.x + std::sin(simTime * 25.0f + (float)x) * 4.0f,
                                    c.y + std::cos(simTime * 23.0f + (float)y) * 4.0f };
                    DrawLineEx(c, mid, 1.4f, Color{ 140, 230, 255, 240 });
                    DrawLineEx(mid, { c.x + (float)TILE_SIZE * 0.6f, c.y + 3.0f }, 1.0f, WHITE);
                }
            }

            // 5. Aktives Feuer auf der Kachel (mehrschichtige Pixel-Flammen)
            if (st.isBurning) {
                Vector2 c = World::TileCenter(x, y);
                float flick1 = std::sin(simTime * 16.0f + (float)(x * 7 + y * 13)) * 1.8f;
                float flick2 = std::cos(simTime * 21.0f + (float)(x * 11 - y * 5)) * 1.5f;
                float r = (4.5f + 3.0f * st.fireIntensity);

                DrawRectangle(px + 1, py + 1, TILE_SIZE - 2, TILE_SIZE - 2, Fade(Color{ 180, 45, 15, 255 }, 0.55f));
                DrawCircleV({ c.x + flick1 * 0.4f, c.y - 1.5f }, r, Color{ 235, 65, 20, 240 });
                DrawCircleV({ c.x + windDir_.x * 2.0f + flick2 * 0.5f, c.y - 3.5f }, r * 0.72f, Color{ 255, 155, 30, 250 });
                DrawCircleV({ c.x + windDir_.x * 2.5f, c.y - 4.5f }, r * 0.42f, Color{ 255, 240, 110, 255 });
            }
        }
    }
}

void EnvironmentalSystem::DrawWorldEffects(const World& world, const Camera2D& camera, float simTime) const {
    (void)world;
    (void)camera;

    // 1. Antigravitations-Anomalien (bläuliche Energieringe am Boden)
    for (const auto& z : antigravZones_) {
        float pulse = 0.6f + 0.4f * std::sin(simTime * 4.5f);
        DrawCircleGradient(z.position, z.radius,
                           Fade(Color{ 110, 160, 255, 255 }, 0.24f * pulse),
                           Fade(Color{ 80, 100, 220, 255 }, 0.0f));
        DrawCircleLines((int)z.position.x, (int)z.position.y, z.radius * (0.75f + 0.15f * pulse),
                        Fade(Color{ 150, 210, 255, 255 }, 0.65f));
    }

    // 2. Physische Ressourcen-Drops & schwebende Trümmer
    for (const auto& d : drops_) {
        DrawEllipse((int)d.position.x, (int)d.position.y + 1, 3, 1, Fade(BLACK, 0.35f));

        Vector2 drawPos = { d.position.x, d.position.y - d.floatHeight };
        if (d.inZeroG) {
            DrawCircleV(drawPos, 4.5f, Fade(Color{ 120, 190, 255, 255 }, 0.45f));
        }

        switch (d.type) {
            case DropItemType::WoodLog:
                DrawRectangleRec({ drawPos.x - 3.0f, drawPos.y - 1.5f, 6.0f, 3.0f }, { 135, 85, 42, 255 });
                break;
            case DropItemType::CharredWood:
                DrawRectangleRec({ drawPos.x - 3.0f, drawPos.y - 1.5f, 6.0f, 3.0f }, { 45, 38, 35, 255 });
                break;
            case DropItemType::StoneChunk:
                DrawCircleV(drawPos, 2.6f, { 145, 145, 152, 255 });
                break;
            case DropItemType::BerryFood:
                DrawCircleV(drawPos, 2.2f, { 210, 45, 60, 255 });
                break;
        }
    }

    // 3. Umwelt-Partikel (Rauch, Dampf, Funken, Windstreifen, Antigrav-Orbs)
    for (const auto& p : particles_) {
        float alpha = std::clamp(p.life / p.maxLife, 0.0f, 1.0f);
        Color c = Fade(p.color, alpha * ((float)p.color.a / 255.0f));

        if (p.type == EnvParticleType::WindSwirl) {
            Vector2 tail = Vector2Subtract(p.position, Vector2Scale(windDir_, 12.0f));
            DrawLineEx(tail, p.position, 1.2f, c);
        } else if (p.type == EnvParticleType::ElectricArc) {
            DrawLineEx(p.position, { p.position.x + (float)GetRandomValue(-5, 5), p.position.y + (float)GetRandomValue(-5, 5) }, 1.5f, c);
        } else {
            DrawCircleV(p.position, p.size, c);
        }
    }
}

void EnvironmentalSystem::DrawWeatherOverlay(int screenW, int screenH, float simTime) const {
    // 1. Diagonal fallende Regenlinien bei Regen oder Sturm
    if (weather_ == WeatherType::Rain || weather_ == WeatherType::Storm) {
        int dropCount = (weather_ == WeatherType::Storm) ? 140 : 85;
        Color rainCol = (weather_ == WeatherType::Storm)
                        ? Color{ 170, 210, 255, 135 }
                        : Color{ 150, 195, 245, 105 };

        for (int i = 0; i < dropCount; ++i) {
            float seedX = (float)((i * 97 + (int)(simTime * 420.0f)) % screenW);
            float seedY = (float)((i * 53 + (int)(simTime * 680.0f)) % screenH);
            Vector2 start = { seedX, seedY };
            Vector2 end   = { seedX + windDir_.x * 14.0f - 4.0f, seedY + 16.0f };
            DrawLineEx(start, end, 1.2f, rainCol);
        }
    }

    // 2. Dichter atmosphärischer Nebel-Schleier
    if (weather_ == WeatherType::Fog) {
        DrawRectangle(0, 48, screenW, screenH - 48, Fade(Color{ 205, 215, 225, 255 }, 0.34f));
    }
}
