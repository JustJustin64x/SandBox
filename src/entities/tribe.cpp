#include "tribe.h"
#include "caveman.h"
#include "config.h"
#include "event_log.h"
#include "world.h"

#include <algorithm>
#include <cmath>

Tribe::Tribe(Vector2 campPosition) : campPos_(campPosition) {
    stock_.food  = cfg::kStartFood;
    stock_.wood  = cfg::kStartWood;
    stock_.stone = cfg::kStartStone;
    stock_.planks = 0;
    stock_.bricks = 0;
    stock_.tools  = 0;
    stock_.torches = 2;
    stock_.pickaxes = 1;
    stock_.provisions = 3;

    buildings_.InitDefaultBuildings(campPos_);
}

void Tribe::InitWorld(const World& world) {
    pois_.InitWorldPOIs(world, campPos_);
}

void Tribe::Update(const World& world, const std::vector<Caveman>& cavemen, int population, float dt, EventLog& log) {
    // 1. POIs aktualisieren (Spawns nach Zeitfenstern & Entdeckung)
    pois_.Update(world, campPos_, age_.CurrentEra(), age_.EraElapsedTime(), cavemen, log);

    // 2. Skilltree aktualisieren (Knotenzustände abgleichen)
    skillTree_.Update(pois_, log);

    // 3. Zeitalter-Fortschritt & Forschung aktualisieren (Time-Gating + Meilensteine)
    age_.Update(stock_, buildings_, skillTree_, population, dt, log);

    // Automatischer Zeitalter-Aufstieg, sobald Epochenzeit UND Meilensteine erfüllt sind
    if (age_.CanAdvance(stock_, buildings_, skillTree_, population)) {
        age_.AdvanceEra(stock_, log);
    }

    // 4. Soziale Beziehungsdynamik & Verblassen von Emotionen
    relationships_.UpdateTick(dt);

    // 5. Nahrungskonservierung / Verderb
    spoilageTimer_ += dt;
    int pantries = buildings_.GetBuildingCount(BuildingType::Pantry, true);
    float spoilageInterval = (pantries > 0) ? 60.0f : 16.0f;

    if (spoilageTimer_ >= spoilageInterval) {
        spoilageTimer_ = 0.0f;
        if (stock_.food > 20) {
            stock_.food--;
            if (pantries == 0) {
                log.Add("1 Einheit Nahrung ist verdorben. Eine Vorratskammer würde helfen!", { 200, 140, 100, 255 });
            }
        }
    }
}

bool Tribe::NeedsMoreHuts(int population) const {
    return buildings_.TotalSleepingCapacity() < population;
}

bool Tribe::FindBuildingSpot(const World& world, Vector2& out) const {
    for (int attempt = 0; attempt < 100; ++attempt) {
        const float angle = (float)GetRandomValue(0, 3600) / 3600.0f * 2.0f * PI;
        const float radius = (float)GetRandomValue(3 * TILE_SIZE, 11 * TILE_SIZE);
        const Vector2 p = { campPos_.x + std::cos(angle) * radius, campPos_.y + std::sin(angle) * radius };

        if (!world.IsWalkable(p)) continue;
        if (Vector2Distance(p, StockpilePosition()) < 32.0f) continue;

        bool blocked = false;
        for (const auto& b : buildings_.All()) {
            if (Vector2Distance(b.position, p) < 32.0f) { blocked = true; break; }
        }
        if (blocked) continue;

        out = p;
        return true;
    }
    return false;
}

int Tribe::EnsureConstructionSite(const World& world, EventLog& log) {
    int existingSite = buildings_.FindActiveConstructionSite(campPos_);
    if (existingSite >= 0) return existingSite;

    // Entscheide, welches Gebäude als nächstes benötigt wird:
    BuildingType nextType = BuildingType::Hut;

    // Haben wir noch keine Werkbank in der Bronzezeit?
    if (age_.CurrentEra() >= Era::BronzeAge && buildings_.GetBuildingCount(BuildingType::CraftBench, false) == 0) {
        nextType = BuildingType::CraftBench;
    }
    // Haben wir noch keine Vorratskammer?
    else if (age_.CurrentEra() >= Era::BronzeAge && buildings_.GetBuildingCount(BuildingType::Pantry, false) == 0 && stock_.food >= 15) {
        nextType = BuildingType::Pantry;
    }
    // Wachturm in der Eisenzeit?
    else if (age_.CurrentEra() >= Era::IronAge && buildings_.GetBuildingCount(BuildingType::Watchtower, false) == 0) {
        nextType = BuildingType::Watchtower;
    }

    Vector2 spot;
    if (!FindBuildingSpot(world, spot)) return -1;

    return buildings_.PlaceBuilding(nextType, spot, stock_, log);
}

void Tribe::Draw(float time) const {
    buildings_.Draw(time);
    DrawStockpile();
}

void Tribe::DrawStockpile() const {
    const Vector2 p = StockpilePosition();
    DrawRectangleRec({ p.x - 11.0f, p.y - 2.0f, 24.0f, 6.0f }, Fade(BLACK, 0.25f));

    // Holzstapel
    for (int i = 0; i < std::min(stock_.wood, 8); ++i)
        DrawRectangleRec({ p.x - 10.0f + (float)(i % 4) * 2.2f, p.y - 2.0f - (float)(i / 4) * 2.0f, 2.0f, 2.0f },
                         { 130, 85, 40, 255 });

    // Steinhaufen
    for (int i = 0; i < std::min(stock_.stone, 8); ++i)
        DrawCircleV({ p.x + 1.0f + (float)(i % 4) * 2.0f, p.y - 1.0f - (float)(i / 4) * 2.0f }, 1.0f,
                    { 150, 148, 145, 255 });

    // Veredelte Balken
    for (int i = 0; i < std::min(stock_.planks, 6); ++i)
        DrawRectangleRec({ p.x + 6.0f, p.y - 4.0f - (float)i * 1.5f, 5.0f, 1.2f }, { 210, 150, 80, 255 });

    // Nahrung
    for (int i = 0; i < std::min(stock_.food, 10); ++i)
        DrawCircleV({ p.x - 8.0f + (float)(i % 5) * 2.2f, p.y + 4.0f + (float)(i / 5) * 2.0f }, 1.0f,
                    (i % 2) ? Color{ 200, 40, 60, 255 } : Color{ 210, 120, 110, 255 });

    // Fackeln & Werkzeuge
    if (stock_.torches > 0) {
        DrawLineEx({ p.x + 10.0f, p.y + 1.0f }, { p.x + 13.0f, p.y - 5.0f }, 1.0f, BROWN);
        DrawPixel((int)p.x + 13, (int)p.y - 5, ORANGE);
    }
}
