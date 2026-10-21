#include "tribe.h"
#include "caveman.h"
#include "config.h"
#include "environmental_system.h"
#include "event_log.h"
#include "world.h"
#include "font_manager.h"

#include <algorithm>
#include <cmath>

Tribe::ProceduralClanInfo Tribe::GenerateProceduralClan(int id, Vector2 location, const World& world) {
    int cx = World::ToTile(location.x);
    int cy = World::ToTile(location.y);

    int freshwaterTiles = 0;
    int saltwaterTiles = 0;
    int mountainTiles = 0;
    int forestTiles = 0;
    int meadowTiles = 0;

    for (int dy = -10; dy <= 10; ++dy) {
        for (int dx = -10; dx <= 10; ++dx) {
            int tx = cx + dx;
            int ty = cy + dy;
            if (!world.IsInBounds(tx, ty)) continue;
            const TileData& td = world.GetTileData(tx, ty);
            if (td.waterType == WaterType::Freshwater || td.isRiver) freshwaterTiles++;
            else if (td.waterType == WaterType::Saltwater) saltwaterTiles++;
            if (td.heightLevel >= 3 || td.isCliff || td.type == TILE_MOUNTAIN) mountainTiles++;
            if (td.type == TILE_FOREST) forestTiles++;
            if (td.type == TILE_GRASS && td.heightLevel <= 2) meadowTiles++;
        }
    }

    std::vector<std::string> prefixes;
    if (freshwaterTiles >= 6) {
        prefixes.push_back("Fluss");
        prefixes.push_back("Strom");
        prefixes.push_back("Schilf");
        prefixes.push_back("Quell");
        prefixes.push_back("Ufer");
    }
    if (saltwaterTiles >= 6) {
        prefixes.push_back("Küsten");
        prefixes.push_back("Gezeiten");
        prefixes.push_back("Wellen");
        prefixes.push_back("Sturm");
        prefixes.push_back("Muschel");
    }
    if (mountainTiles >= 5) {
        prefixes.push_back("Felsen");
        prefixes.push_back("Klippen");
        prefixes.push_back("Granit");
        prefixes.push_back("Berg");
        prefixes.push_back("Gipfel");
        prefixes.push_back("Schluchten");
    }
    if (forestTiles >= 10) {
        prefixes.push_back("Wald");
        prefixes.push_back("Eichen");
        prefixes.push_back("Schatten");
        prefixes.push_back("Moos");
        prefixes.push_back("Wurzel");
    }
    if (meadowTiles >= 20) {
        prefixes.push_back("Sonnen");
        prefixes.push_back("Steppen");
        prefixes.push_back("Wiesen");
        prefixes.push_back("Wind");
        prefixes.push_back("Horizont");
    }
    prefixes.push_back("Bären");
    prefixes.push_back("Wolfs");
    prefixes.push_back("Raben");
    prefixes.push_back("Falken");
    prefixes.push_back("Hirsch");
    prefixes.push_back("Otter");

    static const char* kSuffixes[] = {
        "Sippe", "Bund", "Jäger", "Wächter", "Volk", "Stamm", "Orden", "Läufer", "Wanderer", "Hüter"
    };

    uint32_t seedVal = (uint32_t)(id * 7919 + cx * 31 + cy * 17);
    int pIdx = (int)(seedVal % (uint32_t)prefixes.size());
    int sIdx = (int)((seedVal / 7) % (uint32_t)(sizeof(kSuffixes) / sizeof(kSuffixes[0])));

    std::string clanName = prefixes[pIdx] + "-" + kSuffixes[sIdx];

    // Prozedurale Farbpalette mit Goldenem Schnitt im HSV-Raum für maximale Farbdistanz
    float goldenRatioConjugate = 0.618033988749895f;
    float h = std::fmod((float)(id - 1) * goldenRatioConjugate * 360.0f + 25.0f, 360.0f);
    Color clanColor = ColorFromHSV(h, 0.78f, 0.92f);

    return { clanName, clanColor };
}

