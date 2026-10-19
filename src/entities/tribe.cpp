#include "tribe.h"
#include "caveman.h"
#include "config.h"
#include "environmental_system.h"
#include "event_log.h"
#include "world.h"

#include <algorithm>
#include <cmath>

Tribe::Tribe(Vector2 campPosition) : campPos_(campPosition) {
    stock_.food       = cfg::kStartFood;
    stock_.cookedFood = 2;
    stock_.wood       = cfg::kStartWood;
    stock_.stone      = cfg::kStartStone;
    stock_.planks     = 0;
    stock_.bricks     = 0;
    stock_.tools      = 0;
    stock_.torches    = 2;
    stock_.pickaxes   = 1;
    stock_.provisions = 3;

    buildings_.InitDefaultBuildings(campPos_);
}

void Tribe::InitWorld(const World& world) {
    pois_.InitWorldPOIs(world, campPos_);
}

void Tribe::Update(const World& world, const std::vector<Caveman>& cavemen, int population, float dt, EventLog& log) {
    // 0. Gebäude-Tick: Brennstoffverbrauch der Feuerstellen & natürlicher Witterungsverschleiß
    bool isBadWeather = world.Env() &&
                        (world.Env()->GetWeather() == WeatherType::Rain ||
                         world.Env()->GetWeather() == WeatherType::Storm);
    buildings_.Update(dt, isBadWeather, stock_, log);

    // 1. POIs aktualisieren (Spawns nach Zeitfenstern & Entdeckung)
    pois_.Update(world, campPos_, age_.CurrentEra(), age_.EraElapsedTime(), cavemen, log);

    // 2. Skilltree aktualisieren & autonomen Fokus nach Stammes-Defizit wählen
    skillTree_.Update(pois_, log);
    bool coldUrgent = (buildings_.FindFireNeedingFuel(campPos_) >= 0);
    bool toolsUrgent = (stock_.tools < std::max(1, population / 2));
    for (const auto& c : cavemen) {
        if (!c.IsAlive()) continue;
        if (c.Warmth() < 48.0f) coldUrgent = true;
    }
    skillTree_.AutoSelectFocus(age_.CurrentEra(), coldUrgent, toolsUrgent);

    // 3. Zeitalter-Fortschritt & Forschung aktualisieren (Time-Gating + Meilensteine)
    age_.Update(stock_, buildings_, skillTree_, population, dt, log);

    // Automatischer Zeitalter-Aufstieg, sobald Epochenzeit UND Meilensteine erfüllt sind
    if (age_.CanAdvance(stock_, buildings_, skillTree_, population)) {
        age_.AdvanceEra(stock_, log);
    }

    // 4. Soziale Beziehungsdynamik & kulturelle Kohäsion durch Monumente (Meilenstein E)
    relationships_.UpdateTick(dt);
    int monumentTier = buildings_.GetMaxTier(BuildingType::Monument);
    if (monumentTier > 0) {
        for (const auto& c : cavemen) {
            if (!c.IsAlive()) continue;
            if (Clan* clan = hierarchy_.GetClanMutable(hierarchy_.GetClanId(c.Id()))) {
                clan->cohesion = std::clamp(clan->cohesion + 0.12f * (float)monumentTier * dt, 0.0f, 100.0f);
            }
        }
    }

    // 5. Nahrungskonservierung / Verderb (gekochte Mahlzeiten halten deutlich länger als Rohkost!)
    spoilageTimer_ += dt;
    int pantries = buildings_.GetBuildingCount(BuildingType::Pantry, true);
    float spoilageInterval = (pantries > 0) ? 60.0f : 16.0f;

    if (spoilageTimer_ >= spoilageInterval) {
        spoilageTimer_ = 0.0f;
        if (stock_.food > 18) {
            stock_.food--;
            skillTree_.AddPractice("masonry", 3.5f, log);
            if (pantries == 0) {
                log.Add("1 Einheit rohe Nahrung ist verdorben. Vorratskammer oder Kochen am Feuer schützt Vorräte!", { 200, 140, 100, 255 });
            }
        }
    }

    // 6. Autonomer Siedlungsplaner (Stufe 1: Vollautonome Siedlungsentwicklung)
    UpdateSettlementPlanner(world, cavemen, population, dt, log);
}

