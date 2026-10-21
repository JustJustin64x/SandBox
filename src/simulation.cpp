#include "simulation.h"
#include "config.h"
#include "font_manager.h"
#include "ai/brain_genome.h"
#include "social/social_actions.h"
#include "ai/utility_ai.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace sf {
    struct Vector2i {
        int x = 0;
        int y = 0;
        Vector2i() = default;
        Vector2i(int px, int py) : x(px), y(py) {}
    };
}

namespace {

const char* RoleDefaultNames(Role role, int id) {
    static char buf[64];
    const char* prefix = "";
    switch (role) {
        case Role::Hunter:   prefix = "Jäger"; break;
        case Role::Gatherer: prefix = "Sammlerin"; break;
        case Role::Elder:    prefix = "Ältester"; break;
        case Role::Child:    prefix = "Kind"; break;
    }
    std::snprintf(buf, sizeof(buf), "%s #%d", prefix, id);
    return buf;
}

} // namespace

Simulation::Simulation(int worldWidth, int worldHeight, int screenWidth, int screenHeight)
    : world_(worldWidth, worldHeight, 4242u),
      env_(worldWidth, worldHeight),
      camera_(screenWidth, screenHeight, world_.CenterPosition(), 2.2f) {

    world_.SetEnvironmentalSystem(&env_);

    // 1. Sprite-Sheet laden
    SpriteSheetConfig spriteConfig;
    spriteConfig.path = "assets/cavemen.png";
    spriteConfig.rows = 4;
    spriteConfig.columns = 7;
    spriteConfig.sliceMode = SliceMode::AutoDetect;
    spriteConfig.useColorKey = true;
    spriteConfig.autoDetectKeyColor = true;
    spriteConfig.drawHeight = 22.0f;
    spriteSheet_.Load(spriteConfig);

    // 2. Welt & 4 unabhängige Zivilisationen generieren
    RegenerateWorld(4242u);
}

Tribe* Simulation::GetTribe(int id) {
    for (auto& t : tribes_) {
        if (t->Id() == id) return t.get();
    }
    if (!tribes_.empty()) return tribes_[0].get();
    return nullptr;
}

const Tribe* Simulation::GetTribe(int id) const {
    for (const auto& t : tribes_) {
        if (t->Id() == id) return t.get();
    }
    if (!tribes_.empty()) return tribes_[0].get();
    return nullptr;
}

Tribe& Simulation::CurrentUiTribe() {
    if (selectedCavemanId_ != -1) {
        for (const auto& c : cavemen_) {
            if (c.Id() == selectedCavemanId_) {
                Tribe* tr = GetTribe(c.ClanId());
                if (tr) return *tr;
            }
        }
    }
    int idx = std::clamp(activeTribeIndex_, 0, (int)tribes_.size() - 1);
    if (tribes_[idx]->IsDisbanded()) {
        for (size_t i = 0; i < tribes_.size(); ++i) {
            if (!tribes_[i]->IsDisbanded()) {
                activeTribeIndex_ = (int)i;
                return *tribes_[i];
            }
        }
    }
    return *tribes_[idx];
}

const Tribe& Simulation::CurrentUiTribe() const {
    if (selectedCavemanId_ != -1) {
        for (const auto& c : cavemen_) {
            if (c.Id() == selectedCavemanId_) {
                const Tribe* tr = GetTribe(c.ClanId());
                if (tr) return *tr;
            }
        }
    }
    int idx = std::clamp(activeTribeIndex_, 0, (int)tribes_.size() - 1);
    if (tribes_[idx]->IsDisbanded()) {
        for (size_t i = 0; i < tribes_.size(); ++i) {
            if (!tribes_[i]->IsDisbanded()) {
                return *tribes_[i];
            }
        }
    }
    return *tribes_[idx];
}

int Simulation::GetAliveCount() const {
    int alive = 0;
    for (const auto& c : cavemen_) {
        if (!c.IsDead()) ++alive;
    }
    return alive;
}

void Simulation::SpawnCaveman(Role role, Vector2 worldPos, int preferredClanId, Color clanColor, Gender gender) {
    if (!world_.IsWalkable(worldPos)) return;
    int id = nextCavemanId_++;
    cavemen_.emplace_back(id, RoleDefaultNames(role, id), role, worldPos, gender);

    Tribe* assignedTribe = nullptr;
    if (preferredClanId > 0) {
        assignedTribe = GetTribe(preferredClanId);
    } else {
        // Nächstgelegenen Clan innerhalb von <= 12 Kacheln (192px) zum Einflussbereich/Camp suchen
        float closestDist = 12.0f * TILE_SIZE;
        for (const auto& tr : tribes_) {
            float dist = Vector2Distance(worldPos, tr->CampPosition());
            int tx = World::ToTile(worldPos.x);
            int ty = World::ToTile(worldPos.y);
            if (world_.GetOwnerTribeId(tx, ty) == tr->Id()) {
                dist = 0.0f; // Liegt direkt im Clan-Territorium
            }
            if (dist <= closestDist) {
                closestDist = dist;
                assignedTribe = tr.get();
            }
        }
    }

    if (assignedTribe) {
        int clanId = assignedTribe->Id();
        assignedTribe->Hierarchy().AssignToClan(id, (int)role, clanId);
        const Clan* clan = assignedTribe->Hierarchy().GetClan(clanId);
        std::string clanName = assignedTribe->Name();
        Color bColor = (clanColor.a > 0) ? clanColor : (clan ? clan->bannerColor : assignedTribe->FactionColor());
        SocialRank rank = assignedTribe->Hierarchy().GetRank(id);
        Vector2 homeCamp = assignedTribe->CampPosition();
        cavemen_.back().SyncSocialVisuals(clanId, bColor, (int)rank, homeCamp);

        eventLog_.Add(TextFormat("%s (%s %s) hat sich dem %s angeschlossen",
                                 RoleDefaultNames(role, id), RoleName(role), cavemen_.back().GenderSymbol(), clanName.c_str()),
                      bColor);
    } else {
        // Freier Einzelgänger / Wanderer in der Wildnis
        cavemen_.back().SyncSocialVisuals(-1, WHITE, (int)SocialRank::Outcast, worldPos);
        eventLog_.Add(TextFormat("%s (%s %s) durchstreift die Wildnis als freier Wanderer",
                                 RoleDefaultNames(role, id), RoleName(role), cavemen_.back().GenderSymbol()),
                      LIGHTGRAY);
    }
}

void Simulation::SpawnChildFromParents(Caveman& parentA, Caveman& parentB, Vector2 spawnPos) {
    int id = nextCavemanId_++;
    Personality childPers = Personality::Inherit(parentA.GetPersonality(), parentB.GetPersonality());
    Gender childGender = (GetRandomValue(0, 1) == 0) ? Gender::Male : Gender::Female;
    cavemen_.emplace_back(id, RoleDefaultNames(Role::Child, id), Role::Child, spawnPos, childGender);
    cavemen_.back().SetAgeYears(0.0f);
    cavemen_.back().SetPersonality(childPers);

    // Evolutionäre Genetik: Crossover der Eltern-Genome mit Mutation (6% Rate, 12% Amplitude)
    BrainGenome childGenome = BrainGenome::Crossover(parentA.GetBrainGenome(), parentB.GetBrainGenome(), 0.06f, 0.12f);
    cavemen_.back().SetBrainGenome(childGenome);

    // Kulturelle Weitergabe (Epigenetik & Starthilfe): 20% Prägung durch beide Eltern
    cavemen_.back().GetBrainWeightsMutable().AssimilateCulture(parentA.GetBrainWeights(), 0.20f);
    cavemen_.back().GetBrainWeightsMutable().AssimilateCulture(parentB.GetBrainWeights(), 0.20f);

    int parentClanId = parentA.ClanId();
    Tribe* trPtr = GetTribe(parentClanId);
    if (!trPtr) trPtr = tribes_[0].get();
    Tribe& tr = *trPtr;

    // Biologische Abstammung & Schwangerschafts-Cooldown (BLOCK 3.1)
    int motherId = (parentA.GetGender() == Gender::Female) ? parentA.Id() : parentB.Id();
    int fatherId = (parentA.GetGender() == Gender::Male) ? parentA.Id() : parentB.Id();
    cavemen_.back().SetParents(motherId, fatherId);

    if (parentA.GetGender() == Gender::Female) {
        parentA.SetPregnancyCooldown(210.0f); // 180 bis 240 Sekunden
    } else {
        parentB.SetPregnancyCooldown(210.0f);
    }

    tr.Hierarchy().AssignToClan(id, (int)Role::Child, parentClanId);

    Family* fam = tr.Hierarchy().GetOrCreateFamily(parentA.Id(), parentB.Id());
    if (fam) {
        fam->childrenIds.push_back(id);
    }

    auto setKinEdge = [&](int from, int to) {
        RelationshipEdge& e = tr.Relationships().GetOrCreate(from, to);
        e.affinity = 78.0f;
        e.trust = 85.0f;
        e.respect = 60.0f;
        e.RecalculateTag(true, false);
    };
    setKinEdge(parentA.Id(), id);
    setKinEdge(id, parentA.Id());
    setKinEdge(parentB.Id(), id);
    setKinEdge(id, parentB.Id());

    Vector2 homeCamp = tr.CampPosition();
    cavemen_.back().SyncSocialVisuals(parentClanId, tr.FactionColor(), (int)SocialRank::Worker, homeCamp);

    parentA.TriggerEmote(SocialEmote::Heart, 3.0f);
    parentB.TriggerEmote(SocialEmote::Heart, 3.0f);
    cavemen_.back().TriggerEmote(SocialEmote::Heart, 3.0f);

    eventLog_.Add(TextFormat("Nachwuchs! %s & %s bekommen %s im %s (Erbe: %s)!",
                             parentA.Name().c_str(), parentB.Name().c_str(),
                             cavemen_.back().Name().c_str(), tr.Name().c_str(),
                             childPers.GetPrimaryTraitDescription().c_str()),
                  PINK);
}

void Simulation::SpawnAnimal(Vector2 worldPos) {
    if (!world_.IsWalkable(worldPos)) return;
    int idx = resources_.SpawnAnimal(world_, worldPos);
    if (idx >= 0) {
        eventLog_.Add("Ein neues Wildtier ist aufgetaucht!", { 220, 180, 120, 255 });
    }
}

void Simulation::CycleWeather() {
    env_.CycleWeather(eventLog_);
}

