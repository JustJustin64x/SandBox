#ifndef TRIBE_H
#define TRIBE_H

#include "common.h"
#include "building.h"
#include "age_manager.h"
#include "relationship.h"
#include "social_structure.h"
#include "poi.h"
#include "skill_tree.h"

#include <vector>

class World;
class Caveman;
class EventLog;

// Globales Stammes-Lager (Inventar für Rohstoffe, veredelte Güter & Expeditions-Ausrüstung).
struct Stockpile {
    int food       = 0;
    int cookedFood = 2; // Am Feuer gebratene / gekochte Mahlzeiten (nahrhafter & länger haltbar)
    int wood       = 0;
    int stone      = 0;
    int planks     = 0; // Veredelt aus Holz an der Werkbank
    int bricks     = 0; // Veredelt aus Stein an der Werkbank / im Lehmofen
    int tools      = 0; // Werkzeuge für erhöhte Effizienz
    int huts       = 0; // Anzahl fertiger Hütten
    int hides      = 2; // Tierfelle / Rohleder aus der Jagd (für Kleidung)
    int bones      = 2; // Tierknochen aus der Jagd (für Nadeln, Speere & Bögen)

    // Expeditions-Ausrüstung
    int torches    = 2; // Startausrüstung
    int pickaxes   = 1;
    int provisions = 4;
};

// Der Stamm: TownCenter, Infrastruktur, Inventar, Beziehungen, POIs & Skilltree.
class Tribe {
public:
    explicit Tribe(Vector2 campPosition);

    int Id() const { return id_; }
    const std::string& Name() const { return name_; }
    Color FactionColor() const { return factionColor_; }
    void SetIdentity(int id, const std::string& name, Color color) {
        id_ = id;
        name_ = name;
        factionColor_ = color;
    }

    void InitWorld(const World& world);

    Stockpile& Stock() { return stock_; }
    const Stockpile& Stock() const { return stock_; }

    BuildingManager& Buildings() { return buildings_; }
    const BuildingManager& Buildings() const { return buildings_; }

    AgeManager& Age() { return age_; }
    const AgeManager& Age() const { return age_; }

    RelationshipManager& Relationships() { return relationships_; }
    const RelationshipManager& Relationships() const { return relationships_; }

    SocialHierarchy& Hierarchy() { return hierarchy_; }
    const SocialHierarchy& Hierarchy() const { return hierarchy_; }

    POIManager& POIs() { return pois_; }
    const POIManager& POIs() const { return pois_; }

    SkillTree& Skills() { return skillTree_; }
    const SkillTree& Skills() const { return skillTree_; }

    Vector2 CampPosition() const { return campPos_; }
    Vector2 StockpilePosition() const { return { campPos_.x + 22.0f, campPos_.y + 2.0f }; }

    void Update(const World& world, const std::vector<Caveman>& cavemen, int population, float dt, EventLog& log);
    void Draw(float time, const World* world = nullptr) const;

    // --- Autonomer Siedlungsplaner (Stufe 1: Zero-Player-Bauentwicklung) -----
    struct SettlementPlan {
        bool active = false;
        bool isUpgrade = false;
        BuildingType type = BuildingType::Hut;
        int upgradeBuildingId = -1;
        Vector2 buildCenter = { 0.0f, 0.0f };
        ResourceCost requiredCost = { 0, 0, 0, 0 };
        std::string description;
    };

    bool NeedsMoreHuts(int population) const;
    int SleepingCapacity() const { return buildings_.TotalSleepingCapacity(); }
    int EnsureConstructionSite(const World& world, EventLog& log);
    const SettlementPlan& CurrentPlan() const { return planner_; }
    ResourceCost GetPlannedResourceDeficit() const;
    bool FindBuildingSpotAround(const World& world, Vector2 center, Vector2& out) const;

    // --- Territorien-Visualisierung (Stufe 4: Fraktions-Overlay) -------------
    void DrawTerritoryOverlay(const World& world, float simTime) const;

    // --- Veredelung an der Werkbank ------------------------------------------
    bool CanCraftPlanks() const { return stock_.wood >= 2; }
    bool CanCraftBricks() const { return stock_.stone >= 2; }
    void CraftPlank() { stock_.wood -= 2; stock_.planks++; }
    void CraftBrick() { stock_.stone -= 2; stock_.bricks++; }

    // --- Ausrüstungs-Fertigung -----------------------------------------------
    bool CanCraftTorch() const { return stock_.wood >= 1; }
    bool CanCraftPickaxe() const { return stock_.wood >= 1 && stock_.stone >= 1; }
    void CraftTorch() { if (stock_.wood >= 1) { stock_.wood--; stock_.torches++; } }
    void CraftPickaxe() { if (stock_.wood >= 1 && stock_.stone >= 1) { stock_.wood--; stock_.stone--; stock_.pickaxes++; } }

private:
    void UpdateSettlementPlanner(const World& world, const std::vector<Caveman>& cavemen, int population, float dt, EventLog& log);
    bool FindBuildingSpot(const World& world, Vector2& out) const;
    void DrawStockpile(const World* world = nullptr) const;

    Vector2 campPos_;
    int id_ = 1;
    std::string name_ = "Sonnen-Clan";
    Color factionColor_ = Color{ 215, 50, 50, 255 };
    Stockpile stock_;
    BuildingManager buildings_;
    AgeManager age_;
    RelationshipManager relationships_;
    SocialHierarchy hierarchy_;
    POIManager pois_;
    SkillTree skillTree_;
    SettlementPlan planner_;
    float plannerTimer_ = 0.0f;
    float spoilageTimer_ = 0.0f;
};

#endif // TRIBE_H