bool Tribe::NeedsMoreHuts(int population) const {
    return buildings_.TotalSleepingCapacity() < population;
}

ResourceCost Tribe::GetPlannedResourceDeficit() const {
    if (!planner_.active) return { 0, 0, 0, 0 };
    ResourceCost deficit;
    deficit.wood   = std::max(0, planner_.requiredCost.wood   - stock_.wood);
    deficit.stone  = std::max(0, planner_.requiredCost.stone  - stock_.stone);
    deficit.planks = std::max(0, planner_.requiredCost.planks - stock_.planks);
    deficit.bricks = std::max(0, planner_.requiredCost.bricks - stock_.bricks);
    return deficit;
}

bool Tribe::FindBuildingSpotAround(const World& world, Vector2 center, Vector2& out) const {
    for (int attempt = 0; attempt < 110; ++attempt) {
        const float angle = (float)GetRandomValue(0, 3600) / 3600.0f * 2.0f * PI;
        const float radius = (float)GetRandomValue(3 * TILE_SIZE, 10 * TILE_SIZE);
        const Vector2 p = { center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius };

        if (!world.IsWalkable(p)) continue;
        if (Vector2Distance(p, StockpilePosition()) < 28.0f) continue;

        bool blocked = false;
        for (const auto& b : buildings_.All()) {
            if (Vector2Distance(b.position, p) < 30.0f) { blocked = true; break; }
        }
        if (blocked) continue;

        out = p;
        return true;
    }
    return false;
}

bool Tribe::FindBuildingSpot(const World& world, Vector2& out) const {
    return FindBuildingSpotAround(world, campPos_, out);
}