std::vector<Vector2> Simulation::FindTribeSpawnLocations(int count) {
    std::vector<Vector2> locations;
    int w = world_.Width();
    int h = world_.Height();

    struct Candidate {
        int tx, ty;
        float score;
    };
    std::vector<Candidate> candidates;

    for (int y = 14; y < h - 14; y += 2) {
        for (int x = 14; x < w - 14; x += 2) {
            const TileData& td = world_.GetTileData(x, y);
            // Flachland/Grasland (Höhenstufe 1 oder 2, kein Wasser, keine Klippen)
            if (td.heightLevel < 1 || td.heightLevel > 2) continue;
            if (td.isCliff || td.waterType != WaterType::None) continue;
            if (td.type != TILE_GRASS && td.type != TILE_FOREST) continue;

            // Süßwasser-Distanz ermitteln (4 bis 10 Kacheln)
            int minFreshDist = 999;
            int minSaltDist = 999;
            int woodTiles = 0;
            int stoneTiles = 0;
            int flatTiles = 0;

            for (int dy = -18; dy <= 18; ++dy) {
                for (int dx = -18; dx <= 18; ++dx) {
                    int nx = x + dx, ny = y + dy;
                    if (!world_.IsInBounds(nx, ny)) continue;
                    int dist = std::max(std::abs(dx), std::abs(dy));
                    const TileData& ntd = world_.GetTileData(nx, ny);

                    if (ntd.waterType == WaterType::Freshwater || ntd.isRiver) {
                        if (dist < minFreshDist) minFreshDist = dist;
                    } else if (ntd.waterType == WaterType::Saltwater) {
                        if (dist < minSaltDist) minSaltDist = dist;
                    }

                    if (dist <= 4 && ntd.heightLevel <= 2 && !ntd.isCliff && ntd.waterType == WaterType::None) {
                        flatTiles++;
                    }
                    if (dist <= 18) {
                        if (ntd.type == TILE_FOREST) woodTiles++;
                        if (ntd.type == TILE_MOUNTAIN) stoneTiles++;
                    }
                }
            }

            // Radius 7: Mindestens 80% begehbares Land (Gras/Wald, kein Wasser, kein Berg, keine Klippen)
            int totalRadius7 = 0;
            int walkableRadius7 = 0;
            for (int dy = -7; dy <= 7; ++dy) {
                for (int dx = -7; dx <= 7; ++dx) {
                    if (dx * dx + dy * dy > 49) continue;
                    int nx = x + dx, ny = y + dy;
                    if (!world_.IsInBounds(nx, ny)) continue;
                    totalRadius7++;
                    const TileData& ntd = world_.GetTileData(nx, ny);
                    if ((ntd.type == TILE_GRASS || ntd.type == TILE_FOREST) &&
                        ntd.waterType == WaterType::None && !ntd.isCliff &&
                        ntd.type != TILE_MOUNTAIN && !ntd.isRiver) {
                        walkableRadius7++;
                    }
                }
            }
            if (totalRadius7 > 0 && (float)walkableRadius7 / (float)totalRadius7 < 0.80f) continue;

            // Ausschlusskriterien:
            if (minFreshDist < 3 || minFreshDist > 16) continue;
            if (minSaltDist < 5) continue;
            if (woodTiles < 6 || stoneTiles < 3) continue;
            if (flatTiles < 40) continue;

            // Scoring
            float score = 100.0f;
            score -= std::abs(minFreshDist - 6) * 6.0f;
            score += std::min(40, woodTiles) * 1.5f;
            score += std::min(30, stoneTiles) * 2.0f;
            score += flatTiles * 0.5f;

            candidates.push_back({ x, y, score });
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.score > b.score;
    });

    // AUFTRAG PHASE 2.1: Distanz-Check beim World-Spawning mit claimedSpawns und std::hypot
    std::vector<sf::Vector2i> claimedSpawns;

    for (const auto& cand : candidates) {
        bool isValid = true;
        for (const auto& spawn : claimedSpawns) {
            float distance = std::hypot((float)(cand.tx - spawn.x), (float)(cand.ty - spawn.y));
            if (distance < 45.0f) {
                isValid = false;
                break;
            }
        }

        if (isValid) {
            claimedSpawns.push_back({ cand.tx, cand.ty });
            locations.push_back(World::TileCenter(cand.tx, cand.ty));
            if ((int)locations.size() == count) break;
        }
    }

    // AUFTRAG PHASE 2.1: Fallback mit maximal 100 Random-Versuchen
    // Wenn nach 100 Random-Versuchen kein Platz mit 45 Kacheln Abstand gefunden wird, brich ab (break)
    // und spawne lieber einen Stamm weniger auf der Karte, als eine Kollision zuzulassen!
    while ((int)locations.size() < count) {
        int attempts = 0;
        bool foundValid = false;

        while (!foundValid && attempts < 100) {
            attempts++;
            int rx = GetRandomValue(15, w - 16);
            int ry = GetRandomValue(15, h - 16);

            const TileData& td = world_.GetTileData(rx, ry);
            if (td.waterType != WaterType::None || td.isCliff || td.type == TILE_MOUNTAIN) {
                continue;
            }

            bool isValid = true;
            for (const auto& spawn : claimedSpawns) {
                float distance = std::hypot((float)(rx - spawn.x), (float)(ry - spawn.y));
                if (distance < 45.0f) {
                    isValid = false;
                    break;
                }
            }

            if (isValid) {
                claimedSpawns.push_back({ rx, ry });
                locations.push_back(World::TileCenter(rx, ry));
                foundValid = true;
            }
        }

        if (!foundValid) {
            break; // Lieber einen Stamm weniger spawnen als eine Kollision zuzulassen!
        }
    }

    return locations;
}

std::vector<Vector2> Simulation::SpawnTownCenters(int count) {
    return FindTribeSpawnLocations(count);
}

std::vector<Vector2> Simulation::GenerateTribes(int count) {
    return FindTribeSpawnLocations(count);
}

void Simulation::RegenerateWorld(uint32_t seed) {
    if (seed == 0) {
        std::random_device rd;
        std::mt19937 gen(rd());
        seed = gen();
    }
    world_.RegenerateWorld(seed);
    resources_.Clear();
    cavemen_.clear();
    tribes_.clear();
    nextCavemanId_ = 1;
    selectedCavemanId_ = -1;
    activeTribeIndex_ = 0;

    // 2.1 & 2.2: Dynamische prozedurale Clans (2 bis 4 Ur-Gruppen)
    int numClans = GetRandomValue(2, 4);
    std::vector<Vector2> spawnLocs = FindTribeSpawnLocations(numClans);

    for (int i = 0; i < (int)spawnLocs.size(); ++i) {
        Tribe::ProceduralClanInfo info = Tribe::GenerateProceduralClan(i + 1, spawnLocs[i], world_);
        auto tr = std::make_unique<Tribe>(spawnLocs[i]);
        tr->SetIdentity(i + 1, info.name, info.color);
        tr->InitWorld(world_);
        world_.InitStartingTerritory(i + 1, spawnLocs[i], 52);
        tribes_.push_back(std::move(tr));
    }

    // Ressourcen & Umweltphänomene initialisieren
    resources_.Populate(world_, tribes_[0]->CampPosition());
    env_.InitDemoPhenomena(world_);

    // TEIL 3: Drastische Reduktion der Start-Population (Max. 8–18 Menschen insgesamt!)
    // Jede Ur-Gruppe startet mit nur 3 bis maximal 5 Menschen eng um das Lagerfeuer
    for (size_t t = 0; t < tribes_.size(); ++t) {
        Vector2 camp = tribes_[t]->CampPosition();
        int clanId = tribes_[t]->Id();
        Color cColor = tribes_[t]->FactionColor();

        // 3 bis 5 Menschen pro Ur-Gruppe (bei 4 Gruppen 3-4, bei 2-3 Gruppen bis 5)
        int groupSize = (tribes_.size() >= 4) ? GetRandomValue(3, 4) : GetRandomValue(3, 5);

        // 1. Jäger (Männlich)
        Vector2 pos;
        if (world_.FindRandomWalkablePosition(camp, 25.0f, pos, 30)) {
            SpawnCaveman(Role::Hunter, pos, clanId, cColor, Gender::Male);
        }
        // 2. Sammlerin (Weiblich)
        if (world_.FindRandomWalkablePosition(camp, 25.0f, pos, 30)) {
            SpawnCaveman(Role::Gatherer, pos, clanId, cColor, Gender::Female);
        }
        // 3. Weiterer Sammler oder Ältester
        Role thirdRole = (GetRandomValue(0, 100) < 40) ? Role::Elder : Role::Gatherer;
        Gender g3 = (GetRandomValue(0, 1) == 0) ? Gender::Male : Gender::Female;
        if (world_.FindRandomWalkablePosition(camp, 25.0f, pos, 30)) {
            SpawnCaveman(thirdRole, pos, clanId, cColor, g3);
        }
        // 4. Optional: Jäger oder Sammler
        if (groupSize >= 4) {
            Role r4 = (GetRandomValue(0, 100) < 50) ? Role::Hunter : Role::Gatherer;
            Gender g4 = (r4 == Role::Hunter) ? Gender::Male : ((GetRandomValue(0, 1) == 0) ? Gender::Male : Gender::Female);
            if (world_.FindRandomWalkablePosition(camp, 25.0f, pos, 30)) {
                SpawnCaveman(r4, pos, clanId, cColor, g4);
            }
        }
        // 5. Optional: Heranwachsender oder Ältester
        if (groupSize >= 5) {
            Role r5 = (GetRandomValue(0, 100) < 60) ? Role::Child : Role::Elder;
            Gender g5 = (GetRandomValue(0, 1) == 0) ? Gender::Male : Gender::Female;
            if (world_.FindRandomWalkablePosition(camp, 25.0f, pos, 30)) {
                SpawnCaveman(r5, pos, clanId, cColor, g5);
            }
        }

        // Initiale autonome Anführer-Bestimmung (Schaukampf / Wahl / Ältestenrat)
        tribes_[t]->Hierarchy().ElectLeader(cavemen_, tribes_[t]->Relationships(), eventLog_, cColor, tribes_[t]->Name(), tribes_[t]->Id());
    }

    // Zusätzliche Wildtiere
    for (int i = 0; i < cfg::kAnimalCount; ++i) {
        Vector2 pos;
        if (world_.FindRandomWalkablePosition(world_.CenterPosition(), 220.0f, pos, 30)) {
            SpawnAnimal(pos);
        }
    }

    camera_.SetTarget(tribes_[0]->CampPosition());
    eventLog_.Add(TextFormat("%d Ur-Gruppen besiedeln die Wildnis (Start-Population: %d Menschen)! Seed: %u",
                             (int)tribes_.size(), (int)cavemen_.size(), seed), GOLD);
}

