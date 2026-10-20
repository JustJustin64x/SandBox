#include "world/world.h"
#include "world/environmental_system.h"
#include "entities/tribe.h"
#include "entities/caveman.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <random>
#include <utility>

namespace {

// Kachel-Eigenschaften (Reihenfolge entspricht TileType).
const TileProperty kTileProperties[TILE_TYPE_COUNT] = {
    { "Tiefwasser",  { 22, 60, 130, 255 },   false, true  }, // Tiefwasser unpassierbar (verhindert Ertrinken)
    { "Flachwasser", { 45, 120, 195, 255 },  true,  true  },
    { "Sand",        { 215, 195, 120, 255 }, true,  false },
    { "Gras",        { 80, 160, 60, 255 },   true,  false },
    { "Wald",        { 45, 110, 40, 255 },   true,  false },
    { "Gebirge",     { 130, 125, 120, 255 }, false, false },
    { "Schneegipfel",{ 240, 245, 255, 255 }, false, false },
};

// --- Multi-Octave Noise für prozedurale Weltgenerierung -----------------------
float Hash2D(int x, int y, unsigned int seed) {
    unsigned int h = (unsigned int)x * 374761393u + (unsigned int)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xFFFFFFu) / (float)0xFFFFFFu;
}

float SmoothNoise(float x, float y, unsigned int seed) {
    const int xi = (int)std::floor(x);
    const int yi = (int)std::floor(y);
    float tx = x - (float)xi;
    float ty = y - (float)yi;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    const float a = Hash2D(xi, yi, seed);
    const float b = Hash2D(xi + 1, yi, seed);
    const float c = Hash2D(xi, yi + 1, seed);
    const float d = Hash2D(xi + 1, yi + 1, seed);
    return Lerp(Lerp(a, b, tx), Lerp(c, d, tx), ty);
}

float FractalNoise(float x, float y, unsigned int seed, int octaves = 4) {
    float sum = 0.0f, amplitude = 1.0f, norm = 0.0f, frequency = 1.0f;
    for (int octave = 0; octave < octaves; ++octave) {
        sum += SmoothNoise(x * frequency, y * frequency, seed + (unsigned int)octave * 101u) * amplitude;
        norm += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return sum / norm;
}

constexpr int   kMaxPathExpansions = 32000;
constexpr int   kSmoothLookahead   = 12;
constexpr float kSqrt2             = 1.41421356f;

} // namespace

// ============================================================================
//  Konstruktion & Generierung (Phase 1)
// ============================================================================
World::World(int width, int height, unsigned int seed)
    : width_(std::max(8, width)), height_(std::max(8, height)),
      currentSeed_(seed),
      tiles_((size_t)(std::max(8, width) * std::max(8, height)), TileData{}),
      footTraffic_((size_t)(std::max(8, width) * std::max(8, height)), 0.0f),
      pavedRoad_((size_t)(std::max(8, width) * std::max(8, height)), 0) {
    Generate(seed);
}

void World::RegenerateWorld(unsigned int seed) {
    currentSeed_ = seed;
    Generate(seed);
}

void World::Generate(unsigned int seed) {
    std::fill(footTraffic_.begin(), footTraffic_.end(), 0.0f);
    std::fill(pavedRoad_.begin(), pavedRoad_.end(), 0);

    const float cx = (float)width_ * 0.5f;
    const float cy = (float)height_ * 0.5f;

    // 1.1 Landmassen, Inseln & 3D-Höhenstufen (Simplex-Noise + Domain-Warping)
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            size_t idx = (size_t)(y * width_ + x);
            TileData& td = tiles_[idx];

            // Domain Warping für organische, weitläufige Küstenlinien (skaliert für 240x160)
            float qx = FractalNoise((float)x * 0.016f, (float)y * 0.016f, seed + 101u, 3);
            float qy = FractalNoise((float)x * 0.016f + 5.2f, (float)y * 0.016f + 1.3f, seed + 202u, 3);

            float elevNoise = FractalNoise((float)x * 0.018f + 3.2f * qx,
                                           (float)y * 0.018f + 3.2f * qy,
                                           seed, 5);

            // Sanfter Randabfall zum Kartenrand hin (offener Ozean)
            float edgeDistX = std::min(x, width_ - 1 - x) / ((float)width_ * 0.09f);
            float edgeDistY = std::min(y, height_ - 1 - y) / ((float)height_ * 0.09f);
            float edgeFalloff = std::clamp(std::min(edgeDistX, edgeDistY), 0.0f, 1.0f);

            // Elliptische Distanz zur Mitte für große Kontinentalmasse + vorgelagerte Inseln
            float dx = ((float)x - cx) / cx;
            float dy = ((float)y - cy) / cy;
            float centerDist = std::sqrt(dx * dx + dy * dy);
            float continentMask = 1.0f - std::clamp(centerDist * 0.82f, 0.0f, 1.0f);

            // Kombinierte stetige Höhe (Elevation 0.0f .. 1.0f)
            float elevation = elevNoise * 0.65f + continentMask * 0.35f;
            elevation *= edgeFalloff;
            elevation = std::clamp(elevation, 0.0f, 1.0f);

            // Feuchtigkeitskarte (Moisture 0.0f .. 1.0f)
            float moisture = FractalNoise((float)x * 0.021f, (float)y * 0.021f, seed + 7919u, 4);

            td.elevation = elevation;
            td.moisture = moisture;
            td.flowDirection = { 0.0f, 0.0f };
            td.isRiver = false;
            td.isRamp = false;
            td.isCliff = false;
            td.hasCave = false;

            // Diskrete Höhenstufen (heightLevel 0..6)
            if (elevation < 0.36f) {
                td.heightLevel = 0;
                td.type = TILE_DEEP_WATER;
            } else if (elevation < 0.44f) {
                td.heightLevel = 0;
                td.type = TILE_SHALLOW_WATER;
            } else if (elevation < 0.48f) {
                td.heightLevel = 0;
                td.type = TILE_SAND;
            } else if (elevation < 0.62f) {
                td.heightLevel = 1; // Flachland
                td.type = (moisture > 0.52f) ? TILE_FOREST : TILE_GRASS;
            } else if (elevation < 0.74f) {
                td.heightLevel = 2; // Hügel & Hochland
                if (moisture > 0.60f) td.type = TILE_FOREST;
                else if (moisture < 0.32f) td.type = TILE_MOUNTAIN;
                else td.type = TILE_GRASS;
            } else if (elevation < 0.83f) {
                td.heightLevel = 3; // Untere Gebirgsstufe (Fels)
                td.type = TILE_MOUNTAIN;
            } else if (elevation < 0.90f) {
                td.heightLevel = 4; // Mittlere Gebirgsstufe (Fels)
                td.type = TILE_MOUNTAIN;
            } else if (elevation < 0.95f) {
                td.heightLevel = 5; // Hohe Felsgipfel (Fels)
                td.type = TILE_MOUNTAIN;
            } else {
                td.heightLevel = 5; // Schneegipfel (Stufe 5)
                td.type = TILE_SNOW;
            }
        }
    }

    // 1.2 Hydrologie: Strikte Trennung von Salzwasser und Süßwasser-Seen
    ClassifyWaterAndHydrology();

    // 1.3 Süßwasser-Flüsse von Bergen ins Meer/Seen modellieren (orthogonal 2 Kacheln breit)
    GenerateRivers(seed);

    // 1.3b Phase 4: Sand nur an echten Meeresküsten (Salzwasser) – Binnenland & Flussauen begrünen
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            size_t idx = (size_t)(y * width_ + x);
            TileData& td = tiles_[idx];
            if (td.type == TILE_SAND) {
                bool bordersSaltwater = false;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        int nx = x + dx, ny = y + dy;
                        if (IsInBounds(nx, ny) && tiles_[(size_t)(ny * width_ + nx)].waterType == WaterType::Saltwater) {
                            bordersSaltwater = true;
                            break;
                        }
                    }
                    if (bordersSaltwater) break;
                }
                if (!bordersSaltwater) {
                    td.type = (td.moisture > 0.45f) ? TILE_FOREST : TILE_GRASS;
                    if (td.heightLevel == 0) td.heightLevel = 1;
                }
            }
        }
    }

    // 1.4 Ufer-Distanz-Berechnung (distanceToShore per BFS)
    CalculateDistanceToShore();

    // 1.5 Klippen und begehbare Rampen/Gebirgspässe generieren
    BuildRampsAndCliffs();

    // 1.6 Höhleneingänge an geeigneten Klippenwänden platzieren
    PlaceCaves(seed);

    // 1.7 Region-IDs für O(1)-Insel-Erreichbarkeitsprüfung
    ComputeRegionIds();
}

