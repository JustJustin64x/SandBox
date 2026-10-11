#include "Settlement.h"
#include "world/world.h"
#include "core/event_log.h"

#include <cmath>
#include <algorithm>

// =============================================================================
// RESOURCE INVENTORY
// =============================================================================
int ResourceInventory::Get(CivResourceType type) const {
    switch (type) {
        case CivResourceType::FOOD:   return food;
        case CivResourceType::WOOD:   return wood;
        case CivResourceType::STONE:  return stone;
        case CivResourceType::COPPER: return copper;
        case CivResourceType::IRON:   return iron;
    }
    return 0;
}

void ResourceInventory::Add(CivResourceType type, int amount) {
    if (amount <= 0) return;
    switch (type) {
        case CivResourceType::FOOD:   food += amount; break;
        case CivResourceType::WOOD:   wood += amount; break;
        case CivResourceType::STONE:  stone += amount; break;
        case CivResourceType::COPPER: copper += amount; break;
        case CivResourceType::IRON:   iron += amount; break;
    }
}

bool ResourceInventory::Consume(CivResourceType type, int amount) {
    if (amount <= 0) return true;
    if (!Has(type, amount)) return false;

    switch (type) {
        case CivResourceType::FOOD:   food -= amount; break;
        case CivResourceType::WOOD:   wood -= amount; break;
        case CivResourceType::STONE:  stone -= amount; break;
        case CivResourceType::COPPER: copper -= amount; break;
        case CivResourceType::IRON:   iron -= amount; break;
    }
    return true;
}

bool ResourceInventory::Has(CivResourceType type, int amount) const {
    return Get(type) >= amount;
}

// =============================================================================
// SETTLEMENT
// =============================================================================
Settlement::Settlement(Vector2 centerPosition)
    : centerPos_(centerPosition) {

    // Von Beginn an besitzt das Dorf eine fertige Basishütte für erste Siedler
    SettlementBuilding startHut;
    startHut.id = nextBuildingId_++;
    startHut.type = BuildingType::HUT;
    startHut.name = "Stammeshütte";
    startHut.position = { centerPos_.x - 28.0f, centerPos_.y - 10.0f };
    startHut.progress = 100.0f;
    startHut.isCompleted = true;
    startHut.housingCapacity = 4;
    buildings_.push_back(startHut);
}

int Settlement::HousingCapacity() const {
    int cap = 0;
    for (const auto& b : buildings_) {
        if (b.isCompleted && b.type == BuildingType::HUT) {
            cap += b.housingCapacity;
        }
    }
    return cap;
}

int Settlement::FinishedBuildingCount() const {
    int count = 0;
    for (const auto& b : buildings_) {
        if (b.isCompleted) ++count;
    }
    return count;
}

int Settlement::FinishedBuildingCount(BuildingType type) const {
    int count = 0;
    for (const auto& b : buildings_) {
        if (b.isCompleted && b.type == type) ++count;
    }
    return count;
}

SettlementBuilding* Settlement::GetBuilding(int buildingId) {
    for (auto& b : buildings_) {
        if (b.id == buildingId) return &b;
    }
    return nullptr;
}

Vector2 Settlement::FindRestingSpot(Vector2 fromPos) const {
    // Sucht die nächstgelegene fertige Hütte mit freiem Schlafplatz
    const SettlementBuilding* best = nullptr;
    float bestDist = 1e9f;

    for (const auto& b : buildings_) {
        if (b.isCompleted && b.type == BuildingType::HUT) {
            float d = Vector2Distance(fromPos, b.position);
            if (d < bestDist) {
                bestDist = d;
                best = &b;
            }
        }
    }

    if (best) return best->position;
    return centerPos_; // Notfalls am Dorf-Lagerfeuer ruhen
}

Vector2 Settlement::FindNewBuildingSite(const World& world) const {
    // Sucht einen freien, begehbaren Platz rund um das Dorfzentrum
    static const float offsets[][2] = {
        { -40.0f, -30.0f }, { 40.0f, -30.0f },
        { -50.0f,  20.0f }, { 50.0f,  25.0f },
        {   0.0f, -50.0f }, {  0.0f,  50.0f },
        { -70.0f,   0.0f }, { 70.0f,   0.0f },
        { -60.0f, -50.0f }, { 60.0f, -50.0f }
    };

    for (const auto& off : offsets) {
        Vector2 candidate = { centerPos_.x + off[0], centerPos_.y + off[1] };
        if (!world.IsWalkable(candidate)) continue;

        // Prüfen, ob schon ein Gebäude zu nah an diesem Punkt steht
        bool tooClose = false;
        for (const auto& b : buildings_) {
            if (Vector2Distance(candidate, b.position) < 24.0f) {
                tooClose = true;
                break;
            }
        }
        if (!tooClose) return candidate;
    }

    return { centerPos_.x + 30.0f, centerPos_.y - 30.0f };
}