void Simulation::SetCurrentAction(SelectedAction action) {
    currentAction_ = action;
    isPainting_ = false;
    switch (action) {
        case SelectedAction::NONE:
            break;
        case SelectedAction::TERRAIN_GRASS:
            selectedTile_ = TILE_GRASS;
            break;
        case SelectedAction::TERRAIN_WATER:
            selectedTile_ = TILE_SHALLOW_WATER;
            break;
        case SelectedAction::TERRAIN_FOREST:
            selectedTile_ = TILE_FOREST;
            break;
        case SelectedAction::SPAWN_MAN:
        case SelectedAction::SPAWN_WOMAN:
        case SelectedAction::SPAWN_HUNTER:
        case SelectedAction::SPAWN_ANIMAL:
        case SelectedAction::IGNITE_FIRE:
        case SelectedAction::GRAVITY_ANOMALY:
            break;
    }
}

void Simulation::HandleInput() {
    // Geschwindigkeit (F1-F3)
    if (IsKeyPressed(KEY_F1)) timeSpeed_ = 1.0f;
    if (IsKeyPressed(KEY_F2)) timeSpeed_ = 2.0f;
    if (IsKeyPressed(KEY_F3)) timeSpeed_ = 4.0f;

    // Welt neu generieren mit Taste 'R'
    if (IsKeyPressed(KEY_R)) {
        RegenerateWorld(0);
    }

    // Wetter umschalten mit Taste 'U' (oder über TAB-Menü)
    if (IsKeyPressed(KEY_U)) {
        CycleWeather();
    }

    // Territorien-Overlay umschalten mit Taste 'T'
    if (IsKeyPressed(KEY_T)) {
        showTerritoryOverlay_ = !showTerritoryOverlay_;
    }

    // Skilltree / Technologiebaum umschalten mit Taste 'K'
    if (IsKeyPressed(KEY_K)) {
        skillTreeUI_.Toggle();
        CancelActiveInput();
    }

    // Zivilisationen durchschalten (Tasten 1..9)
    for (int k = 0; k < (int)tribes_.size() && k < 9; ++k) {
        if (IsKeyPressed(KEY_ONE + k)) {
            activeTribeIndex_ = k;
            camera_.SetTarget(tribes_[k]->CampPosition());
        }
    }

    // Aktives Werkzeug (Spawnen / Malen) jederzeit abwählen mit Rechtsklick, 'Q', '0' oder Backspace
    if (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_ZERO) || IsKeyPressed(KEY_BACKSPACE)) {
        SetCurrentAction(SelectedAction::NONE);
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        if (currentAction_ != SelectedAction::NONE) {
            SetCurrentAction(SelectedAction::NONE);
        } else {
            selectedCavemanId_ = -1;
        }
    }

    Vector2 mouseScreen = GetMousePosition();

    // Klickbare Zivilisations-Badges zum Wechseln der beobachteten Zivilisation (in der oberen Leiste)
    float tabX = 135.0f;
    float tabY = 9.0f;
    float tabW = 102.0f;
    float tabH = 26.0f;
    int clickTabIdx = 0;
    for (size_t i = 0; i < tribes_.size(); ++i) {
        if (tribes_[i]->IsDisbanded()) continue;
        Rectangle tabR = { tabX + (float)clickTabIdx * 108.0f, tabY, tabW, tabH };
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouseScreen, tabR)) {
            activeTribeIndex_ = (int)i;
            camera_.SetTarget(tribes_[i]->CampPosition());
            CancelActiveInput();
            return;
        }
        clickTabIdx++;
    }

    // Wenn der Skilltree geöffnet ist, Eingaben exklusiv an das Overlay leiten
    if (skillTreeUI_.IsOpen()) {
        skillTreeUI_.Update(CurrentUiTribe(), mouseScreen, IsMouseButtonPressed(MOUSE_BUTTON_LEFT));
        return;
    }

    // Klick auf den Territorien-Button oder Skilltree-Button in der oberen Leiste
    Rectangle btnTerritory = { (float)SCREEN_WIDTH - 275.0f, 8.0f, 118.0f, 28.0f };
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouseScreen, btnTerritory)) {
        showTerritoryOverlay_ = !showTerritoryOverlay_;
        CancelActiveInput();
        return;
    }

    Rectangle btnSkillTree = { (float)SCREEN_WIDTH - 148.0f, 8.0f, 95.0f, 28.0f };
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouseScreen, btnSkillTree)) {
        skillTreeUI_.Toggle();
        CancelActiveInput();
        return;
    }

    // Optionaler Debug-Shortcut 'B' (falls der Spieler trotzdem manuell einen Bau anstoßen möchte)
    if (IsKeyPressed(KEY_B)) {
        int site = CurrentUiTribe().EnsureConstructionSite(world_, eventLog_);
        if (site < 0) {
            eventLog_.Add("Kein freier Bauplatz oder nicht genug Ressourcen!", RED);
        }
    }

    Vector2 mouseWorld = camera_.ScreenToWorld(mouseScreen);

    bool outOfVirtualScreen = (mouseScreen.x < 0.0f || mouseScreen.x > (float)SCREEN_WIDTH ||
                               mouseScreen.y < 0.0f || mouseScreen.y > (float)SCREEN_HEIGHT);
    bool inTopBar = (mouseScreen.y <= 44.0f);
    bool inSubBar = (mouseScreen.y <= 74.0f && mouseScreen.x <= 560.0f);
    bool inBottomHUD = (mouseScreen.y >= SCREEN_HEIGHT - 38.0f && mouseScreen.x <= 380.0f);
    bool inInspector = false;
    if (selectedCavemanId_ != -1) {
        float panelW = 330.0f;
        float panelH = 575.0f;
        float panelX = (float)SCREEN_WIDTH - panelW - 14.0f;
        float panelY = 52.0f;
        Rectangle inspRect = { panelX, panelY, panelW, panelH };
        if (CheckCollisionPointRec(mouseScreen, inspRect)) {
            inInspector = true;
            // Schließen-Button oben rechts [ESC] anklickbar
            Rectangle btnClose = { panelX + panelW - 52.0f, panelY + 6.0f, 40.0f, 24.0f };
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouseScreen, btnClose)) {
                selectedCavemanId_ = -1;
                return;
            }
        }
    }
    bool canInteractWithWorld = !outOfVirtualScreen && !inTopBar && !inSubBar && !inBottomHUD && !inInspector;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (canInteractWithWorld) {
            bool clickedCaveman = false;
            bool isPointSpawnTool = (currentAction_ == SelectedAction::SPAWN_MAN ||
                                     currentAction_ == SelectedAction::SPAWN_WOMAN ||
                                     currentAction_ == SelectedAction::SPAWN_HUNTER ||
                                     currentAction_ == SelectedAction::SPAWN_ANIMAL ||
                                     currentAction_ == SelectedAction::GRAVITY_ANOMALY);
            if (!isPointSpawnTool) {
                for (const auto& c : cavemen_) {
                    if (c.IsDead()) continue;
                    Vector2 cVisualPos = { c.Position().x, c.Position().y + world_.GetVisualYOffset(c.Position()) };
                    if (Vector2Distance(mouseWorld, cVisualPos) < 14.0f) {
                        selectedCavemanId_ = c.Id();
                        clickedCaveman = true;
                        break;
                    }
                }
            }

            if (!clickedCaveman) {
                switch (currentAction_) {
                    case SelectedAction::NONE:
                        // Normaler Cursor: Klick ins Leere hebt die Figuren-Selektion auf
                        selectedCavemanId_ = -1;
                        break;
                    case SelectedAction::SPAWN_MAN:
                    case SelectedAction::SPAWN_HUNTER:
                        SpawnCaveman(Role::Hunter, mouseWorld, -1, BLANK, Gender::Male);
                        break;
                    case SelectedAction::SPAWN_WOMAN:
                        SpawnCaveman(Role::Gatherer, mouseWorld, -1, BLANK, Gender::Female);
                        break;
                    case SelectedAction::SPAWN_ANIMAL:
                        SpawnAnimal(mouseWorld);
                        break;
                    case SelectedAction::IGNITE_FIRE:
                        isPainting_ = true;
                        env_.IgniteRadius(world_, mouseWorld, 14.0f, eventLog_);
                        break;
                    case SelectedAction::GRAVITY_ANOMALY:
                        env_.SpawnAntigravityAnomaly(mouseWorld, 38.0f, 35.0f, false, eventLog_);
                        break;
                    default: {
                        isPainting_ = true;
                        int tx = 0, ty = 0;
                        if (world_.GetTileAtRenderPos(mouseWorld, tx, ty)) {
                            world_.SetTile(tx, ty, selectedTile_);
                        }
                        break;
                    }
                }
            }
        }
    } else if (isPainting_ && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && currentAction_ != SelectedAction::NONE) {
        if (canInteractWithWorld) {
            int tx = 0, ty = 0;
            if (world_.GetTileAtRenderPos(mouseWorld, tx, ty)) {
                if (currentAction_ == SelectedAction::IGNITE_FIRE) {
                    env_.IgniteTile(world_, tx, ty, 0.9f);
                } else {
                    world_.SetTile(tx, ty, selectedTile_);
                }
            }
        }
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        isPainting_ = false;
    }
}

