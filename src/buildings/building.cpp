#include "building.h"
#include "tribe.h"
#include "age_manager.h"
#include "event_log.h"
#include <cmath>
#include <algorithm>

const char* BuildingTypeName(BuildingType type, int tier) {
    switch (type) {
        case BuildingType::TownCenter:
            if (tier == 1) return "Feuerkreis";
            if (tier == 2) return "Langhaus";
            return "Rathaus";
        case BuildingType::Hut:
            if (tier == 1) return "Notunterkunft";
            if (tier == 2) return "Feste Holzhütte";
            return "Steinhaus";
        case BuildingType::Pantry:
            return "Vorratskammer";
        case BuildingType::CraftBench:
            return "Werkbank";
        case BuildingType::Watchtower:
            return "Wachturm";
    }
    return "Gebäude";
}

std::string Building::DisplayName() const {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s (Stufe %d)", BuildingTypeName(type, tier), tier);
    return std::string(buf);
}

ResourceCost Building::GetBuildCost() const {
    switch (type) {
        case BuildingType::TownCenter: return { 6, 4, 0, 0 };
        case BuildingType::Hut:        return { 6, 3, 0, 0 };
        case BuildingType::Pantry:     return { 10, 6, 2, 0 };
        case BuildingType::CraftBench: return { 8, 4, 0, 0 };
        case BuildingType::Watchtower: return { 12, 8, 4, 0 };
    }
    return { 5, 5, 0, 0 };
}

ResourceCost Building::GetUpgradeCost() const {
    if (type == BuildingType::TownCenter) {
        if (tier == 1) return { 14, 10, 6, 0 };
        if (tier == 2) return { 20, 16, 12, 8 };
    } else if (type == BuildingType::Hut) {
        if (tier == 1) return { 8, 4, 4, 0 };
        if (tier == 2) return { 10, 10, 6, 6 };
    }
    return { 20, 20, 10, 10 };
}

bool Building::CanUpgrade(int currentAge) const {
    if (underUpgrade || !finished) return false;
    if (tier >= 3) return false;
    // Tier 2 erfordert mind. Bronzezeit, Tier 3 mind. Eisenzeit
    if (tier == 1 && currentAge < 1) return false;
    if (tier == 2 && currentAge < 2) return false;
    return true;
}

// ============================================================================
//  BuildingManager
// ============================================================================
BuildingManager::BuildingManager() {}

void BuildingManager::InitDefaultBuildings(Vector2 campPos) {
    buildings_.clear();
    nextId_ = 1;

    // Stadtzentrum (Stammesplatz / Feuerkreis)
    Building tc;
    tc.id = nextId_++;
    tc.type = BuildingType::TownCenter;
    tc.tier = 1;
    tc.position = campPos;
    tc.progress = 100.0f;
    tc.finished = true;
    tc.warmthRadius = 55.0f;
    tc.capacity = 2;
    buildings_.push_back(tc);
}

Building* BuildingManager::Get(int id) {
    for (auto& b : buildings_) {
        if (b.id == id) return &b;
    }
    return nullptr;
}

const Building* BuildingManager::Get(int id) const {
    for (const auto& b : buildings_) {
        if (b.id == id) return &b;
    }
    return nullptr;
}

int BuildingManager::GetBuildingCount(BuildingType type, bool finishedOnly) const {
    int count = 0;
    for (const auto& b : buildings_) {
        if (b.type == type && (!finishedOnly || b.finished)) {
            count++;
        }
    }
    return count;
}

int BuildingManager::GetMaxTier(BuildingType type) const {
    int maxTier = 0;
    for (const auto& b : buildings_) {
        if (b.type == type && b.finished) {
            maxTier = std::max(maxTier, b.tier);
        }
    }
    return maxTier;
}

int BuildingManager::TotalSleepingCapacity() const {
    int total = 0;
    for (const auto& b : buildings_) {
        if (b.finished && (b.type == BuildingType::Hut || b.type == BuildingType::TownCenter)) {
            total += b.capacity;
        }
    }
    return total;
}