void Tribe::UpdateSettlementPlanner(const World& world, const std::vector<Caveman>& cavemen, int population, float dt, EventLog& log) {
    (void)cavemen;
    plannerTimer_ += dt;
    if (plannerTimer_ < 2.5f) return;
    plannerTimer_ = 0.0f;

    // Wenn bereits eine aktive Baustelle / Reparatur / Ausbau läuft, erst diese fertigbauen lassen
    if (buildings_.FindActiveConstructionSite(campPos_) >= 0) {
        planner_.active = false;
        return;
    }

    planner_.active = false;
    planner_.isUpgrade = false;
    planner_.upgradeBuildingId = -1;
    planner_.buildCenter = campPos_;

    int eraIdx = age_.EraIndex();
    Era era = age_.CurrentEra();
    int desiredFarms = (era >= Era::BronzeAge) ? 2 : ((skillTree_.IsUnlocked("tailoring") || skillTree_.IsUnlocked("carpentry")) ? 1 : 0);

    // Priorität 1: Fehlende Schlafplätze (Betten < Pop) -> Neue Hütte planen
    if (buildings_.TotalSleepingCapacity() < population) {
        Building proto;
        proto.type = BuildingType::Hut;
        proto.tier = 1;
        planner_.active = true;
        planner_.type = BuildingType::Hut;
        planner_.requiredCost = proto.GetBuildCost();
        planner_.description = "Neue Hütte (Schlafplätze fehlen)";
    }
    // Priorität 2: Steinklingen ("flint") oder Bronzezeit erreicht & noch keine Werkbank -> Werkbank planen
    else if ((skillTree_.IsUnlocked("flint") || era >= Era::BronzeAge) &&
             buildings_.GetBuildingCount(BuildingType::CraftBench, false) == 0) {
        Building proto;
        proto.type = BuildingType::CraftBench;
        proto.tier = 1;
        planner_.active = true;
        planner_.type = BuildingType::CraftBench;
        planner_.requiredCost = proto.GetBuildCost();
        planner_.description = "Werkbank (Werkzeuge, Kleidung & Balken)";
    }
    // Priorität 2b: Neolithische Wende -> Ackerfeld (FarmPlot) nach Entdeckung von Ackerbau & Saatgut
    else if ((skillTree_.IsUnlocked("agriculture") || era >= Era::BronzeAge) &&
             buildings_.GetBuildingCount(BuildingType::FarmPlot, false) < desiredFarms) {
        Building proto;
        proto.type = BuildingType::FarmPlot;
        proto.tier = 1;
        planner_.active = true;
        planner_.type = BuildingType::FarmPlot;
        planner_.requiredCost = proto.GetBuildCost();
        planner_.description = "Ackerfeld (Getreideanbau)";
    }
    // Priorität 3: Vorräte verderben / Bronzezeit erreicht & keine Vorratskammer -> Pantry planen
    else if (buildings_.GetBuildingCount(BuildingType::Pantry, false) == 0 &&
             (stock_.food >= 16 || era >= Era::BronzeAge)) {
        Building proto;
        proto.type = BuildingType::Pantry;
        proto.tier = 1;
        planner_.active = true;
        planner_.type = BuildingType::Pantry;
        planner_.requiredCost = proto.GetBuildCost();
        planner_.description = "Vorratskammer (Schutz vor Verderb)";
    }
    // Priorität 3b: Lehmofen (Kiln) ab Fackel-Technologie / Bronzezeit für Ziegelbrand, Kochen & Verhüttung
    else if ((skillTree_.IsUnlocked("torches") || era >= Era::BronzeAge) &&
             buildings_.GetBuildingCount(BuildingType::Kiln, false) == 0) {
        Building proto;
        proto.type = BuildingType::Kiln;
        proto.tier = 1;
        planner_.active = true;
        planner_.type = BuildingType::Kiln;
        planner_.requiredCost = proto.GetBuildCost();
        planner_.description = "Lehmofen (Ziegelbrand, Kochen & Schmelze)";
    }
    // Priorität 3c: Tierzucht-Gehege (AnimalPen) ab Bronzezeit / Zimmererkunst (Meilenstein D)
    else if ((era >= Era::BronzeAge || skillTree_.IsUnlocked("carpentry")) &&
             buildings_.GetBuildingCount(BuildingType::AnimalPen, false) == 0) {
        Building proto;
        proto.type = BuildingType::AnimalPen;
        proto.tier = 1;
        planner_.active = true;
        planner_.type = BuildingType::AnimalPen;
        planner_.requiredCost = proto.GetBuildCost();
        planner_.description = "Tiergehege (Domestizierte Herde)";
    }
    // Priorität 3d: Stammes-Monument / Totempfahl (Monument) für Kultur & Moral (Meilenstein E)
    else if ((skillTree_.IsUnlocked("torches") || era >= Era::BronzeAge) &&
             buildings_.GetBuildingCount(BuildingType::Monument, false) == 0) {
        Building proto;
        proto.type = BuildingType::Monument;
        proto.tier = 1;
        planner_.active = true;
        planner_.type = BuildingType::Monument;
        planner_.requiredCost = proto.GetBuildCost();
        planner_.description = "Totempfahl (Stammeskultur & Moral)";
    }
    // Priorität 4: Stammeszentrum (TownCenter), Lehmofen (Kiln) oder Monument auf nächste Epochen-Stufe ausbauen
    else {
        for (const auto& b : buildings_.All()) {
            if ((b.type == BuildingType::TownCenter || b.type == BuildingType::Kiln || b.type == BuildingType::Monument) &&
                b.CanUpgrade(eraIdx)) {
                planner_.active = true;
                planner_.isUpgrade = true;
                planner_.upgradeBuildingId = b.id;
                planner_.type = b.type;
                planner_.requiredCost = b.GetUpgradeCost();
                planner_.description = TextFormat("Ausbau %s -> %s", BuildingTypeName(b.type, b.tier), BuildingTypeName(b.type, b.tier + 1));
                break;
            }
        }
    }

    // Priorität 5: Wachturm in der Eisenzeit
    if (!planner_.active && era >= Era::IronAge && buildings_.GetBuildingCount(BuildingType::Watchtower, false) == 0) {
        Building proto;
        proto.type = BuildingType::Watchtower;
        proto.tier = 1;
        planner_.active = true;
        planner_.type = BuildingType::Watchtower;
        planner_.requiredCost = proto.GetBuildCost();
        planner_.description = "Wachturm (Grenzschutz)";
    }

    // Priorität 6: Bestehende Hütten auf Stufe 2 (Holzhütte) / Stufe 3 (Steinhaus) ausbauen
    if (!planner_.active && era >= Era::BronzeAge) {
        for (const auto& b : buildings_.All()) {
            if (b.type == BuildingType::Hut && b.CanUpgrade(eraIdx)) {
                planner_.active = true;
                planner_.isUpgrade = true;
                planner_.upgradeBuildingId = b.id;
                planner_.type = b.type;
                planner_.requiredCost = b.GetUpgradeCost();
                planner_.description = TextFormat("Hütten-Ausbau -> %s", BuildingTypeName(BuildingType::Hut, b.tier + 1));
                break;
            }
        }
    }

    // Priorität 7: Abgespaltene Splitter-Siedlungen mit einer eigenen Hütte ausbauen
    if (!planner_.active) {
        for (const auto& s : hierarchy_.GetSplinterSettlements()) {
            int hutsNearSplinter = 0;
            for (const auto& b : buildings_.All()) {
                if (b.type == BuildingType::Hut && Vector2Distance(b.position, s.centerPos) < 95.0f) {
                    hutsNearSplinter++;
                }
            }
            if (hutsNearSplinter < 1) {
                Building proto;
                proto.type = BuildingType::Hut;
                proto.tier = 1;
                planner_.active = true;
                planner_.type = BuildingType::Hut;
                planner_.buildCenter = s.centerPos;
                planner_.requiredCost = proto.GetBuildCost();
                planner_.description = TextFormat("Hütte für %s", s.factionName.c_str());
                break;
            }
        }
    }

    if (!planner_.active) return;

    // Prüfen, ob alle benötigten Baumaterialien im Lager vorhanden sind:
    const ResourceCost& c = planner_.requiredCost;
    if (stock_.wood >= c.wood && stock_.stone >= c.stone &&
        stock_.planks >= c.planks && stock_.bricks >= c.bricks) {
        if (planner_.isUpgrade && planner_.upgradeBuildingId >= 0) {
            buildings_.StartUpgrade(planner_.upgradeBuildingId, stock_, age_, log);
            planner_.active = false;
        } else {
            Vector2 spot;
            if (FindBuildingSpotAround(world, planner_.buildCenter, spot)) {
                int id = buildings_.PlaceBuilding(planner_.type, spot, stock_, log);
                if (id >= 0) {
                    planner_.active = false;
                }
            }
        }
    }
}

