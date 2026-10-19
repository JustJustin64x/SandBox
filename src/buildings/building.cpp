#include "building.h"
#include "tribe.h"
#include "age_manager.h"
#include "event_log.h"
#include "font_manager.h"
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
        case BuildingType::Kiln:
            if (tier == 1) return "Lehmofen";
            return "Rennofen (Schmelze)";
        case BuildingType::FarmPlot:
            return "Ackerfeld";
        case BuildingType::AnimalPen:
            return "Tiergehege";
        case BuildingType::Monument:
            if (tier == 1) return "Totempfahl";
            if (tier == 2) return "Megalith-Steinkreis";
            return "Tempel-Altar";
        case BuildingType::Grave:
            if (tier == 1) return "Ahnen-Steingrab";
            return "Megalith-Hügelgrab";
    }
    return "Gebäude";
}

std::string Building::DisplayName() const {
    if (type == BuildingType::Grave && !customLabel.empty()) {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "%s von %s", BuildingTypeName(type, tier), customLabel.c_str());
        return std::string(buf);
    }
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
        case BuildingType::Kiln:       return { 6, 8, 0, 0 };
        case BuildingType::FarmPlot:   return { 5, 2, 0, 0 };
        case BuildingType::AnimalPen:  return { 9, 3, 2, 0 };
        case BuildingType::Monument:   return { 8, 8, 0, 0 };
        case BuildingType::Grave:      return { 0, 0, 0, 0 };
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
    } else if (type == BuildingType::Kiln) {
        if (tier == 1) return { 8, 12, 2, 4 };
    } else if (type == BuildingType::Monument) {
        if (tier == 1) return { 6, 14, 4, 0 };
        if (tier == 2) return { 8, 18, 6, 8 };
    }
    return { 20, 20, 10, 10 };
}

bool Building::CanUpgrade(int currentAge) const {
    if (underUpgrade || !finished) return false;
    if (type == BuildingType::Kiln) {
        return tier == 1 && currentAge >= 2; // Stufe 2 (Rennofen) ab Eisenzeit
    }
    if (type != BuildingType::TownCenter && type != BuildingType::Hut && type != BuildingType::Monument) {
        return false;
    }
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
    tc.fuel = 100.0f;
    tc.finished = true;
    tc.warmthRadius = 55.0f;
    tc.capacity = 2;
    buildings_.push_back(tc);
}

