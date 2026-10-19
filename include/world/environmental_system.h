#ifndef ENVIRONMENTAL_SYSTEM_H
#define ENVIRONMENTAL_SYSTEM_H

#include "core/common.h"
#include <vector>

class World;
class ResourceManager;
class BuildingManager;
class Caveman;
class EventLog;

// ============================================================================
//  1. Wetter-Typen & Globale Umwelt-Parameter
// ============================================================================
enum class WeatherType {
    Clear = 0,  // Klarer Himmel
    Rain,       // Regen (löscht Feuer langsam, durchnässt Boden, kühlt ab)
    Storm,      // Sturm (starker Wind facht Feuer an, Blitzeinschläge, Regen)
    Fog         // Nebel (reduziert Sichtweite drastisch)
};

const char* WeatherTypeName(WeatherType type);
Color WeatherTypeColor(WeatherType type);

// ============================================================================
//  2. Kachelbasierter Umwelt- & Materialzustand (160x100 Grid)
// ============================================================================
struct TileEnvState {
    // --- Feuer & Verbrennung ---
    bool isBurning = false;
    float fireIntensity = 0.0f;      // 0.0 .. 1.0 (Flammenhöhe & Hitzestrahlung)
    float fireTimer = 0.0f;          // Verbleibende Brenndauer in Sekunden
    float fuel = 1.0f;               // Brennstoff-Vorrat (Gras ~0.45, Wald ~1.0)
    bool scorched = false;           // Ausgebrannt / verkohlter Ascheboden
    float scorchedRecovery = 0.0f;   // Regeneration zurück zu Gras

    // --- Wasser, Nässe & Elektrizität ---
    float wetness = 0.0f;            // 0.0 (trocken) .. 1.0 (durchnässt)
    float electrifiedTimer = 0.0f;   // > 0.0: Wasser steht unter Starkstrom
    float electricIntensity = 0.0f;  // 0.0 .. 1.0 für Blitzbögen-Rendering

    // --- Temperatur & Aggregatzustände (Eis / Frost) ---
    float temperature = 20.0f;       // Lokale Temperatur in °C
    float frost = 0.0f;              // 0.0 .. 1.0 (Vereisungsgrad / Glätte)
    bool frozen = false;             // Wasser zu tragfähigem Eis gefroren
};

// ============================================================================
//  3. Dynamische Physik-Objekte (Boden-Drops, Antigravitation)
// ============================================================================
enum class DropItemType {
    WoodLog = 0,
    StoneChunk,
    CharredWood,
    BerryFood
};

struct GroundDrop {
    DropItemType type = DropItemType::WoodLog;
    Vector2 position = { 0.0f, 0.0f };
    Vector2 velocity = { 0.0f, 0.0f };
    int amount = 1;
    float lifeTimer = 90.0f;

    // Antigravitations-Physik (Schwerelosigkeit)
    float floatHeight = 0.0f;        // Schwebehöhe über dem Boden (Pixel)
    float spiralPhase = 0.0f;        // Rotationswinkel im Schwerefeld
    bool inZeroG = false;
};

struct AntigravityZone {
    Vector2 position = { 0.0f, 0.0f };
    float radius = 38.0f;
    float duration = 25.0f;
    float elapsed = 0.0f;
    bool permanent = false;
};

// ============================================================================
//  4. Umwelt-Partikelsystem (Rauch, Dampf, Funken, Laub, Antigrav-Staub)
// ============================================================================
enum class EnvParticleType {
    Smoke,          // Aufsteigender grauer Rauch über Feuer/Stümpfen
    Steam,          // Weißer Wasserdampf (Feuer trifft auf Wasser/Regen)
    FlameSpark,     // Glühende Feuerfunken im Wind
    ElectricArc,    // Blau-weiße Stromfunken (bei Blitzeinschlag im Sturm)
    WindLeaf,       // Herabfallendes / vom Wind gewehtes Laub
    WindSwirl,      // Weiße Windböen-Streifen
    DebrisChip,     // Holzsplitter / Gesteinsbrocken bei Zerstörung
    AntigravOrb     // Bläulich-violett schwebende Antigravitations-Partikel
};

struct EnvParticle {
    EnvParticleType type = EnvParticleType::Smoke;
    Vector2 position = { 0.0f, 0.0f };
    Vector2 velocity = { 0.0f, 0.0f };
    float life = 1.0f;
    float maxLife = 1.0f;
    float size = 2.0f;
    float heightOffset = 0.0f;
    Color color = WHITE;
};