void Simulation::UpdateDemographicsAndFactions(float dt) {
    // 1. Soziale Visualisierung (Clan-Farbring, Rang-Krone, Heimatlager) für alle Figuren synchronisieren
    for (auto& c : cavemen_) {
        if (c.IsDead()) continue;
        Tribe* tr = GetTribe(c.ClanId());
        if (!tr) continue;
        SocialRank rank = tr->Hierarchy().GetRank(c.Id());
        c.SyncSocialVisuals(tr->Id(), tr->FactionColor(), (int)rank, tr->CampPosition());
    }

    // 2. Stufe 3: Demografie, Fortpflanzung & genetische Vererbung pro Stamm
    demographicTimer_ += dt;
    if (demographicTimer_ >= 4.0f) {
        demographicTimer_ = 0.0f;

        for (auto& tr : tribes_) {
            int tribePop = 0;
            for (const auto& c : cavemen_) {
                if (!c.IsDead() && c.ClanId() == tr->Id()) tribePop++;
            }
            tr->SetPopulation(tribePop);

            int totalFood = tr->Stock().food + tr->Stock().cookedFood;
            bool hasShelter = tr->Buildings().GetBuildingCount(BuildingType::Hut, true) > 0;

            // 2a. Prüfe schwangere Mütter auf Geburt (AUFTRAG PHASE 1.1)
            for (size_t i = 0; i < cavemen_.size(); ++i) {
                if (cavemen_[i].IsDead() || cavemen_[i].ClanId() != tr->Id()) continue;
                if (!cavemen_[i].IsPregnant()) continue;

                if (cavemen_[i].PregnancyTimer() >= 12.0f) {
                    int partnerId = tr->Relationships().GetPartner(cavemen_[i].Id());
                    int partnerIdx = -1;
                    if (partnerId >= 0) {
                        for (size_t j = 0; j < cavemen_.size(); ++j) {
                            if (!cavemen_[j].IsDead() && cavemen_[j].Id() == partnerId) {
                                partnerIdx = (int)j;
                                break;
                            }
                        }
                    }

                    // Strikte Biologie (AUFTRAG PHASE 1.1): Zwingend 1x Mann und 1x Frau
                    if (partnerIdx >= 0 && cavemen_[i].GetGender() != cavemen_[partnerIdx].GetGender()) {
                        Vector2 spawnPos = cavemen_[i].Position();
                        int hutId = tr->Buildings().FindBestShelter(spawnPos);
                        if (hutId >= 0) {
                            if (const Building* hut = tr->Buildings().Get(hutId)) {
                                world_.FindRandomWalkablePosition(hut->position, 16.0f, spawnPos, 15);
                            }
                        } else {
                            world_.FindRandomWalkablePosition(tr->CampPosition(), 20.0f, spawnPos, 15);
                        }

                        if (tr->Stock().food >= 2) tr->Stock().food -= 2;
                        else if (tr->Stock().cookedFood >= 2) tr->Stock().cookedFood -= 2;

                        cavemen_[i].SetPregnant(false);
                        SpawnChildFromParents(cavemen_[i], cavemen_[partnerIdx], spawnPos);
                    } else {
                        cavemen_[i].SetPregnant(false);
                    }
                }
            }

            // 2b. Ambient-Fortpflanzung für bestehende Paare (Populations-Kapazität bis 50)
            if (tribePop < 50 && totalFood >= 4 && (hasShelter || tribePop < 10)) {
                for (size_t i = 0; i < cavemen_.size(); ++i) {
                    if (cavemen_[i].IsDead() || cavemen_[i].ClanId() != tr->Id() || cavemen_[i].GetRole() == Role::Child) continue;
                    if (cavemen_[i].Hunger() > 65.0f || cavemen_[i].IsPregnant()) continue;

                    int partnerId = tr->Relationships().GetPartner(cavemen_[i].Id());
                    if (partnerId < 0 || partnerId < cavemen_[i].Id()) continue;

                    int partnerIdx = -1;
                    for (size_t j = 0; j < cavemen_.size(); ++j) {
                        if (!cavemen_[j].IsDead() && cavemen_[j].Id() == partnerId && cavemen_[j].GetRole() != Role::Child) {
                            partnerIdx = (int)j;
                            break;
                        }
                    }
                    if (partnerIdx < 0) continue;

                    // Strikte Biologie (AUFTRAG PHASE 1.1): Zwingend 1x Mann und 1x Frau
                    if (cavemen_[i].GetGender() == cavemen_[partnerIdx].GetGender()) continue;

                    // Schwangerschafts-Cooldown (180-240s) für die Mutter prüfen
                    Caveman& mother = (cavemen_[i].GetGender() == Gender::Female) ? cavemen_[i] : cavemen_[partnerIdx];
                    Caveman& father = (cavemen_[i].GetGender() == Gender::Male) ? cavemen_[i] : cavemen_[partnerIdx];
                    if (mother.PregnancyCooldown() > 0.0f || mother.IsPregnant()) continue;

                    // Inzest-Sperre prüfen (AUFTRAG PHASE 1.2)
                    if (!SocialActionEvaluator::CanBecomePartners(&mother, &father)) continue;

                    // Familien-Limit (maximal 3-4 Kinder pro Paar)
                    Family* fam = tr->Hierarchy().GetOrCreateFamily(cavemen_[i].Id(), cavemen_[partnerIdx].Id());
                    if (fam && fam->childrenIds.size() >= 4) continue;

                    mother.SetPregnant(true);
                    mother.SetPregnancyCooldown(210.0f);
                    mother.TriggerEmote(SocialEmote::Heart, 3.5f);
                    father.TriggerEmote(SocialEmote::Heart, 3.5f);
                    break;
                }
            }
        }
    }
}

void Simulation::CheckLeadershipChallenges(float dt) {
    challengeTimer_ += dt;
    if (challengeTimer_ < 12.0f) return;
    challengeTimer_ = 0.0f;

    for (auto& tr : tribes_) {
        tr->Hierarchy().CheckLeadershipChallenge(*tr, cavemen_, eventLog_);
    }
}

bool Simulation::FindSecessionSettlementSpot(Vector2& outPos) const {
    struct Candidate {
        int tx, ty;
        float score;
    };
    std::vector<Candidate> candidates;

    for (int y = 10; y < world_.Height() - 10; y += 2) {
        for (int x = 10; x < world_.Width() - 10; x += 2) {
            const TileData& td = world_.GetTileData(x, y);
            if (td.ownerTribeId != -1) continue; // Nur neutrales Land!
            if (td.waterType != WaterType::None || td.isCliff || td.heightLevel > 2) continue;
            if (td.type != TILE_GRASS && td.type != TILE_FOREST) continue;

            Vector2 pos = World::TileCenter(x, y);
            // Mindestabstand zu existierenden Camps: mind. 35 Kacheln
            bool tooClose = false;
            for (const auto& tr : tribes_) {
                float distTiles = Vector2Distance(pos, tr->CampPosition()) / (float)TILE_SIZE;
                if (distTiles < 35.0f) {
                    tooClose = true;
                    break;
                }
            }
            if (tooClose) continue;

            // Qualität des Siedlungsortes bewerten (Süßwasser, Wald, Stein)
            int woodTiles = 0, stoneTiles = 0, freshTiles = 0, flatTiles = 0;
            for (int dy = -8; dy <= 8; ++dy) {
                for (int dx = -8; dx <= 8; ++dx) {
                    int nx = x + dx, ny = y + dy;
                    if (!world_.IsInBounds(nx, ny)) continue;
                    const TileData& ntd = world_.GetTileData(nx, ny);
                    if (ntd.waterType == WaterType::Freshwater || ntd.isRiver) freshTiles++;
                    if (ntd.type == TILE_FOREST) woodTiles++;
                    if (ntd.type == TILE_MOUNTAIN) stoneTiles++;
                    if (std::abs(dx) <= 3 && std::abs(dy) <= 3 && ntd.heightLevel <= 2 && !ntd.isCliff) flatTiles++;
                }
            }

            if (flatTiles < 20) continue;
            float score = (float)flatTiles * 1.5f + (float)freshTiles * 3.0f + (float)woodTiles * 1.2f + (float)stoneTiles * 1.8f;
            candidates.push_back({ x, y, score });
        }
    }

    if (candidates.empty()) return false;
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.score > b.score;
    });

    int pickIdx = GetRandomValue(0, std::min(4, (int)candidates.size() - 1));
    outPos = World::TileCenter(candidates[pickIdx].tx, candidates[pickIdx].ty);
    return true;
}

void Simulation::CheckAndExecuteSecession(float dt) {
    schismTimer_ += dt;
    if (schismTimer_ < 16.0f) return;
    schismTimer_ = 0.0f;

    // Maximale Clan-Anzahl zur Stabilität begrenzen
    if (tribes_.size() >= 8) return;

    for (size_t tIdx = 0; tIdx < tribes_.size(); ++tIdx) {
        Tribe& parentTribe = *tribes_[tIdx];

        int memberCount = 0;
        int chiefId = parentTribe.Hierarchy().GetChiefIdForClan(parentTribe.Id());
        Caveman* rebel = nullptr;
        float lowestLoyalty = 20.0f; // Schwellenwert < 20

        for (auto& c : cavemen_) {
            if (c.IsDead() || c.ClanId() != parentTribe.Id()) continue;
            memberCount++;
            if (c.GetRole() == Role::Child || c.Id() == chiefId) continue;

            if (c.Loyalty() < lowestLoyalty) {
                lowestLoyalty = c.Loyalty();
                rebel = &c;
            }
        }

        // Parent Clan muss mind. 12 Mitglieder haben, damit er nicht ausstirbt & Sezession realistisch ist
        if (memberCount < 12 || !rebel) continue;

        // Suizid-Schisma verhindern (AUFTRAG PHASE 2.2): Anführer muss gesund und satt sein
        float timeOfDay = std::fmod(totalSimTime_, cfg::kDayCycleDuration) / cfg::kDayCycleDuration;
        WorldContext ctx{ world_, parentTribe, resources_, eventLog_, memberCount, timeOfDay, &cavemen_, &tribes_, &env_ };
        std::string secedeReason;
        if (UtilityAI::CalculateSecedeScore(rebel, ctx, secedeReason) <= 0.0f) continue;

        // Finde passenden Siedlungsplatz im unbesiedelten, neutralen Land
        Vector2 targetCampPos;
        if (!FindSecessionSettlementSpot(targetCampPos)) continue;

        // Gefährten rekrutieren: Partner und enge Freunde (müssen ebenfalls lebensfähig sein)
        std::vector<Caveman*> followers;
        int partnerId = parentTribe.Relationships().GetPartner(rebel->Id());

        for (auto& c : cavemen_) {
            if (c.IsDead() || c.ClanId() != parentTribe.Id() || c.Id() == rebel->Id() || c.Id() == chiefId) continue;
            if (c.Hp() < 70.0f || c.Warmth() < 50.0f || c.Hunger() > 50.0f) continue;

            if (c.Id() == partnerId) {
                followers.push_back(&c);
            } else if (followers.size() < 2) {
                float aff = parentTribe.Relationships().GetAffinity(c.Id(), rebel->Id());
                float tru = parentTribe.Relationships().GetTrust(c.Id(), rebel->Id());
                if (aff >= 40.0f || tru >= 55.0f || c.Loyalty() < 30.0f) {
                    followers.push_back(&c);
                }
            }
        }

        // Nicht allein in die Wildnis abspalten
        if (followers.empty()) continue;

        int newClanId = 1;
        for (const auto& tr : tribes_) {
            if (tr->Id() >= newClanId) newClanId = tr->Id() + 1;
        }

        auto clanInfo = Tribe::GenerateProceduralClan(newClanId, targetCampPos, world_);
        auto newTribe = std::make_unique<Tribe>(targetCampPos);
        newTribe->SetIdentity(newClanId, clanInfo.name, clanInfo.color);
        newTribe->SetFormed(true);
        newTribe->InitWorld(world_);

        // Startvorräte für die Pioniere
        newTribe->Stock().food = 6;
        newTribe->Stock().wood = 8;
        newTribe->Stock().stone = 4;
        newTribe->Stock().torches = 2;

        // Start-Territorium beanspruchen (Basis 45-60 Kacheln)
        world_.InitStartingTerritory(newClanId, targetCampPos, 48);

        std::string oldClanName = parentTribe.Name();

        // Abtrünniger wird Häuptling des neuen Stammes
        rebel->SyncSocialVisuals(newClanId, clanInfo.color, (int)SocialRank::Chief, targetCampPos);
        newTribe->Hierarchy().AssignToClan(rebel->Id(), (int)rebel->GetRole(), newClanId);
        newTribe->Hierarchy().SetRank(rebel->Id(), SocialRank::Chief);
        rebel->SetLoyalty(90.0f);
        rebel->TriggerEmote(SocialEmote::Crown, 5.0f);
        rebel->SetDestinationDirect(world_, targetCampPos, true);

        // Gefolgschaft migriert mit
        for (auto* f : followers) {
            f->SyncSocialVisuals(newClanId, clanInfo.color, (int)SocialRank::Worker, targetCampPos);
            newTribe->Hierarchy().AssignToClan(f->Id(), (int)f->GetRole(), newClanId);
            f->SetLoyalty(80.0f);
            f->TriggerEmote(SocialEmote::Chat, 4.0f);
            f->SetDestinationDirect(world_, targetCampPos, true);
        }

        eventLog_.Add(TextFormat("[SCHISMA & NEUGRÜNDUNG] %s bricht mit dem %s und führt %d Gefährten ins Exil! Neuer Stamm gegründet: %s",
                                 rebel->Name().c_str(), oldClanName.c_str(), (int)followers.size() + 1, clanInfo.name.c_str()),
                      clanInfo.color);

        tribes_.push_back(std::move(newTribe));
        break; // Nur ein Schisma pro Zyklus
    }
}