int BuildingManager::PlaceBuilding(BuildingType type, Vector2 pos, Stockpile& stock, EventLog& log) {
    Building b;
    b.id = nextId_++;
    b.type = type;
    b.tier = 1;
    b.position = pos;
    b.progress = 0.0f;
    b.finished = false;
    b.underUpgrade = false;

    if (type == BuildingType::Hut) {
        b.capacity = 2;
        b.warmthRadius = 25.0f;
    } else if (type == BuildingType::Pantry) {
        b.capacity = 0;
    } else if (type == BuildingType::CraftBench) {
        b.capacity = 1; // 1 Handwerker
    } else if (type == BuildingType::Watchtower) {
        b.capacity = 1; // 1 Wächter
        b.viewRange = 120.0f;
    }

    ResourceCost cost = b.GetBuildCost();
    if (stock.wood < cost.wood || stock.stone < cost.stone ||
        stock.planks < cost.planks || stock.bricks < cost.bricks) {
        return -1;
    }

    stock.wood -= cost.wood;
    stock.stone -= cost.stone;
    stock.planks -= cost.planks;
    stock.bricks -= cost.bricks;

    buildings_.push_back(b);
    log.Add(TextFormat("Baustelle für %s angelegt!", BuildingTypeName(type, 1)), { 240, 200, 90, 255 });
    return b.id;
}

bool BuildingManager::StartUpgrade(int buildingId, Stockpile& stock, const AgeManager& age, EventLog& log) {
    Building* b = Get(buildingId);
    if (!b || !b->CanUpgrade(age.EraIndex())) return false;

    ResourceCost cost = b->GetUpgradeCost();
    if (stock.wood < cost.wood || stock.stone < cost.stone ||
        stock.planks < cost.planks || stock.bricks < cost.bricks) {
        return false;
    }

    stock.wood -= cost.wood;
    stock.stone -= cost.stone;
    stock.planks -= cost.planks;
    stock.bricks -= cost.bricks;

    b->underUpgrade = true;
    b->progress = 0.0f;
    log.Add(TextFormat("Ausbau von %s auf Stufe %d gestartet!", BuildingTypeName(b->type, b->tier), b->tier + 1),
            { 240, 180, 80, 255 });
    return true;
}

bool BuildingManager::AddBuildProgress(int buildingId, float amount, Stockpile& stock, EventLog& log) {
    Building* b = Get(buildingId);
    if (!b || (!b->underUpgrade && b->finished)) return false;

    b->progress = std::min(100.0f, b->progress + amount);
    if (b->progress < 100.0f) return false;

    if (b->underUpgrade) {
        b->tier++;
        b->underUpgrade = false;
        b->progress = 100.0f;

        // Upgrade-Vorteile anwenden
        if (b->type == BuildingType::TownCenter) {
            b->warmthRadius = 55.0f + b->tier * 20.0f;
            b->capacity = 2 + b->tier;
        } else if (b->type == BuildingType::Hut) {
            b->capacity = 2 + (b->tier - 1); // Stufe 1: 2, Stufe 2: 3, Stufe 3: 4
            b->warmthEfficiency = 1.0f + (b->tier - 1) * 0.5f;
            b->warmthRadius = 25.0f + b->tier * 10.0f;
        }

        log.Add(TextFormat("Erweiterung abgeschlossen: %s!", b->DisplayName().c_str()), { 120, 230, 120, 255 });
    } else {
        b->finished = true;
        b->progress = 100.0f;
        if (b->type == BuildingType::Hut) {
            stock.huts++;
        }
        log.Add(TextFormat("Bau abgeschlossen: %s!", b->DisplayName().c_str()), { 120, 230, 120, 255 });
    }
    return true;
}

int BuildingManager::FindBestShelter(Vector2 from) const {
    int bestId = -1;
    float bestScore = -1.0f;

    for (const auto& b : buildings_) {
        if (!b.IsAvailableShelter()) continue;
        float dist = Vector2Distance(from, b.position);
        // Bevorzuge höhere Stufen und nähere Gebäude
        float score = (float)(b.tier * 200.0f) - dist;
        if (score > bestScore) {
            bestScore = score;
            bestId = b.id;
        }
    }
    return bestId;
}

int BuildingManager::FindActiveConstructionSite(Vector2 from) const {
    int bestId = -1;
    float bestDist = 1e9f;

    for (const auto& b : buildings_) {
        if (!b.NeedsWork()) continue;
        if (b.builders >= b.maxBuilders) continue;

        float dist = Vector2Distance(from, b.position);
        // Stadtzentrum-Upgrades haben Priorität
        if (b.type == BuildingType::TownCenter) dist *= 0.5f;

        if (dist < bestDist) {
            bestDist = dist;
            bestId = b.id;
        }
    }
    return bestId;
}

int BuildingManager::FindCraftBench(Vector2 from) const {
    int bestId = -1;
    float bestDist = 1e9f;

    for (const auto& b : buildings_) {
        if (b.type != BuildingType::CraftBench || !b.finished) continue;
        if (b.occupants >= b.capacity) continue;

        float dist = Vector2Distance(from, b.position);
        if (dist < bestDist) {
            bestDist = dist;
            bestId = b.id;
        }
    }
    return bestId;
}