void World::ClassifyWaterAndHydrology() {
    std::vector<bool> visited((size_t)(width_ * height_), false);
    std::queue<std::pair<int, int>> q;

    // Vom gesamten Kartenrand aus über alle zusammenhängenden Wasserkacheln fluten
    for (int x = 0; x < width_; ++x) {
        int idxTop = 0 * width_ + x;
        if (tiles_[idxTop].type == TILE_DEEP_WATER || tiles_[idxTop].type == TILE_SHALLOW_WATER) {
            visited[idxTop] = true;
            tiles_[idxTop].waterType = WaterType::Saltwater;
            q.push({ x, 0 });
        }
        int idxBottom = (height_ - 1) * width_ + x;
        if (tiles_[idxBottom].type == TILE_DEEP_WATER || tiles_[idxBottom].type == TILE_SHALLOW_WATER) {
            visited[idxBottom] = true;
            tiles_[idxBottom].waterType = WaterType::Saltwater;
            q.push({ x, height_ - 1 });
        }
    }

    for (int y = 0; y < height_; ++y) {
        int idxLeft = y * width_ + 0;
        if (!visited[idxLeft] && (tiles_[idxLeft].type == TILE_DEEP_WATER || tiles_[idxLeft].type == TILE_SHALLOW_WATER)) {
            visited[idxLeft] = true;
            tiles_[idxLeft].waterType = WaterType::Saltwater;
            q.push({ 0, y });
        }
        int idxRight = y * width_ + (width_ - 1);
        if (!visited[idxRight] && (tiles_[idxRight].type == TILE_DEEP_WATER || tiles_[idxRight].type == TILE_SHALLOW_WATER)) {
            visited[idxRight] = true;
            tiles_[idxRight].waterType = WaterType::Saltwater;
            q.push({ width_ - 1, y });
        }
    }

    static const int kDx[4] = { 1, -1, 0, 0 };
    static const int kDy[4] = { 0, 0, 1, -1 };

    while (!q.empty()) {
        auto [cx, cy] = q.front();
        q.pop();

        for (int i = 0; i < 4; ++i) {
            int nx = cx + kDx[i], ny = cy + kDy[i];
            if (!IsInBounds(nx, ny)) continue;
            size_t nidx = (size_t)(ny * width_ + nx);
            if (visited[nidx]) continue;

            if (tiles_[nidx].type == TILE_DEEP_WATER || tiles_[nidx].type == TILE_SHALLOW_WATER) {
                visited[nidx] = true;
                tiles_[nidx].waterType = WaterType::Saltwater;
                q.push({ nx, ny });
            }
        }
    }

    // Alle Binnenwasserkacheln ohne direkte Verbindung zum offenen Ozean sind Süßwasser-Seen
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            size_t idx = (size_t)(y * width_ + x);
            TileData& td = tiles_[idx];
            if (td.type == TILE_DEEP_WATER || td.type == TILE_SHALLOW_WATER) {
                if (td.waterType != WaterType::Saltwater) {
                    td.waterType = WaterType::Freshwater;
                    // Süßwasser-Seen von Gras/Wald umgeben statt Sandstrand
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            int nx = x + dx, ny = y + dy;
                            if (IsInBounds(nx, ny)) {
                                TileData& ntd = tiles_[(size_t)(ny * width_ + nx)];
                                if (ntd.type == TILE_SAND && ntd.heightLevel == 0) {
                                    ntd.type = (ntd.moisture > 0.50f) ? TILE_FOREST : TILE_GRASS;
                                    ntd.heightLevel = 1;
                                }
                            }
                        }
                    }
                }
            } else {
                td.waterType = WaterType::None;
            }
        }
    }
}

void World::GenerateRivers(unsigned int seed) {
    std::mt19937 rng(seed + 98765u);

    // Quellkacheln in Gebirgen (heightLevel >= 3)
    std::vector<std::pair<int, int>> springs;
    for (int y = 5; y < height_ - 5; ++y) {
        for (int x = 5; x < width_ - 5; ++x) {
            const TileData& td = tiles_[(size_t)(y * width_ + x)];
            if (td.heightLevel >= 3 && td.moisture > 0.40f) {
                springs.push_back({ x, y });
            }
        }
    }

    if (springs.empty()) return;
    std::shuffle(springs.begin(), springs.end(), rng);

    // 4 bis 8 Flüsse generieren
    int numRivers = std::clamp((int)springs.size(), 4, 8);
    std::vector<std::pair<int, int>> activeSprings;
    for (const auto& sp : springs) {
        bool farEnough = true;
        for (const auto& other : activeSprings) {
            int ddx = sp.first - other.first;
            int ddy = sp.second - other.second;
            if (ddx * ddx + ddy * ddy < 18 * 18) {
                farEnough = false;
                break;
            }
        }
        if (farEnough) {
            activeSprings.push_back(sp);
            if ((int)activeSprings.size() >= numRivers) break;
        }
    }

    // Phase 4: Strikte 4-orthogonale Richtungen für ruhigen, natürlichen Flusslauf (kein diagonales Zickzack)
    static const int kDx4[4] = { 1, -1, 0, 0 };
    static const int kDy4[4] = { 0, 0, 1, -1 };

    auto carveRiverTile = [this](int x, int y, Vector2 flowDir) {
        if (!IsInBounds(x, y)) return;
        TileData& td = tiles_[(size_t)(y * width_ + x)];
        if (td.waterType == WaterType::Saltwater) return;
        td.type = TILE_SHALLOW_WATER;
        td.waterType = WaterType::Freshwater;
        td.isRiver = true;
        td.heightLevel = 0;
        td.isCliff = false;
        td.flowDirection = flowDir;

        // Uferkacheln um den Fluss von Wüstensand befreien (saftige Auen/Gras)
        static const int kBankDx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
        static const int kBankDy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
        for (int b = 0; b < 8; ++b) {
            int bx = x + kBankDx[b], by = y + kBankDy[b];
            if (IsInBounds(bx, by)) {
                TileData& btd = tiles_[(size_t)(by * width_ + bx)];
                if (btd.waterType == WaterType::None && btd.type == TILE_SAND) {
                    btd.type = (btd.moisture > 0.45f) ? TILE_FOREST : TILE_GRASS;
                    if (btd.heightLevel == 0) btd.heightLevel = 1;
                }
            }
        }
    };

    for (const auto& spring : activeSprings) {
        int curX = spring.first;
        int curY = spring.second;
        int maxSteps = 240;

        while (maxSteps-- > 0) {
            size_t curIdx = (size_t)(curY * width_ + curX);
            TileData& curTd = tiles_[curIdx];

            // Wenn wir Ozean oder einen See erreichen, mündet der Fluss
            if (curTd.waterType == WaterType::Saltwater ||
                (curTd.waterType == WaterType::Freshwater && !curTd.isRiver && maxSteps < 230)) {
                break;
            }

            // Nächste Kachel per orthogonalem Downhill-Gradient suchen
            int bestX = -1, bestY = -1;
            float lowestElev = curTd.elevation;

            for (int i = 0; i < 4; ++i) {
                int nx = curX + kDx4[i];
                int ny = curY + kDy4[i];
                if (!IsInBounds(nx, ny)) continue;

                float noiseOffset = (float)(rng() % 100) / 100.0f * 0.015f;
                float candidateElev = tiles_[(size_t)(ny * width_ + nx)].elevation + noiseOffset;
                if (candidateElev < lowestElev) {
                    lowestElev = candidateElev;
                    bestX = nx;
                    bestY = ny;
                }
            }

            if (bestX == -1 || (bestX == curX && bestY == curY)) {
                // Lokale Senke: Fluss mündet / endet
                break;
            }

            Vector2 flowDir = { (float)(bestX - curX), (float)(bestY - curY) };
            float len = Vector2Length(flowDir);
            if (len > 0.001f) flowDir = Vector2Scale(flowDir, 1.0f / len);

            // 2 Kacheln breites Flussbett (Hauptkachel + orthogonale Flanke)
            carveRiverTile(curX, curY, flowDir);
            int flankX = curX - (bestY - curY);
            int flankY = curY + (bestX - curX);
            carveRiverTile(flankX, flankY, flowDir);

            curX = bestX;
            curY = bestY;
        }
    }
}