int Tribe::EnsureConstructionSite(const World& world, EventLog& log) {
    int existingSite = buildings_.FindActiveConstructionSite(campPos_);
    if (existingSite >= 0) return existingSite;

    BuildingType nextType = planner_.active ? planner_.type : BuildingType::Hut;
    Vector2 center = planner_.active ? planner_.buildCenter : campPos_;

    Vector2 spot;
    if (!FindBuildingSpotAround(world, center, spot)) return -1;

    return buildings_.PlaceBuilding(nextType, spot, stock_, log);
}

void Tribe::DrawTerritoryOverlay(const World& world, float simTime) const {
    float pulse = 0.85f + 0.15f * std::sin(simTime * 2.5f);

    auto drawTerritoryZone = [&](Vector2 center, float radius, Color bannerColor, const char* title, const char* subtitle) {
        int minTx = std::max(0, World::ToTile(center.x - radius));
        int maxTx = std::min(world.Width() - 1, World::ToTile(center.x + radius));
        int minTy = std::max(0, World::ToTile(center.y - radius));
        int maxTy = std::min(world.Height() - 1, World::ToTile(center.y + radius));

        for (int ty = minTy; ty <= maxTy; ++ty) {
            for (int tx = minTx; tx <= maxTx; ++tx) {
                Vector2 tileCenter = { (tx + 0.5f) * TILE_SIZE, (ty + 0.5f) * TILE_SIZE };
                float d = Vector2Distance(center, tileCenter);
                if (d <= radius) {
                    bool isBorder = (d >= radius - (float)TILE_SIZE * 1.35f);
                    float alpha = isBorder ? (0.34f * pulse) : 0.13f;
                    DrawRectangle(tx * TILE_SIZE, ty * TILE_SIZE, TILE_SIZE, TILE_SIZE, Fade(bannerColor, alpha));
                }
            }
        }

        DrawCircleLines((int)center.x, (int)center.y, radius, Fade(bannerColor, 0.75f));
        int tw = MeasureText(title, 10);
        DrawRectangle((int)center.x - tw / 2 - 4, (int)(center.y - radius - 18.0f), tw + 8, 22, Fade(BLACK, 0.78f));
        DrawRectangleLines((int)center.x - tw / 2 - 4, (int)(center.y - radius - 18.0f), tw + 8, 22, Fade(bannerColor, 0.9f));
        DrawText(title, (int)center.x - tw / 2, (int)(center.y - radius - 16.0f), 10, bannerColor);
        if (subtitle && subtitle[0] != '\0') {
            int sw = MeasureText(subtitle, 8);
            DrawText(subtitle, (int)center.x - sw / 2, (int)(center.y - radius - 6.0f), 8, LIGHTGRAY);
        }
    };

    // 1. Haupt-Siedlungsterritorium (Farbe des regierenden Clans oder Faktion)
    Color mainColor = factionColor_;
    int chiefId = hierarchy_.GetChiefId();
    if (chiefId >= 0) {
        if (const Clan* rulingClan = hierarchy_.GetClan(hierarchy_.GetClanId(chiefId))) {
            mainColor = rulingClan->bannerColor;
        }
    }
    float mainRadius = 120.0f + (float)buildings_.GetMaxTier(BuildingType::TownCenter) * 20.0f;
    drawTerritoryZone(campPos_, mainRadius, mainColor,
                      name_.c_str(),
                      TextFormat("Hauptsiedlung (%s)", EraName(age_.CurrentEra())));

    // 2. Territorien abgespaltener Clans / Fraktionen
    for (const auto& s : hierarchy_.GetSplinterSettlements()) {
        drawTerritoryZone(s.centerPos, s.territoryRadius, s.bannerColor,
                          s.factionName.c_str(), "Unabhängiger Splitter-Stamm");
    }
}