// ============================================================================
//  5. EnvironmentalSystem – Kachel-Physik, Wetter & Kettenreaktionen
// ============================================================================
class EnvironmentalSystem {
public:
    EnvironmentalSystem(int width, int height);

    void InitDemoPhenomena(World& world);

    // Haupt-Update: Partikel mit 60 FPS, Kachel-Automaten getaktet (alle 0.15s)
    void Update(World& world,
                ResourceManager& resources,
                BuildingManager& buildings,
                std::vector<Caveman>& cavemen,
                EventLog& log,
                float timeOfDay,
                float dt);

    // --- Spieler- & Sandbox-Eingriffe -----------------------------------------
    void IgniteTile(World& world, int tx, int ty, float intensity = 0.85f);
    void IgniteRadius(World& world, Vector2 worldPos, float radiusPx, EventLog& log);
    void SpawnAntigravityAnomaly(Vector2 worldPos, float radiusPx, float duration, bool permanent, EventLog& log);
    void TriggerLightningStrike(World& world, ResourceManager& resources, BuildingManager& buildings,
                                std::vector<Caveman>& cavemen, Vector2 worldPos, EventLog& log);

    void SetWeather(WeatherType weather, EventLog* log = nullptr);
    void CycleWeather(EventLog& log);
    WeatherType GetWeather() const { return weather_; }
    Vector2 GetWindDirection() const { return windDir_; }
    float GetWindStrength() const { return windStrength_; }
    float GetGlobalTemperature() const { return globalTemp_; }

    // --- Abfragen für Pathfinding (A*), Utility AI & Bewegung -----------------
    const TileEnvState& GetState(int tx, int ty) const;
    TileEnvState& GetStateMutable(int tx, int ty);

    bool IsTileDangerous(int tx, int ty) const;
    bool IsHazardNear(Vector2 worldPos, float radiusPx, Vector2& outHazardCenter) const;
    float GetTileTraversalCost(int tx, int ty) const;
    float GetMovementSpeedMultiplier(Vector2 worldPos) const;
    float GetEnvironmentalWarmthDelta(Vector2 worldPos) const;
    float GetVisionMultiplier( float timeOfDay, bool hasTorch) const;

    // --- Physische Ressourcen-Drops aufheben ----------------------------------
    int FindNearestGroundDrop(Vector2 from, float maxRadiusPx) const;
    bool CollectGroundDrop(int index, DropItemType& outType, int& outAmount);
    void SpawnGroundDrop(DropItemType type, Vector2 pos, int amount);

    // --- Rendering (Welt-Ebene & Wetter-Overlay) ------------------------------
    void DrawTileOverlays(const World& world, const Camera2D& camera, float simTime) const;
    void DrawWorldEffects(const World& world, const Camera2D& camera, float simTime) const;
    void DrawWeatherOverlay(int screenW, int screenH, float simTime) const;

private:
    void UpdateGridCellularAutomata(World& world,
                                    ResourceManager& resources,
                                    BuildingManager& buildings,
                                    std::vector<Caveman>& cavemen,
                                    EventLog& log,
                                    float timeOfDay,
                                    float tickDt);
    void PropagateElectricity(const World& world);
    void UpdateParticles(const World& world, float dt);
    void UpdateGroundDrops(float dt);
    void SpawnParticle(EnvParticleType type, Vector2 pos, Vector2 vel, float life, float size, Color col);

    bool IsFlammableTile(const World& world, int tx, int ty) const;

    int width_;
    int height_;
    std::vector<TileEnvState> grid_;
    std::vector<TileEnvState> nextGrid_;

    // Wetter & Klima
    WeatherType weather_ = WeatherType::Clear;
    float weatherTimer_ = 75.0f;
    Vector2 windDir_ = { 0.85f, -0.35f };
    float windStrength_ = 0.35f;
    float windAngle_ = -0.4f;
    float globalTemp_ = 18.0f;

    // Tick-Steuerung für zelluläre Automaten
    float gridTickAccumulator_ = 0.0f;
    float lightningCooldown_ = 8.0f;
    float wildfireLogCooldown_ = 0.0f;
    float collateralLogCooldown_ = 0.0f;

    // Objekte & Partikel
    std::vector<GroundDrop> drops_;
    std::vector<AntigravityZone> antigravZones_;
    std::vector<EnvParticle> particles_;
};

#endif // ENVIRONMENTAL_SYSTEM_H