void World::CalculateDistanceToShore() {
    std::queue<std::pair<int, int>> q;

    // 1. Alle Kacheln initialisieren: Land = 0, Wasser = 255
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            size_t idx = (size_t)(y * width_ + x);
            TileData& td = tiles_[idx];
            if (td.type != TILE_DEEP_WATER && td.type != TILE_SHALLOW_WATER) {
                td.distanceToShore = 0; // Land
            } else {
                td.distanceToShore = 255;
            }
        }
    }

    static const int kDx4[4] = { 1, -1, 0, 0 };
    static const int kDy4[4] = { 0, 0, 1, -1 };

    // 2. Alle Wasserkacheln, die direkt an Land grenzen, auf Distanz 1 setzen
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            size_t idx = (size_t)(y * width_ + x);
            if (tiles_[idx].distanceToShore != 255) continue;

            bool adjToLand = false;
            for (int i = 0; i < 4; ++i) {
                int nx = x + kDx4[i], ny = y + kDy4[i];
                if (IsInBounds(nx, ny) && tiles_[(size_t)(ny * width_ + nx)].distanceToShore == 0) {
                    adjToLand = true;
                    break;
                }
            }

            if (adjToLand) {
                tiles_[idx].distanceToShore = 1;
                q.push({ x, y });
            }
        }
    }

    // 3. Multi-Source BFS durch alle Wasserkacheln
    while (!q.empty()) {
        auto [cx, cy] = q.front();
        q.pop();
        uint8_t curDist = tiles_[(size_t)(cy * width_ + cx)].distanceToShore;

        for (int i = 0; i < 4; ++i) {
            int nx = cx + kDx4[i], ny = cy + kDy4[i];
            if (!IsInBounds(nx, ny)) continue;
            size_t nidx = (size_t)(ny * width_ + nx);
            TileData& ntd = tiles_[nidx];

            if (ntd.distanceToShore == 255 && curDist < 254) {
                ntd.distanceToShore = curDist + 1;
                q.push({ nx, ny });
            }
        }
    }
}

void World::BuildRampsAndCliffs() {
    static const int kDx4[4] = { 1, -1, 0, 0 };
    static const int kDy4[4] = { 0, 0, 1, -1 };

    // 1. Klippen erkennen: Höhenunterschied >= 1 zu Nachbarn
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            size_t idx = (size_t)(y * width_ + x);
            TileData& td = tiles_[idx];
            td.isCliff = false;
            td.isRamp = false;

            for (int i = 0; i < 4; ++i) {
                int nx = x + kDx4[i], ny = y + kDy4[i];
                if (IsInBounds(nx, ny)) {
                    int diff = (int)td.heightLevel - (int)tiles_[(size_t)(ny * width_ + nx)].heightLevel;
                    if (std::abs(diff) >= 1) {
                        td.isCliff = true;
                        break;
                    }
                }
            }
        }
    }

    // 2. Natürliche Rampen / Bergaufgänge an Hügeln und Gebirgen schneiden
    for (int y = 1; y < height_ - 1; ++y) {
        for (int x = 1; x < width_ - 1; ++x) {
            size_t idx = (size_t)(y * width_ + x);
            TileData& td = tiles_[idx];
            if (!td.isCliff || td.heightLevel == 0) continue;

            // Regelmäßige Rampenverteilung (alle ~6 bis 8 Kacheln entlang von Klippen)
            if ((x * 7 + y * 13) % 7 == 0) {
                for (int i = 0; i < 4; ++i) {
                    int nx = x + kDx4[i], ny = y + kDy4[i];
                    if (IsInBounds(nx, ny)) {
                        TileData& ntd = tiles_[(size_t)(ny * width_ + nx)];
                        // Rampe verbindet benachbarte Höhenstufen mit Höhendifferenz 1
                        if (std::abs((int)td.heightLevel - (int)ntd.heightLevel) == 1) {
                            td.isRamp = true;
                            td.isCliff = false;
                            ntd.isRamp = true;
                            ntd.isCliff = false;
                            break;
                        }
                    }
                }
            }
        }
    }
}

void World::PlaceCaves(unsigned int seed) {
    std::mt19937 rng(seed + 54321u);

    // Suche Klippen an Gebirgen (heightLevel >= 3) mit südlich/westlich angrenzendem Flachland
    std::vector<std::pair<int, int>> candidates;
    for (int y = 2; y < height_ - 2; ++y) {
        for (int x = 2; x < width_ - 2; ++x) {
            const TileData& td = tiles_[(size_t)(y * width_ + x)];
            if (td.heightLevel >= 3 && td.type == TILE_MOUNTAIN) {
                // Südlicher Nachbar liegt tiefer (Klippenfront!)
                const TileData& southTd = tiles_[(size_t)((y + 1) * width_ + x)];
                if (southTd.heightLevel <= 2 && (southTd.type == TILE_GRASS || southTd.type == TILE_FOREST)) {
                    candidates.push_back({ x, y });
                }
            }
        }
    }

    std::shuffle(candidates.begin(), candidates.end(), rng);
    int cavesPlaced = 0;
    int targetCaves = std::clamp((int)candidates.size(), 4, 8);

    for (const auto& pos : candidates) {
        bool farEnough = true;
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                if (tiles_[(size_t)(y * width_ + x)].hasCave) {
                    int dx = pos.first - x, dy = pos.second - y;
                    if (dx * dx + dy * dy < 22 * 22) {
                        farEnough = false;
                        break;
                    }
                }
            }
            if (!farEnough) break;
        }

        if (farEnough) {
            tiles_[(size_t)(pos.second * width_ + pos.first)].hasCave = true;
            cavesPlaced++;
            if (cavesPlaced >= targetCaves) break;
        }
    }
}

void World::ComputeRegionIds() {
    for (auto& td : tiles_) td.regionId = 0;

    uint16_t nextRegionId = 1;
    static const int kDx4[4] = { 1, -1, 0, 0 };
    static const int kDy4[4] = { 0, 0, 1, -1 };

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            size_t idx = (size_t)(y * width_ + x);
            TileData& td = tiles_[idx];
            if (td.regionId != 0) continue;
            // Nur begehbares Land oder durchschwimmbares Küstenwasser (dist <= 8)
            if (td.distanceToShore > 8 && (td.type == TILE_DEEP_WATER || td.type == TILE_SHALLOW_WATER)) continue;
            if (td.isCliff && !td.isRamp) continue;

            uint16_t curReg = nextRegionId++;
            std::queue<std::pair<int, int>> q;
            td.regionId = curReg;
            q.push({ x, y });

            while (!q.empty()) {
                auto [cx, cy] = q.front();
                q.pop();

                for (int i = 0; i < 4; ++i) {
                    int nx = cx + kDx4[i], ny = cy + kDy4[i];
                    if (!IsInBounds(nx, ny)) continue;
                    size_t nidx = (size_t)(ny * width_ + nx);
                    TileData& ntd = tiles_[nidx];

                    if (ntd.regionId != 0) continue;
                    if (ntd.distanceToShore > 8 && (ntd.type == TILE_DEEP_WATER || ntd.type == TILE_SHALLOW_WATER)) continue;
                    if (!CanTraverseBetween(cx, cy, nx, ny)) continue;

                    ntd.regionId = curReg;
                    q.push({ nx, ny });
                }
            }
        }
    }
}

Vector2 World::CenterPosition() const {
    return TileCenter(width_ / 2, height_ / 2);
}

// ============================================================================
//  Kachelzugriff & Eigenschaften
// ============================================================================
const TileProperty& World::GetProperty(TileType type) {
    if (type >= 0 && type < TILE_TYPE_COUNT) return kTileProperties[type];
    return kTileProperties[TILE_DEEP_WATER];
}

bool World::IsInBounds(int x, int y) const {
    return x >= 0 && x < width_ && y >= 0 && y < height_;
}

TileType World::GetTile(int x, int y) const {
    if (!IsInBounds(x, y)) return TILE_DEEP_WATER;
    return tiles_[(size_t)(y * width_ + x)].type;
}

void World::SetTile(int x, int y, TileType type) {
    if (!IsInBounds(x, y)) return;
    tiles_[(size_t)(y * width_ + x)].type = type;
}

const TileData& World::GetTileData(int x, int y) const {
    static const TileData kFallback{};
    if (!IsInBounds(x, y)) return kFallback;
    return tiles_[(size_t)(y * width_ + x)];
}

TileData& World::GetTileData(int x, int y) {
    static TileData kFallback{};
    if (!IsInBounds(x, y)) return kFallback;
    return tiles_[(size_t)(y * width_ + x)];
}

uint8_t World::GetHeightLevel(int x, int y) const {
    return GetTileData(x, y).heightLevel;
}

float World::GetElevation(int x, int y) const {
    return GetTileData(x, y).elevation;
}

float World::GetMoisture(int x, int y) const {
    return GetTileData(x, y).moisture;
}