void Simulation::CheckTownCenterConquest() {
    if (tribes_.size() <= 1) return;

    for (size_t i = 0; i < tribes_.size(); ++i) {
        Tribe& defTribe = *tribes_[i];
        int campTx = World::ToTile(defTribe.CampPosition().x);
        int campTy = World::ToTile(defTribe.CampPosition().y);
        int campOwner = world_.GetOwnerTribeId(campTx, campTy);

        if (campOwner >= 0 && campOwner != defTribe.Id()) {
            Tribe* conqTribe = GetTribe(campOwner);
            if (!conqTribe) continue;

            std::string defName = defTribe.Name();
            std::string conqName = conqTribe->Name();
            Color conqColor = conqTribe->FactionColor();
            int defId = defTribe.Id();
            int conqId = conqTribe->Id();

            // Territorium übertragen:
            for (int y = 0; y < world_.Height(); ++y) {
                for (int x = 0; x < world_.Width(); ++x) {
                    if (world_.GetOwnerTribeId(x, y) == defId) {
                        world_.SetOwnerTribeId(x, y, conqId);
                        world_.SetClaimStrength(x, y, 35.0f);
                    }
                }
            }

            // Überlebende assimilieren:
            int assimilated = 0;
            for (auto& c : cavemen_) {
                if (c.IsAlive() && c.ClanId() == defId) {
                    c.SyncSocialVisuals(conqId, conqColor, (int)SocialRank::Worker, conqTribe->CampPosition());
                    conqTribe->Hierarchy().AssignToClan(c.Id(), (int)c.GetRole(), conqId);
                    c.SetLoyalty(50.0f);
                    c.TriggerEmote(SocialEmote::Chat, 4.0f);
                    c.SetDestinationDirect(world_, conqTribe->CampPosition(), true);
                    assimilated++;
                }
            }

            eventLog_.Add(TextFormat("[STAMMESUNTERGANG] Das Hauptlager des %s wurde von %s erobert! %d Überlebende wurden assimiliert.",
                                     defName.c_str(), conqName.c_str(), assimilated), conqColor);

            tribes_.erase(tribes_.begin() + i);
            if (activeTribeIndex_ >= (int)tribes_.size()) {
                activeTribeIndex_ = std::max(0, (int)tribes_.size() - 1);
            }
            break;
        }
    }
}

void Simulation::Update(float dt) {
    HandleInput();
    camera_.Update(dt);

    if (isPaused_) return;

    float effectiveDt = dt * timeSpeed_;
    totalSimTime_ += effectiveDt;
    eventLog_.SetTime(totalSimTime_);

    float timeOfDay = std::fmod(totalSimTime_, cfg::kDayCycleDuration) / cfg::kDayCycleDuration;

    // Sub-stepping für stabile Logik
    float remaining = effectiveDt;
    while (remaining > 0.0f) {
        float step = std::min(remaining, cfg::kMaxSimStep);

        // 1. Umwelt-Physik, Feuer, Strom, Wetter, Partikel & dynamische Wege updaten
        env_.Update(world_, resources_, CurrentUiTribe().Buildings(), cavemen_, eventLog_, timeOfDay, step);
        world_.UpdateRoads(step);
        world_.UpdateTerritories(tribes_, cavemen_, step);

        // 2. Ressourcen updaten
        for (const auto& t : tribes_) {
            resources_.Update(world_, t->CampPosition(), step);
        }

        // 3. Stämme, autonomer Siedlungsplaner & Führung updaten
        int population = GetAliveCount();
        for (auto& t : tribes_) {
            if (t->IsDisbanded()) continue;

            // Prüfe auf Untergang / Auslöschung: Wenn Stamm geformt ist, aber keine lebenden Erwachsenen mehr existieren
            if (t->IsFormed()) {
                int livingAdults = 0;
                for (const auto& c : cavemen_) {
                    if (!c.IsDead() && c.ClanId() == t->Id() && c.GetRole() != Role::Child && c.AgeYears() >= 16.0f) {
                        livingAdults++;
                    }
                }
                if (livingAdults == 0) {
                    t->SetDisbanded(true);
                    eventLog_.Add(TextFormat("[Untergang] Der Stamm '%s' hat keine überlebenden Erwachsenen mehr und löst sich auf!",
                                             t->Name().c_str()), t->FactionColor());

                    // Suche nächsten aktiven, nicht aufgelösten Stamm für Waisen-Adoption
                    Tribe* nearestActiveTribe = nullptr;
                    float nearestDist = 1e9f;
                    for (auto& other : tribes_) {
                        if (!other->IsDisbanded() && other->Id() != t->Id()) {
                            float d = Vector2Distance(t->CampPosition(), other->CampPosition());
                            if (d < nearestDist) {
                                nearestDist = d;
                                nearestActiveTribe = other.get();
                            }
                        }
                    }

                    // Alle überlebenden Kinder / Waisen werden vom Nachbarstamm aufgenommen
                    for (auto& c : cavemen_) {
                        if (!c.IsDead() && c.ClanId() == t->Id()) {
                            if (nearestActiveTribe) {
                                c.SyncSocialVisuals(nearestActiveTribe->Id(), nearestActiveTribe->FactionColor(), (int)SocialRank::Worker, nearestActiveTribe->CampPosition());
                                nearestActiveTribe->Hierarchy().AssignToClan(c.Id(), (int)c.GetRole(), nearestActiveTribe->Id());
                                eventLog_.Add(TextFormat("%s (Kind) wurde als Waise vom %s aufgenommen.",
                                                         c.Name().c_str(), nearestActiveTribe->Name().c_str()), nearestActiveTribe->FactionColor());
                            }
                        }
                    }
                    continue;
                }
            }

            t->Update(world_, cavemen_, population, step, eventLog_);
            t->Hierarchy().UpdateLeadership(t->Relationships(), cavemen_, eventLog_, t->FactionColor(), t->Name(), t->Id());
        }

        // 4. Demografie (Nachwuchs & Vererbung), Herausforderungs-Duelle, Schisma & Clan-Visuals
        UpdateDemographicsAndFactions(step);
        CheckLeadershipChallenges(step);
        CheckAndExecuteSecession(step);
        CheckTownCenterConquest();

        // 5. Steinzeitmenschen mit Utility AI, Soziologie & Umwelt-Kontext updaten
        for (auto& c : cavemen_) {
            Tribe* tr = GetTribe(c.ClanId());
            if (!tr) tr = tribes_[0].get();
            WorldContext ctx{ world_, *tr, resources_, eventLog_, population, timeOfDay, &cavemen_, &tribes_, &env_ };
            c.Update(ctx, step);
        }

        remaining -= step;
    }
}

