#ifndef BUILDING_H
#define BUILDING_H

#include "common.h"
#include <string>
#include <vector>

class World;
class EventLog;
class AgeManager;

enum class BuildingType {
    TownCenter = 0,   // Stammesplatz / Langhaus / Rathaus
    Hut,              // Zelt -> Feste Holzhütte -> Steinhaus
    Pantry,           // Vorratskammer (reduziert Verderb)
    CraftBench,       // Werkbank (Veredelung Holz->Balken, Stein->Ziegel)
    Watchtower,       // Wachturm / Signalfeuer (Sichtbereich, Kältewarnung)
    Kiln,             // Lehmofen / Rennofen (Keramik, Ziegelbrennen & Erzverhüttung)
    FarmPlot,         // Ackerfeld (Aussaat -> Grüne Halme -> Reifes Getreide)
    AnimalPen,        // Tiergehege (Domestizierte Herdentiere für Fleisch, Fell & Knochen)
    Monument,         // Totempfahl -> Megalith-Steinkreis -> Tempel-Altar (Kultur & Kohäsion)
    Grave             // Ahnengrab / Hügelgrab für verstorbene Stammesmitglieder
};

const char* BuildingTypeName(BuildingType type, int tier);

struct ResourceCost {
    int wood = 0;
    int stone = 0;
    int planks = 0;
    int bricks = 0;
};

struct Building {
    int id = -1;
    BuildingType type = BuildingType::Hut;
    int tier = 1;                 // Stufe 1, 2 oder 3
    Vector2 position = { 0, 0 };
    float progress = 0.0f;        // 0..100%
    float hp = 100.0f;            // Struktur-Integrität (für Feuer/Wetter-Verschleiß)
    float maxHp = 100.0f;
    float fuel = 100.0f;          // 0..100% Brennstoff (für Lagerfeuer / TownCenter & Kiln)
    float cropGrowth = 0.0f;      // 0..100% Wachstums-/Erntezyklus (für FarmPlot & AnimalPen)
    std::string customLabel;      // z.B. Name des Verstorbenen bei einem Ahnengrab (Grave)
    bool finished = false;
    bool underUpgrade = false;

    // Belegungs- & Arbeitersteuerung
    int builders = 0;
    int maxBuilders = 2;
    int occupants = 0;            // Aktuell Schläfer oder Handwerker
    int capacity = 2;             // Schlafplätze oder Arbeitsplätze

    // Effekte
    float warmthRadius = 0.0f;    // Radius in dem Wärme abgegeben wird
    float warmthEfficiency = 1.0f;// Isolationswert
    float viewRange = 60.0f;      // Wachturm-Sichtbereich

    bool HasActiveFire() const {
        if (!finished) return false;
        if (type == BuildingType::TownCenter || type == BuildingType::Kiln) {
            return fuel > 0.5f;
        }
        return warmthRadius > 0.0f;
    }

    bool NeedsFuel() const {
        return finished && (type == BuildingType::TownCenter || type == BuildingType::Kiln) && fuel < 55.0f;
    }

    bool NeedsFarmTending() const {
        return finished && !underUpgrade &&
               (type == BuildingType::FarmPlot || type == BuildingType::AnimalPen) &&
               (cropGrowth < 20.0f || cropGrowth >= 90.0f);
    }

    bool IsHarvestReady() const {
        return finished && !underUpgrade &&
               (type == BuildingType::FarmPlot || type == BuildingType::AnimalPen) &&
               cropGrowth >= 90.0f;
    }

    bool IsAvailableShelter() const {
        return finished && !underUpgrade && hp > 20.0f &&
               (type == BuildingType::Hut || type == BuildingType::TownCenter) && occupants < capacity;
    }

    bool NeedsWork() const {
        if (type == BuildingType::Grave) return false;
        return !finished || underUpgrade || (finished && hp < maxHp - 8.0f);
    }

    std::string DisplayName() const;
    ResourceCost GetBuildCost() const;
    ResourceCost GetUpgradeCost() const;
    bool CanUpgrade(int currentAge) const;
};

class BuildingManager {
public:
    BuildingManager();

    void InitDefaultBuildings(Vector2 campPos);
    void Update(float dt, bool isBadWeather, struct Stockpile& stock, EventLog& log);

    int PlaceBuilding(BuildingType type, Vector2 pos, struct Stockpile& stock, EventLog& log);
    int SpawnGrave(Vector2 pos, const std::string& deceasedName, int currentEraIdx, EventLog& log);
    bool StartUpgrade(int buildingId, struct Stockpile& stock, const AgeManager& age, EventLog& log);
    bool AddBuildProgress(int buildingId, float amount, struct Stockpile& stock, EventLog& log);

    // Abfragen für KI
    int FindBestShelter(Vector2 from) const;
    int FindActiveConstructionSite(Vector2 from) const;
    int FindCraftBench(Vector2 from) const;
    int FindFireNeedingFuel(Vector2 from) const;
    int FindFarmOrPenNeedingWork(Vector2 from) const;
    int FindMonumentOrGrave(Vector2 from) const;
    float GetWarmthAt(Vector2 pos) const;

    // Getters & Verwaltung
    Building* Get(int id);
    const Building* Get(int id) const;
    std::vector<Building>& All() { return buildings_; }
    const std::vector<Building>& All() const { return buildings_; }

    int GetBuildingCount(BuildingType type, bool finishedOnly = true) const;
    int GetMaxTier(BuildingType type) const;
    int TotalSleepingCapacity() const;

    void Draw(float simTime) const;

private:
    void DrawBuilding(const Building& b, float simTime) const;
    void DrawConstructionSite(const Building& b) const;
    void DrawTownCenter(const Building& b, float simTime) const;
    void DrawHut(const Building& b, float simTime) const;
    void DrawPantry(const Building& b) const;
    void DrawCraftBench(const Building& b) const;
    void DrawWatchtower(const Building& b, float simTime) const;
    void DrawKiln(const Building& b, float simTime) const;
    void DrawFarmPlot(const Building& b, float simTime) const;
    void DrawAnimalPen(const Building& b, float simTime) const;
    void DrawMonument(const Building& b, float simTime) const;
    void DrawGrave(const Building& b, float simTime) const;

    std::vector<Building> buildings_;
    int nextId_ = 1;
};

#endif // BUILDING_H