Tribe::Tribe(Vector2 campPosition) : campPos_(campPosition) {
    stock_.food       = cfg::kStartFood;
    stock_.cookedFood = 6;
    stock_.wood       = cfg::kStartWood;
    stock_.stone      = cfg::kStartStone;
    stock_.planks     = 0;
    stock_.bricks     = 0;
    stock_.tools      = 2;
    stock_.torches    = 2;
    stock_.pickaxes   = 1;
    stock_.provisions = 3;

    buildings_.InitDefaultBuildings(campPos_);
}

void Tribe::InitWorld(const World& world) {
    pois_.InitWorldPOIs(world, campPos_);
}

void Tribe::Update(const World& world, const std::vector<Caveman>& cavemen, int population, float dt, EventLog& log) {
    population_ = population;
    hierarchy_.Update(dt);
    // 0. Gebäude-Tick: Brennstoffverbrauch der Feuerstellen & natürlicher Witterungsverschleiß
    bool isBadWeather = world.Env() &&
                        (world.Env()->GetWeather() == WeatherType::Rain ||
                         world.Env()->GetWeather() == WeatherType::Storm);
    buildings_.Update(dt, isBadWeather, stock_, log);

    // 0b. Organische Clan-Konstituierung ("Bottom-Up")
    if (!isFormed_) {
        formationTimer_ += dt;
        bool fireActive = buildings_.GetWarmthAt(campPos_) > 0.0f;
        bool hasShelter = buildings_.GetBuildingCount(BuildingType::Hut, true) > 0;
        int clanMembers = 0;
        for (const auto& c : cavemen) {
            if (c.IsAlive() && c.ClanId() == id_) clanMembers++;
        }
        if (formationTimer_ >= 10.0f || (formationTimer_ >= 3.0f && (fireActive || hasShelter || clanMembers >= 3))) {
            isFormed_ = true;
            log.Add(TextFormat("Die Ur-Gruppe am Lagerfeuer (%.0f, %.0f) hat sich feierlich als '%s' konstituiert!",
                               campPos_.x, campPos_.y, name_.c_str()), factionColor_);
        }
    }

    // 1. POIs aktualisieren (Spawns nach Zeitfenstern & Entdeckung)
    pois_.Update(world, campPos_, age_.CurrentEra(), age_.EraElapsedTime(), cavemen, log);

    // 2. Skilltree aktualisieren & autonomen Fokus nach Stammes-Defizit wählen
    skillTree_.Update(pois_, log);
    skillTree_.CheckEpochAdvancement(&age_, &log);
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

    // 5. Nahrungskonservierung / Verderb (AUFTRAG PHASE 4: Pantry stoppt Verderb & Event-Log Rate-Limit)
    UpdateDecay(dt, log);

    // 6. Strategische Expansionsziele nach Ressourcen-Defiziten ermitteln (Phase 3)
    UpdateStrategicExpansionPlan(world);

    // 7. Autonomer Siedlungsplaner (Stufe 1 & Phase 3: Zero-Player-Bauentwicklung)
    UpdateSettlementPlanner(world, cavemen, population, dt, log);

    // 8. Territoriale Reichskapazität berechnen (Phase 1)
    UpdateEmpireCapacity(cavemen);
}

// AUFTRAG PHASE 4.2 & 4.3: Vorratskammer-Verderb & Rate-Limiting
bool Tribe::HasBuilding(BuildingType type, bool finishedOnly) const {
    return buildings_.HasBuilding(type, finishedOnly);
}

Building* Tribe::GetBuilding(BuildingType type, bool finishedOnly) {
    return buildings_.GetBuilding(type, finishedOnly);
}

const Building* Tribe::GetBuilding(BuildingType type, bool finishedOnly) const {
    return buildings_.GetBuilding(type, finishedOnly);
}