WaterType World::GetWaterType(int x, int y) const {
    return GetTileData(x, y).waterType;
}

bool World::IsRiver(int x, int y) const {
    return GetTileData(x, y).isRiver;
}

Vector2 World::GetFlowDirection(int x, int y) const {
    return GetTileData(x, y).flowDirection;
}

bool World::IsRamp(int x, int y) const {
    return GetTileData(x, y).isRamp;
}

bool World::IsCliff(int x, int y) const {
    return GetTileData(x, y).isCliff;
}

uint8_t World::GetDistanceToShore(int x, int y) const {
    return GetTileData(x, y).distanceToShore;
}

uint16_t World::GetRegionId(int x, int y) const {
    return GetTileData(x, y).regionId;
}

bool World::HasCave(int x, int y) const {
    return GetTileData(x, y).hasCave;
}

bool World::IsNearFreshwater(int tx, int ty, int radius) const {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int nx = tx + dx, ny = ty + dy;
            if (IsInBounds(nx, ny)) {
                const auto& td = GetTileData(nx, ny);
                if (td.waterType == WaterType::Freshwater || td.isRiver) {
                    return true;
                }
            }
        }
    }
    return false;
}

int World::GetOwnerTribeId(int x, int y) const {
    if (!IsInBounds(x, y)) return -1;
    return tiles_[(size_t)(y * width_ + x)].ownerTribeId;
}

void World::SetOwnerTribeId(int x, int y, int tribeId) {
    if (!IsInBounds(x, y)) return;
    tiles_[(size_t)(y * width_ + x)].ownerTribeId = tribeId;
}

float World::GetClaimStrength(int x, int y) const {
    if (!IsInBounds(x, y)) return 0.0f;
    return tiles_[(size_t)(y * width_ + x)].claimStrength;
}

void World::SetClaimStrength(int x, int y, float strength) {
    if (!IsInBounds(x, y)) return;
    tiles_[(size_t)(y * width_ + x)].claimStrength = std::clamp(strength, 0.0f, 100.0f);
}

void World::ModifyClaim(int x, int y, int tribeId, float delta) {
    if (!IsInBounds(x, y)) return;
    TileData& td = tiles_[(size_t)(y * width_ + x)];
    if (td.ownerTribeId == tribeId) {
        td.claimStrength = std::clamp(td.claimStrength + delta, 0.0f, 100.0f);
        if (td.claimStrength <= 0.0f) {
            td.ownerTribeId = -1;
            td.claimStrength = 0.0f;
        }
    } else if (td.ownerTribeId == -1) {
        if (delta > 0.0f) {
            td.ownerTribeId = tribeId;
            td.claimStrength = std::clamp(delta, 0.0f, 100.0f);
        }
    } else {
        // Fremdes Territorium: Erst gegnerischen Anspruch reduzieren
        td.claimStrength -= delta;
        if (td.claimStrength <= 0.0f) {
            td.ownerTribeId = tribeId;
            td.claimStrength = std::max(20.0f, std::clamp(-td.claimStrength, 0.0f, 100.0f));
        }
    }
}

float World::EvaluateTileStrategicValue(int x, int y, const ResourceStrategicWeights& weights) const {
    if (!IsInBounds(x, y)) return -100.0f;
    const TileData& td = tiles_[(size_t)(y * width_ + x)];

    if (td.isCliff || td.type == TILE_DEEP_WATER) return -50.0f;

    float score = 0.0f;

    // 1. Terrain-Eigenwert
    if (td.type == TILE_GRASS && td.heightLevel <= 2) score += weights.flatGrassland;
    if (td.type == TILE_FOREST) score += weights.forestWood;
    if (td.type == TILE_MOUNTAIN || td.heightLevel >= 3) score += weights.stoneMountain;
    if (td.hasCave) score += weights.poiDiscovery;

    // 2. Umfeld-Scoring (Radius 3 Kacheln)
    for (int dy = -3; dy <= 3; ++dy) {
        for (int dx = -3; dx <= 3; ++dx) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx, ny = y + dy;
            if (!IsInBounds(nx, ny)) continue;
            const TileData& ntd = tiles_[(size_t)(ny * width_ + nx)];
            int dist = std::max(std::abs(dx), std::abs(dy));
            float invDist = 1.0f / (float)dist;

            if (ntd.waterType == WaterType::Freshwater || ntd.isRiver) {
                score += weights.freshwater * invDist * 0.45f;
            }
            if (ntd.type == TILE_FOREST) {
                score += weights.forestWood * invDist * 0.30f;
            }
            if (ntd.type == TILE_MOUNTAIN) {
                score += weights.stoneMountain * invDist * 0.25f;
            }
            if (ntd.hasCave) {
                score += weights.poiDiscovery * invDist * 0.55f;
            }
        }
    }

    return score;
}

void World::InitStartingTerritory(int tribeId, Vector2 campPos, int tileCount) {
    int cx = ToTile(campPos.x);
    int cy = ToTile(campPos.y);

    struct Candidate {
        int x, y;
        float dist;
    };
    std::vector<Candidate> queue;

    for (int dy = -8; dy <= 8; ++dy) {
        for (int dx = -8; dx <= 8; ++dx) {
            int tx = cx + dx, ty = cy + dy;
            if (!IsInBounds(tx, ty)) continue;
            const TileData& td = tiles_[(size_t)(ty * width_ + tx)];
            if (td.isCliff || td.waterType != WaterType::None ||
                td.type == TILE_DEEP_WATER || td.type == TILE_SHALLOW_WATER || td.isRiver) continue;
            float d = std::sqrt((float)(dx * dx + dy * dy));
            float noise = (float)((tx * 13 + ty * 37) % 7) * 0.25f;
            queue.push_back({ tx, ty, d + noise });
        }
    }

    std::sort(queue.begin(), queue.end(), [](const Candidate& a, const Candidate& b) {
        return a.dist < b.dist;
    });

    int claimed = 0;
    for (const auto& c : queue) {
        TileData& td = tiles_[(size_t)(c.y * width_ + c.x)];
        td.ownerTribeId = tribeId;
        td.claimStrength = 100.0f;
        claimed++;
        if (claimed >= tileCount) break;
    }
}

void World::CalculateTribeInfluenceField(const Tribe& tribe, std::vector<float>& outInfluence) const {
    outInfluence.assign((size_t)(width_ * height_), 0.0f);
    int tribeId = tribe.Id();

    bool hasExpansionTarget = tribe.GetExpansionTarget().active;
    int targetTx = hasExpansionTarget ? ToTile(tribe.GetExpansionTarget().position.x) : 0;
    int targetTy = hasExpansionTarget ? ToTile(tribe.GetExpansionTarget().position.y) : 0;

    struct QItem {
        int x, y;
        float dist;
        bool operator>(const QItem& o) const { return dist > o.dist; }
    };

    const auto& buildings = tribe.Buildings().All();
    std::vector<float> localDist;

    for (const auto& b : buildings) {
        if (!b.IsTerritoryAnchor()) continue;

        float maxRadius = b.GetAnchorRadius();
        float basePower = b.GetInfluencePower();
        if (maxRadius <= 0.0f || basePower <= 0.0f) continue;

        int ax = ToTile(b.position.x);
        int ay = ToTile(b.position.y);
        if (!IsInBounds(ax, ay)) continue;

        int rInt = (int)std::ceil(maxRadius) + 1;
        int minX = std::max(0, ax - rInt);
        int maxX = std::min(width_ - 1, ax + rInt);
        int minY = std::max(0, ay - rInt);
        int maxY = std::min(height_ - 1, ay + rInt);

        localDist.assign((size_t)(width_ * height_), 99999.0f);
        std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> pq;

        localDist[(size_t)(ay * width_ + ax)] = 0.0f;
        pq.push({ ax, ay, 0.0f });

        while (!pq.empty()) {
            QItem cur = pq.top();
            pq.pop();

            if (cur.dist > localDist[(size_t)(cur.y * width_ + cur.x)]) continue;
            if (cur.dist > maxRadius) continue;

            float power = basePower * (1.0f - (cur.dist / maxRadius));
            size_t idx = (size_t)(cur.y * width_ + cur.x);
            outInfluence[idx] = std::max(outInfluence[idx], power);

            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = cur.x + dx, ny = cur.y + dy;
                    if (nx < minX || nx > maxX || ny < minY || ny > maxY) continue;

                    const TileData& ntd = tiles_[(size_t)(ny * width_ + nx)];
                    if (ntd.waterType != WaterType::None || ntd.type == TILE_DEEP_WATER ||
                        ntd.type == TILE_SHALLOW_WATER || ntd.isRiver) {
                        continue;
                    }
                    if (ntd.isCliff) continue;

                    float resistance = 1.0f;
                    if (ntd.type == TILE_FOREST || ntd.type == TILE_MOUNTAIN) resistance = 1.30f;
                    if (ntd.isRamp) resistance = 2.2f;

                    const TileData& ctd = tiles_[(size_t)(cur.y * width_ + cur.x)];
                    int heightDiff = std::abs((int)ntd.heightLevel - (int)ctd.heightLevel);
                    if (heightDiff >= 1 && !ntd.isRamp && !ctd.isRamp) {
                        resistance = 7.0f;
                    }

                    int friendlyNeighbors = 0;
                    static const int kOrtDx[4] = { 1, -1, 0, 0 };
                    static const int kOrtDy[4] = { 0, 0, 1, -1 };
                    for (int i = 0; i < 4; ++i) {
                        int ox = nx + kOrtDx[i], oy = ny + kOrtDy[i];
                        if (IsInBounds(ox, oy) && tiles_[(size_t)(oy * width_ + ox)].ownerTribeId == tribeId) {
                            friendlyNeighbors++;
                        }
                    }
                    if (friendlyNeighbors <= 1) {
                        resistance += 1.6f;
                    } else if (friendlyNeighbors >= 3) {
                        resistance = std::max(0.7f, resistance - 0.25f);
                    }

                    // Direktionale Ausrichtung (Phase 3): 0.75x Widerstand in Richtung des strategischen Bedarfs
                    if (hasExpansionTarget) {
                        float curTargDist = std::hypot((float)(targetTx - cur.x), (float)(targetTy - cur.y));
                        float nextTargDist = std::hypot((float)(targetTx - nx), (float)(targetTy - ny));
                        if (nextTargDist < curTargDist) {
                            resistance *= 0.75f;
                        }
                    }

                    float stepDist = (dx != 0 && dy != 0) ? 1.414f : 1.0f;
                    float nextDist = cur.dist + stepDist * resistance;

                    if (nextDist <= maxRadius && nextDist < localDist[(size_t)(ny * width_ + nx)]) {
                        localDist[(size_t)(ny * width_ + nx)] = nextDist;
                        pq.push({ nx, ny, nextDist });
                    }
                }
            }
        }
    }
}