int Settlement::PlanBuilding(BuildingType type, Vector2 spot, EventLog* log) {
    int woodCost = 0, stoneCost = 0, copperCost = 0;
    CivilizationTech::GetBuildingCosts(type, woodCost, stoneCost, copperCost);

    // Prüfen, ob das Dorf die Baukosten decken kann
    if (!inventory_.Has(CivResourceType::WOOD, woodCost) ||
        !inventory_.Has(CivResourceType::STONE, stoneCost) ||
        !inventory_.Has(CivResourceType::COPPER, copperCost)) {
        return -1;
    }

    // Kosten aus dem Lager entnehmen
    inventory_.Consume(CivResourceType::WOOD, woodCost);
    inventory_.Consume(CivResourceType::STONE, stoneCost);
    if (copperCost > 0) {
        inventory_.Consume(CivResourceType::COPPER, copperCost);
    }

    SettlementBuilding b;
    b.id = nextBuildingId_++;
    b.type = type;
    b.name = CivilizationTech::BuildingToString(type);
    b.position = spot;
    b.progress = 0.0f;
    b.isCompleted = false;
    b.housingCapacity = (type == BuildingType::HUT) ? 3 : 0;
    buildings_.push_back(b);

    // Direkt eine Bauaufgabe für die Baustelle erzeugen
    auto buildTask = std::make_shared<Task>();
    buildTask->id = nextTaskId_++;
    buildTask->type = TaskType::CONSTRUCT_BUILDING;
    buildTask->buildingId = b.id;
    buildTask->targetPosition = spot;
    buildTask->requiredProgress = 100.0f;
    taskQueue_.push_back(buildTask);

    if (log) {
        log->Add(TextFormat("Bau begonnen: %s an (%.0f, %.0f)", b.name.c_str(), spot.x, spot.y), GOLD);
    }

    return b.id;
}

bool Settlement::AddBuildProgress(int buildingId, float amount, EventLog* log) {
    SettlementBuilding* b = GetBuilding(buildingId);
    if (!b || b->isCompleted) return false;

    b->progress += amount;
    if (b->progress >= 100.0f) {
        b->progress = 100.0f;
        b->isCompleted = true;
        if (log) {
            log->Add(TextFormat("Gebäude fertiggestellt: %s!", b->name.c_str()), GREEN);
        }
        return true;
    }
    return false;
}

