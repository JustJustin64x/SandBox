#include "world/world.h"
#include "world/environmental_system.h"

#include <algorithm>
#include <queue>
#include <utility>

namespace {

// Kachel-Eigenschaften (Reihenfolge entspricht TileType).
const TileProperty kTileProperties[TILE_TYPE_COUNT] = {
    { "Tiefwasser",  { 28, 70, 140, 255 },   false, true  },
    { "Flachwasser", { 45, 115, 185, 255 },  false, true  },
    { "Sand",        { 215, 195, 120, 255 }, true,  false },
    { "Gras",        { 80, 160, 60, 255 },   true,  false },
    { "Wald",        { 45, 110, 40, 255 },   true,  false },
    { "Gebirge",     { 130, 125, 120, 255 }, false, false },
};

// --- Einfaches Value-Noise für die Weltgenerierung ---------------------------
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

float FractalNoise(float x, float y, unsigned int seed) {
    float sum = 0.0f, amplitude = 1.0f, norm = 0.0f, frequency = 1.0f;
    for (int octave = 0; octave < 4; ++octave) {
        sum += SmoothNoise(x * frequency, y * frequency, seed + (unsigned int)octave * 101u) * amplitude;
        norm += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return sum / norm;
}

constexpr int   kMaxPathExpansions = 30000;
constexpr int   kSmoothLookahead   = 14;
constexpr float kSqrt2             = 1.41421356f;

} // namespace

// ============================================================================
//  Konstruktion & Generierung
// ============================================================================
World::World(int width, int height, unsigned int seed)
    : width_(std::max(8, width)), height_(std::max(8, height)),
      tiles_((size_t)(std::max(8, width) * std::max(8, height)), TILE_DEEP_WATER),
      footTraffic_((size_t)(std::max(8, width) * std::max(8, height)), 0.0f),
      pavedRoad_((size_t)(std::max(8, width) * std::max(8, height)), 0) {
    Generate(seed);
}

void World::Generate(unsigned int seed) {
    std::fill(footTraffic_.begin(), footTraffic_.end(), 0.0f);
    std::fill(pavedRoad_.begin(), pavedRoad_.end(), 0);
    const float cx = (float)width_ * 0.5f;
    const float cy = (float)height_ * 0.5f;

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            // Elliptische Insel: Distanz zur Mitte normiert auf 0..1
            const float dx = ((float)x - cx) / cx;
            const float dy = ((float)y - cy) / cy;
            const float dist = std::sqrt(dx * dx + dy * dy);

            const float shape    = FractalNoise((float)x * 0.07f, (float)y * 0.07f, seed);
            const float rocks    = FractalNoise((float)x * 0.09f, (float)y * 0.09f, seed + 7919u);
            const float moisture = FractalNoise((float)x * 0.08f, (float)y * 0.08f, seed + 104729u);
            const float land     = 0.45f * shape + 0.75f * (1.0f - dist);

            TileType type;
            if (land < 0.42f)       type = TILE_DEEP_WATER;
            else if (land < 0.50f)  type = TILE_SHALLOW_WATER;
            else if (land < 0.55f)  type = TILE_SAND;
            else if (rocks > 0.64f) type = TILE_MOUNTAIN;
            else if (moisture > 0.54f) type = TILE_FOREST;
            else                    type = TILE_GRASS;

            tiles_[(size_t)(y * width_ + x)] = type;
        }
    }

    // Lagerplatz in der Mitte freiräumen (Grasland), damit der Stamm Platz hat.
    const int campX = width_ / 2;
    const int campY = height_ / 2;
    const int clearRadius = 9;
    for (int y = campY - clearRadius; y <= campY + clearRadius; ++y) {
        for (int x = campX - clearRadius; x <= campX + clearRadius; ++x) {
            const int ddx = x - campX, ddy = y - campY;
            if (ddx * ddx + ddy * ddy <= clearRadius * clearRadius) SetTile(x, y, TILE_GRASS);
        }
    }
}

Vector2 World::CenterPosition() const {
    return TileCenter(width_ / 2, height_ / 2);
}

// ============================================================================
//  Kachelzugriff
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
    return tiles_[(size_t)(y * width_ + x)];
}

void World::SetTile(int x, int y, TileType type) {
    if (!IsInBounds(x, y)) return;
    tiles_[(size_t)(y * width_ + x)] = type;
}