void World::SmoothTerritoryBorders(int tribeId, const Tribe* tribe) {
    static const int kDx[4] = { 1, -1, 0, 0 };
    static const int kDy[4] = { 0, 0, 1, -1 };

    std::vector<std::pair<int, int>> toAdd;
    std::vector<std::pair<int, int>> toRemove;

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const TileData& td = tiles_[(size_t)(y * width_ + x)];

            if (td.waterType != WaterType::None || td.type == TILE_DEEP_WATER ||
                td.type == TILE_SHALLOW_WATER || td.isRiver || td.isCliff) {
                continue;
            }

            int friendlyNeighbors = 0;
            for (int i = 0; i < 4; ++i) {
                int nx = x + kDx[i], ny = y + kDy[i];
                if (IsInBounds(nx, ny) && tiles_[(size_t)(ny * width_ + nx)].ownerTribeId == tribeId) {
                    friendlyNeighbors++;
                }
            }

            if (td.ownerTribeId == -1 && friendlyNeighbors >= 3) {
                toAdd.push_back({ x, y });
            } else if (td.ownerTribeId == tribeId && friendlyNeighbors <= 1) {
                bool isAnchorTile = false;
                if (tribe) {
                    for (const auto& b : tribe->Buildings().All()) {
                        if (b.IsTerritoryAnchor()) {
                            if (ToTile(b.position.x) == x && ToTile(b.position.y) == y) {
                                isAnchorTile = true;
                                break;
                            }
                        }
                    }
                }
                if (!isAnchorTile) {
                    toRemove.push_back({ x, y });
                }
            }
        }
    }

    for (const auto& p : toAdd) {
        TileData& td = tiles_[(size_t)(p.second * width_ + p.first)];
        td.ownerTribeId = tribeId;
        td.claimStrength = 60.0f;
    }
    for (const auto& p : toRemove) {
        TileData& td = tiles_[(size_t)(p.second * width_ + p.first)];
        td.ownerTribeId = -1;
        td.claimStrength = 0.0f;
    }
}

void World::UpdateTerritories(const std::vector<std::unique_ptr<Tribe>>& tribes,
                              const std::vector<Caveman>& cavemen,
                              float dt) {
    if (tribes.empty()) return;

    static float territoryTimer = 0.0f;
    territoryTimer += dt;
    if (territoryTimer < 1.5f) return;
    territoryTimer = 0.0f;

    // 1. Reichskapazität aller Clans aktualisieren
    for (const auto& tr : tribes) {
        tr->UpdateEmpireCapacity(cavemen);
    }

    // 2. Multi-Source Dijkstra Einflussfelder berechnen (ausgelöste/ausgestorbene Clans überspringen)
    std::vector<std::vector<float>> tribeInfluences(tribes.size());
    for (size_t i = 0; i < tribes.size(); ++i) {
        if (!tribes[i]->IsDisbanded()) {
            CalculateTribeInfluenceField(*tribes[i], tribeInfluences[i]);
        } else {
            tribeInfluences[i].assign((size_t)(width_ * height_), 0.0f);
        }
    }

    // Sofortige Bereinigung der Gebiete aufgelöster Stämme
    for (const auto& tr : tribes) {
        if (tr->IsDisbanded()) {
            for (int i = 0; i < width_ * height_; ++i) {
                if (tiles_[i].ownerTribeId == tr->Id()) {
                    tiles_[i].ownerTribeId = -1;
                    tiles_[i].claimStrength = 0.0f;
                }
            }
        }
    }

    // 3. Wasser- und Klippenkacheln neutral halten
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            TileData& td = tiles_[(size_t)(y * width_ + x)];
            if (td.waterType != WaterType::None || td.type == TILE_DEEP_WATER ||
                td.type == TILE_SHALLOW_WATER || td.isRiver || td.isCliff) {
                td.ownerTribeId = -1;
                td.claimStrength = 0.0f;
            }
        }
    }

    // 4. Beanspruchungskandidaten nach höchster Einflussstärke ermitteln
    struct TileClaim {
        int x, y;
        float power;
    };
    std::vector<std::vector<TileClaim>> candidates(tribes.size());

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            TileData& td = tiles_[(size_t)(y * width_ + x)];
            if (td.waterType != WaterType::None || td.type == TILE_DEEP_WATER ||
                td.type == TILE_SHALLOW_WATER || td.isRiver || td.isCliff) {
                continue;
            }

            int bestTribeIdx = -1;
            float bestPower = 6.0f;

            for (size_t i = 0; i < tribes.size(); ++i) {
                float inf = tribeInfluences[i][(size_t)(y * width_ + x)];
                if (inf > bestPower) {
                    bestPower = inf;
                    bestTribeIdx = (int)i;
                }
            }

            if (bestTribeIdx >= 0) {
                candidates[(size_t)bestTribeIdx].push_back({ x, y, bestPower });
            }
        }
    }

    // 5. Reichskapazität & Outside-In Retraction anwenden
    std::vector<int> newOwners((size_t)(width_ * height_), -1);
    std::vector<float> newStrengths((size_t)(width_ * height_), 0.0f);

    for (size_t i = 0; i < tribes.size(); ++i) {
        int maxCapacity = tribes[i]->MaxTerritoryCapacity();
        auto& list = candidates[i];

        // Sortieren nach absteigender Stärke: Kernbereich bleibt sicher, Ränder verfallen bei Überkapazität
        std::sort(list.begin(), list.end(), [](const TileClaim& a, const TileClaim& b) {
            return a.power > b.power;
        });

        int claimCount = std::min((int)list.size(), maxCapacity);
        for (int c = 0; c < claimCount; ++c) {
            size_t idx = (size_t)(list[c].y * width_ + list[c].x);
            newOwners[idx] = tribes[i]->Id();
            newStrengths[idx] = std::clamp(list[c].power, 15.0f, 100.0f);
        }
        tribes[i]->SetCurrentTerritoryCount(claimCount);
    }

    for (size_t idx = 0; idx < (size_t)(width_ * height_); ++idx) {
        tiles_[idx].ownerTribeId = newOwners[idx];
        tiles_[idx].claimStrength = newStrengths[idx];
    }

    // 6. Glättung & Loch-Auffüllung (Cellular Automata)
    for (size_t i = 0; i < tribes.size(); ++i) {
        SmoothTerritoryBorders(tribes[i]->Id(), tribes[i].get());
    }

    // Kacheln nach Glättung final zählen
    for (size_t i = 0; i < tribes.size(); ++i) {
        int count = 0;
        int tId = tribes[i]->Id();
        for (const auto& td : tiles_) {
            if (td.ownerTribeId == tId) count++;
        }
        tribes[i]->SetCurrentTerritoryCount(count);
    }
}

