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
    int food   = 0;
    int wood   = 0;
    int stone  = 0;
    int planks = 0;   // Veredelt aus Holz an der Werkbank
    int bricks = 0;   // Veredelt aus Stein an der Werkbank
    int tools  = 0;   // Werkzeuge für erhöhte Effizienz
    int huts   = 0;   // Anzahl fertiger Hütten

    // Expeditions-Ausrüstung
    int torches    = 2; // Startausrüstung
    int pickaxes   = 1;
    int provisions = 4;
};

// Der Stamm: TownCenter, Infrastruktur, Inventar, Beziehungen, POIs & Skilltree.
class Tribe {
public:
    explicit Tribe(Vector2 campPosition);

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
    void Draw(float time) const;

    // --- Bau- & Infrastruktur-Hilfen -----------------------------------------
    bool NeedsMoreHuts(int population) const;
    int SleepingCapacity() const { return buildings_.TotalSleepingCapacity(); }
    int EnsureConstructionSite(const World& world, EventLog& log);

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
    bool FindBuildingSpot(const World& world, Vector2& out) const;
    void DrawStockpile() const;

    Vector2 campPos_;
    Stockpile stock_;
    BuildingManager buildings_;
    AgeManager age_;
    RelationshipManager relationships_;
    SocialHierarchy hierarchy_;
    POIManager pois_;
    SkillTree skillTree_;
    float spoilageTimer_ = 0.0f;
};

#endif // TRIBE_H