float BuildingManager::GetWarmthAt(Vector2 pos) const {
    float warmth = 0.0f;
    for (const auto& b : buildings_) {
        if (!b.finished) continue;
        if (b.warmthRadius > 0.0f) {
            float d = Vector2Distance(pos, b.position);
            if (d < b.warmthRadius) {
                float factor = 1.0f - (d / b.warmthRadius);
                warmth += factor * 20.0f;
            }
        }
    }
    return warmth;
}

// ============================================================================
//  Zeichnen der Gebäude
// ============================================================================
void BuildingManager::Draw(float simTime) const {
    for (const auto& b : buildings_) {
        DrawBuilding(b, simTime);
    }
}

void BuildingManager::DrawBuilding(const Building& b, float simTime) const {
    switch (b.type) {
        case BuildingType::TownCenter: DrawTownCenter(b, simTime); break;
        case BuildingType::Hut:        DrawHut(b, simTime); break;
        case BuildingType::Pantry:     DrawPantry(b); break;
        case BuildingType::CraftBench: DrawCraftBench(b); break;
        case BuildingType::Watchtower: DrawWatchtower(b, simTime); break;
    }

    // Fortschrittsbalken bei Bau oder Upgrade
    if (b.NeedsWork()) {
        float x = b.position.x;
        float y = b.position.y - 28.0f;
        float pct = b.progress / 100.0f;
        DrawRectangleRec({ x - 12.0f, y, 24.0f, 3.5f }, Fade(BLACK, 0.7f));
        DrawRectangleRec({ x - 12.0f, y, 24.0f * pct, 3.5f }, b.underUpgrade ? ORANGE : GOLD);
        DrawRectangleLines((int)(x - 12.0f), (int)y, 24, 4, WHITE);
    }
}

void BuildingManager::DrawTownCenter(const Building& b, float simTime) const {
    float x = b.position.x, y = b.position.y;
    DrawEllipse((int)x, (int)(y + 2), 18, 5, Fade(BLACK, 0.3f));

    if (b.tier == 1) {
        // Stufe 1: Feuerkreis (Lagerfeuer + Totempfähle)
        for (int i = 0; i < 8; ++i) {
            float a = (float)i / 8.0f * 2.0f * PI;
            DrawCircleV({ x + std::cos(a) * 8.0f, y + std::sin(a) * 4.5f }, 2.0f, { 130, 125, 120, 255 });
        }
        float flick = std::sin(simTime * 12.0f) * 0.8f;
        DrawCircleV({ x, y - 2.0f }, 4.5f + flick * 0.4f, Fade(ORANGE, 0.9f));
        DrawCircleV({ x, y - 4.0f }, 2.8f, { 255, 230, 90, 255 });
        DrawCircleV({ x, y - 4.0f }, b.warmthRadius * 0.4f, Fade(ORANGE, 0.08f));
    } else if (b.tier == 2) {
        // Stufe 2: Langhaus (Holzbalken + Giebeldach)
        DrawRectangle((int)(x - 16), (int)(y - 14), 32, 14, { 120, 75, 40, 255 });
        DrawTriangle({ x, y - 24.0f }, { x - 19.0f, y - 14.0f }, { x + 19.0f, y - 14.0f }, { 90, 55, 30, 255 });
        DrawRectangle((int)(x - 4), (int)(y - 7), 8, 7, { 40, 25, 15, 255 }); // Tür
        DrawCircleV({ x, y - 14.0f }, b.warmthRadius * 0.35f, Fade(ORANGE, 0.06f));
    } else {
        // Stufe 3: Rathaus (Steinmauerwerk + Ziegeldach)
        DrawRectangle((int)(x - 18), (int)(y - 18), 36, 18, { 140, 140, 145, 255 });
        DrawRectangleLines((int)(x - 18), (int)(y - 18), 36, 18, { 90, 90, 95, 255 });
        DrawTriangle({ x, y - 28.0f }, { x - 22.0f, y - 18.0f }, { x + 22.0f, y - 18.0f }, { 170, 70, 50, 255 });
        DrawRectangle((int)(x - 5), (int)(y - 9), 10, 9, { 60, 40, 25, 255 });
        DrawCircleV({ x, y - 16.0f }, b.warmthRadius * 0.35f, Fade(GOLD, 0.08f));
    }
}