std::vector<BorderContact> World::GetBorderContacts(int tribeId) const {
    std::unordered_map<int, BorderContact> contactMap;
    static const int kDx[4] = { 1, -1, 0, 0 };
    static const int kDy[4] = { 0, 0, 1, -1 };

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const TileData& td = tiles_[(size_t)(y * width_ + x)];
            if (td.ownerTribeId != tribeId) continue;

            for (int i = 0; i < 4; ++i) {
                int nx = x + kDx[i], ny = y + kDy[i];
                if (!IsInBounds(nx, ny)) continue;
                const TileData& ntd = tiles_[(size_t)(ny * width_ + nx)];
                if (ntd.ownerTribeId >= 0 && ntd.ownerTribeId != tribeId) {
                    BorderContact& bc = contactMap[ntd.ownerTribeId];
                    bc.neighborTribeId = ntd.ownerTribeId;
                    bc.sharedBorderTiles++;
                    if (bc.contactTiles.size() < 24) {
                        bc.contactTiles.push_back({ nx, ny });
                    }
                }
            }
        }
    }

    std::vector<BorderContact> result;
    for (auto& pair : contactMap) {
        result.push_back(std::move(pair.second));
    }
    return result;
}

bool World::IsWalkableTile(int x, int y) const {
    if (!IsInBounds(x, y)) return false;
    const TileData& td = tiles_[(size_t)(y * width_ + x)];

    // Klippen ohne Rampe sind unpassierbar
    if (td.isCliff && !td.isRamp) return false;

    // Tiefes Salzwasser weit vom Ufer entfernt (> 8 Kacheln) ist strikt unpassierbar
    if (td.waterType == WaterType::Saltwater && td.distanceToShore > 8) return false;

    // Basis-Begehbarkeit der TileProperty
    if (GetProperty(td.type).isWalkable) return true;
    if (env_ && env_->GetState(x, y).frozen && !env_->IsTileDangerous(x, y)) return true;

    return false;
}

bool World::IsWalkable(Vector2 worldPos) const {
    return IsWalkableTile(ToTile(worldPos.x), ToTile(worldPos.y));
}

bool World::CanTraverseBetween(int fromX, int fromY, int toX, int toY) const {
    if (!IsInBounds(fromX, fromY) || !IsInBounds(toX, toY)) return false;
    const TileData& tA = tiles_[(size_t)(fromY * width_ + fromX)];
    const TileData& tB = tiles_[(size_t)(toY * width_ + toX)];

    // Klippen ohne Rampe sind unpassierbar
    if ((tA.isCliff && !tA.isRamp) || (tB.isCliff && !tB.isRamp)) return false;

    // Höhenunterschied >= 1 ist nur über Rampen passierbar
    if (std::abs((int)tA.heightLevel - (int)tB.heightLevel) >= 1) {
        if (!tA.isRamp && !tB.isRamp) return false;
    }

    if (tB.distanceToShore > 8 && (tB.type == TILE_DEEP_WATER || tB.type == TILE_SHALLOW_WATER)) return false;

    return true;
}

bool World::HasNeighbor(int x, int y, TileType type) const {
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if ((dx || dy) && IsInBounds(x + dx, y + dy) && GetTile(x + dx, y + dy) == type) {
                return true;
            }
        }
    }
    return false;
}

Vector2 World::TileCenter(int tx, int ty) {
    return { (float)(tx * TILE_SIZE) + TILE_SIZE * 0.5f, (float)(ty * TILE_SIZE) + TILE_SIZE * 0.5f };
}

// ============================================================================
//  Suche
// ============================================================================
bool World::FindRandomWalkablePosition(Vector2 center, float radiusPx, Vector2& out,
                                       int attempts, TileType requiredType) const {
    for (int i = 0; i < attempts; ++i) {
        const float angle = (float)GetRandomValue(0, 3600) / 3600.0f * 2.0f * PI;
        const float r = std::sqrt((float)GetRandomValue(0, 1000) / 1000.0f) * radiusPx;
        const Vector2 p = { center.x + std::cos(angle) * r, center.y + std::sin(angle) * r };
        const int tx = ToTile(p.x), ty = ToTile(p.y);
        if (!IsWalkableTile(tx, ty)) continue;
        if (env_ && env_->IsTileDangerous(tx, ty)) continue;
        if (requiredType != TILE_TYPE_COUNT && GetTile(tx, ty) != requiredType) continue;
        out = p;
        return true;
    }
    return false;
}

bool World::FindRandomTile(const std::function<bool(int, int)>& predicate, Vector2& out, int attempts) const {
    for (int i = 0; i < attempts; ++i) {
        const int tx = GetRandomValue(0, width_ - 1);
        const int ty = GetRandomValue(0, height_ - 1);
        if (!predicate(tx, ty)) continue;
        const Vector2 c = TileCenter(tx, ty);
        out = { c.x + (float)GetRandomValue(-4, 4), c.y + (float)GetRandomValue(-4, 4) };
        return true;
    }
    return false;
}

bool World::FindNearestWalkableTile(int tx, int ty, int maxRadius, int& outX, int& outY) const {
    if (IsWalkableTile(tx, ty) && (!env_ || !env_->IsTileDangerous(tx, ty))) {
        outX = tx; outY = ty; return true;
    }
    for (int r = 1; r <= maxRadius; ++r) {
        int bestDist = 1 << 30;
        bool found = false;
        for (int y = ty - r; y <= ty + r; ++y) {
            for (int x = tx - r; x <= tx + r; ++x) {
                if (std::abs(x - tx) != r && std::abs(y - ty) != r) continue;
                if (!IsWalkableTile(x, y)) continue;
                if (env_ && env_->IsTileDangerous(x, y)) continue;
                const int d = (x - tx) * (x - tx) + (y - ty) * (y - ty);
                if (d < bestDist) { bestDist = d; outX = x; outY = y; found = true; }
            }
        }
        if (found) return true;
    }
    return false;
}

// ============================================================================
//  Dynamische Wege (Trampelpfade & Gepflasterte Straßen)
// ============================================================================
void World::AddFootTraffic(int tx, int ty, float amount, bool canPave) {
    if (!IsInBounds(tx, ty)) return;
    TileType t = GetTile(tx, ty);
    if (t != TILE_GRASS && t != TILE_FOREST && t != TILE_SAND) return;

    const size_t idx = (size_t)(ty * width_ + tx);
    footTraffic_[idx] = std::min(100.0f, footTraffic_[idx] + amount);
    if (canPave && footTraffic_[idx] >= 78.0f) {
        pavedRoad_[idx] = 1;
    }
}

float World::GetFootTraffic(int tx, int ty) const {
    if (!IsInBounds(tx, ty)) return 0.0f;
    return footTraffic_[(size_t)(ty * width_ + tx)];
}

bool World::IsPavedRoad(int tx, int ty) const {
    if (!IsInBounds(tx, ty)) return false;
    return pavedRoad_[(size_t)(ty * width_ + tx)] != 0;
}

float World::GetRoadSpeedMultiplier(Vector2 worldPos) const {
    const int tx = ToTile(worldPos.x);
    const int ty = ToTile(worldPos.y);
    if (!IsInBounds(tx, ty)) return 1.0f;
    const size_t idx = (size_t)(ty * width_ + tx);
    if (pavedRoad_[idx]) return 1.25f;
    if (footTraffic_[idx] >= 35.0f) return 1.12f;
    if (footTraffic_[idx] >= 15.0f) return 1.05f;
    return 1.0f;
}

void World::UpdateRoads(float dt) {
    const float decay = 0.08f * dt;
    const size_t n = footTraffic_.size();
    for (size_t i = 0; i < n; ++i) {
        if (pavedRoad_[i] || footTraffic_[i] <= 0.0f) continue;
        float rate = (footTraffic_[i] > 55.0f) ? (decay * 0.25f) : decay;
        footTraffic_[i] = std::max(0.0f, footTraffic_[i] - rate);
    }
}