void Tribe::Draw(float time, const World* world) const {
    buildings_.Draw(time, world);
    DrawStockpile(world);

    // Wenn der Siedlungsplaner auf Rohstoffe für das nächste Projekt wartet, dezentes Planungs-Banner am Lager
    if (planner_.active) {
        float visualY = campPos_.y + 18.0f + (world ? world->GetVisualYOffset(campPos_) : 0.0f);
        Vector2 p = { campPos_.x, visualY };
        int tw = MeasureText(planner_.description.c_str(), 8);
        DrawRectangle((int)p.x - tw / 2 - 3, (int)p.y - 1, tw + 6, 10, Fade(BLACK, 0.65f));
        DrawText(planner_.description.c_str(), (int)p.x - tw / 2, (int)p.y, 8, Fade(GOLD, 0.9f));
    }
}

void Tribe::DrawStockpile(const World* world) const {
    Vector2 p = StockpilePosition();
    if (world) {
        p.y += world->GetVisualYOffset(p);
    }
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

    // Rohe Nahrung
    for (int i = 0; i < std::min(stock_.food, 10); ++i)
        DrawCircleV({ p.x - 8.0f + (float)(i % 5) * 2.2f, p.y + 4.0f + (float)(i / 5) * 2.0f }, 1.0f,
                    (i % 2) ? Color{ 200, 40, 60, 255 } : Color{ 210, 120, 110, 255 });

    // Gebratene Mahlzeiten (goldbraun)
    for (int i = 0; i < std::min(stock_.cookedFood, 8); ++i)
        DrawCircleV({ p.x + 3.0f + (float)(i % 4) * 2.2f, p.y + 4.0f + (float)(i / 4) * 2.0f }, 1.2f,
                    { 225, 145, 55, 255 });

    // Fackeln & Werkzeuge
    if (stock_.torches > 0) {
        DrawLineEx({ p.x + 10.0f, p.y + 1.0f }, { p.x + 13.0f, p.y - 5.0f }, 1.0f, BROWN);
        DrawPixel((int)p.x + 13, (int)p.y - 5, ORANGE);
    }
}

