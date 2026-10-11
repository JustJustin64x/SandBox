#pragma once

#include "core/common.h"
#include "CivilizationTech.h"

#include <vector>
#include <memory>
#include <string>

// Forward Declarations zur Vermeidung zirkulärer Abhängigkeiten
class Creature;
class World;
class EventLog;

// =============================================================================
// AUFGABEN-TYPEN (JOB BOARD SYSTEM)
// =============================================================================
enum class TaskType {
    GATHER_RESOURCE,    // Rohstoff in der Spielwelt abbauen und ins Lager tragen
    CONSTRUCT_BUILDING  // An einer Baustelle arbeiten bis das Gebäude fertig ist
};

// Eine konkrete Aufgabe, die einer Creature zugewiesen werden kann
struct Task {
    int id = 0;
    TaskType type = TaskType::GATHER_RESOURCE;
    CivResourceType resource = CivResourceType::FOOD;
    int buildingId = -1;              // Nur gültig bei CONSTRUCT_BUILDING
    Vector2 targetPosition = { 0.0f, 0.0f };
    float currentProgress = 0.0f;     // Fortschritt bei dieser Aufgabe (0..100)
    float requiredProgress = 100.0f;
    bool isAssigned = false;          // True, wenn ein Creature daran arbeitet
    bool isCompleted = false;         // True, wenn Aufgabe abgeschlossen
    int assignedCreatureId = -1;
};

// =============================================================================
// GEBÄUDE-STRUKTUR
// =============================================================================
struct SettlementBuilding {
    int id = 0;
    BuildingType type = BuildingType::HUT;
    std::string name;
    Vector2 position = { 0.0f, 0.0f };
    float progress = 0.0f;            // Baufortschritt (0..100 %)
    bool isCompleted = false;         // Fertiggestellt?
    int housingCapacity = 2;          // Wie viele Schlafplätze dieses Gebäude bietet
    int currentSleepers = 0;          // Aktuell belegte Plätze
};

// =============================================================================
// ZENTRALES DORFLAGER (RESSOURCEN-INVENTAR)
// =============================================================================
struct ResourceInventory {
    int food   = 30; // Startvorrat Nahrung
    int wood   = 20; // Startvorrat Holz
    int stone  = 15; // Startvorrat Stein
    int copper = 0;  // Ab Bronzezeit
    int iron   = 0;  // Ab Eisenzeit

    int Get(CivResourceType type) const;
    void Add(CivResourceType type, int amount);
    bool Consume(CivResourceType type, int amount);
    bool Has(CivResourceType type, int amount) const;
};

// =============================================================================
// SIEDLUNGS-KLASSE (SETTLEMENT)
// =============================================================================
class Settlement {
public:
    explicit Settlement(Vector2 centerPosition);

    // --- Lager-Zugriff -------------------------------------------------------
    ResourceInventory& Inventory()             { return inventory_; }
    const ResourceInventory& Inventory() const { return inventory_; }

    // --- Positionen ----------------------------------------------------------
    Vector2 CenterPosition() const    { return centerPos_; }
    Vector2 StockpilePosition() const { return { centerPos_.x + 22.0f, centerPos_.y + 4.0f }; }

    // --- Aufgabenverwaltung (Job Board) --------------------------------------
    // Analysiert Lagerbestände & Infrastruktur und generiert neue Aufträge
    void AnalyzeAndGenerateTasks(const CivilizationTech& tech, const World& world, int livingCreatures, EventLog* log = nullptr);

    // Weist einer freien Creature die nächste sinnvolle Aufgabe zu
    std::shared_ptr<Task> RequestTaskForCreature(int creatureId);

    // Gibt eine Aufgabe frei (z.B. wenn Creature hungrig wird oder stirbt)
    void ReleaseTask(int taskId);

    // Schließt eine Aufgabe ab und lagert Ressourcen ein
    void CompleteTask(int taskId, CivResourceType deliveredResource, int amount, EventLog* log = nullptr);

    const std::vector<std::shared_ptr<Task>>& TaskQueue() const { return taskQueue_; }

    // --- Gebäude-Verwaltung --------------------------------------------------
    int HousingCapacity() const;
    int FinishedBuildingCount() const;
    int FinishedBuildingCount(BuildingType type) const;
    Vector2 FindRestingSpot(Vector2 fromPos) const;

    // Erzeugt einen Bauauftrag für ein neues Gebäude (zieht Ressourcen ab)
    int PlanBuilding(BuildingType type, Vector2 spot, EventLog* log = nullptr);

    // Fügt Baufortschritt hinzu; true wenn Gebäude gerade fertig wurde
    bool AddBuildProgress(int buildingId, float amount, EventLog* log = nullptr);

    const std::vector<SettlementBuilding>& Buildings() const { return buildings_; }
    SettlementBuilding* GetBuilding(int buildingId);

    // --- Zeichnen / Debug ----------------------------------------------------
    void Draw(float simTime, const CivilizationTech& tech) const;

private:
    Vector2 FindNewBuildingSite(const World& world) const;

    Vector2 centerPos_;
    ResourceInventory inventory_;
    std::vector<SettlementBuilding> buildings_;
    std::vector<std::shared_ptr<Task>> taskQueue_;

    int nextTaskId_ = 1;
    int nextBuildingId_ = 1;
};