bool World::IsWalkableTile(int x, int y) const {
    if (!IsInBounds(x, y)) return false;
    if (GetProperty(GetTile(x, y)).isWalkable) return true;
    if (env_ && env_->GetState(x, y).frozen && !env_->IsTileDangerous(x, y)) return true;
    return false;
}

bool World::IsWalkable(Vector2 worldPos) const {
    return IsWalkableTile(ToTile(worldPos.x), ToTile(worldPos.y));
}

bool World::HasNeighbor(int x, int y, TileType type) const {
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if ((dx || dy) && IsInBounds(x + dx, y + dy) && GetTile(x + dx, y + dy) == type) return true;
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
    if (IsWalkableTile(tx, ty) && (!env_ || !env_->IsTileDangerous(tx, ty))) { outX = tx; outY = ty; return true; }
    for (int r = 1; r <= maxRadius; ++r) {
        int bestDist = 1 << 30;
        bool found = false;
        for (int y = ty - r; y <= ty + r; ++y) {
            for (int x = tx - r; x <= tx + r; ++x) {
                if (std::abs(x - tx) != r && std::abs(y - ty) != r) continue;   // nur Ring
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
    if (pavedRoad_[idx]) return 1.25f;              // Gepflasterte Straße: +25% Tempo
    if (footTraffic_[idx] >= 35.0f) return 1.12f;   // Ausgetretener Trampelpfad: +12% Tempo
    if (footTraffic_[idx] >= 15.0f) return 1.05f;   // Plattgetretenes Gras: +5% Tempo
    return 1.0f;
}

void World::UpdateRoads(float dt) {
    const float decay = 0.08f * dt;
    const size_t n = footTraffic_.size();
    for (size_t i = 0; i < n; ++i) {
        if (pavedRoad_[i] || footTraffic_[i] <= 0.0f) continue;
        // Wenig genutzte Pfade wachsen langsam wieder zu; Hauptwege bleiben stabiler
        float rate = (footTraffic_[i] > 55.0f) ? (decay * 0.25f) : decay;
        footTraffic_[i] = std::max(0.0f, footTraffic_[i] - rate);
    }
}

// ============================================================================
//  Pfadsuche
// ============================================================================
bool World::HasLineOfSight(Vector2 a, Vector2 b) const {
    const float dist = Vector2Distance(a, b);
    const int steps = std::max(1, (int)(dist / 2.0f));
    for (int i = 0; i <= steps; ++i) {
        const Vector2 p = Vector2Lerp(a, b, (float)i / (float)steps);
        const int tx = ToTile(p.x), ty = ToTile(p.y);
        if (!IsWalkableTile(tx, ty)) return false;
        if (env_ && env_->IsTileDangerous(tx, ty)) return false;
    }
    return true;
}

bool World::FindPath(Vector2 from, Vector2 to, std::vector<Vector2>& outPath) const {
    outPath.clear();

    const int sx = ToTile(from.x), sy = ToTile(from.y);
    int gx = ToTile(to.x), gy = ToTile(to.y);
    if (!IsInBounds(sx, sy)) return false;

    const bool targetWalkable = IsWalkableTile(gx, gy) && (!env_ || !env_->IsTileDangerous(gx, gy));
    if (!targetWalkable && !FindNearestWalkableTile(gx, gy, 5, gx, gy)) return false;
    const Vector2 finalPoint = targetWalkable ? to : TileCenter(gx, gy);

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
    if (++stamp_ == 0) {   // Überlauf: Stempel zurücksetzen
        std::fill(openStamp_.begin(), openStamp_.end(), 0u);
        std::fill(closedStamp_.begin(), closedStamp_.end(), 0u);
        stamp_ = 1;
    }

    auto heuristic = [gx, gy](int x, int y) {
        const float dx = (float)std::abs(x - gx), dy = (float)std::abs(y - gy);
        return (dx + dy) + (kSqrt2 - 2.0f) * std::min(dx, dy);   // Oktil-Distanz
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
    static const float kCost[8] = { 1, 1, 1, 1, kSqrt2, kSqrt2, kSqrt2, kSqrt2 };

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
            // Diagonalen nur, wenn beide angrenzenden Kacheln frei sind (keine Ecken schneiden)
            if (k >= 4 && (!IsWalkableTile(cx + kDx[k], cy) || !IsWalkableTile(cx, cy + kDy[k]))) continue;

            const int ni = ny * width_ + nx;
            if (closedStamp_[(size_t)ni] == stamp_) continue;

            const float envCost = env_ ? env_->GetTileTraversalCost(nx, ny) : 1.0f;
            const float roadFactor = pavedRoad_[(size_t)ni] ? 0.78f
                                   : (footTraffic_[(size_t)ni] >= 35.0f ? 0.88f : 1.0f);
            const float g = gScore_[(size_t)current] + kCost[k] * envCost * roadFactor;
            if (openStamp_[(size_t)ni] != stamp_ || g < gScore_[(size_t)ni]) {
                openStamp_[(size_t)ni] = stamp_;
                gScore_[(size_t)ni] = g;
                cameFrom_[(size_t)ni] = current;
                open.push({ g + heuristic(nx, ny), ni });
            }
        }
    }
    if (!found) return false;

    // Rohpfad rekonstruieren (Kachelmitten)
    std::vector<Vector2> raw;
    for (int c = goal; c != -1; c = cameFrom_[(size_t)c]) raw.push_back(TileCenter(c % width_, c / width_));
    std::reverse(raw.begin(), raw.end());
    raw.front() = from;
    raw.back() = finalPoint;

    // Glätten: so weit wie möglich geradeaus laufen (begrenzter Vorausblick).
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
//  Rendering (nur sichtbarer Ausschnitt wird gezeichnet)
// ============================================================================
void World::Draw(const Camera2D& camera) const {
    const Vector2 topLeft = GetScreenToWorld2D({ 0.0f, 0.0f }, camera);
    const Vector2 bottomRight = GetScreenToWorld2D({ (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT }, camera);

    const int x0 = std::max(0, ToTile(topLeft.x) - 1);
    const int y0 = std::max(0, ToTile(topLeft.y) - 1);
    const int x1 = std::min(width_ - 1, ToTile(bottomRight.x) + 1);
    const int y1 = std::min(height_ - 1, ToTile(bottomRight.y) + 1);

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            Color c = GetProperty(GetTile(x, y)).color;
            // Leichte Helligkeitsvariation pro Kachel für einen lebendigeren Boden
            const int shade = (int)(Hash2D(x, y, 1337u) * 12.0f) - 6;
            c.r = (unsigned char)Clamp((float)c.r + (float)shade, 0.0f, 255.0f);
            c.g = (unsigned char)Clamp((float)c.g + (float)shade, 0.0f, 255.0f);
            c.b = (unsigned char)Clamp((float)c.b + (float)shade, 0.0f, 255.0f);
            const int px = x * TILE_SIZE;
            const int py = y * TILE_SIZE;
            DrawRectangle(px, py, TILE_SIZE, TILE_SIZE, c);

            // Dynamische Trampelpfade & gepflasterte Straßen auf dem Boden
            const size_t idx = (size_t)(y * width_ + x);
            if (pavedRoad_[idx]) {
                DrawRectangle(px + 1, py + 1, TILE_SIZE - 2, TILE_SIZE - 2, Color{ 146, 142, 134, 235 });
                DrawRectangle(px + 3, py + 3, 3, 3, Color{ 118, 114, 106, 220 });
                DrawRectangle(px + TILE_SIZE - 5, py + TILE_SIZE - 5, 3, 3, Color{ 168, 164, 156, 220 });
            } else if (footTraffic_[idx] >= 35.0f) {
                unsigned char alpha = (unsigned char)Clamp(125.0f + footTraffic_[idx] * 0.9f, 135.0f, 220.0f);
                DrawRectangle(px + 1, py + 1, TILE_SIZE - 2, TILE_SIZE - 2, Color{ 138, 108, 72, alpha });
                DrawRectangle(px + 4, py + 5, 2, 2, Color{ 112, 86, 56, alpha });
            } else if (footTraffic_[idx] >= 15.0f) {
                DrawRectangle(px + 2, py + 2, TILE_SIZE - 4, TILE_SIZE - 4, Color{ 126, 138, 74, 95 });
            }
        }
    }

    DrawRectangleLines(0, 0, width_ * TILE_SIZE, height_ * TILE_SIZE, DARKGRAY);
}