// ============================================================================
//  Pfadsuche & Bewegungskosten (Phase 2)
// ============================================================================
float World::GetMovementCost(int fromX, int fromY, int toX, int toY, int actorTribeId, bool ignoreTerritory) const {
    if (!CanTraverseBetween(fromX, fromY, toX, toY)) return 9999.0f;

    const TileData& td = tiles_[(size_t)(toY * width_ + toX)];
    float baseCost = 1.0f;

    // Gestaffelte Bewegungskosten:
    if (td.type == TILE_SHALLOW_WATER || td.type == TILE_DEEP_WATER) {
        if (td.waterType == WaterType::Freshwater && td.type == TILE_SHALLOW_WATER) {
            baseCost = 3.5f; // Seichtes Süßwasser / Flussfurt
        } else if (td.distanceToShore <= 4) {
            baseCost = 7.0f; // Tiefes Süßwasser / küstennahes Salzwasser
        } else if (td.distanceToShore <= 8) {
            baseCost = 15.0f; // Weiter entferntes Salzwasser
        } else {
            return 9999.0f; // Ozean unpassierbar
        }
    } else {
        // Festland
        if (td.isRamp) {
            baseCost = 1.8f; // Bergauf-Rampe
        } else {
            size_t idx = (size_t)(toY * width_ + toX);
            if (pavedRoad_[idx]) baseCost = 0.8f;
            else if (footTraffic_[idx] >= 35.0f) baseCost = 0.9f;
            else baseCost = 1.0f;
        }
    }

    if (env_) {
        baseCost *= env_->GetTileTraversalCost(toX, toY);
    }

    // A*-Pathfinding-Respekt vor fremden Grenzen (Phase 2):
    // Fremdes Clan-Territorium erhält extrem hohe Bewegungskosten (+45.0f),
    // sodass friedliche Figuren fremde Gebiete im Alltag umlaufen.
    if (!ignoreTerritory && actorTribeId >= 0 && td.ownerTribeId >= 0 && td.ownerTribeId != actorTribeId) {
        baseCost += 45.0f;
    }

    return baseCost;
}

bool World::HasLineOfSight(Vector2 a, Vector2 b) const {
    const float dist = Vector2Distance(a, b);
    const int steps = std::max(1, (int)(dist / 2.0f));
    int prevX = ToTile(a.x), prevY = ToTile(a.y);

    for (int i = 0; i <= steps; ++i) {
        const Vector2 p = Vector2Lerp(a, b, (float)i / (float)steps);
        const int tx = ToTile(p.x), ty = ToTile(p.y);
        if (!IsWalkableTile(tx, ty)) return false;
        if (!CanTraverseBetween(prevX, prevY, tx, ty)) return false;
        if (env_ && env_->IsTileDangerous(tx, ty)) return false;
        prevX = tx;
        prevY = ty;
    }
    return true;
}

bool World::FindPath(Vector2 from, Vector2 to, std::vector<Vector2>& outPath, int actorTribeId, bool ignoreTerritory) const {
    outPath.clear();

    const int sx = ToTile(from.x), sy = ToTile(from.y);
    int gx = ToTile(to.x), gy = ToTile(to.y);
    if (!IsInBounds(sx, sy)) return false;

    // Zielbegehbarkeit prüfen
    const bool targetWalkable = IsWalkableTile(gx, gy) && (!env_ || !env_->IsTileDangerous(gx, gy));
    if (!targetWalkable && !FindNearestWalkableTile(gx, gy, 5, gx, gy)) return false;
    const Vector2 finalPoint = targetWalkable ? to : TileCenter(gx, gy);

    // O(1) Insel-/Erreichbarkeitsprüfung: Liegen Start und Ziel in unterschiedlichen Regionen?
    uint16_t regStart = GetRegionId(sx, sy);
    uint16_t regGoal = GetRegionId(gx, gy);
    if (regStart != 0 && regGoal != 0 && regStart != regGoal) {
        return false; // Sofortiger Abbruch ohne teure A*-Expansion!
    }

    if (sx == gx && sy == gy) {
        outPath.push_back(finalPoint);
        return true;
    }

    const size_t n = (size_t)(width_ * height_);
    if (gScore_.size() != n) {
        gScore_.assign(n, 0.0f);
        cameFrom_.assign(n, -1);
        openStamp_.assign(n, 0u);
        closedStamp_.assign(n, 0u);
        stamp_ = 0;
    }
    if (++stamp_ == 0) {
        std::fill(openStamp_.begin(), openStamp_.end(), 0u);
        std::fill(closedStamp_.begin(), closedStamp_.end(), 0u);
        stamp_ = 1;
    }

    auto heuristic = [gx, gy](int x, int y) {
        const float dx = (float)std::abs(x - gx), dy = (float)std::abs(y - gy);
        return (dx + dy) + (kSqrt2 - 2.0f) * std::min(dx, dy);
    };

    using QueueEntry = std::pair<float, int>;
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> open;

    const int start = sy * width_ + sx;
    const int goal  = gy * width_ + gx;
    gScore_[(size_t)start] = 0.0f;
    cameFrom_[(size_t)start] = -1;
    openStamp_[(size_t)start] = stamp_;
    open.push({ heuristic(sx, sy), start });

    static const int   kDx[8]   = { 1, -1, 0, 0, 1, 1, -1, -1 };
    static const int   kDy[8]   = { 0, 0, 1, -1, 1, -1, 1, -1 };
    static const float kDist[8] = { 1.0f, 1.0f, 1.0f, 1.0f, kSqrt2, kSqrt2, kSqrt2, kSqrt2 };

    bool found = false;
    int expanded = 0;

    while (!open.empty()) {
        const int current = open.top().second;
        open.pop();
        if (closedStamp_[(size_t)current] == stamp_) continue;
        closedStamp_[(size_t)current] = stamp_;

        if (current == goal) { found = true; break; }
        if (++expanded > kMaxPathExpansions) break;

        const int cx = current % width_;
        const int cy = current / width_;

        for (int k = 0; k < 8; ++k) {
            const int nx = cx + kDx[k], ny = cy + kDy[k];
            if (!IsWalkableTile(nx, ny)) continue;
            if (!CanTraverseBetween(cx, cy, nx, ny)) continue;

            // Diagonalsperre: Keine Klippen oder Ecken schneiden
            if (k >= 4 && (!IsWalkableTile(cx + kDx[k], cy) || !IsWalkableTile(cx, cy + kDy[k]))) continue;

            const int ni = ny * width_ + nx;
            if (closedStamp_[(size_t)ni] == stamp_) continue;

            const float moveCost = GetMovementCost(cx, cy, nx, ny, actorTribeId, ignoreTerritory);
            if (moveCost >= 9000.0f) continue;

            const float g = gScore_[(size_t)current] + kDist[k] * moveCost;
            if (openStamp_[(size_t)ni] != stamp_ || g < gScore_[(size_t)ni]) {
                openStamp_[(size_t)ni] = stamp_;
                gScore_[(size_t)ni] = g;
                cameFrom_[(size_t)ni] = current;
                open.push({ g + heuristic(nx, ny), ni });
            }
        }
    }

    if (!found) return false;

    // Pfad rekonstruieren
    std::vector<Vector2> raw;
    for (int c = goal; c != -1; c = cameFrom_[(size_t)c]) {
        raw.push_back(TileCenter(c % width_, c / width_));
    }
    std::reverse(raw.begin(), raw.end());
    raw.front() = from;
    raw.back() = finalPoint;

    // Glättung
    size_t i = 0;
    while (i + 1 < raw.size()) {
        size_t best = i + 1;
        const size_t limit = std::min(raw.size() - 1, i + (size_t)kSmoothLookahead);
        for (size_t j = i + 2; j <= limit; ++j) {
            if (HasLineOfSight(raw[i], raw[j])) best = j;
            else break;
        }
        outPath.push_back(raw[best]);
        i = best;
    }

    return true;
}

// ============================================================================
//  2.5D Stacked Elevation & Visual Projections (Phase 4)
// ============================================================================
float World::GetVisualYOffset(Vector2 worldPos) const {
    int tx = ToTile(worldPos.x);
    int ty = ToTile(worldPos.y);
    if (!IsInBounds(tx, ty)) return 0.0f;
    return -((float)tiles_[(size_t)(ty * width_ + tx)].heightLevel * ELEVATION_PIXEL_HEIGHT);
}