void Settlement::AnalyzeAndGenerateTasks(const CivilizationTech& tech, const World& world, int livingCreatures, EventLog* log) {
    // -------------------------------------------------------------------------
    // AUFGABEN-ANALYSE & BEDARFSERMITTLUNG (JOB BOARD SYSTEM)
    // -------------------------------------------------------------------------

    // Alte abgeschlossene Aufgaben aufräumen
    taskQueue_.erase(
        std::remove_if(taskQueue_.begin(), taskQueue_.end(),
            [](const std::shared_ptr<Task>& t) { return t->isCompleted; }),
        taskQueue_.end()
    );

    // Wenn bereits genug Aufgaben für alle Kreaturen existieren, nicht überfluten
    if ((int)taskQueue_.size() >= std::max(6, livingCreatures * 2)) {
        return;
    }

    // 1. Priorität: NAHRUNGSERWERB
    // Wenn Nahrung knapp wird (< 30 Einheiten), haben Sammelaufgaben oberste Priorität
    int unassignedFoodTasks = 0;
    for (const auto& t : taskQueue_) {
        if (t->type == TaskType::GATHER_RESOURCE && t->resource == CivResourceType::FOOD && !t->isAssigned) {
            unassignedFoodTasks++;
        }
    }

    if (inventory_.food < 30 && unassignedFoodTasks < 3) {
        auto foodTask = std::make_shared<Task>();
        foodTask->id = nextTaskId_++;
        foodTask->type = TaskType::GATHER_RESOURCE;
        foodTask->resource = CivResourceType::FOOD;
        foodTask->targetPosition = { centerPos_.x + (float)((rand() % 90) - 45), centerPos_.y + (float)((rand() % 90) - 45) };
        foodTask->requiredProgress = 100.0f;
        taskQueue_.push_back(foodTask);
    }

    // 2. Priorität: WOHNRAUM (HÜTTENBAU)
    // Wenn die Bevölkerung die Bettenanzahl übersteigt, plane eine neue Hütte
    int totalHutCapacity = HousingCapacity();
    bool hasActiveHutConstruction = false;
    for (const auto& b : buildings_) {
        if (!b.isCompleted && b.type == BuildingType::HUT) {
            hasActiveHutConstruction = true;
            break;
        }
    }

    if (!hasActiveHutConstruction && livingCreatures >= totalHutCapacity) {
        int wCost = 0, sCost = 0, cCost = 0;
        CivilizationTech::GetBuildingCosts(BuildingType::HUT, wCost, sCost, cCost);
        if (inventory_.wood >= wCost && inventory_.stone >= sCost) {
            Vector2 spot = FindNewBuildingSite(world);
            PlanBuilding(BuildingType::HUT, spot, log);
        }
    }

    // 3. Priorität: TECHNOLOGIE-GEBÄUDE (Schmelzofen in Bronzezeit, Schmiede in Eisenzeit)
    if (tech.CurrentAge() >= Age::BRONZE_AGE && FinishedBuildingCount(BuildingType::SMELTER) == 0) {
        bool smeltingSiteActive = false;
        for (const auto& b : buildings_) {
            if (b.type == BuildingType::SMELTER) { smeltingSiteActive = true; break; }
        }
        if (!smeltingSiteActive) {
            int wCost = 0, sCost = 0, cCost = 0;
            CivilizationTech::GetBuildingCosts(BuildingType::SMELTER, wCost, sCost, cCost);
            if (inventory_.wood >= wCost && inventory_.stone >= sCost) {
                Vector2 spot = FindNewBuildingSite(world);
                PlanBuilding(BuildingType::SMELTER, spot, log);
            }
        }
    }

    if (tech.CurrentAge() >= Age::IRON_AGE && FinishedBuildingCount(BuildingType::FORGE) == 0) {
        bool forgeSiteActive = false;
        for (const auto& b : buildings_) {
            if (b.type == BuildingType::FORGE) { forgeSiteActive = true; break; }
        }
        if (!forgeSiteActive) {
            int wCost = 0, sCost = 0, cCost = 0;
            CivilizationTech::GetBuildingCosts(BuildingType::FORGE, wCost, sCost, cCost);
            if (inventory_.wood >= wCost && inventory_.stone >= sCost && inventory_.copper >= cCost) {
                Vector2 spot = FindNewBuildingSite(world);
                PlanBuilding(BuildingType::FORGE, spot, log);
            }
        }
    }

    // 4. Priorität: MATERIALIEN BESCHAFFEN (Holz, Stein, Kupfer, Eisen)
    auto countUnassigned = [&](CivResourceType res) {
        int c = 0;
        for (const auto& t : taskQueue_) {
            if (t->type == TaskType::GATHER_RESOURCE && t->resource == res && !t->isAssigned) ++c;
        }
        return c;
    };

    // Holz-Auftrag generieren
    if (inventory_.wood < 60 && countUnassigned(CivResourceType::WOOD) < 2) {
        auto t = std::make_shared<Task>();
        t->id = nextTaskId_++;
        t->type = TaskType::GATHER_RESOURCE;
        t->resource = CivResourceType::WOOD;
        t->targetPosition = { centerPos_.x + (float)((rand() % 140) - 70), centerPos_.y + (float)((rand() % 140) - 70) };
        taskQueue_.push_back(t);
    }

    // Stein-Auftrag generieren
    if (inventory_.stone < 50 && countUnassigned(CivResourceType::STONE) < 2) {
        auto t = std::make_shared<Task>();
        t->id = nextTaskId_++;
        t->type = TaskType::GATHER_RESOURCE;
        t->resource = CivResourceType::STONE;
        t->targetPosition = { centerPos_.x + (float)((rand() % 160) - 80), centerPos_.y + (float)((rand() % 160) - 80) };
        taskQueue_.push_back(t);
    }

    // Kupfer-Auftrag (wenn in Bronzezeit freigeschaltet)
    if (tech.IsResourceUnlocked(CivResourceType::COPPER) && inventory_.copper < 40 && countUnassigned(CivResourceType::COPPER) < 2) {
        auto t = std::make_shared<Task>();
        t->id = nextTaskId_++;
        t->type = TaskType::GATHER_RESOURCE;
        t->resource = CivResourceType::COPPER;
        t->targetPosition = { centerPos_.x + (float)((rand() % 180) - 90), centerPos_.y + (float)((rand() % 180) - 90) };
        taskQueue_.push_back(t);
    }

    // Eisen-Auftrag (wenn in Eisenzeit freigeschaltet)
    if (tech.IsResourceUnlocked(CivResourceType::IRON) && inventory_.iron < 40 && countUnassigned(CivResourceType::IRON) < 2) {
        auto t = std::make_shared<Task>();
        t->id = nextTaskId_++;
        t->type = TaskType::GATHER_RESOURCE;
        t->resource = CivResourceType::IRON;
        t->targetPosition = { centerPos_.x + (float)((rand() % 200) - 100), centerPos_.y + (float)((rand() % 200) - 100) };
        taskQueue_.push_back(t);
    }
}

