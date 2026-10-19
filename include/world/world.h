#pragma once

#include "core/common.h"

#include <cstdint>
#include <functional>
#include <vector>

enum TileType {
    TILE_DEEP_WATER = 0,
    TILE_SHALLOW_WATER,
    TILE_SAND,
    TILE_GRASS,
    TILE_FOREST,
    TILE_MOUNTAIN,
    TILE_SNOW,
    TILE_TYPE_COUNT
};

enum class WaterType : uint8_t {
    None = 0,
    Saltwater = 1,
    Freshwater = 2
};

struct TileProperty {
    const char* name;
    Color color;
    bool isWalkable;
    bool isLiquid;
};

// Vollständige Datenstruktur für jede Kachel (Phase 1: 3D-Höhenstufen, Hydrologie & Klippen)
struct TileData {
    TileType type = TILE_DEEP_WATER;
    uint8_t heightLevel = 0;       // 0 = Wasser/Strand, 1 = Flachland, 2 = Hügel/Hochland, 3–5 = Gebirgsstufen, 6 = Schneegipfel
    float elevation = 0.0f;        // 0.0f bis 1.0f (stetige Höhe)
    float moisture = 0.0f;         // 0.0f bis 1.0f (Feuchtigkeit für Wälder)
    WaterType waterType = WaterType::None;
    bool isRiver = false;          // Süßwasser-Fluss
    Vector2 flowDirection = { 0.0f, 0.0f }; // Fluss-Fließrichtung für Strömungs-Visualisierung
    bool isRamp = false;           // Slope / begehbarer Bergaufgang
    bool isCliff = false;          // Steile unpassierbare Klippenwand (Höhenunterschied >= 1 ohne Rampe)
    uint8_t distanceToShore = 0;   // Abstand zum nächsten festen Ufer in Kacheln (per BFS berechnet)
    uint16_t regionId = 0;         // Zusammenhängende Land-/Flachwasser-Region für O(1) Erreichbarkeits-Checks
    bool hasCave = false;          // Natürlicher Höhleneingang in der Klippenwand
};

class EnvironmentalSystem;

// Kachelbasierte Spielwelt inkl. prozeduraler Generierung, 3D-Höhenstufen und Hydrologie.
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

    // --- Kachelzugriff & 3D-Hydrologie (Phase 1) ------------------------------
    bool IsInBounds(int x, int y) const;
    TileType GetTile(int x, int y) const;
    void SetTile(int x, int y, TileType type);
    static const TileProperty& GetProperty(TileType type);

    const TileData& GetTileData(int x, int y) const;
    TileData& GetTileData(int x, int y);

    uint8_t GetHeightLevel(int x, int y) const;
    float GetElevation(int x, int y) const;
    float GetMoisture(int x, int y) const;
    WaterType GetWaterType(int x, int y) const;
    bool IsRiver(int x, int y) const;
    Vector2 GetFlowDirection(int x, int y) const;
    bool IsRamp(int x, int y) const;
    bool IsCliff(int x, int y) const;
    uint8_t GetDistanceToShore(int x, int y) const;
    uint16_t GetRegionId(int x, int y) const;
    bool HasCave(int x, int y) const;
    bool IsNearFreshwater(int tx, int ty, int radius) const;

    // 2.5D Stacked Elevation & Visual Projections (Phase 4)
    float GetVisualYOffset(Vector2 worldPos) const;
    bool GetTileAtRenderPos(Vector2 worldPos, int& outX, int& outY) const;

    // Begehbarkeits- & Nachbarschaftsprüfungen
    bool IsWalkableTile(int x, int y) const;
    bool IsWalkable(Vector2 worldPos) const;
    bool CanTraverseBetween(int fromX, int fromY, int toX, int toY) const;
    bool HasNeighbor(int x, int y, TileType type) const;

    static int ToTile(float pixel) { return (int)std::floor(pixel / (float)TILE_SIZE); }
    static Vector2 TileCenter(int tx, int ty);

    // --- Suche ------------------------------------------------------------------
    bool FindRandomWalkablePosition(Vector2 center, float radiusPx, Vector2& out,
                                    int attempts = 40,
                                    TileType requiredType = TILE_TYPE_COUNT) const;
    bool FindRandomTile(const std::function<bool(int, int)>& predicate, Vector2& out,
                        int attempts = 400) const;
    bool FindNearestWalkableTile(int tx, int ty, int maxRadius, int& outX, int& outY) const;

    // --- Dynamische Wege (Trampelpfade & Gepflasterte Straßen) ----------------
    void AddFootTraffic(int tx, int ty, float amount, bool canPave = false);
    float GetFootTraffic(int tx, int ty) const;
    bool IsPavedRoad(int tx, int ty) const;
    float GetRoadSpeedMultiplier(Vector2 worldPos) const;
    void UpdateRoads(float dt);

    // --- Pfadsuche (Phase 2 optimiert) ---------------------------------------
    float GetMovementCost(int fromX, int fromY, int toX, int toY) const;
    bool FindPath(Vector2 from, Vector2 to, std::vector<Vector2>& outPath) const;
    bool HasLineOfSight(Vector2 a, Vector2 b) const;

    // --- Weltgenerierung (Phase 1) -------------------------------------------
    void RegenerateWorld(unsigned int seed);
    unsigned int CurrentSeed() const { return currentSeed_; }

    // --- Rendering -----------------------------------------------------------
    void Draw(const Camera2D& camera) const;

private:
    void Generate(unsigned int seed);
    void ClassifyWaterAndHydrology();
    void GenerateRivers(unsigned int seed);
    void CalculateDistanceToShore();
    void BuildRampsAndCliffs();
    void PlaceCaves(unsigned int seed);
    void ComputeRegionIds();

    int width_;
    int height_;
    unsigned int currentSeed_ = 4242u;
    std::vector<TileData> tiles_;
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