void Tribe::UpdateDecay(float dt, EventLog& log) {
    if (lastDecayLogTimer > 0.0f) {
        lastDecayLogTimer -= dt;
    }

    // AUFTRAG PHASE 4.2: Vorratskammer (Pantry) stoppt Verderb
    bool hasPantry = HasBuilding(BuildingType::Pantry) && GetBuilding(BuildingType::Pantry)->IsConstructed();
    float decayChance = 1.0f;
    if (hasPantry) {
        decayChance *= 0.0f; // Gar kein Verderb im Lager bei existierender Vorratskammer
    }

    if (decayChance <= 0.0f) {
        return; // Verderb vollständig gestoppt
    }

    spoilageTimer_ += dt;
    float spoilageInterval = 16.0f / decayChance;
    if (spoilageTimer_ >= spoilageInterval) {
        spoilageTimer_ = 0.0f;
        if (stock_.food > 18) {
            stock_.food--;
            skillTree_.AddPractice("masonry", 3.5f, log);

            // AUFTRAG PHASE 4.3: Rate-Limiting für das Event-Log (Max 1 Log-Eintrag alle 30 Sekunden)
            if (lastDecayLogTimer <= 0.0f) {
                log.Add("1 Einheit rohe Nahrung ist verdorben. Vorratskammer oder Kochen am Feuer schützt Vorräte!", { 200, 140, 100, 255 });
                EventLog::AddMessage("1 Einheit rohe Nahrung ist verdorben. Vorratskammer oder Kochen am Feuer schützt Vorräte!", { 200, 140, 100, 255 });
                lastDecayLogTimer = 30.0f;
            }
        }
    }
}

void Tribe::UpdateDecay(float dt) {
    if (EventLog::Instance()) {
        UpdateDecay(dt, *EventLog::Instance());
    }
}

void Tribe::UpdateStrategicExpansionPlan(const World& world) {
    bool needWood = stock_.wood < 8;
    bool needStone = stock_.stone < 6;
    bool needFood = (stock_.food + stock_.cookedFood) < 10;

    int cx = World::ToTile(campPos_.x);
    int cy = World::ToTile(campPos_.y);

    int bestX = -1, bestY = -1;
    float bestScore = -1.0f;
    std::string bestReason = "";

    // Suche im Umkreis von 10 bis 35 Kacheln um das Camp nach Ressourcen/POIs außerhalb der Grenzen
    for (int dy = -35; dy <= 35; ++dy) {
        for (int dx = -35; dx <= 35; ++dx) {
            int distSq = dx * dx + dy * dy;
            if (distSq < 10 * 10 || distSq > 35 * 35) continue;

            int tx = cx + dx, ty = cy + dy;
            if (!world.IsInBounds(tx, ty)) continue;

            const TileData& td = world.GetTileData(tx, ty);
            if (td.ownerTribeId == id_) continue;
            if (td.waterType != WaterType::None || td.type == TILE_DEEP_WATER ||
                td.type == TILE_SHALLOW_WATER || td.isRiver || td.isCliff) {
                continue;
            }

            float score = 0.0f;
            std::string reason = "";

            if (needWood && td.type == TILE_FOREST) {
                score += 45.0f;
                reason = "Holzvorkommen";
            }
            if (needStone && td.type == TILE_MOUNTAIN) {
                score += 40.0f;
                reason = "Steinbrüche";
            }
            if (needFood && world.IsNearFreshwater(tx, ty, 3)) {
                score += 35.0f;
                reason = "Süßwasser & Nahrung";
            }

            for (const auto& poi : pois_.All()) {
                if (poi.spawned && poi.state != POIState::Explored) {
                    float pd = Vector2Distance(World::TileCenter(tx, ty), poi.position);
                    if (pd < 3.0f * TILE_SIZE) {
                        score += 55.0f;
                        reason = "POI-Entdeckung (" + std::string(poi.name) + ")";
                        break;
                    }
                }
            }

            if (score > 0.0f) {
                float dist = std::sqrt((float)distSq);
                score -= dist * 0.45f;

                if (score > bestScore) {
                    bestScore = score;
                    bestX = tx;
                    bestY = ty;
                    bestReason = reason;
                }
            }
        }
    }

    if (bestScore > 5.0f) {
        expansionTarget_.active = true;
        expansionTarget_.position = World::TileCenter(bestX, bestY);
        expansionTarget_.reason = bestReason;
    } else {
        expansionTarget_.active = false;
    }
}