bool World::GetTileAtRenderPos(Vector2 worldPos, int& outX, int& outY) const {
    int candX = ToTile(worldPos.x);
    if (candX < 0 || candX >= width_) return false;

    int baseTy = ToTile(worldPos.y);
    int maxY = std::min(height_ - 1, baseTy + 4);
    int minY = std::max(0, baseTy - 1);

    // Von unten (Süden) nach oben (Norden) prüfen, da südlichere/vordere Kacheln vor nördlicheren gezeichnet werden
    for (int y = maxY; y >= minY; --y) {
        const TileData& td = tiles_[(size_t)(y * width_ + candX)];
        float topY = (float)y * TILE_SIZE - (float)td.heightLevel * ELEVATION_PIXEL_HEIGHT;
        int hSouth = IsInBounds(candX, y + 1) ? tiles_[(size_t)((y + 1) * width_ + candX)].heightLevel : 0;
        float bottomY = (float)(y + 1) * TILE_SIZE - (float)std::min((int)td.heightLevel, hSouth) * ELEVATION_PIXEL_HEIGHT;

        if (worldPos.y >= topY && worldPos.y < bottomY) {
            outX = candX;
            outY = y;
            return true;
        }
    }

    outX = std::clamp(candX, 0, width_ - 1);
    outY = std::clamp(baseTy, 0, height_ - 1);
    return true;
}

// ============================================================================
//  Rendering (Phase 4 "On-Top 3D" 2.5D Stacked Elevation Renderer)
// ============================================================================
void World::Draw(const Camera2D& camera) const {
    const Vector2 topLeft = GetScreenToWorld2D({ 0.0f, 0.0f }, camera);
    const Vector2 bottomRight = GetScreenToWorld2D({ (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT }, camera);

    const int x0 = std::max(0, ToTile(topLeft.x) - 1);
    // 4 Kacheln Puffer nach oben, da Kacheln bis zu 36px (6 * 6px) nach oben versetzt sind
    const int y0 = std::max(0, ToTile(topLeft.y) - 4);
    const int x1 = std::min(width_ - 1, ToTile(bottomRight.x) + 1);
    const int y1 = std::min(height_ - 1, ToTile(bottomRight.y) + 2);

    float simTime = (float)GetTime();

    // Strikte Top-to-Bottom Renderreihenfolge (y = y0 bis y1) für perfekte Verdeckung
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const TileData& td = tiles_[(size_t)(y * width_ + x)];
            Color c = GetProperty(td.type).color;

            // 1. Süßwasser vs. Salzwasser Farbgebung
            if (td.type == TILE_DEEP_WATER || td.type == TILE_SHALLOW_WATER) {
                if (td.waterType == WaterType::Freshwater) {
                    c = (td.type == TILE_SHALLOW_WATER) ? Color{ 62, 168, 228, 255 } : Color{ 38, 118, 192, 255 };
                } else {
                    c = (td.type == TILE_SHALLOW_WATER) ? Color{ 48, 138, 190, 255 } : Color{ 22, 68, 142, 255 };
                }
            }

            // 2. Höhen-Schattierung für 3D-Relief
            if (td.heightLevel > 0) {
                int lightBonus = (int)(td.heightLevel * 6);
                c.r = (unsigned char)std::min(255, c.r + lightBonus);
                c.g = (unsigned char)std::min(255, c.g + lightBonus);
                c.b = (unsigned char)std::min(255, c.b + lightBonus);
            }

            const int px = x * TILE_SIZE;
            const int py = (int)((float)(y * TILE_SIZE) - (float)td.heightLevel * ELEVATION_PIXEL_HEIGHT);

            // Obere Kachel-Platte
            DrawRectangle(px, py, TILE_SIZE, TILE_SIZE, c);

            // 3. Drop Shadows & Ambient Occlusion (wenn Nord- oder Westnachbar höher liegt)
            if (y > 0 && tiles_[(size_t)((y - 1) * width_ + x)].heightLevel > td.heightLevel) {
                DrawRectangle(px, py, TILE_SIZE, 3, Fade(BLACK, 0.28f));
            }
            if (x > 0 && tiles_[(size_t)(y * width_ + (x - 1))].heightLevel > td.heightLevel) {
                DrawRectangle(px, py, 2, TILE_SIZE, Fade(BLACK, 0.22f));
            }

            // 4. Animierter Wellenschaum an Küsten
            if ((td.type == TILE_SHALLOW_WATER || td.type == TILE_DEEP_WATER) && td.distanceToShore <= 1) {
                float wave = std::sin(simTime * 3.5f + (float)(x * 3 + y * 7) * 0.4f);
                DrawRectangleLines(px + 1, py + 1, TILE_SIZE - 2, TILE_SIZE - 2, Fade(WHITE, 0.22f + 0.18f * wave));
            }

            // 5. Animierte Strömungslinien bei Gebirgsflüssen
            if (td.isRiver) {
                Vector2 flow = td.flowDirection;
                if (Vector2Length(flow) < 0.1f) flow = { 1.0f, 0.0f };
                float animOffset = std::fmod(simTime * 12.0f + (float)(x * 5 + y * 11), 8.0f) - 4.0f;
                Vector2 pStart = { (float)px + 8.0f + flow.x * (animOffset - 3.0f), (float)py + 8.0f + flow.y * (animOffset - 3.0f) };
                Vector2 pEnd   = { (float)px + 8.0f + flow.x * (animOffset + 3.0f), (float)py + 8.0f + flow.y * (animOffset + 3.0f) };
                DrawLineEx(pStart, pEnd, 1.6f, Fade(SKYBLUE, 0.65f));
                DrawLineEx({ pStart.x + 0.8f, pStart.y }, { pEnd.x + 0.8f, pEnd.y }, 1.0f, Fade(WHITE, 0.45f));
            }

            // 6. Trampelpfade & Straßen auf erhöhter Ebene
            const size_t idx = (size_t)(y * width_ + x);
            if (pavedRoad_[idx]) {
                DrawRectangle(px + 1, py + 1, TILE_SIZE - 2, TILE_SIZE - 2, Color{ 146, 142, 134, 235 });
                DrawRectangleLines(px + 1, py + 1, TILE_SIZE - 2, TILE_SIZE - 2, Color{ 115, 110, 105, 255 });
            } else if (footTraffic_[idx] >= 35.0f) {
                unsigned char alpha = (unsigned char)std::clamp(125.0f + footTraffic_[idx] * 0.9f, 135.0f, 220.0f);
                DrawRectangle(px + 1, py + 1, TILE_SIZE - 2, TILE_SIZE - 2, Color{ 138, 108, 72, alpha });
            }

            // 7. Begehbare Rampen (natürliche Steintreppen / Erdaufschüttungen)
            if (td.isRamp) {
                DrawRectangle(px + 2, py + 3, TILE_SIZE - 4, 3, Color{ 165, 140, 95, 230 });
                DrawRectangle(px + 2, py + 9, TILE_SIZE - 4, 3, Color{ 148, 122, 78, 230 });
                DrawRectangleLines(px + 2, py + 2, TILE_SIZE - 4, TILE_SIZE - 4, Fade(GOLD, 0.45f));
            }

            // 8. Klippenwände nach Süden mit horizontalen Gesteinsschichten
            int hSouth = IsInBounds(x, y + 1) ? tiles_[(size_t)((y + 1) * width_ + x)].heightLevel : 0;
            if (td.heightLevel > hSouth) {
                int cliffH = (int)((float)(td.heightLevel - hSouth) * ELEVATION_PIXEL_HEIGHT);
                int cliffY = py + TILE_SIZE;

                // Grundkörper der Klippenwand
                DrawRectangle(px, cliffY, TILE_SIZE, cliffH, Color{ 64, 58, 54, 255 });

                // Horizontale Sediment-/Gesteinsschichten (Schiefer, Granit)
                for (int s = 2; s < cliffH - 1; s += 3) {
                    Color strataCol = ((s / 3) % 2 == 0) ? Color{ 82, 75, 70, 255 } : Color{ 50, 45, 42, 255 };
                    DrawLine(px, cliffY + s, px + TILE_SIZE, cliffY + s, strataCol);
                }
                DrawLine(px, cliffY + cliffH - 1, px + TILE_SIZE, cliffY + cliffH - 1, Color{ 38, 34, 31, 255 });

                // Höhleneingang in der Klippenwand
                if (td.hasCave) {
                    int caveH = std::max(4, cliffH - 2);
                    DrawRectangle(px + 4, cliffY + 1, 8, caveH, Color{ 16, 12, 10, 255 });
                    DrawCircle(px + 8, cliffY + 2, 4.0f, Color{ 16, 12, 10, 255 });
                    DrawRectangleLines(px + 4, cliffY + 1, 8, caveH, Color{ 90, 85, 80, 255 });
                }
            } else if (td.hasCave) {
                // Höhle auf flacher Felskachel
                DrawEllipse(px + 8, py + 8, 5, 4, Color{ 16, 12, 10, 255 });
                DrawEllipseLines(px + 8, py + 8, 5, 4, Color{ 90, 85, 80, 255 });
            }
        }
    }

    DrawRectangleLines(0, 0, width_ * TILE_SIZE, height_ * TILE_SIZE, DARKGRAY);
}