std::shared_ptr<Task> Settlement::RequestTaskForCreature(int creatureId) {
    // Weist die erste freie, nicht abgeschlossene Aufgabe zu
    for (auto& t : taskQueue_) {
        if (!t->isAssigned && !t->isCompleted) {
            t->isAssigned = true;
            t->assignedCreatureId = creatureId;
            return t;
        }
    }
    return nullptr;
}

void Settlement::ReleaseTask(int taskId) {
    for (auto& t : taskQueue_) {
        if (t->id == taskId) {
            t->isAssigned = false;
            t->assignedCreatureId = -1;
            break;
        }
    }
}

void Settlement::CompleteTask(int taskId, CivResourceType deliveredResource, int amount, EventLog* log) {
    for (auto& t : taskQueue_) {
        if (t->id == taskId) {
            t->isCompleted = true;
            t->isAssigned = false;

            if (t->type == TaskType::GATHER_RESOURCE && amount > 0) {
                inventory_.Add(deliveredResource, amount);
                if (log) {
                    log->Add(TextFormat("+%d %s im Lager deponiert", amount, CivilizationTech::ResourceToString(deliveredResource)), LIME);
                }
            }
            break;
        }
    }
}

void Settlement::Draw(float simTime, const CivilizationTech& tech) const {
    (void)tech;
    // 1. Lagerfeuer / Dorfzentrum
    float pulse = std::sin(simTime * 6.0f) * 2.0f;
    DrawCircle((int)centerPos_.x, (int)centerPos_.y, 7.0f + pulse, Fade(ORANGE, 0.7f));
    DrawCircle((int)centerPos_.x, (int)centerPos_.y, 4.0f, YELLOW);
    DrawText("Dorfzentrum", (int)centerPos_.x - 30, (int)centerPos_.y + 12, 11, RAYWHITE);

    // 2. Lager-Bereich visualisieren
    Vector2 stockPos = StockpilePosition();
    DrawRectangle((int)stockPos.x - 8, (int)stockPos.y - 6, 16, 12, Fade(BROWN, 0.6f));
    DrawRectangleLines((int)stockPos.x - 8, (int)stockPos.y - 6, 16, 12, GOLD);

    // 3. Gebäude zeichnen
    for (const auto& b : buildings_) {
        int bx = (int)b.position.x;
        int by = (int)b.position.y;

        if (b.isCompleted) {
            Color buildingColor = DARKBROWN;
            if (b.type == BuildingType::STORAGE) buildingColor = BROWN;
            if (b.type == BuildingType::SMELTER) buildingColor = Color{ 140, 70, 40, 255 }; // Kupfer/Bronze
            if (b.type == BuildingType::FORGE)   buildingColor = Color{ 100, 100, 120, 255 }; // Eisen/Stein

            DrawRectangle(bx - 12, by - 12, 24, 24, buildingColor);
            DrawRectangleLines(bx - 12, by - 12, 24, 24, RAYWHITE);
            DrawText(b.name.c_str(), bx - 20, by - 22, 10, RAYWHITE);
        } else {
            // Baustelle mit Fortschrittsbalken
            DrawRectangle(bx - 12, by - 12, 24, 24, Fade(DARKGRAY, 0.7f));
            DrawRectangleLines(bx - 12, by - 12, 24, 24, YELLOW);

            // Fortschrittsbalken über der Baustelle
            int barW = 24;
            int barH = 4;
            DrawRectangle(bx - 12, by - 18, barW, barH, Fade(BLACK, 0.8f));
            DrawRectangle(bx - 12, by - 18, (int)(barW * (b.progress / 100.0f)), barH, ORANGE);
            DrawText("Baustelle", bx - 18, by - 28, 10, YELLOW);
        }
    }
}