void Tribe::UpdateEmpireCapacity(const std::vector<Caveman>& cavemen) {
    int tcTier = buildings_.GetMaxTier(BuildingType::TownCenter);
    float baseCapacity = 52.0f; // Phase 3: TC Basis 45-60 Kacheln
    if (tcTier == 2) baseCapacity = 78.0f;
    else if (tcTier >= 3) baseCapacity = 115.0f;

    int adultPop = 0;
    float totalMorale = 0.0f;
    int memberCount = 0;
    for (const auto& c : cavemen) {
        if (!c.IsAlive() || c.ClanId() != id_) continue;
        memberCount++;
        totalMorale += c.Morale();
        if (c.AgeYears() >= 16.0f) {
            adultPop++;
        }
    }
    float avgMorale = (memberCount > 0) ? (totalMorale / (float)memberCount) : 50.0f;

    int huts = buildings_.GetBuildingCount(BuildingType::Hut, true);
    int workshops = buildings_.GetBuildingCount(BuildingType::CraftBench, true) +
                    buildings_.GetBuildingCount(BuildingType::Kiln, true) +
                    buildings_.GetBuildingCount(BuildingType::Pantry, true);
    int farms = buildings_.GetBuildingCount(BuildingType::FarmPlot, true) +
                buildings_.GetBuildingCount(BuildingType::AnimalPen, true);
    int monuments = buildings_.GetBuildingCount(BuildingType::Monument, true);
    int totems = buildings_.GetBuildingCount(BuildingType::BorderTotem, true);
    int outposts = buildings_.GetBuildingCount(BuildingType::Outpost, true);
    int watchtowers = buildings_.GetBuildingCount(BuildingType::Watchtower, true);

    // Alle fertigen Gebäude fungieren als territoriale Anker und erweitern die Reichskapazität
    float rawCapacity = baseCapacity + ((float)adultPop * 5.0f) +
                        ((float)huts * 4.0f) + ((float)workshops * 5.0f) +
                        ((float)farms * 6.0f) + ((float)monuments * 8.0f) +
                        ((float)totems * 14.0f) + ((float)outposts * 24.0f) + ((float)watchtowers * 35.0f);

    float moralFactor = std::clamp(0.60f + (avgMorale / 100.0f) * 0.65f, 0.60f, 1.25f);

    float eraMultiplier = 1.0f;
    if (age_.CurrentEra() == Era::BronzeAge) eraMultiplier = 1.35f;
    else if (age_.CurrentEra() == Era::IronAge) eraMultiplier = 1.80f;

    float finalCapacity = rawCapacity * moralFactor * eraMultiplier;
    maxTerritoryCapacity_ = std::max(48, (int)std::round(finalCapacity));
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
    for (int attempt = 0; attempt < 120; ++attempt) {
        const float angle = (float)GetRandomValue(0, 3600) / 3600.0f * 2.0f * PI;
        const float radius = (float)GetRandomValue(3 * TILE_SIZE, 11 * TILE_SIZE);
        const Vector2 p = { center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius };

        if (!world.IsWalkable(p)) continue;
        if (Vector2Distance(p, StockpilePosition()) < 2.0f * TILE_SIZE) continue;

        // Mindestens 3 Kacheln Abstand zum Lagerfeuer (TownCenter)
        if (Vector2Distance(p, campPos_) < 3.0f * TILE_SIZE) continue;

        // Gebäude dürfen nur im eigenen Territorium oder direkt am Grenzrand platziert werden
        int tx = World::ToTile(p.x);
        int ty = World::ToTile(p.y);
        if (!world.IsInBounds(tx, ty)) continue;
        int owner = world.GetOwnerTribeId(tx, ty);
        if (owner != id_) {
            bool bordersOwn = false;
            static const int kDx[4] = { 1, -1, 0, 0 };
            static const int kDy[4] = { 0, 0, 1, -1 };
            for (int d = 0; d < 4; ++d) {
                int nx = tx + kDx[d], ny = ty + kDy[d];
                if (world.IsInBounds(nx, ny) && world.GetOwnerTribeId(nx, ny) == id_) {
                    bordersOwn = true;
                    break;
                }
            }
            if (!bordersOwn) continue;
        }

        // Mindestens 3 Kacheln Abstand zu allen anderen Gebäuden (BLOCK 4.2)
        bool blocked = false;
        for (const auto& b : buildings_.All()) {
            if (Vector2Distance(b.position, p) < 3.0f * TILE_SIZE) { blocked = true; break; }
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

bool Tribe::FindGraveyardSpot(const World& world, Vector2& out) const {
    // BLOCK 4.2: Platziere Gräber geordnet am äußeren Rand des Dorfes (Friedhofs-/Gedenksektor, 8-14 Kacheln vom Camp)
    for (int attempt = 0; attempt < 80; ++attempt) {
        float angle = (float)GetRandomValue(30, 120) / 180.0f * PI;
        float radius = (float)GetRandomValue(8 * TILE_SIZE, 14 * TILE_SIZE);
        Vector2 p = { campPos_.x + std::cos(angle) * radius, campPos_.y - std::sin(angle) * radius };

        if (!world.IsWalkable(p)) continue;

        // Mindestens 3 Kacheln Abstand zu allen anderen Gebäuden
        bool blocked = false;
        for (const auto& b : buildings_.All()) {
            if (Vector2Distance(b.position, p) < 3.0f * TILE_SIZE) { blocked = true; break; }
        }
        if (blocked) continue;

        int tx = World::ToTile(p.x);
        int ty = World::ToTile(p.y);
        if (!world.IsInBounds(tx, ty)) continue;
        int owner = world.GetOwnerTribeId(tx, ty);
        if (owner != id_) continue;

        out = p;
        return true;
    }
    return FindBuildingSpotAround(world, campPos_, out);
}

void Tribe::UpdateSettlementPlanner(const World& world, const std::vector<Caveman>& cavemen, int population, float dt, EventLog& log) {
    (void)population;
    plannerTimer_ += dt;
    if (plannerTimer_ < 2.5f) return;
    plannerTimer_ = 0.0f;

    // Wenn Stamm ausgelöscht oder aufgelöst ist, keine Baupläne erstellen!
    if (isDisbanded_) {
        planner_.active = false;
        return;
    }

    // Zähle lebende Clanmitglieder
    int clanPop = 0;
    for (const auto& c : cavemen) {
        if (c.IsAlive() && c.ClanId() == id_) clanPop++;
    }
    if (clanPop == 0) {
        planner_.active = false;
        return;
    }

    // Wenn bereits eine Baustelle oder ein Gebäude-Ausbau im Gang ist, erst fertigstellen (maximal 1 aktive Baustelle!)
    if (buildings_.HasUnfinishedBuilding()) {
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

    // Priorität 1: Fehlende Schlafplätze (Betten < Clan-Bevölkerung) -> Neue Hütte planen
    if (buildings_.TotalSleepingCapacity() < clanPop) {
        Building proto;
        proto.type = BuildingType::Hut;
        proto.tier = 1;
        planner_.active = true;
        planner_.type = BuildingType::Hut;
        planner_.requiredCost = proto.GetBuildCost();
        planner_.description = "Neue Hütte (Schlafplätze fehlen)";
    }
    // Priorität 2: Epochen-Upgrade Stammeszentrum (Feuerkreis -> Langhaus -> Rathaus)
    else if ([&]() {
        for (const auto& b : buildings_.All()) {
            if (b.type == BuildingType::TownCenter && b.CanUpgrade(eraIdx)) {
                planner_.active = true;
                planner_.isUpgrade = true;
                planner_.upgradeBuildingId = b.id;
                planner_.type = b.type;
                planner_.requiredCost = b.GetUpgradeCost();
                planner_.description = TextFormat("Epochen-Ausbau %s -> %s", BuildingTypeName(b.type, b.tier), BuildingTypeName(b.type, b.tier + 1));
                return true;
            }
        }
        return false;
    }()) {}
    // Priorität 3: Steinklingen ("flint") oder Bronzezeit erreicht & noch keine Werkbank -> Werkbank planen
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
    // Priorität 3b: Neolithische Wende -> Ackerfeld (FarmPlot) nach Entdeckung von Ackerbau & Saatgut
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
    // Priorität 3c: Vorräte verderben / Bronzezeit erreicht & keine Vorratskammer -> Pantry planen
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
    // Priorität 3d: Lehmofen (Kiln) ab Fackel-Technologie / Bronzezeit für Ziegelbrand, Kochen & Verhüttung
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
    // Priorität 4 (BLOCK 1.2): Bestehende Hütten auf Stufe 2 (Holzhütte) / Stufe 3 (Steinhaus) ausbauen!
    else if (era >= Era::BronzeAge && [&]() {
        for (const auto& b : buildings_.All()) {
            if (b.type == BuildingType::Hut && b.CanUpgrade(eraIdx)) {
                planner_.active = true;
                planner_.isUpgrade = true;
                planner_.upgradeBuildingId = b.id;
                planner_.type = b.type;
                planner_.requiredCost = b.GetUpgradeCost();
                planner_.description = TextFormat("Hütten-Ausbau -> %s", BuildingTypeName(BuildingType::Hut, b.tier + 1));
                return true;
            }
        }
        return false;
    }()) {}
    // Priorität 5: Wachturm in der Eisenzeit
    else if (era >= Era::IronAge && buildings_.GetBuildingCount(BuildingType::Watchtower, false) == 0) {
        Building proto;
        proto.type = BuildingType::Watchtower;
        proto.tier = 1;
        planner_.active = true;
        planner_.type = BuildingType::Watchtower;
        planner_.requiredCost = proto.GetBuildCost();
        planner_.description = "Wachturm (Grenzschutz)";
    }
    // Priorität 6: Lehmofen (Kiln) oder Monument auf nächste Stufe ausbauen
    else if ([&]() {
        for (const auto& b : buildings_.All()) {
            if ((b.type == BuildingType::Kiln || b.type == BuildingType::Monument) && b.CanUpgrade(eraIdx)) {
                planner_.active = true;
                planner_.isUpgrade = true;
                planner_.upgradeBuildingId = b.id;
                planner_.type = b.type;
                planner_.requiredCost = b.GetUpgradeCost();
                planner_.description = TextFormat("Ausbau %s -> %s", BuildingTypeName(b.type, b.tier), BuildingTypeName(b.type, b.tier + 1));
                return true;
            }
        }
        return false;
    }()) {}
    // Priorität 7: Tierzucht-Gehege (AnimalPen) ab Bronzezeit / Zimmererkunst
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
    // Priorität 8: Stammes-Monument / Totempfahl (Monument) für Kultur & Moral
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

    // Priorität 8 (Phase 3): Grenz-Totem / Außenposten zur strategischen Rohstoff-Expansion
    if (!planner_.active && expansionTarget_.active) {
        int maxAnchorsAllowed = (era >= Era::BronzeAge) ? 4 : 2;
        int currentAnchors = buildings_.GetBuildingCount(BuildingType::BorderTotem, false) +
                             buildings_.GetBuildingCount(BuildingType::Outpost, false);

        if (currentAnchors < maxAnchorsAllowed) {
            float minDistToAnchor = 99999.0f;
            for (const auto& b : buildings_.All()) {
                if (b.IsTerritoryAnchor()) {
                    float d = Vector2Distance(b.position, expansionTarget_.position);
                    if (d < minDistToAnchor) minDistToAnchor = d;
                }
            }

            if (minDistToAnchor > 10.0f * TILE_SIZE) {
                Vector2 dir = Vector2Subtract(expansionTarget_.position, campPos_);
                float len = Vector2Length(dir);
                if (len > 1.0f) {
                    dir = Vector2Scale(dir, 1.0f / len);
                    Vector2 totemCandidate = Vector2Add(campPos_, Vector2Scale(dir, 9.0f * TILE_SIZE));

                    BuildingType bType = (era >= Era::BronzeAge) ? BuildingType::Outpost : BuildingType::BorderTotem;
                    Building proto;
                    proto.type = bType;
                    proto.tier = 1;

                    planner_.active = true;
                    planner_.type = bType;
                    planner_.buildCenter = totemCandidate;
                    planner_.requiredCost = proto.GetBuildCost();
                    planner_.description = TextFormat("%s (%s)", BuildingTypeName(bType, 1), expansionTarget_.reason.c_str());
                }
            }
        }
    }

    if (!planner_.active) return;

    // Mindestens 60% der Gesamtkosten müssen im Lager vorrätig sein, bevor ein Bauauftrag erteilt wird:
    const ResourceCost& c = planner_.requiredCost;
    int totalCost = c.wood + c.stone + c.planks + c.bricks;
    int curAvailable = std::min(stock_.wood, c.wood) + std::min(stock_.stone, c.stone) +
                       std::min(stock_.planks, c.planks) + std::min(stock_.bricks, c.bricks);
    if (totalCost > 0 && (float)curAvailable < (float)totalCost * 0.60f) {
        return;
    }

    // Prüfen, ob alle benötigten Baumaterialien im Lager vorhanden sind:
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
    if (buildings_.HasUnfinishedBuilding()) {
        return buildings_.FindActiveConstructionSite(campPos_);
    }

    if (planner_.active && planner_.isUpgrade && planner_.upgradeBuildingId >= 0) {
        const ResourceCost& c = planner_.requiredCost;
        if (stock_.wood >= c.wood && stock_.stone >= c.stone &&
            stock_.planks >= c.planks && stock_.bricks >= c.bricks) {
            if (buildings_.StartUpgrade(planner_.upgradeBuildingId, stock_, age_, log)) {
                int uid = planner_.upgradeBuildingId;
                planner_.active = false;
                return uid;
            }
        }
        return -1;
    }

    BuildingType nextType = planner_.active ? planner_.type : BuildingType::Hut;
    Building proto;
    proto.type = nextType;
    proto.tier = 1;
    ResourceCost cost = proto.GetBuildCost();
    int totalCost = cost.wood + cost.stone + cost.planks + cost.bricks;
    int curAvailable = std::min(stock_.wood, cost.wood) + std::min(stock_.stone, cost.stone) +
                       std::min(stock_.planks, cost.planks) + std::min(stock_.bricks, cost.bricks);
    if (totalCost > 0 && (float)curAvailable < (float)totalCost * 0.60f) {
        return -1;
    }

    Vector2 center = planner_.active ? planner_.buildCenter : campPos_;
    Vector2 spot;
    if (!FindBuildingSpotAround(world, center, spot)) return -1;

    return buildings_.PlaceBuilding(nextType, spot, stock_, log);
}

void Tribe::DrawTerritoryOverlay(const World& world, float /*simTime*/) const {
    Color mainColor = factionColor_;
    int chiefId = hierarchy_.GetChiefId();
    if (chiefId >= 0) {
        if (const Clan* rulingClan = hierarchy_.GetClan(hierarchy_.GetClanId(chiefId))) {
            mainColor = rulingClan->bannerColor;
        }
    }

    int w = world.Width();
    int h = world.Height();

    // Kachelgenaues Rendering der Grenzen (Phase 1.4):
    // 1. Dezentes halbtransparentes Farb-Overlay (Alpha 40)
    // 2. Scharfe 2.5px dicke Außenkanten an allen Grenzflächen
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const TileData& td = world.GetTileData(x, y);
            if (td.ownerTribeId != id_) continue;
            if (td.waterType != WaterType::None || td.type == TILE_DEEP_WATER ||
                td.type == TILE_SHALLOW_WATER || td.isRiver) continue;

            const float visualYOff = (float)td.heightLevel * ELEVATION_PIXEL_HEIGHT;
            const float px = (float)(x * TILE_SIZE);
            const float py = (float)(y * TILE_SIZE) - visualYOff;

            // Kachel-Füllung
            DrawRectangleRec({ px, py, (float)TILE_SIZE, (float)TILE_SIZE }, Fade(mainColor, 0.18f));

            // Scharfe Kanten zu Nachbarkacheln mit anderem Besitzer
            // Nord
            if (y == 0 || world.GetTileData(x, y - 1).ownerTribeId != id_) {
                DrawLineEx({ px, py }, { px + (float)TILE_SIZE, py }, 2.5f, mainColor);
            }
            // Süd
            if (y == h - 1 || world.GetTileData(x, y + 1).ownerTribeId != id_) {
                DrawLineEx({ px, py + (float)TILE_SIZE }, { px + (float)TILE_SIZE, py + (float)TILE_SIZE }, 2.5f, mainColor);
            }
            // West
            if (x == 0 || world.GetTileData(x - 1, y).ownerTribeId != id_) {
                DrawLineEx({ px, py }, { px, py + (float)TILE_SIZE }, 2.5f, mainColor);
            }
            // Ost
            if (x == w - 1 || world.GetTileData(x + 1, y).ownerTribeId != id_) {
                DrawLineEx({ px + (float)TILE_SIZE, py }, { px + (float)TILE_SIZE, py + (float)TILE_SIZE }, 2.5f, mainColor);
            }
        }
    }
}

void Tribe::Draw(float time, const World* world, Vector2 mouseWorld) const {
    if (isDisbanded_) return; // Ausgelöschte Stämme ausblenden (BLOCK 3.5)
    buildings_.Draw(time, world, mouseWorld);
    DrawStockpile(world);

    // Clan-Banner über dem Lagerfeuer/TownCenter (saubere Schrift & sanfte Ecken)
    float campYOffset = world ? world->GetVisualYOffset(campPos_) : 0.0f;
    Vector2 labelPos = { campPos_.x, campPos_.y - 58.0f + campYOffset };
    float tw = FontManager::MeasureText(name_.c_str(), 13.0f);
    DrawRectangleRounded({ labelPos.x - tw * 0.5f - 6.0f, labelPos.y - 3.0f, tw + 12.0f, 18.0f }, 0.35f, 4, Fade(Color{ 14, 18, 24, 255 }, 0.88f));
    DrawRectangleRoundedLinesEx({ labelPos.x - tw * 0.5f - 6.0f, labelPos.y - 3.0f, tw + 12.0f, 18.0f }, 0.35f, 4, 1.2f, factionColor_);
    FontManager::DrawTextCenteredShadow(name_.c_str(), labelPos.x, labelPos.y, 13.0f, factionColor_);

    // Wenn der Siedlungsplaner auf Rohstoffe wartet: Hover-Info bei Mouse-Hover über dem Lager (BLOCK 4.1)
    if (planner_.active && Vector2Distance(mouseWorld, campPos_) <= 1.5f * TILE_SIZE) {
        float visualY = campPos_.y - 78.0f + campYOffset;
        Vector2 p = { campPos_.x, visualY };
        float pw = FontManager::MeasureText(planner_.description.c_str(), 11.0f);
        DrawRectangleRounded({ p.x - pw * 0.5f - 6.0f, p.y - 2.0f, pw + 12.0f, 16.0f }, 0.35f, 3, Fade(BLACK, 0.75f));
        FontManager::DrawTextCenteredShadow(planner_.description.c_str(), p.x, p.y, 11.0f, Fade(GOLD, 0.95f));
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

float Tribe::CognitiveMultiplier(int population) const {
    float mult = 1.0f;

    // 1. Kultureller & technologischer Fortschritt
    Era era = age_.CurrentEra();
    if (era == Era::BronzeAge) mult += 0.25f;
    else if (era >= Era::IronAge) mult += 0.50f;

    if (skillTree_.IsUnlocked("fire")) mult += 0.05f;             // Gekochte Speisen -> Gehirnwachstum
    if (skillTree_.IsUnlocked("tailoring")) mult += 0.05f;        // Schutz vor Witterung -> mehr Denkzeit
    if (skillTree_.IsUnlocked("animal_husbandry")) mult += 0.08f;// Domestizierung
    if (skillTree_.IsUnlocked("agriculture")) mult += 0.10f;      // Sesshaftigkeit & Vorratshaltung
    if (skillTree_.IsUnlocked("megalith")) mult += 0.15f;         // Kulturelles Gedächtnis & Rituale
    if (skillTree_.IsUnlocked("carpentry")) mult += 0.05f;        // Geometrisches Bauen
    if (skillTree_.IsUnlocked("masonry")) mult += 0.10f;          // Schrift & Steinmetzkunst

    // 2. Ressourcen-Wohlstand vs. akuter Notstand
    int pop = (population > 0) ? population : 4;
    if (stock_.food >= pop * 3) {
        mult += 0.15f; // Wohlstand ermöglicht Freizeit, Lernen & Experimente
    } else if (stock_.food < pop) {
        mult -= 0.10f; // Harter Notstand dämpft kognitive Muße
    }

    // 3. Demografische Vielfalt & Gemeinschaftsgröße
    if (pop >= 8) mult += 0.10f;       // Breiter Genpool & generationenübergreifender Wissensaustausch
    else if (pop <= 2) mult -= 0.15f;  // Soziale Isolation

    return std::clamp(mult, 0.60f, 2.20f);
}