void BuildingManager::DrawHut(const Building& b, float simTime) const {
    (void)simTime;
    float x = b.position.x, y = b.position.y;
    DrawEllipse((int)x, (int)(y + 1), 12, 3, Fade(BLACK, 0.25f));

    if (b.tier == 1) {
        // Zelt / Notunterkunft
        DrawTriangle({ x, y - 18.0f }, { x - 11.0f, y }, { x + 11.0f, y }, { 150, 100, 55, 255 });
        DrawTriangle({ x, y - 8.0f }, { x - 3.5f, y }, { x + 3.5f, y }, { 40, 25, 15, 255 });
    } else if (b.tier == 2) {
        // Feste Holzhütte
        DrawRectangle((int)(x - 11), (int)(y - 12), 22, 12, { 135, 90, 50, 255 });
        DrawTriangle({ x, y - 20.0f }, { x - 13.0f, y - 12.0f }, { x + 13.0f, y - 12.0f }, { 100, 65, 35, 255 });
        DrawRectangle((int)(x - 3), (int)(y - 7), 6, 7, { 45, 30, 18, 255 });
        DrawRectangle((int)(x + 4), (int)(y - 9), 4, 4, { 220, 200, 120, 255 }); // Fenster
    } else {
        // Steinhaus
        DrawRectangle((int)(x - 12), (int)(y - 14), 24, 14, { 155, 150, 145, 255 });
        DrawTriangle({ x, y - 22.0f }, { x - 14.0f, y - 14.0f }, { x + 14.0f, y - 14.0f }, { 120, 55, 40, 255 });
        DrawRectangle((int)(x - 3), (int)(y - 8), 6, 8, { 50, 35, 20, 255 });
        DrawRectangle((int)(x - 9), (int)(y - 10), 4, 4, { 180, 210, 230, 255 });
        DrawRectangle((int)(x + 5), (int)(y - 10), 4, 4, { 180, 210, 230, 255 });
    }

    if (b.occupants > 0 && b.finished) {
        DrawText("z", (int)(x + 7), (int)(y - 20), 8, RAYWHITE);
    }
}

void BuildingManager::DrawPantry(const Building& b) const {
    float x = b.position.x, y = b.position.y;
    DrawEllipse((int)x, (int)(y + 2), 14, 4, Fade(BLACK, 0.3f));

    // Vorratskammer (auf Stelzen)
    DrawRectangle((int)(x - 12), (int)(y - 16), 24, 13, { 160, 115, 65, 255 });
    DrawTriangle({ x, y - 23.0f }, { x - 14.0f, y - 16.0f }, { x + 14.0f, y - 16.0f }, { 110, 75, 40, 255 });
    DrawRectangle((int)(x - 9), (int)(y - 3), 3, 4, { 80, 50, 25, 255 }); // Stelze
    DrawRectangle((int)(x + 6), (int)(y - 3), 3, 4, { 80, 50, 25, 255 }); // Stelze
    DrawCircleV({ x, y - 8.0f }, 2.0f, RED); // Vorrats-Symbol
}

void BuildingManager::DrawCraftBench(const Building& b) const {
    float x = b.position.x, y = b.position.y;
    DrawEllipse((int)x, (int)(y + 1), 12, 3, Fade(BLACK, 0.25f));

    // Holztisch / Werkbank
    DrawRectangle((int)(x - 10), (int)(y - 9), 20, 5, { 140, 95, 50, 255 });
    DrawRectangle((int)(x - 8), (int)(y - 4), 3, 5, { 90, 60, 30, 255 });
    DrawRectangle((int)(x + 5), (int)(y - 4), 3, 5, { 90, 60, 30, 255 });
    // Amboss / Werkzeug auf der Werkbank
    DrawRectangle((int)(x - 4), (int)(y - 12), 8, 3, { 100, 100, 105, 255 });
}

void BuildingManager::DrawWatchtower(const Building& b, float simTime) const {
    float x = b.position.x, y = b.position.y;
    DrawEllipse((int)x, (int)(y + 1), 10, 3, Fade(BLACK, 0.25f));

    // Hoher Wachturm mit Signalfeuer oben
    DrawLineEx({ x - 6.0f, y }, { x - 3.0f, y - 26.0f }, 2.0f, { 110, 75, 40, 255 });
    DrawLineEx({ x + 6.0f, y }, { x + 3.0f, y - 26.0f }, 2.0f, { 110, 75, 40, 255 });
    DrawLineEx({ x - 5.0f, y - 13.0f }, { x + 5.0f, y - 13.0f }, 1.5f, { 110, 75, 40, 255 });

    // Plattform
    DrawRectangle((int)(x - 7), (int)(y - 30), 14, 4, { 130, 90, 45, 255 });

    // Signalfeuer
    float flick = std::sin(simTime * 14.0f) * 0.7f;
    DrawCircleV({ x, y - 32.0f }, 3.0f + flick * 0.3f, ORANGE);
    DrawCircleV({ x, y - 33.0f }, 1.5f, YELLOW);
}