void BuildingManager::Update(float dt, bool isBadWeather, Stockpile& stock, EventLog& log) {
    (void)stock;
    for (auto& b : buildings_) {
        if (!b.finished) continue;

        // 1. Brennstoffverbrauch für Feuerstellen (TownCenter & Kiln)
        if (b.type == BuildingType::TownCenter || b.type == BuildingType::Kiln) {
            if (b.fuel > 0.0f) {
                bool openFire = (b.type == BuildingType::TownCenter && b.tier == 1);
                float burnRate = openFire ? (isBadWeather ? 1.65f : 0.72f) : (isBadWeather ? 0.65f : 0.48f);
                float prevFuel = b.fuel;
                b.fuel = std::max(0.0f, b.fuel - burnRate * dt);
                if (prevFuel > 0.0f && b.fuel <= 0.0f) {
                    log.Add(TextFormat("FEUER ERLOSCHEN: %s hat kein Brennholz mehr und kühlt aus!", b.DisplayName().c_str()),
                            { 130, 185, 255, 255 });
                }
            }
        }

        // 2. Neolithischer Wachstumszyklus: Ackerfelder (FarmPlot) & Tiergehege (AnimalPen)
        if (b.type == BuildingType::FarmPlot) {
            // Regen bewässert die Äcker und beschleunigt das Getreidewachstum!
            float growthRate = isBadWeather ? 4.2f : 2.6f;
            b.cropGrowth = std::min(100.0f, b.cropGrowth + growthRate * dt);
        } else if (b.type == BuildingType::AnimalPen) {
            b.cropGrowth = std::min(100.0f, b.cropGrowth + 2.4f * dt);
        }

        // 3. Natürlicher Witterungs-Verschleiß (Meilenstein C: Zelt altert schneller als Holz oder Stein)
        if (!b.underUpgrade && b.type != BuildingType::Grave && b.type != BuildingType::Monument && b.hp > 25.0f) {
            float baseWear = (b.tier == 1) ? 0.14f : (b.tier == 2) ? 0.07f : 0.03f;
            if (isBadWeather) baseWear *= 2.2f;
            b.hp = std::max(25.0f, b.hp - baseWear * dt);
        }
    }
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
    b.fuel = 100.0f;
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
    } else if (type == BuildingType::Kiln) {
        b.capacity = 1; // 1 Schmelzer / Töpfer
        b.warmthRadius = 38.0f;
    } else if (type == BuildingType::FarmPlot) {
        b.capacity = 1; // 1 Bauer
        b.cropGrowth = 15.0f;
    } else if (type == BuildingType::AnimalPen) {
        b.capacity = 1; // 1 Hirte
        b.cropGrowth = 30.0f;
    } else if (type == BuildingType::Monument) {
        b.capacity = 2;
        b.warmthRadius = 32.0f;
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

int BuildingManager::SpawnGrave(Vector2 pos, const std::string& deceasedName, int currentEraIdx, EventLog& log) {
    // Maximal 14 sichtbare Ahnengräber auf der Karte behalten, damit das Dorf nicht überfüllt wird
    int graveCount = GetBuildingCount(BuildingType::Grave, false);
    if (graveCount >= 14) {
        for (auto it = buildings_.begin(); it != buildings_.end(); ++it) {
            if (it->type == BuildingType::Grave) {
                buildings_.erase(it);
                break;
            }
        }
    }

    Building g;
    g.id = nextId_++;
    g.type = BuildingType::Grave;
    g.tier = std::clamp(1 + currentEraIdx, 1, 3);
    g.position = pos;
    g.progress = 100.0f;
    g.finished = true;
    g.capacity = 1;
    g.customLabel = deceasedName;
    buildings_.push_back(g);

    log.Add(TextFormat("AHNENKULT: Ein %s für %s wurde nahe der Siedlung errichtet.",
                       BuildingTypeName(BuildingType::Grave, g.tier), deceasedName.c_str()),
            { 195, 180, 225, 255 });
    return g.id;
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
    if (!b || !b->NeedsWork()) return false;

    // Reparatur eines beschädigten fertigen Gebäudes
    if (b->finished && !b->underUpgrade && b->hp < b->maxHp) {
        b->hp = std::min(b->maxHp, b->hp + amount * 1.6f);
        if (b->hp >= b->maxHp) {
            b->hp = b->maxHp;
            log.Add(TextFormat("%s wurde vollständig instand gesetzt!", b->DisplayName().c_str()), { 120, 230, 120, 255 });
            return true;
        }
        return false;
    }

    b->progress = std::min(100.0f, b->progress + amount);
    if (b->progress < 100.0f) return false;

    if (b->underUpgrade) {
        b->tier++;
        b->underUpgrade = false;
        b->progress = 100.0f;
        b->maxHp = 100.0f + (b->tier - 1) * 80.0f;
        b->hp = b->maxHp;
        b->fuel = 100.0f;

        // Upgrade-Vorteile anwenden
        if (b->type == BuildingType::TownCenter) {
            b->warmthRadius = 55.0f + b->tier * 20.0f;
            b->capacity = 2 + b->tier;
        } else if (b->type == BuildingType::Hut) {
            b->capacity = 2 + (b->tier - 1); // Stufe 1: 2, Stufe 2: 3, Stufe 3: 4
            b->warmthEfficiency = 1.0f + (b->tier - 1) * 0.5f;
            b->warmthRadius = 25.0f + b->tier * 10.0f;
        } else if (b->type == BuildingType::Kiln) {
            b->warmthRadius = 48.0f;
        }

        log.Add(TextFormat("Erweiterung abgeschlossen: %s!", b->DisplayName().c_str()), { 120, 230, 120, 255 });
    } else {
        b->finished = true;
        b->progress = 100.0f;
        b->hp = b->maxHp;
        b->fuel = 100.0f;
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
        // Bevorzuge höhere Stufen, aktive Feuerstellen und nähere Gebäude
        float fireBonus = b.HasActiveFire() ? 90.0f : 0.0f;
        float score = (float)(b.tier * 200.0f) + fireBonus - dist;
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
        if ((b.type != BuildingType::CraftBench && b.type != BuildingType::Kiln) || !b.finished) continue;
        if (b.occupants >= b.capacity) continue;

        float dist = Vector2Distance(from, b.position);
        // Rennofen/Lehmofen wird leicht bevorzugt, wenn vorhanden
        if (b.type == BuildingType::Kiln && b.HasActiveFire()) dist *= 0.75f;
        if (dist < bestDist) {
            bestDist = dist;
            bestId = b.id;
        }
    }
    return bestId;
}

int BuildingManager::FindFireNeedingFuel(Vector2 from) const {
    int bestId = -1;
    float bestScore = -1.0f;

    for (const auto& b : buildings_) {
        if (!b.NeedsFuel()) continue;
        float dist = Vector2Distance(from, b.position);
        float urgency = (100.0f - b.fuel) * 3.0f;
        if (b.type == BuildingType::TownCenter) urgency += 80.0f;
        float score = urgency - dist * 0.4f;
        if (score > bestScore) {
            bestScore = score;
            bestId = b.id;
        }
    }
    return bestId;
}

int BuildingManager::FindFarmOrPenNeedingWork(Vector2 from) const {
    int bestId = -1;
    float bestScore = -1.0f;

    for (const auto& b : buildings_) {
        if (!b.NeedsFarmTending()) continue;
        if (b.occupants >= b.capacity) continue;
        float dist = Vector2Distance(from, b.position);
        float priority = b.IsHarvestReady() ? 220.0f : 110.0f;
        float score = priority - dist * 0.5f;
        if (score > bestScore) {
            bestScore = score;
            bestId = b.id;
        }
    }
    return bestId;
}

int BuildingManager::FindMonumentOrGrave(Vector2 from) const {
    int bestId = -1;
    float bestScore = -1.0f;

    for (const auto& b : buildings_) {
        if (!b.finished) continue;
        if (b.type != BuildingType::Monument && b.type != BuildingType::Grave) continue;
        float dist = Vector2Distance(from, b.position);
        float bonus = (b.type == BuildingType::Monument) ? (80.0f + b.tier * 40.0f) : 45.0f;
        float score = bonus - dist * 0.6f;
        if (score > bestScore) {
            bestScore = score;
            bestId = b.id;
        }
    }
    return bestId;
}

float BuildingManager::GetWarmthAt(Vector2 pos) const {
    float warmth = 0.0f;
    for (const auto& b : buildings_) {
        if (!b.finished) continue;
        if (!b.HasActiveFire()) continue;
        if (b.warmthRadius > 0.0f) {
            float d = Vector2Distance(pos, b.position);
            if (d < b.warmthRadius) {
                float factor = 1.0f - (d / b.warmthRadius);
                float fuelScale = (b.type == BuildingType::TownCenter || b.type == BuildingType::Kiln)
                    ? (0.45f + 0.55f * (b.fuel / 100.0f))
                    : 1.0f;
                warmth += factor * 20.0f * fuelScale;
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
    float x = b.position.x;
    float y = b.position.y;

    // A) Mehrstufige Baustelle, solange das Gebäude noch nicht fertiggestellt ist (Meilenstein C)
    if (!b.finished) {
        DrawConstructionSite(b);
    } else {
        switch (b.type) {
            case BuildingType::TownCenter: DrawTownCenter(b, simTime); break;
            case BuildingType::Hut:        DrawHut(b, simTime); break;
            case BuildingType::Pantry:     DrawPantry(b); break;
            case BuildingType::CraftBench: DrawCraftBench(b); break;
            case BuildingType::Watchtower: DrawWatchtower(b, simTime); break;
            case BuildingType::Kiln:       DrawKiln(b, simTime); break;
            case BuildingType::FarmPlot:   DrawFarmPlot(b, simTime); break;
            case BuildingType::AnimalPen:  DrawAnimalPen(b, simTime); break;
            case BuildingType::Monument:   DrawMonument(b, simTime); break;
            case BuildingType::Grave:      DrawGrave(b, simTime); break;
        }

        // Baugerüst-Streben während eines laufenden Upgrades auf nächste Epochen-Stufe
        if (b.underUpgrade) {
            DrawLineEx({ x - 14.0f, y + 1.0f }, { x - 14.0f, y - 20.0f }, 1.5f, { 185, 135, 70, 235 });
            DrawLineEx({ x + 14.0f, y + 1.0f }, { x + 14.0f, y - 20.0f }, 1.5f, { 185, 135, 70, 235 });
            DrawLineEx({ x - 15.0f, y - 10.0f }, { x + 15.0f, y - 10.0f }, 1.2f, { 185, 135, 70, 215 });
        }

        // Sichtbare Verschleiß-/Riss-Spuren, wenn Struktur-HP durch Wetter/Feuer unter 70% fällt
        if (b.hp < b.maxHp * 0.70f) {
            DrawLineEx({ x - 4.0f, y - 11.0f }, { x - 1.0f, y - 5.0f }, 1.2f, Color{ 45, 30, 20, 220 });
            DrawLineEx({ x - 1.0f, y - 5.0f },  { x + 2.0f, y - 8.0f }, 1.0f, Color{ 45, 30, 20, 220 });
        }
    }

    // Fortschrittsbalken bei Bau/Upgrade oder Schadensbalken bei Reparaturbedarf
    if (b.NeedsWork()) {
        float barY = y - 29.0f;
        bool isRepair = (b.finished && !b.underUpgrade && b.hp < b.maxHp);
        float pct = isRepair ? std::clamp(b.hp / b.maxHp, 0.0f, 1.0f) : (b.progress / 100.0f);
        DrawRectangleRec({ x - 12.0f, barY, 24.0f, 3.5f }, Fade(BLACK, 0.75f));
        DrawRectangleRec({ x - 12.0f, barY, 24.0f * pct, 3.5f }, isRepair ? ORANGE : (b.underUpgrade ? SKYBLUE : GOLD));
        DrawRectangleLines((int)(x - 12.0f), (int)barY, 24, 4, WHITE);
    }
}

void BuildingManager::DrawConstructionSite(const Building& b) const {
    float x = b.position.x;
    float y = b.position.y;

    // 1. Fundament-Grube & abgestecktes Baufeld (immer sichtbar ab 0%)
    DrawRectangle((int)(x - 12), (int)(y - 6), 24, 9, Color{ 118, 92, 60, 215 });
    DrawRectangleLines((int)(x - 12), (int)(y - 6), 24, 9, Color{ 175, 145, 95, 220 });

    // 4 Eckpflöcke
    DrawRectangle((int)(x - 12), (int)(y - 8), 2, 4, Color{ 150, 105, 55, 255 });
    DrawRectangle((int)(x + 10), (int)(y - 8), 2, 4, Color{ 150, 105, 55, 255 });
    DrawRectangle((int)(x - 12), (int)(y + 1), 2, 4, Color{ 150, 105, 55, 255 });
    DrawRectangle((int)(x + 10), (int)(y + 1), 2, 4, Color{ 150, 105, 55, 255 });

    // Aufgeschichtetes Baumaterial neben der Grube
    DrawRectangle((int)(x - 15), (int)(y + 2), 6, 3, Color{ 135, 88, 42, 255 });
    DrawCircle((int)(x + 14), (int)(y + 3), 2.2f, Color{ 155, 155, 162, 255 });

    // 2. Phase 2 (ab 34%): Aufragendes Holzgerüst & Sparren
    if (b.progress >= 34.0f) {
        DrawLineEx({ x - 10.0f, y + 1.0f }, { x - 10.0f, y - 11.0f }, 2.0f, Color{ 140, 95, 48, 255 });
        DrawLineEx({ x + 10.0f, y + 1.0f }, { x + 10.0f, y - 11.0f }, 2.0f, Color{ 140, 95, 48, 255 });
        DrawLineEx({ x - 11.0f, y - 10.0f }, { x + 11.0f, y - 10.0f }, 1.6f, Color{ 160, 110, 58, 255 });
        // Offene Dachsparren (A-Rahmen)
        DrawLineEx({ x - 11.0f, y - 10.0f }, { x, y - 19.0f }, 1.5f, Color{ 175, 125, 65, 255 });
        DrawLineEx({ x + 11.0f, y - 10.0f }, { x, y - 19.0f }, 1.5f, Color{ 175, 125, 65, 255 });
    }

    // 3. Phase 3 (ab 70%): Teilweise gedecktes Dach & Wandfüllung
    if (b.progress >= 70.0f) {
        DrawRectangle((int)(x - 9), (int)(y - 9), 18, 9, Color{ 130, 88, 48, 210 });
        DrawTriangle({ x, y - 18.0f }, { x - 10.0f, y - 10.0f }, { x + 2.0f, y - 10.0f }, Color{ 148, 102, 54, 225 });
    }
}

void BuildingManager::DrawTownCenter(const Building& b, float simTime) const {
    float x = b.position.x, y = b.position.y;
    DrawEllipse((int)x, (int)(y + 2), 18, 5, Fade(BLACK, 0.3f));

    if (b.tier == 1) {
        // Stufe 1: Feuerkreis (Steinkreis + Bratgestell + Lagerfeuer)
        for (int i = 0; i < 8; ++i) {
            float a = (float)i / 8.0f * 2.0f * PI;
            DrawCircleV({ x + std::cos(a) * 8.0f, y + std::sin(a) * 4.5f }, 2.0f, { 130, 125, 120, 255 });
        }
        // Verkohlte Holzscheite in der Mitte
        DrawLineEx({ x - 4.0f, y + 1.0f }, { x + 4.0f, y - 2.0f }, 2.0f, Color{ 75, 48, 28, 255 });
        DrawLineEx({ x - 4.0f, y - 2.0f }, { x + 4.0f, y + 1.0f }, 2.0f, Color{ 65, 40, 24, 255 });

        // Kleines Brat-/Kochgestell über der Feuerstelle
        DrawLineEx({ x - 7.0f, y + 1.0f }, { x - 5.0f, y - 9.0f }, 1.2f, Color{ 115, 78, 40, 255 });
        DrawLineEx({ x + 7.0f, y + 1.0f }, { x + 5.0f, y - 9.0f }, 1.2f, Color{ 115, 78, 40, 255 });
        DrawLineEx({ x - 5.5f, y - 8.0f }, { x + 5.5f, y - 8.0f }, 1.0f, Color{ 95, 65, 35, 255 });

        if (b.HasActiveFire()) {
            float fuelRatio = std::clamp(b.fuel / 100.0f, 0.25f, 1.0f);
            float flick = std::sin(simTime * 12.0f) * 0.8f;
            DrawCircleV({ x, y - 2.0f }, (4.5f + flick * 0.4f) * fuelRatio, Fade(ORANGE, 0.92f));
            DrawCircleV({ x, y - 4.0f }, 2.8f * fuelRatio, { 255, 235, 95, 255 });
            DrawCircleV({ x, y - 4.0f }, b.warmthRadius * 0.4f * fuelRatio, Fade(ORANGE, 0.08f));
            // Brutzelndes Fleischstück am Spieß
            DrawCircleV({ x, y - 8.0f }, 2.0f, Color{ 185, 85, 50, 255 });
        } else {
            // Erloschenes Feuer: Kalte graue Asche
            DrawCircleV({ x, y - 1.0f }, 3.2f, Color{ 90, 92, 96, 235 });
            FontManager::DrawTextCenteredShadow("KALT", x, y - 22.0f, 10.0f, SKYBLUE);
        }
    } else if (b.tier == 2) {
        // Stufe 2: Langhaus (Holzbalken + Giebeldach + Herdfeuer)
        DrawRectangle((int)(x - 16), (int)(y - 14), 32, 14, { 120, 75, 40, 255 });
        DrawTriangle({ x, y - 24.0f }, { x - 19.0f, y - 14.0f }, { x + 19.0f, y - 14.0f }, { 90, 55, 30, 255 });
        DrawRectangle((int)(x - 4), (int)(y - 7), 8, 7, b.HasActiveFire() ? Color{ 210, 120, 45, 255 } : Color{ 40, 25, 15, 255 });
        if (b.HasActiveFire()) {
            DrawCircleV({ x, y - 14.0f }, b.warmthRadius * 0.35f, Fade(ORANGE, 0.07f));
            float smokeY = y - 26.0f - std::fmod(simTime * 8.0f, 10.0f);
            DrawCircleV({ x + 6.0f, smokeY }, 2.2f, Fade(LIGHTGRAY, 0.45f));
        }
    } else {
        // Stufe 3: Rathaus (Steinmauerwerk + Ziegeldach)
        DrawRectangle((int)(x - 18), (int)(y - 18), 36, 18, { 140, 140, 145, 255 });
        DrawRectangleLines((int)(x - 18), (int)(y - 18), 36, 18, { 90, 90, 95, 255 });
        DrawTriangle({ x, y - 28.0f }, { x - 22.0f, y - 18.0f }, { x + 22.0f, y - 18.0f }, { 170, 70, 50, 255 });
        DrawRectangle((int)(x - 5), (int)(y - 9), 10, 9, { 60, 40, 25, 255 });
        if (b.HasActiveFire()) {
            DrawCircleV({ x, y - 16.0f }, b.warmthRadius * 0.35f, Fade(GOLD, 0.08f));
        }
    }

    // Dezente Brennstoff-Anzeige unter dem Feuerkreis, wenn Holz knapp wird (< 55%)
    if (b.fuel < 55.0f) {
        DrawRectangle((int)(x - 10), (int)(y + 6), 20, 2, Fade(BLACK, 0.65f));
        DrawRectangle((int)(x - 10), (int)(y + 6), (int)(20.0f * (b.fuel / 100.0f)), 2, (b.fuel > 20.0f) ? ORANGE : RED);
    }
}

void BuildingManager::DrawHut(const Building& b, float simTime) const {
    float x = b.position.x, y = b.position.y;
    DrawEllipse((int)x, (int)(y + 1), 12, 3, Fade(BLACK, 0.25f));

    if (b.tier == 1) {
        // Zelt / Notunterkunft (mit Fell-Texturstreifen)
        DrawTriangle({ x, y - 18.0f }, { x - 11.0f, y }, { x + 11.0f, y }, { 150, 100, 55, 255 });
        DrawLineEx({ x, y - 18.0f }, { x - 6.0f, y }, 1.0f, Color{ 120, 78, 40, 220 });
        DrawLineEx({ x, y - 18.0f }, { x + 6.0f, y }, 1.0f, Color{ 120, 78, 40, 220 });
        DrawTriangle({ x, y - 8.0f }, { x - 3.5f, y }, { x + 3.5f, y }, { 40, 25, 15, 255 });
    } else if (b.tier == 2) {
        // Feste Holzhütte + Rauchabzug
        DrawRectangle((int)(x - 11), (int)(y - 12), 22, 12, { 135, 90, 50, 255 });
        DrawTriangle({ x, y - 20.0f }, { x - 13.0f, y - 12.0f }, { x + 13.0f, y - 12.0f }, { 100, 65, 35, 255 });
        DrawRectangle((int)(x - 3), (int)(y - 7), 6, 7, { 45, 30, 18, 255 });
        DrawRectangle((int)(x + 4), (int)(y - 9), 4, 4, { 235, 205, 115, 255 }); // Warm erleuchtetes Fenster
    } else {
        // Steinhaus mit Kamin
        DrawRectangle((int)(x + 5), (int)(y - 23), 4, 7, { 120, 115, 115, 255 }); // Schornstein
        DrawRectangle((int)(x - 12), (int)(y - 14), 24, 14, { 155, 150, 145, 255 });
        DrawTriangle({ x, y - 22.0f }, { x - 14.0f, y - 14.0f }, { x + 14.0f, y - 14.0f }, { 120, 55, 40, 255 });
        DrawRectangle((int)(x - 3), (int)(y - 8), 6, 8, { 50, 35, 20, 255 });
        DrawRectangle((int)(x - 9), (int)(y - 10), 4, 4, { 240, 210, 125, 255 });
        DrawRectangle((int)(x + 5), (int)(y - 10), 4, 4, { 240, 210, 125, 255 });
        float smokeOffset = std::fmod(simTime * 7.0f + (float)b.id * 2.0f, 9.0f);
        DrawCircleV({ x + 7.0f, y - 24.0f - smokeOffset }, 1.8f, Fade(LIGHTGRAY, 0.45f));
    }

    if (b.occupants > 0 && b.finished) {
        FontManager::DrawTextShadow("zZ", x + 5.0f, y - 22.0f, 11.0f, RAYWHITE);
    }
}

void BuildingManager::DrawPantry(const Building& b) const {
    float x = b.position.x, y = b.position.y;
    DrawEllipse((int)x, (int)(y + 2), 14, 4, Fade(BLACK, 0.3f));

    // Vorratskammer (auf Stelzen + Tonkrüge darunter)
    DrawRectangle((int)(x - 12), (int)(y - 16), 24, 13, { 160, 115, 65, 255 });
    DrawTriangle({ x, y - 23.0f }, { x - 14.0f, y - 16.0f }, { x + 14.0f, y - 16.0f }, { 110, 75, 40, 255 });
    DrawRectangle((int)(x - 9), (int)(y - 3), 3, 4, { 80, 50, 25, 255 }); // Stelze
    DrawRectangle((int)(x + 6), (int)(y - 3), 3, 4, { 80, 50, 25, 255 }); // Stelze
    DrawCircleV({ x - 2.0f, y - 1.0f }, 2.0f, Color{ 185, 110, 65, 255 }); // Keramikkrug
    DrawCircleV({ x + 2.5f, y - 1.0f }, 2.0f, Color{ 175, 100, 60, 255 }); // Keramikkrug
    DrawCircleV({ x, y - 8.0f }, 2.0f, RED); // Vorrats-Symbol
}

void BuildingManager::DrawCraftBench(const Building& b) const {
    float x = b.position.x, y = b.position.y;
    DrawEllipse((int)x, (int)(y + 1), 12, 3, Fade(BLACK, 0.25f));

    // Holztisch / Werkbank + aufgespanntes Gerbfell & Amboss
    DrawRectangle((int)(x - 10), (int)(y - 9), 20, 5, { 140, 95, 50, 255 });
    DrawRectangle((int)(x - 8), (int)(y - 4), 3, 5, { 90, 60, 30, 255 });
    DrawRectangle((int)(x + 5), (int)(y - 4), 3, 5, { 90, 60, 30, 255 });
    DrawRectangle((int)(x - 5), (int)(y - 12), 7, 3, { 105, 110, 120, 255 });
    DrawRectangle((int)(x + 3), (int)(y - 11), 5, 2, { 185, 135, 85, 255 });
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

void BuildingManager::DrawKiln(const Building& b, float simTime) const {
    float x = b.position.x, y = b.position.y;
    DrawEllipse((int)x, (int)(y + 1), 13, 4, Fade(BLACK, 0.3f));

    if (b.tier == 1) {
        // Stufe 1: Kuppelförmiger Lehm-/Töpferofen
        DrawCircleV({ x, y - 6.0f }, 9.5f, Color{ 175, 112, 68, 255 });
        DrawRectangle((int)(x - 9), (int)(y - 6), 18, 7, Color{ 160, 100, 58, 255 });
    } else {
        // Stufe 2: Hoher Stein-Rennofen (Schmelzofen)
        DrawRectangle((int)(x - 9), (int)(y - 16), 18, 17, Color{ 125, 120, 122, 255 });
        DrawRectangleLines((int)(x - 9), (int)(y - 16), 18, 17, Color{ 80, 76, 78, 255 });
    }

    // Brennkammer-Öffnung unten
    if (b.HasActiveFire()) {
        float pulse = 0.75f + 0.25f * std::sin(simTime * 10.0f);
        DrawCircleV({ x, y - 3.0f }, 4.0f, Fade(ORANGE, pulse));
        DrawCircleV({ x, y - 3.0f }, 2.2f, YELLOW);
        DrawCircleV({ x, y - 6.0f }, b.warmthRadius * 0.32f, Fade(ORANGE, 0.07f));
        // Aufsteigender Rauch / Funken aus dem Abzug
        float topY = (b.tier == 1) ? (y - 15.0f) : (y - 18.0f);
        DrawCircleV({ x, topY - std::fmod(simTime * 9.0f, 8.0f) }, 2.0f, Fade(DARKGRAY, 0.55f));
    } else {
        DrawCircleV({ x, y - 3.0f }, 3.5f, Color{ 40, 35, 35, 255 });
    }
}

void BuildingManager::DrawFarmPlot(const Building& b, float simTime) const {
    float x = b.position.x, y = b.position.y;

    // Gepflügter dunkler Ackerboden mit Holz-Einfassung
    DrawRectangle((int)(x - 13), (int)(y - 10), 26, 15, Color{ 82, 54, 34, 240 });
    DrawRectangleLines((int)(x - 13), (int)(y - 10), 26, 15, Color{ 130, 92, 52, 245 });

    // 3 Ackerfurchen + Wachstumsphasen (Saat -> Grüne Halme -> Reifes goldenes Getreide)
    for (int row = 0; row < 3; ++row) {
        float ry = y - 7.0f + (float)row * 4.5f;
        DrawLineEx({ x - 11.0f, ry }, { x + 11.0f, ry }, 1.2f, Color{ 58, 38, 22, 230 });

        for (int col = 0; col < 6; ++col) {
            float cx = x - 9.5f + (float)col * 3.8f;
            float sway = std::sin(simTime * 3.2f + (float)(row * 3 + col)) * 0.9f;

            if (b.cropGrowth < 35.0f) {
                // Phase 1: Zarte grüne Keimlinge
                DrawPixel((int)cx, (int)(ry - 1.0f), Color{ 95, 195, 75, 255 });
            } else if (b.cropGrowth < 85.0f) {
                // Phase 2: Wachsende grüne Getreidehalme
                DrawLineEx({ cx, ry }, { cx + sway * 0.6f, ry - 4.0f }, 1.1f, Color{ 85, 185, 65, 255 });
            } else {
                // Phase 3: Reife goldgelbe Ähren im Wind
                DrawLineEx({ cx, ry }, { cx + sway, ry - 6.0f }, 1.2f, Color{ 230, 190, 55, 255 });
                DrawCircleV({ cx + sway, ry - 6.2f }, 1.1f, Color{ 250, 215, 75, 255 });
            }
        }
    }

    if (b.IsHarvestReady()) {
        FontManager::DrawTextCenteredShadow("ERNTE", x, y - 24.0f, 11.0f, GOLD);
    }
}

void BuildingManager::DrawAnimalPen(const Building& b, float simTime) const {
    float x = b.position.x, y = b.position.y;

    // Weideboden & Holzzaun-Gehege
    DrawRectangle((int)(x - 14), (int)(y - 11), 28, 16, Color{ 96, 118, 58, 215 });
    DrawRectangleLines((int)(x - 14), (int)(y - 11), 28, 16, Color{ 148, 102, 54, 255 });
    // Zaunpfosten
    for (int px = -14; px <= 14; px += 7) {
        DrawRectangle((int)x + px - 1, (int)(y - 13), 2, 4, Color{ 120, 80, 42, 255 });
        DrawRectangle((int)x + px - 1, (int)(y + 3),  2, 4, Color{ 120, 80, 42, 255 });
    }
    // Futtertrog & Wassertrog
    DrawRectangle((int)(x - 12), (int)(y - 9), 7, 3, Color{ 70, 145, 215, 245 });
    DrawCircleV({ x + 9.0f, y - 7.0f }, 2.5f, Color{ 220, 185, 75, 245 });

    // 2 domestizierte Schafe/Ziegen, die im Gehege grasen
    for (int i = 0; i < 2; ++i) {
        float ax = x - 5.0f + (float)i * 9.0f + std::sin(simTime * 1.3f + (float)(b.id + i * 4)) * 2.5f;
        float ay = y - 2.5f + std::cos(simTime * 1.1f + (float)i * 2.0f) * 1.5f;
        // Wollkörper + Kopf
        DrawEllipse((int)ax, (int)ay, 3, 2, Color{ 242, 238, 225, 255 });
        DrawCircleV({ ax + 2.5f, ay - 1.0f }, 1.3f, Color{ 85, 75, 68, 255 });
    }

    if (b.IsHarvestReady()) {
        FontManager::DrawTextCenteredShadow("WOLLE", x, y - 25.0f, 11.0f, RAYWHITE);
    }
}

void BuildingManager::DrawMonument(const Building& b, float simTime) const {
    float x = b.position.x, y = b.position.y;
    DrawEllipse((int)x, (int)(y + 2), 13, 4, Fade(BLACK, 0.32f));

    if (b.tier == 1) {
        // Stufe 1: Geschnitzter Totempfahl mit Flügeln & Stammesmaske
        DrawRectangle((int)(x - 3), (int)(y - 24), 6, 25, Color{ 138, 88, 44, 255 });
        // Geschnitzte Flügel
        DrawTriangle({ x - 12.0f, y - 18.0f }, { x - 3.0f, y - 20.0f }, { x - 3.0f, y - 14.0f }, Color{ 195, 75, 55, 255 });
        DrawTriangle({ x + 12.0f, y - 18.0f }, { x + 3.0f, y - 14.0f }, { x + 3.0f, y - 20.0f }, Color{ 195, 75, 55, 255 });
        // Leuchtende Geister-Augen
        DrawPixel((int)x - 1, (int)(y - 21), GOLD);
        DrawPixel((int)x + 1, (int)(y - 21), GOLD);
        DrawPixel((int)x - 1, (int)(y - 13), SKYBLUE);
        DrawPixel((int)x + 1, (int)(y - 13), SKYBLUE);
    } else if (b.tier == 2) {
        // Stufe 2: Megalith-Steinkreis / Dolmen (Bronzezeit)
        DrawCircleLines((int)x, (int)(y - 2), 13.0f, Fade(SKYBLUE, 0.35f));
        DrawRectangle((int)(x - 10), (int)(y - 18), 5, 19, Color{ 135, 138, 145, 255 });
        DrawRectangle((int)(x + 5),  (int)(y - 18), 5, 19, Color{ 135, 138, 145, 255 });
        DrawRectangle((int)(x - 13), (int)(y - 22), 26, 5, Color{ 155, 158, 165, 255 }); // Deckstein
        float glow = 0.55f + 0.40f * std::sin(simTime * 3.0f);
        DrawCircleV({ x, y - 9.0f }, 3.0f, Fade(SKYBLUE, glow));
    } else {
        // Stufe 3: Antiker Tempel-Altar mit ewiger Flamme (Eisenzeit)
        DrawRectangle((int)(x - 15), (int)(y - 4), 30, 5, Color{ 175, 172, 168, 255 });
        DrawRectangle((int)(x - 11), (int)(y - 19), 4, 15, Color{ 210, 208, 202, 255 });
        DrawRectangle((int)(x + 7),  (int)(y - 19), 4, 15, Color{ 210, 208, 202, 255 });
        DrawTriangle({ x, y - 27.0f }, { x - 14.0f, y - 19.0f }, { x + 14.0f, y - 19.0f }, Color{ 195, 165, 95, 255 });
        float flick = std::sin(simTime * 10.0f) * 0.6f;
        DrawCircleV({ x, y - 9.0f }, 3.8f + flick * 0.4f, GOLD);
        DrawCircleV({ x, y - 10.0f }, 2.0f, RAYWHITE);
    }
}

void BuildingManager::DrawGrave(const Building& b, float simTime) const {
    (void)simTime;
    float x = b.position.x, y = b.position.y;

    // Erdhügel + Grabstein / Dolmen
    DrawEllipse((int)x, (int)(y + 1), 7, 3, Color{ 98, 78, 54, 235 });
    if (b.tier == 1) {
        DrawCircleV({ x, y - 4.0f }, 3.5f, Color{ 135, 135, 142, 255 });
        DrawRectangle((int)(x - 3), (int)(y - 4), 6, 5, Color{ 125, 125, 132, 255 });
    } else {
        // Megalith-Grabmal mit kleinem Querstein
        DrawRectangle((int)(x - 4), (int)(y - 9), 8, 9, Color{ 148, 150, 158, 255 });
        DrawRectangle((int)(x - 6), (int)(y - 11), 12, 3, Color{ 168, 170, 178, 255 });
    }
    // Kleine Grabblume
    DrawPixel((int)x + 3, (int)y, Color{ 245, 195, 70, 255 });
}

