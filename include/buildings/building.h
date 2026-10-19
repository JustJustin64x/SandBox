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
    Watchtower        // Wachturm / Signalfeuer (Sichtbereich, Kältewarnung)
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

    bool IsAvailableShelter() const {
        return finished && !underUpgrade && (type == BuildingType::Hut || type == BuildingType::TownCenter) && occupants < capacity;
    }

    bool NeedsWork() const {
        return !finished || underUpgrade;
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

    int PlaceBuilding(BuildingType type, Vector2 pos, struct Stockpile& stock, EventLog& log);
    bool StartUpgrade(int buildingId, struct Stockpile& stock, const AgeManager& age, EventLog& log);
    bool AddBuildProgress(int buildingId, float amount, struct Stockpile& stock, EventLog& log);

    // Abfragen für KI
    int FindBestShelter(Vector2 from) const;
    int FindActiveConstructionSite(Vector2 from) const;
    int FindCraftBench(Vector2 from) const;
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
    void DrawTownCenter(const Building& b, float simTime) const;
    void DrawHut(const Building& b, float simTime) const;
    void DrawPantry(const Building& b) const;
    void DrawCraftBench(const Building& b) const;
    void DrawWatchtower(const Building& b, float simTime) const;

    std::vector<Building> buildings_;
    int nextId_ = 1;
};

#endif // BUILDING_H

