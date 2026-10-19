#pragma once

#include "core/common.h"

#include <functional>
#include <vector>

enum TileType {
    TILE_DEEP_WATER = 0,
    TILE_SHALLOW_WATER,
    TILE_SAND,
    TILE_GRASS,
    TILE_FOREST,
    TILE_MOUNTAIN,
    TILE_TYPE_COUNT
};

struct TileProperty {
    const char* name;
    Color color;
    bool isWalkable;
    bool isLiquid;
};

class EnvironmentalSystem;

// Kachelbasierte Spielwelt inkl. Generierung, Begehbarkeit und A*-Pfadsuche.
class World {
public:
    World(int width, int height, unsigned int seed);

    int Width() const  { return width_; }
    int Height() const { return height_; }
    float PixelWidth() const  { return (float)(width_ * TILE_SIZE); }
    float PixelHeight() const { return (float)(height_ * TILE_SIZE); }
    Vector2 CenterPosition() const;

    void SetEnvironmentalSystem(const EnvironmentalSystem* env) { env_ = env; }
    const EnvironmentalSystem* Env() const { return env_; }

    // --- Kachelzugriff --------------------------------------------------------
    bool IsInBounds(int x, int y) const;
    TileType GetTile(int x, int y) const;
    void SetTile(int x, int y, TileType type);
    static const TileProperty& GetProperty(TileType type);

    bool IsWalkableTile(int x, int y) const;
    bool IsWalkable(Vector2 worldPos) const;
    bool HasNeighbor(int x, int y, TileType type) const;

    static int ToTile(float pixel) { return (int)std::floor(pixel / (float)TILE_SIZE); }
    static Vector2 TileCenter(int tx, int ty);

    // --- Suche ------------------------------------------------------------------
    // Zufällige begehbare Position im Radius (optional nur auf einem Kacheltyp).
    bool FindRandomWalkablePosition(Vector2 center, float radiusPx, Vector2& out,
                                    int attempts = 40,
                                    TileType requiredType = TILE_TYPE_COUNT) const;
    // Zufällige Kachel, die ein Prädikat erfüllt (gibt Kachelmitte zurück).
    bool FindRandomTile(const std::function<bool(int, int)>& predicate, Vector2& out,
                        int attempts = 400) const;
    bool FindNearestWalkableTile(int tx, int ty, int maxRadius, int& outX, int& outY) const;

    // --- Dynamische Wege (Trampelpfade & Gepflasterte Straßen) ----------------
    void AddFootTraffic(int tx, int ty, float amount, bool canPave = false);
    float GetFootTraffic(int tx, int ty) const;
    bool IsPavedRoad(int tx, int ty) const;
    float GetRoadSpeedMultiplier(Vector2 worldPos) const;
    void UpdateRoads(float dt);

    // --- Pfadsuche ---------------------------------------------------------------
    // A* auf dem Kachelraster (8 Richtungen, keine Eckabkürzungen) mit
    // anschließender Glättung. outPath enthält Wegpunkte ohne Startposition.
    bool FindPath(Vector2 from, Vector2 to, std::vector<Vector2>& outPath) const;
    bool HasLineOfSight(Vector2 a, Vector2 b) const;

    // --- Rendering ---------------------------------------------------------------
    void Draw(const Camera2D& camera) const;

private:
    void Generate(unsigned int seed);

    int width_;
    int height_;
    std::vector<TileType> tiles_;
    std::vector<float> footTraffic_;
    std::vector<unsigned char> pavedRoad_;
    const EnvironmentalSystem* env_ = nullptr;

    // Wiederverwendete A*-Puffer (Stempel-Technik vermeidet Neuinitialisierung).
    mutable std::vector<float> gScore_;
    mutable std::vector<int> cameFrom_;
    mutable std::vector<unsigned int> openStamp_;
    mutable std::vector<unsigned int> closedStamp_;
    mutable unsigned int stamp_ = 0;
};