void Simulation::DrawRelationshipLines(const Caveman& selected) const {
    float fromYOffset = world_.GetVisualYOffset(selected.Position());
    Vector2 from = { selected.Position().x, selected.Position().y - 8.0f + fromYOffset };
    int selId = selected.Id();
    const Tribe* trPtr = GetTribe(selected.ClanId());
    if (!trPtr) trPtr = tribes_[0].get();
    const Tribe& tr = *trPtr;

    for (const auto& other : cavemen_) {
        if (other.IsDead() || other.Id() == selId) continue;

        const RelationshipEdge* edge = tr.Relationships().Get(selId, other.Id());
        RelationshipTag tag = edge ? edge->tag : RelationshipTag::Stranger;
        bool isPartner = (tr.Relationships().GetPartner(selId) == other.Id());
        bool isKin = tr.Hierarchy().AreKin(selId, other.Id());

        Color lineColor = BLANK;
        const char* relLabel = nullptr;
        float thickness = 1.2f;

        if (isPartner || tag == RelationshipTag::Partner) {
            lineColor = Color{ 255, 105, 180, 235 }; // Pink: Partner
            relLabel = "Partner";
            thickness = 2.0f;
        } else if (tag == RelationshipTag::Enemy || tag == RelationshipTag::Rival) {
            lineColor = Color{ 245, 70, 70, 215 };   // Rot: Rivalen/Feinde
            relLabel = (tag == RelationshipTag::Enemy) ? "Feind" : "Rivale";
            thickness = 1.5f;
        } else if (tag == RelationshipTag::BestFriend) {
            lineColor = Color{ 90, 235, 115, 210 };  // Grün: Bester Freund
            relLabel = "Bester Freund";
            thickness = 1.5f;
        } else if (isKin) {
            lineColor = Color{ 255, 215, 70, 160 };  // Gold: Familie/Verwandtschaft
            thickness = 1.2f;
        } else if (tag == RelationshipTag::Friend) {
            lineColor = Color{ 90, 235, 115, 140 };  // Grün: Freunde
            thickness = 1.0f;
        }

        if (lineColor.a > 0) {
            float toYOffset = world_.GetVisualYOffset(other.Position());
            Vector2 to = { other.Position().x, other.Position().y - 8.0f + toYOffset };
            DrawLineEx(from, to, thickness, lineColor);
            DrawCircleV(to, 2.5f, lineColor);

            if (relLabel != nullptr) {
                Vector2 mid = { (from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f - 6.0f };
                float tw = FontManager::MeasureText(relLabel, 11.0f);
                DrawRectangleRounded({ mid.x - tw * 0.5f - 4.0f, mid.y - 1.0f, tw + 8.0f, 15.0f }, 0.35f, 3, Fade(BLACK, 0.75f));
                FontManager::DrawTextCenteredShadow(relLabel, mid.x, mid.y + 1.0f, 11.0f, lineColor);
            }
        }
    }
}

void Simulation::DrawWorld() const {
    BeginMode2D(camera_.Get());

    // 1. Kachel-Welt + Phase-1-Kachelzustände (Feuer, Eis, Krater, Strombögen)
    world_.Draw(camera_.Get());

    Vector2 mouseWorld = GetScreenToWorld2D(GetMousePosition(), camera_.Get());

    // Territorien & Gebäude aller unabhängigen Stämme zeichnen
    for (const auto& t : tribes_) {
        if (showTerritoryOverlay_) {
            t->DrawTerritoryOverlay(world_, totalSimTime_);
        }
        t->POIs().Draw(camera_.Get(), totalSimTime_);
        t->Draw(totalSimTime_, &world_, mouseWorld, false); // AUFTRAG PHASE 3: Gebäude separat im Y-Sort zeichnen
    }

    env_.DrawTileOverlays(world_, camera_.Get(), totalSimTime_);

    // 2. Ressourcen (inkl. 3D-Bäume mit Drop Shadows auf Geländehöhe)
    resources_.Draw(camera_.Get(), &world_);

    // 5. Phase-1-Welteffekte (Energie-Kristalle, Boden-Drops, Antigrav-Anomalien, Partikel)
    env_.DrawWorldEffects(world_, camera_.Get(), totalSimTime_);

    // 6. Nacht-Lichtstimmung
    float timeOfDay = std::fmod(totalSimTime_, cfg::kDayCycleDuration) / cfg::kDayCycleDuration;
    bool isNight = (timeOfDay > 0.72f || timeOfDay < 0.22f);
    if (isNight) {
        DrawRectangle(0, 0, world_.Width() * TILE_SIZE, world_.Height() * TILE_SIZE, Fade(Color{ 10, 15, 45, 255 }, 0.22f));
    }

    // 6b. Beziehungslinien der selektierten Figur (Stufe 2: Show, Don't Tell)
    if (selectedCavemanId_ != -1) {
        for (const auto& c : cavemen_) {
            if (c.Id() == selectedCavemanId_ && !c.IsDead()) {
                DrawRelationshipLines(c);
                break;
            }
        }
    }

    // 7. Strikte 2.5D Tiefen-/Y-Sortierung: Gebäude & Figuren gemeinsam sortieren
    // AUFTRAG PHASE 3: Figuren stehen korrekt hinter oder vor Gebäuden
    struct YDrawable {
        float y;
        std::function<void()> draw;
    };
    std::vector<YDrawable> renderList;
    renderList.reserve(cavemen_.size() + 64);

    const Vector2 viewTL = GetScreenToWorld2D({ 0.0f, 0.0f }, camera_.Get());
    const Vector2 viewBR = GetScreenToWorld2D({ (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT }, camera_.Get());
    const Rectangle cullRect = { viewTL.x - 48.0f, viewTL.y - 64.0f, (viewBR.x - viewTL.x) + 96.0f, (viewBR.y - viewTL.y) + 128.0f };

    // A) Gebäude aller aktiven Stämme
    for (const auto& t : tribes_) {
        if (t->IsDisbanded()) continue;
        for (const auto& b : t->Buildings().All()) {
            if (!CheckCollisionPointRec(b.position, cullRect)) continue;
            float visualY = b.position.y + world_.GetVisualYOffset(b.position);
            renderList.push_back({
                visualY,
                [tPtr = t.get(), b, visualY, mouseWorld, this]() {
                    Building bCopy = b;
                    bCopy.position.y = visualY;
                    tPtr->Buildings().DrawBuilding(bCopy, totalSimTime_, mouseWorld);
                }
            });
        }
    }

    // B) Lebende Steinzeitmenschen
    for (const auto& c : cavemen_) {
        if (c.IsDead()) continue;
        if (!CheckCollisionPointRec(c.Position(), cullRect)) continue;
        float visualY = c.Position().y + world_.GetVisualYOffset(c.Position());
        renderList.push_back({
            visualY,
            [&c, visualY, this, isNight]() {
                bool highlighted = (c.Id() == selectedCavemanId_);
                if (highlighted) {
                    c.DrawDebugPath();
                }
                c.Draw(spriteSheet_, highlighted, visualY - c.Position().y, isNight);
            }
        });
    }

    // C) Sortierung nach visueller Y-Koordinate (Top-to-Bottom)
    std::sort(renderList.begin(), renderList.end(), [](const YDrawable& a, const YDrawable& b) {
        return a.y < b.y;
    });

    // D) In korrekter Z-Reihenfolge zeichnen
    for (const auto& item : renderList) {
        item.draw();
    }

    // 8. Aktions-Vorschau an Mausposition (nur wenn ein Werkzeug aktiv ausgewählt ist)
    Vector2 mouseScreen = GetMousePosition();
    if (currentAction_ != SelectedAction::NONE &&
        mouseScreen.x >= 0 && mouseScreen.x <= SCREEN_WIDTH && mouseScreen.y > 48 && mouseScreen.y < SCREEN_HEIGHT - 36) {
        Vector2 mouseWorld = camera_.ScreenToWorld(mouseScreen);
        if (currentAction_ == SelectedAction::SPAWN_MAN || currentAction_ == SelectedAction::SPAWN_HUNTER) {
            DrawCircleLines((int)mouseWorld.x, (int)mouseWorld.y, 14.0f, SKYBLUE);
        } else if (currentAction_ == SelectedAction::SPAWN_WOMAN) {
            DrawCircleLines((int)mouseWorld.x, (int)mouseWorld.y, 14.0f, PINK);
        } else if (currentAction_ == SelectedAction::SPAWN_ANIMAL) {
            DrawCircleLines((int)mouseWorld.x, (int)mouseWorld.y, 14.0f, ORANGE);
        } else if (currentAction_ == SelectedAction::IGNITE_FIRE) {
            DrawCircleLines((int)mouseWorld.x, (int)mouseWorld.y, 14.0f, RED);
        } else if (currentAction_ == SelectedAction::GRAVITY_ANOMALY) {
            DrawCircleLines((int)mouseWorld.x, (int)mouseWorld.y, 38.0f, Color{ 150, 120, 255, 255 });
        } else {
            int tx = World::ToTile(mouseWorld.x);
            int ty = World::ToTile(mouseWorld.y);
            DrawRectangleLines(tx * TILE_SIZE, ty * TILE_SIZE, TILE_SIZE, TILE_SIZE, WHITE);
        }
    }

    EndMode2D();
}

void Simulation::DrawUI() const {
    int screenW = SCREEN_WIDTH;
    int screenH = SCREEN_HEIGHT;

    // -------------------------------------------------------------------------
    // 0. Wetter-Overlay (Regenstreifen & Nebel über der Spielwelt)
    // -------------------------------------------------------------------------
    env_.DrawWeatherOverlay(screenW, screenH, totalSimTime_);

    const Tribe& uiTribe = CurrentUiTribe();
    Vector2 mouseScreen = GetMousePosition();

    // -------------------------------------------------------------------------
    // 1. Obere Statusleiste (y: 0..44) - Schlank, modern, kein Textüberlauf!
    // -------------------------------------------------------------------------
    DrawRectangle(0, 0, screenW, 44, Fade(Color{ 14, 17, 23, 255 }, 0.94f));
    DrawLine(0, 44, screenW, 44, Fade(GRAY, 0.35f));

    // Logo / Titel
    FontManager::DrawTextShadow("PRIMITIVE", 16.0f, 12.0f, 20.0f, RAYWHITE);

    // Zivilisations-Badges zum schnellen Durchschalten der lebenden Stämme (BLOCK 3.5)
    float tabX = 135.0f;
    float tabY = 9.0f;
    float tabW = 102.0f;
    float tabH = 26.0f;
    int renderedTabIdx = 0;
    for (size_t i = 0; i < tribes_.size(); ++i) {
        if (tribes_[i]->IsDisbanded()) continue; // Tote Stämme aus der Top-Leiste entfernen
        bool isActive = ((int)i == activeTribeIndex_);
        Rectangle tabR = { tabX + (float)renderedTabIdx * 108.0f, tabY, tabW, tabH };
        bool hov = CheckCollisionPointRec(mouseScreen, tabR);
        Color baseCol = tribes_[i]->FactionColor();

        DrawRectangleRounded(tabR, 0.35f, 4, isActive ? Fade(baseCol, 0.85f) : (hov ? Fade(baseCol, 0.45f) : Fade(BLACK, 0.70f)));
        DrawRectangleRoundedLinesEx(tabR, 0.35f, 4, isActive ? 1.8f : 1.0f, isActive ? GOLD : baseCol);

        std::string tabLabel = TextFormat("[%d] %s", (int)renderedTabIdx + 1, tribes_[i]->Name().c_str());
        FontManager::DrawTextCenteredShadow(tabLabel.c_str(),
                                            tabR.x + tabR.width * 0.5f, tabR.y + 5.0f, 13.0f, RAYWHITE);
        renderedTabIdx++;
    }

    // Vorräte & Ressourcen in der Mitte (sauber platziert ab x = 485)
    const auto& stock = uiTribe.Stock();
    int clanPop = 0;
    for (const auto& c : cavemen_) {
        if (c.IsAlive() && c.ClanId() == uiTribe.Id()) clanPop++;
    }
    int sleepCap = uiTribe.SleepingCapacity();
    float stockX = 485.0f;
    float stockY = 14.0f;

    // Pop
    FontManager::DrawTextShadow(TextFormat("👥 %d", clanPop), stockX, stockY, 14.0f, RAYWHITE);
    stockX += 58.0f;

    // Betten
    Color bedCol = (clanPop > sleepCap) ? RED : ((sleepCap == clanPop) ? GOLD : LIME);
    FontManager::DrawTextShadow(TextFormat("🛏️ %d/%d", sleepCap, clanPop), stockX, stockY, 14.0f, bedCol);
    stockX += 78.0f;

    // Nahrung
    Color foodCol = (stock.food + stock.cookedFood > 5) ? LIME : RED;
    if (stock.cookedFood > 0) {
        FontManager::DrawTextShadow(TextFormat("🍖 %d(+%d)", stock.food, stock.cookedFood), stockX, stockY, 14.0f, foodCol);
        stockX += 88.0f;
    } else {
        FontManager::DrawTextShadow(TextFormat("🍖 %d", stock.food), stockX, stockY, 14.0f, foodCol);
        stockX += 65.0f;
    }

    // Holz, Stein, Fell
    FontManager::DrawTextShadow(TextFormat("🪵 %d", stock.wood), stockX, stockY, 14.0f, Color{ 220, 165, 95, 255 });
    stockX += 60.0f;
    FontManager::DrawTextShadow(TextFormat("🪨 %d", stock.stone), stockX, stockY, 14.0f, Color{ 205, 205, 210, 255 });
    stockX += 60.0f;
    FontManager::DrawTextShadow(TextFormat("🥋 %d", stock.hides), stockX, stockY, 14.0f, Color{ 225, 185, 135, 255 });
    stockX += 55.0f;

    // Veredelte Güter (nur wenn vorhanden)
    if (stock.planks > 0) {
        FontManager::DrawTextShadow(TextFormat("🪵+ %d", stock.planks), stockX, stockY, 14.0f, Color{ 245, 195, 115, 255 });
        stockX += 62.0f;
    }
    if (stock.bricks > 0) {
        FontManager::DrawTextShadow(TextFormat("🧱 %d", stock.bricks), stockX, stockY, 14.0f, Color{ 225, 225, 235, 255 });
        stockX += 58.0f;
    }

    // Buttons rechts: [T] Territorien & [K] Skilltree & FPS
    Rectangle btnTerritory = { (float)screenW - 275.0f, 8.0f, 118.0f, 28.0f };
    bool hovTerr = CheckCollisionPointRec(mouseScreen, btnTerritory);
    DrawRectangleRounded(btnTerritory, 0.25f, 4,
                         showTerritoryOverlay_ ? Color{ 55, 95, 50, 255 } : (hovTerr ? Color{ 40, 58, 48, 255 } : Color{ 25, 36, 30, 255 }));
    DrawRectangleRoundedLinesEx(btnTerritory, 0.25f, 4, 1.2f, showTerritoryOverlay_ ? LIME : SKYBLUE);
    FontManager::DrawTextCenteredShadow("[T] Territorium", btnTerritory.x + btnTerritory.width * 0.5f, btnTerritory.y + 6.0f, 14.0f,
                                        showTerritoryOverlay_ ? LIME : SKYBLUE);

    Rectangle btnSkillTree = { (float)screenW - 148.0f, 8.0f, 95.0f, 28.0f };
    bool hovTree = CheckCollisionPointRec(mouseScreen, btnSkillTree);
    DrawRectangleRounded(btnSkillTree, 0.25f, 4, hovTree ? Color{ 50, 75, 120, 255 } : Color{ 30, 42, 68, 255 });
    DrawRectangleRoundedLinesEx(btnSkillTree, 0.25f, 4, 1.2f, GOLD);
    FontManager::DrawTextCenteredShadow("[K] Skilltree", btnSkillTree.x + btnSkillTree.width * 0.5f, btnSkillTree.y + 6.0f, 14.0f, GOLD);

    DrawFPS(screenW - 44, 14);

    // -------------------------------------------------------------------------
    // 1b. Dezent schwebende Status-Pille (y = 48) - Ersetzt klobige 380px-Kästen
    // -------------------------------------------------------------------------
    Era era = uiTribe.Age().CurrentEra();
    HistoricalEpoch curEpoch = uiTribe.Age().CurrentEpoch();
    float timeOfDay = std::fmod(totalSimTime_, cfg::kDayCycleDuration) / cfg::kDayCycleDuration;
    bool isNight = (timeOfDay > 0.72f || timeOfDay < 0.22f);
    std::string speedStr = isPaused_ ? "PAUSE" : TextFormat("%.0fx", timeSpeed_);

    std::string epochSummary = TextFormat("%s  •  %s  •  %s  [%s]",
                                          HistoricalEpochName(curEpoch),
                                          isNight ? "Nacht" : "Tag",
                                          WeatherTypeName(env_.GetWeather()),
                                          speedStr.c_str());
    float epW = FontManager::MeasureText(epochSummary.c_str(), 13.0f);
    DrawRectangleRounded({ 14.0f, 48.0f, epW + 18.0f, 24.0f }, 0.4f, 4, Fade(Color{ 14, 18, 24, 255 }, 0.88f));
    DrawRectangleRoundedLinesEx({ 14.0f, 48.0f, epW + 18.0f, 24.0f }, 0.4f, 4, 1.0f, Fade(EraColor(era), 0.75f));
    FontManager::DrawTextShadow(epochSummary.c_str(), 23.0f, 53.0f, 13.0f, EraColor(era));

    // Territorium-Status (wenn [T] aktiv) oder Siedlungsplan-Chip
    if (showTerritoryOverlay_) {
        int currentTiles = uiTribe.CurrentTerritoryCount();
        int maxCapacity = uiTribe.MaxTerritoryCapacity();
        int anchorCount = 0;
        for (const auto& b : uiTribe.Buildings().All()) {
            if (b.IsTerritoryAnchor()) anchorCount++;
        }
        std::string focusText = uiTribe.GetExpansionTarget().active ? uiTribe.GetExpansionTarget().reason : "Kernland";
        std::string terrInfo = TextFormat("Territorium: %d / %d Kacheln (Anker: %d) • Fokus: %s",
                                          currentTiles, maxCapacity, anchorCount, focusText.c_str());
        float tw = FontManager::MeasureText(terrInfo.c_str(), 13.0f);
        float tx = 14.0f + epW + 26.0f;
        DrawRectangleRounded({ tx, 48.0f, tw + 18.0f, 24.0f }, 0.4f, 4, Fade(Color{ 14, 18, 24, 255 }, 0.88f));
        DrawRectangleRoundedLinesEx({ tx, 48.0f, tw + 18.0f, 24.0f }, 0.4f, 4, 1.0f, Fade(uiTribe.FactionColor(), 0.85f));
        FontManager::DrawTextShadow(terrInfo.c_str(), tx + 9.0f, 53.0f, 13.0f, uiTribe.FactionColor());
    } else if (uiTribe.CurrentPlan().active) {
        std::string planText = TextFormat("Bauprojekt: %s", uiTribe.CurrentPlan().description.c_str());
        float pw = FontManager::MeasureText(planText.c_str(), 13.0f);
        float px = 14.0f + epW + 26.0f;
        DrawRectangleRounded({ px, 48.0f, pw + 18.0f, 24.0f }, 0.4f, 4, Fade(Color{ 14, 18, 24, 255 }, 0.88f));
        DrawRectangleRoundedLinesEx({ px, 48.0f, pw + 18.0f, 24.0f }, 0.4f, 4, 1.0f, Fade(GOLD, 0.70f));
        FontManager::DrawTextShadow(planText.c_str(), px + 9.0f, 53.0f, 13.0f, RAYWHITE);
    }

    // -------------------------------------------------------------------------
    // 2. Inspektor-Panel für selektierten Steinzeitmenschen (rechts)
    // -------------------------------------------------------------------------
    const Caveman* selected = nullptr;
    for (const auto& c : cavemen_) {
        if (c.Id() == selectedCavemanId_ && !c.IsDead()) {
            selected = &c;
            break;
        }
    }

    if (selected) {
        int panelW = 330;
        int panelH = 575;
        int panelX = screenW - panelW - 14;
        int panelY = 52;

        DrawRectangleRounded({ (float)panelX, (float)panelY, (float)panelW, (float)panelH }, 0.035f, 6, Fade(Color{ 14, 18, 24, 255 }, 0.94f));
        DrawRectangleRoundedLinesEx({ (float)panelX, (float)panelY, (float)panelW, (float)panelH }, 0.035f, 6, 1.5f, Fade(RoleColor(selected->GetRole()), 0.85f));

        // Schließen-Button oben rechts
        FontManager::DrawTextShadow("[ESC]", panelX + panelW - 48.0f, panelY + 12.0f, 12.0f, GRAY);

        // Name, Geschlecht, Rolle & Lebensalter
        FontManager::DrawTextShadow(TextFormat("%s  %s", selected->Name().c_str(), selected->GenderSymbol()),
                                    panelX + 14.0f, panelY + 12.0f, 22.0f, RAYWHITE);
        FontManager::DrawTextShadow(TextFormat("%s (%s)  |  Alter: %.0f / %.0f J.",
                                               RoleName(selected->GetRole()), selected->GenderName(),
                                               selected->AgeYears(), selected->MaxLifespan()),
                                    panelX + 14.0f, panelY + 38.0f, 15.0f, RoleColor(selected->GetRole()));

        // Clan & Rang
        const Tribe* trPtr = GetTribe(selected->ClanId());
        if (trPtr) {
            SocialRank rank = trPtr->Hierarchy().GetRank(selected->Id());
            FontManager::DrawTextShadow(TextFormat("%s  |  %s",
                                                   trPtr->Name().c_str(),
                                                   SocialRankName(rank)),
                                        panelX + 14.0f, panelY + 58.0f, 14.0f, trPtr->FactionColor());

            int clanAnchors = 0;
            for (const auto& b : trPtr->Buildings().All()) {
                if (b.IsTerritoryAnchor()) clanAnchors++;
            }
            FontManager::DrawTextShadow(TextFormat("Territorium: %d / %d Kacheln (Anker: %d)",
                                                   trPtr->CurrentTerritoryCount(), trPtr->MaxTerritoryCapacity(), clanAnchors),
                                        panelX + 14.0f, panelY + 76.0f, 13.0f, trPtr->FactionColor());
        } else {
            FontManager::DrawTextShadow("Freier Wanderer (Kein Clan)",
                                        panelX + 14.0f, panelY + 58.0f, 14.0f, LIGHTGRAY);
            FontManager::DrawTextShadow("Territorium: Keine Stammesbindung",
                                        panelX + 14.0f, panelY + 76.0f, 13.0f, DARKGRAY);
        }

        // Persönlichkeits-Kurzprofil
        const Personality& p = selected->GetPersonality();
        FontManager::DrawTextShadow(TextFormat("Wesen: %s (Dom: %+.1f, Alt: %+.1f, Soz: %+.1f)",
                                               p.GetPrimaryTraitDescription().c_str(), p.dominance, p.altruism, p.sociability),
                                           panelX + 14.0f, panelY + 94.0f, 13.0f, LIGHTGRAY);

        // Beziehungen & Partner
        int partnerId = trPtr ? trPtr->Relationships().GetPartner(selected->Id()) : -1;
        const Caveman* partnerPtr = nullptr;
        if (partnerId >= 0) {
            for (const auto& c : cavemen_) {
                if (c.Id() == partnerId && !c.IsDead()) {
                    partnerPtr = &c;
                    break;
                }
            }
        }
        std::string partnerStr = "Keiner";
        if (partnerPtr) {
            partnerStr = TextFormat("%s %s", partnerPtr->Name().c_str(), partnerPtr->GenderSymbol());
        } else if (partnerId >= 0) {
            partnerStr = TextFormat("#%d", partnerId);
        }
        auto friends = trPtr ? trPtr->Relationships().GetFriends(selected->Id()) : std::vector<int>{};
        auto enemies = trPtr ? trPtr->Relationships().GetEnemies(selected->Id()) : std::vector<int>{};
        FontManager::DrawTextShadow(TextFormat("Partner: %s | Freunde: %d | Rivalen: %d",
                                               partnerStr.c_str(),
                                               (int)friends.size(), (int)enemies.size()),
                                    panelX + 14.0f, panelY + 112.0f, 14.0f, (partnerId >= 0 ? PINK : RAYWHITE));

        // Status & Tätigkeit
        FontManager::DrawTextShadow(TextFormat("Status: %s", StateName(selected->State())), panelX + 14.0f, panelY + 132.0f, 15.0f, YELLOW);
        FontManager::DrawTextShadow(TextFormat("Tätigkeit: %s", selected->ActivityText().c_str()), panelX + 14.0f, panelY + 150.0f, 14.0f, LIGHTGRAY);

        auto drawStatBar = [&](const char* label, float val, float maxVal, int y, Color barColor) {
            FontManager::DrawTextShadow(TextFormat("%s: %.0f/%.0f", label, val, maxVal), panelX + 14.0f, (float)y, 12.0f, RAYWHITE);
            int barX = panelX + 124;
            int barW = 188;
            int barH = 10;
            DrawRectangleRounded({ (float)barX, (float)y + 2.0f, (float)barW, (float)barH }, 0.4f, 3, Fade(GRAY, 0.35f));
            float pct = std::clamp(val / maxVal, 0.0f, 1.0f);
            if (pct > 0.01f) {
                DrawRectangleRounded({ (float)barX, (float)y + 2.0f, (float)barW * pct, (float)barH }, 0.4f, 3, barColor);
            }
        };

        drawStatBar("HP", selected->Hp(), 100.0f, panelY + 172, (selected->Hp() > 50) ? GREEN : RED);
        drawStatBar("AP", selected->Ap(), 100.0f, panelY + 189, SKYBLUE);
        drawStatBar("Hunger", selected->Hunger(), 100.0f, panelY + 206, (selected->Hunger() > 70) ? RED : ORANGE);
        drawStatBar("Wärme", selected->Warmth(), 100.0f, panelY + 223, (selected->Warmth() < 30) ? RED : (selected->Warmth() < 60) ? YELLOW : ORANGE);
        drawStatBar("Müdigkeit", selected->Tiredness(), 100.0f, panelY + 240, (selected->Tiredness() > 80) ? PURPLE : BLUE);
        drawStatBar("Motivation", selected->Morale(), 100.0f, panelY + 257, (selected->Morale() > 60) ? LIME : GOLD);
        drawStatBar("Gemeinschaft", 100.0f - selected->SocialNeed(), 100.0f, panelY + 274, (selected->SocialNeed() > 70) ? ORANGE : GREEN);

        // Kleidung & Ausrüstung (Meilenstein A)
        FontManager::DrawTextShadow(TextFormat("Kleidung: %s", ClothingTierName(selected->GetClothing())),
                                    panelX + 14.0f, panelY + 295.0f, 13.0f, Color{ 225, 190, 135, 255 });
        if (selected->GetToolTier() != ToolTier::BareHands) {
            FontManager::DrawTextShadow(TextFormat("Werkzeug: %s (%.0f%%) | Waffe: %s",
                                                   ToolTierName(selected->GetToolTier()), selected->ToolDurability(),
                                                   WeaponTierName(selected->GetWeaponTier())),
                                        panelX + 14.0f, panelY + 313.0f, 13.0f, Color{ 190, 225, 255, 255 });
        } else {
            FontManager::DrawTextShadow(TextFormat("Werkzeug: %s | Waffe: %s",
                                                   ToolTierName(selected->GetToolTier()), WeaponTierName(selected->GetWeaponTier())),
                                        panelX + 14.0f, panelY + 313.0f, 13.0f, LIGHTGRAY);
        }

        if (selected->Carrying() != CarryType::None) {
            const char* carryName = (selected->Carrying() == CarryType::Food) ? "Nahrung" :
                                    (selected->Carrying() == CarryType::Wood) ? "Holz" :
                                    (selected->Carrying() == CarryType::Stone) ? "Stein" :
                                    (selected->Carrying() == CarryType::Planks) ? "Balken" : "Ziegel";
            FontManager::DrawTextShadow(TextFormat("Trägt: %d x %s (%s)", selected->CarryAmount(), carryName, ContainerTierName(selected->GetContainer())),
                                        panelX + 14.0f, panelY + 331.0f, 14.0f, GREEN);
        } else {
            FontManager::DrawTextShadow(TextFormat("Tragehilfe: %s", ContainerTierName(selected->GetContainer())),
                                        panelX + 14.0f, panelY + 331.0f, 13.0f, DARKGRAY);
        }

        // --- Utility AI Log & Entscheidungsgrund ---
        DrawLine(panelX + 10, panelY + 352, panelX + panelW - 10, panelY + 352, Fade(GRAY, 0.5f));
        FontManager::DrawTextShadow("KI-Entscheidung (Utility AI):", panelX + 14.0f, panelY + 358.0f, 14.0f, GOLD);
        std::string reasonStr = selected->DecisionReason();
        if (reasonStr.rfind("Grund: ", 0) == 0) {
            FontManager::DrawTextShadow(TextFormat("-> %s", reasonStr.c_str()), panelX + 14.0f, panelY + 376.0f, 13.0f, RAYWHITE);
        } else {
            FontManager::DrawTextShadow(TextFormat("-> Grund: %s", reasonStr.c_str()), panelX + 14.0f, panelY + 376.0f, 13.0f, RAYWHITE);
        }
        FontManager::DrawTextShadow(TextFormat("Nutzen-Score: %.2f  |  Wissen: +%.0f%%", selected->DecisionScore(), selected->KnowledgeBonus() * 100.0f),
                                    panelX + 14.0f, panelY + 394.0f, 13.0f, SKYBLUE);

        // --- Evolutionäres Gehirn & Reinforcement Learning ---
        DrawLine(panelX + 10, panelY + 412, panelX + panelW - 10, panelY + 412, Fade(GRAY, 0.5f));
        FontManager::DrawTextShadow("Evolution & Lernen (Gehirn-Genom):", panelX + 14.0f, panelY + 418.0f, 14.0f, Color{ 150, 225, 255, 255 });

        const BrainGenome& bg = selected->GetBrainGenome();
        const BrainWeights& bw = selected->GetBrainWeights();
        float genePlast = bg.GetGene(BrainGene::Neuroplasticity);
        float cogMult = trPtr ? trPtr->CognitiveMultiplier((int)cavemen_.size()) : 1.0f;
        float effAlpha = 0.05f * genePlast * cogMult;

        FontManager::DrawTextShadow(TextFormat("Plastizität: %.2fx (a=%.3f) | Exp: %d",
                                               genePlast, effAlpha, bw.totalExperiences),
                                    panelX + 14.0f, panelY + 436.0f, 13.0f, RAYWHITE);

        int top1 = -1, top2 = -1;
        float max1 = -1.0f, max2 = -1.0f;
        for (int g = 0; g < (int)BrainGene::Count; ++g) {
            if ((BrainGene)g == BrainGene::Neuroplasticity) continue;
            float w = bw.weights[g];
            if (w > max1) {
                max2 = max1; top2 = top1;
                max1 = w; top1 = g;
            } else if (w > max2) {
                max2 = w; top2 = g;
            }
        }
        const char* name1 = (top1 >= 0) ? BrainGeneName((BrainGene)top1) : "-";
        const char* name2 = (top2 >= 0) ? BrainGeneName((BrainGene)top2) : "-";
        FontManager::DrawTextShadow(TextFormat("Top-Triebe: %s (%.2f), %s (%.2f)",
                                               name1, max1, name2, max2),
                                    panelX + 14.0f, panelY + 454.0f, 13.0f, Color{ 190, 235, 255, 255 });

        FontManager::DrawTextShadow(TextFormat("Lektion: %s (%+.2f)",
                                               bw.lastLessonDescription.c_str(), bw.lastAdjustment),
                                    panelX + 14.0f, panelY + 472.0f, 12.0f,
                                    (bw.lastAdjustment >= 0 ? Color{ 140, 240, 140, 255 } : Color{ 255, 150, 140, 255 }));

        // Letzte prägende Erinnerung
        const auto& memories = selected->Memory().Entries();
        if (!memories.empty()) {
            const auto& lastMem = memories.back();
            FontManager::DrawTextShadow(TextFormat("Erinnerung: %s (zu #%d)", MemoryTypeName(lastMem.type), lastMem.otherId),
                                        panelX + 14.0f, panelY + 494.0f, 13.0f, (lastMem.baseImpact >= 0 ? LIME : ORANGE));
        }

        // Legende für Beziehungslinien auf der Karte
        FontManager::DrawTextShadow("Linien: Pink=Partner | Gold=Familie | Grün=Freund | Rot=Rivale", panelX + 14.0f, panelY + 520.0f, 12.0f, LIGHTGRAY);
        FontManager::DrawTextShadow("Klick auf anderen Menschen zum Wechseln", panelX + 14.0f, panelY + 540.0f, 12.0f, GRAY);
    }

    // -------------------------------------------------------------------------
    // 3. Ereignis-Log (unten links) - Schlankes Toast-Feed
    // -------------------------------------------------------------------------
    const auto& logEntries = eventLog_.Entries();
    if (!logEntries.empty()) {
        float maxTextW = 200.0f;
        for (const auto& entry : logEntries) {
            float tw = FontManager::MeasureText(entry.text.c_str(), 13.0f);
            if (tw > maxTextW) maxTextW = tw;
        }
        maxTextW = std::min(maxTextW, (float)screenW * 0.40f);
        int totalH = (int)logEntries.size() * 20 + 8;
        int boxY = screenH - 42 - (int)logEntries.size() * 20 - 4;
        DrawRectangleRounded({ 14.0f, (float)boxY, maxTextW + 20.0f, (float)totalH }, 0.15f, 4, Fade(Color{ 14, 18, 24, 255 }, 0.78f));
        DrawRectangleRoundedLinesEx({ 14.0f, (float)boxY, maxTextW + 20.0f, (float)totalH }, 0.15f, 4, 1.0f, Fade(GRAY, 0.35f));

        int logY = boxY + 5;
        for (const auto& entry : logEntries) {
            FontManager::DrawTextShadow(entry.text.c_str(), 20.0f, (float)logY, 13.0f, Fade(entry.color, 0.95f));
            logY += 20;
        }
    }

    // -------------------------------------------------------------------------
    // 4. Interaktives Skilltree-Overlay (über allen anderen Elementen)
    // -------------------------------------------------------------------------
    skillTreeUI_.Draw(uiTribe, mouseScreen);
}

