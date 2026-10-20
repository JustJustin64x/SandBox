#include "simulation.h"
#include "config.h"
#include "font_manager.h"

#include <algorithm>
#include <cmath>
#include <random>

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
    cavemen_.back().SetPersonality(childPers);

    int parentClanId = parentA.ClanId();
    Tribe* trPtr = GetTribe(parentClanId);
    if (!trPtr) trPtr = tribes_[0].get();
    Tribe& tr = *trPtr;

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

            // Ausschlusskriterien:
            if (minFreshDist < 3 || minFreshDist > 14) continue;
            if (minSaltDist <= 8) continue;
            if (woodTiles < 6 || stoneTiles < 3) continue;
            if (flatTiles < 45) continue;

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

    for (const auto& cand : candidates) {
        Vector2 pos = World::TileCenter(cand.tx, cand.ty);
        bool tooClose = false;
        for (const auto& loc : locations) {
            float distTiles = Vector2Distance(pos, loc) / (float)TILE_SIZE;
            if (distTiles < 42.0f) {
                tooClose = true;
                break;
            }
        }
        if (!tooClose) {
            locations.push_back(pos);
            if ((int)locations.size() == count) break;
        }
    }

    // Fallback falls unwegsames Terrain
    while ((int)locations.size() < count) {
        Vector2 fallback;
        if (world_.FindRandomWalkablePosition(world_.CenterPosition(), 300.0f, fallback, 60)) {
            locations.push_back(fallback);
        } else {
            locations.push_back(world_.CenterPosition());
        }
    }

    return locations;
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
        tribes_[t]->Hierarchy().ElectLeader(cavemen_, tribes_[t]->Relationships(), eventLog_, cColor, tribes_[t]->Name());
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

    // Klickbare Zivilisations-Badges zum Wechseln der beobachteten Zivilisation
    float tabX = 14.0f;
    float tabY = 110.0f;
    for (size_t i = 0; i < tribes_.size(); ++i) {
        if (tabX + 130.0f > (float)GetScreenWidth() - 14.0f) {
            tabX = 14.0f;
            tabY += 30.0f;
        }
        Rectangle tabR = { tabX, tabY, 130.0f, 26.0f };
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouseScreen, tabR)) {
            activeTribeIndex_ = (int)i;
            camera_.SetTarget(tribes_[i]->CampPosition());
            CancelActiveInput();
            return;
        }
        tabX += 136.0f;
    }

    // Wenn der Skilltree geöffnet ist, Eingaben exklusiv an das Overlay leiten
    if (skillTreeUI_.IsOpen()) {
        skillTreeUI_.Update(CurrentUiTribe(), mouseScreen, IsMouseButtonPressed(MOUSE_BUTTON_LEFT));
        return;
    }

    // Klick auf den Territorien-Button oder Skilltree-Button in der oberen Leiste
    Rectangle btnTerritory = { (float)SCREEN_WIDTH - 368.0f, 10.0f, 122.0f, 28.0f };
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouseScreen, btnTerritory)) {
        showTerritoryOverlay_ = !showTerritoryOverlay_;
        CancelActiveInput();
        return;
    }

    Rectangle btnSkillTree = { (float)SCREEN_WIDTH - 240.0f, 10.0f, 130.0f, 28.0f };
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
    bool inTopBar = (mouseScreen.y <= 48);
    bool inBottomHUD = (mouseScreen.y >= SCREEN_HEIGHT - 38 && mouseScreen.x <= 560);
    bool inInspector = false;
    if (selectedCavemanId_ != -1) {
        float panelW = 320.0f;
        float panelH = 445.0f;
        float panelX = (float)SCREEN_WIDTH - panelW - 16.0f;
        float panelY = 60.0f;
        Rectangle inspRect = { panelX, panelY, panelW, panelH };
        if (CheckCollisionPointRec(mouseScreen, inspRect)) {
            inInspector = true;
        }
    }
    bool canInteractWithWorld = !outOfVirtualScreen && !inTopBar && !inBottomHUD && !inInspector;

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
    if (demographicTimer_ >= 16.0f) {
        demographicTimer_ = 0.0f;

        for (auto& tr : tribes_) {
            int tribePop = 0;
            for (const auto& c : cavemen_) {
                if (!c.IsDead() && c.ClanId() == tr->Id()) tribePop++;
            }

            if (tribePop < 24 && tr->Stock().food >= std::max(6, tribePop + 2) &&
                tr->Buildings().GetBuildingCount(BuildingType::Hut, true) > 0) {
                for (size_t i = 0; i < cavemen_.size(); ++i) {
                    if (cavemen_[i].IsDead() || cavemen_[i].ClanId() != tr->Id() || cavemen_[i].GetRole() == Role::Child) continue;
                    if (cavemen_[i].Hunger() > 65.0f) continue;

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

                    Vector2 spawnPos = cavemen_[i].Position();
                    int hutId = tr->Buildings().FindBestShelter(spawnPos);
                    if (hutId >= 0) {
                        if (const Building* hut = tr->Buildings().Get(hutId)) {
                            world_.FindRandomWalkablePosition(hut->position, 16.0f, spawnPos, 15);
                        }
                    }

                    tr->Stock().food = std::max(0, tr->Stock().food - 3);
                    SpawnChildFromParents(cavemen_[i], cavemen_[partnerIdx], spawnPos);
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

        // Finde passenden Siedlungsplatz im unbesiedelten, neutralen Land
        Vector2 targetCampPos;
        if (!FindSecessionSettlementSpot(targetCampPos)) continue;

        // Gefährten rekrutieren: Partner und enge Freunde
        std::vector<Caveman*> followers;
        int partnerId = parentTribe.Relationships().GetPartner(rebel->Id());

        for (auto& c : cavemen_) {
            if (c.IsDead() || c.ClanId() != parentTribe.Id() || c.Id() == rebel->Id() || c.Id() == chiefId) continue;
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
            t->Update(world_, cavemen_, population, step, eventLog_);
            t->Hierarchy().UpdateLeadership(t->Relationships(), cavemen_, eventLog_, t->FactionColor(), t->Name());
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
        float thickness = 1.4f;

        if (isPartner || tag == RelationshipTag::Partner) {
            lineColor = Color{ 255, 105, 180, 235 }; // Pink: Partner
            relLabel = "Partner";
            thickness = 2.2f;
        } else if (isKin) {
            lineColor = Color{ 255, 215, 70, 225 };  // Gold: Familie/Verwandtschaft
            relLabel = "Familie";
            thickness = 1.9f;
        } else if (tag == RelationshipTag::BestFriend || tag == RelationshipTag::Friend) {
            lineColor = Color{ 90, 235, 115, 210 };  // Grün: Freunde
            relLabel = (tag == RelationshipTag::BestFriend) ? "Bester Freund" : "Freund";
            thickness = 1.6f;
        } else if (tag == RelationshipTag::Enemy || tag == RelationshipTag::Rival) {
            lineColor = Color{ 245, 70, 70, 215 };   // Rot: Rivalen/Feinde
            relLabel = (tag == RelationshipTag::Enemy) ? "Feind" : "Rivale";
            thickness = 1.6f;
        }

        if (relLabel != nullptr) {
            float toYOffset = world_.GetVisualYOffset(other.Position());
            Vector2 to = { other.Position().x, other.Position().y - 8.0f + toYOffset };
            DrawLineEx(from, to, thickness, lineColor);
            DrawCircleV(to, 2.8f, lineColor);

            Vector2 mid = { (from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f - 6.0f };
            float tw = FontManager::MeasureText(relLabel, 11.0f);
            DrawRectangle((int)(mid.x - tw * 0.5f - 3.0f), (int)(mid.y - 1.0f), (int)(tw + 6.0f), 14, Fade(BLACK, 0.78f));
            FontManager::DrawTextShadow(relLabel, mid.x - tw * 0.5f, mid.y, 11.0f, lineColor);
        }
    }
}

void Simulation::DrawWorld() const {
    BeginMode2D(camera_.Get());

    // 1. Kachel-Welt + Phase-1-Kachelzustände (Feuer, Eis, Krater, Strombögen)
    world_.Draw(camera_.Get());

    // Territorien & Gebäude aller unabhängigen Stämme zeichnen
    for (const auto& t : tribes_) {
        if (showTerritoryOverlay_) {
            t->DrawTerritoryOverlay(world_, totalSimTime_);
        }
        t->POIs().Draw(camera_.Get(), totalSimTime_);
        t->Draw(totalSimTime_, &world_);
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

    // 7. Steinzeitmenschen Y-sortiert mit 2.5D Höhenversatz zeichnen
    std::vector<const Caveman*> sortedCavemen;
    sortedCavemen.reserve(cavemen_.size());
    for (const auto& c : cavemen_) {
        if (!c.IsDead()) sortedCavemen.push_back(&c);
    }
    std::sort(sortedCavemen.begin(), sortedCavemen.end(), [this](const Caveman* a, const Caveman* b) {
        float yA = a->Position().y + world_.GetVisualYOffset(a->Position());
        float yB = b->Position().y + world_.GetVisualYOffset(b->Position());
        return yA < yB;
    });

    const Vector2 viewTL = GetScreenToWorld2D({ 0.0f, 0.0f }, camera_.Get());
    const Vector2 viewBR = GetScreenToWorld2D({ (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT }, camera_.Get());
    const Rectangle cullRect = { viewTL.x - 32.0f, viewTL.y - 48.0f, (viewBR.x - viewTL.x) + 64.0f, (viewBR.y - viewTL.y) + 96.0f };

    for (const auto* c : sortedCavemen) {
        if (!CheckCollisionPointRec(c->Position(), cullRect)) continue;
        bool highlighted = (c->Id() == selectedCavemanId_);
        if (highlighted) {
            c->DrawDebugPath();
        }
        float visualYOffset = world_.GetVisualYOffset(c->Position());
        c->Draw(spriteSheet_, highlighted, visualYOffset);
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

    // Autonomer Siedlungsplaner & God-Sim Status oben links unter der Statusleiste
    DrawRectangle(14, 56, 380, 48, Fade(BLACK, 0.85f));
    DrawRectangleLines(14, 56, 380, 48, uiTribe.FactionColor());
    FontManager::DrawTextShadow(TextFormat("Zivilisation: %s", uiTribe.Name().c_str()), 22.0f, 61.0f, 15.0f, uiTribe.FactionColor());
    const char* planText = uiTribe.CurrentPlan().active
        ? TextFormat("Siedlungsplan: %s", uiTribe.CurrentPlan().description.c_str())
        : "Siedlungsplan: Vollständig ausgebaut / Autonom aktiv";
    FontManager::DrawTextShadow(planText, 22.0f, 81.0f, 14.0f, RAYWHITE);

    // Zivilisations-Badges zum schnellen Durchschalten der Stämme
    float tabX = 14.0f;
    float tabY = 110.0f;
    Vector2 mouseScreen = GetMousePosition();
    for (size_t i = 0; i < tribes_.size(); ++i) {
        if (tabX + 130.0f > (float)screenW - 14.0f) {
            tabX = 14.0f;
            tabY += 30.0f;
        }
        bool isActive = ((int)i == activeTribeIndex_);
        Rectangle tabR = { tabX, tabY, 130.0f, 26.0f };
        bool hov = CheckCollisionPointRec(mouseScreen, tabR);
        DrawRectangleRec(tabR, isActive ? Fade(tribes_[i]->FactionColor(), 0.90f) : (hov ? Fade(tribes_[i]->FactionColor(), 0.50f) : Fade(BLACK, 0.78f)));
        DrawRectangleLinesEx(tabR, isActive ? 2.0f : 1.0f, isActive ? GOLD : tribes_[i]->FactionColor());
        FontManager::DrawTextCenteredShadow(TextFormat("[%d] %s", (int)i + 1, tribes_[i]->Name().c_str()),
                                            tabR.x + tabR.width * 0.5f, tabR.y + 5.0f, 13.0f, RAYWHITE);
        tabX += 136.0f;
    }

    // -------------------------------------------------------------------------
    // 1. Obere Statusleiste
    // -------------------------------------------------------------------------
    DrawRectangle(0, 0, screenW, 50, Fade(BLACK, 0.90f));
    DrawRectangleLines(0, 0, screenW, 50, Fade(GRAY, 0.5f));

    FontManager::DrawTextShadow("PRIMITIVE SANDBOX", 16.0f, 12.0f, 22.0f, RAYWHITE);

    // Zeitalter & Kausales 14-Epochen-Modell der aktiven Zivilisation mit Statusanzeige
    Era era = uiTribe.Age().CurrentEra();
    HistoricalEpoch curEpoch = uiTribe.Age().CurrentEpoch();

    float timeOfDay = std::fmod(totalSimTime_, cfg::kDayCycleDuration) / cfg::kDayCycleDuration;
    bool isNight = (timeOfDay > 0.72f || timeOfDay < 0.22f);
    std::string speedStr = isPaused_ ? "PAUSE" : TextFormat("%.0fx", timeSpeed_);

    std::string epochSummary = TextFormat("%s | %s | %s [%s]",
                                          HistoricalEpochName(curEpoch),
                                          isNight ? "NACHT" : "TAG",
                                          WeatherTypeName(env_.GetWeather()),
                                          speedStr.c_str());

    FontManager::DrawTextShadow(epochSummary.c_str(), 235.0f, 15.0f, 15.0f, EraColor(era));

    // Lager-Vorräte (Rohstoffe & Veredelt) der aktiven Zivilisation
    const auto& stock = uiTribe.Stock();
    int pop = GetAliveCount();
    int sleepCap = uiTribe.SleepingCapacity();

    float stockX = 245.0f + FontManager::MeasureText(epochSummary.c_str(), 15.0f) + 20.0f;

    FontManager::DrawTextShadow(TextFormat("Pop: %d", pop), stockX, 15.0f, 15.0f, RAYWHITE);
    stockX += 72.0f;

    // Phase 5: Invertiertes Betten-Verhältnis (Betten/Pop, rot wenn Betten fehlen)
    Color bedCol = (pop > sleepCap) ? RED : ((sleepCap == pop) ? GOLD : LIME);
    FontManager::DrawTextShadow(TextFormat("Betten: %d/%d", sleepCap, pop), stockX, 15.0f, 15.0f, bedCol);
    stockX += 105.0f;

    FontManager::DrawTextShadow(TextFormat("Nahrung: %d (+%d)", stock.food, stock.cookedFood), stockX, 15.0f, 15.0f,
                                (stock.food + stock.cookedFood > 5) ? LIME : RED);
    stockX += 135.0f;

    FontManager::DrawTextShadow(TextFormat("Holz: %d", stock.wood), stockX, 15.0f, 15.0f, Color{ 215, 155, 80, 255 });
    stockX += 75.0f;
    FontManager::DrawTextShadow(TextFormat("Stein: %d", stock.stone), stockX, 15.0f, 15.0f, Color{ 200, 200, 200, 255 });
    stockX += 75.0f;
    FontManager::DrawTextShadow(TextFormat("Fell: %d", stock.hides), stockX, 15.0f, 15.0f, Color{ 225, 180, 125, 255 });
    stockX += 70.0f;

    if (stock.planks > 0 || stock.bricks > 0 || era >= Era::BronzeAge) {
        FontManager::DrawTextShadow(TextFormat("Balken: %d", stock.planks), stockX, 15.0f, 15.0f, Color{ 240, 185, 105, 255 });
        stockX += 85.0f;
        FontManager::DrawTextShadow(TextFormat("Ziegel: %d", stock.bricks), stockX, 15.0f, 15.0f, Color{ 215, 215, 225, 255 });
    }

    // Territorien-Button & Skilltree-Button
    Rectangle btnTerritory = { (float)screenW - 370.0f, 9.0f, 130.0f, 32.0f };
    bool hovTerr = CheckCollisionPointRec(mouseScreen, btnTerritory);
    DrawRectangleRec(btnTerritory, showTerritoryOverlay_ ? Color{ 70, 110, 65, 255 } : (hovTerr ? Color{ 45, 70, 55, 255 } : Color{ 28, 42, 35, 255 }));
    DrawRectangleLinesEx(btnTerritory, 1.2f, showTerritoryOverlay_ ? LIME : SKYBLUE);
    FontManager::DrawTextCenteredShadow("[T] Territorien", btnTerritory.x + btnTerritory.width * 0.5f, btnTerritory.y + 6.0f, 15.0f, showTerritoryOverlay_ ? LIME : SKYBLUE);

    Rectangle btnSkillTree = { (float)screenW - 225.0f, 9.0f, 135.0f, 32.0f };
    bool hovTree = CheckCollisionPointRec(mouseScreen, btnSkillTree);
    DrawRectangleRec(btnSkillTree, hovTree ? Color{ 55, 80, 130, 255 } : Color{ 32, 45, 75, 255 });
    DrawRectangleLinesEx(btnSkillTree, 1.2f, GOLD);
    FontManager::DrawTextCenteredShadow("[K] Skilltree", btnSkillTree.x + btnSkillTree.width * 0.5f, btnSkillTree.y + 6.0f, 16.0f, GOLD);

    DrawFPS(screenW - 75, 15);

    // Statusleiste für Territorien bei aktivem Overlay (Phase 4)
    if (showTerritoryOverlay_) {
        int currentTiles = uiTribe.CurrentTerritoryCount();
        int maxCapacity = uiTribe.MaxTerritoryCapacity();
        int anchorCount = 0;
        for (const auto& b : uiTribe.Buildings().All()) {
            if (b.IsTerritoryAnchor()) anchorCount++;
        }
        std::string focusText = uiTribe.GetExpansionTarget().active ? uiTribe.GetExpansionTarget().reason : "Kernland";
        std::string territoryInfo = TextFormat("Territorium: %d / %d Kacheln  |  Anker: %d  |  Fokus: %s",
                                               currentTiles, maxCapacity, anchorCount, focusText.c_str());

        int textW = MeasureText(territoryInfo.c_str(), 14);
        int barW = textW + 36;
        int barX = (screenW - barW) / 2;
        DrawRectangle(barX, 54, barW, 26, Fade(BLACK, 0.88f));
        DrawRectangleLines(barX, 54, barW, 26, uiTribe.FactionColor());
        FontManager::DrawTextCenteredShadow(territoryInfo.c_str(), barX + barW * 0.5f, 58.0f, 14.0f, uiTribe.FactionColor());
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
        int panelW = 340;
        int panelH = 550;
        int panelX = screenW - panelW - 16;
        int panelY = 60;

        DrawRectangle(panelX, panelY, panelW, panelH, Fade(BLACK, 0.92f));
        DrawRectangleLines(panelX, panelY, panelW, panelH, RoleColor(selected->GetRole()));

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
            FontManager::DrawTextShadow(TextFormat("%s: %.0f/%.0f", label, val, maxVal), panelX + 14.0f, (float)y, 13.0f, RAYWHITE);
            int barX = panelX + 130;
            int barW = 185;
            int barH = 11;
            DrawRectangle(barX, y + 2, barW, barH, Fade(GRAY, 0.4f));
            float pct = std::clamp(val / maxVal, 0.0f, 1.0f);
            DrawRectangle(barX, y + 2, (int)(barW * pct), barH, barColor);
            DrawRectangleLines(barX, y + 2, barW, barH, Fade(WHITE, 0.5f));
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
        FontManager::DrawTextShadow("KI-Entscheidung (Utility AI):", panelX + 14.0f, panelY + 359.0f, 14.0f, GOLD);
        FontManager::DrawTextShadow(TextFormat("-> Grund: %s", selected->DecisionReason().c_str()), panelX + 14.0f, panelY + 378.0f, 13.0f, RAYWHITE);
        FontManager::DrawTextShadow(TextFormat("Nutzen-Score: %.2f  |  Wissen: +%.0f%%", selected->DecisionScore(), selected->KnowledgeBonus() * 100.0f),
                                    panelX + 14.0f, panelY + 398.0f, 13.0f, SKYBLUE);

        // Letzte prägende Erinnerung
        const auto& memories = selected->Memory().Entries();
        if (!memories.empty()) {
            const auto& lastMem = memories.back();
            FontManager::DrawTextShadow(TextFormat("Erinnerung: %s (zu #%d)", MemoryTypeName(lastMem.type), lastMem.otherId),
                                        panelX + 14.0f, panelY + 422.0f, 13.0f, (lastMem.baseImpact >= 0 ? LIME : ORANGE));
        }

        // Legende für Beziehungslinien auf der Karte
        FontManager::DrawTextShadow("Linien: Pink=Partner | Gold=Familie | Grün=Freund | Rot=Rivale", panelX + 14.0f, panelY + 456.0f, 12.0f, LIGHTGRAY);
        FontManager::DrawTextShadow("Klick auf anderen Menschen zum Wechseln", panelX + 14.0f, panelY + 478.0f, 12.0f, GRAY);
    }

    // -------------------------------------------------------------------------
    // 3. Ereignis-Log (unten links)
    // -------------------------------------------------------------------------
    const auto& logEntries = eventLog_.Entries();
    if (!logEntries.empty()) {
        float maxTextW = 220.0f;
        for (const auto& entry : logEntries) {
            float tw = FontManager::MeasureText(entry.text.c_str(), 14.0f);
            if (tw > maxTextW) maxTextW = tw;
        }
        int totalH = (int)logEntries.size() * 22 + 8;
        int boxY = screenH - 58 - (int)logEntries.size() * 22 - 4;
        DrawRectangleRounded({ 12.0f, (float)boxY, maxTextW + 24.0f, (float)totalH }, 0.15f, 4, Fade(BLACK, 0.78f));
        DrawRectangleRoundedLines({ 12.0f, (float)boxY, maxTextW + 24.0f, (float)totalH }, 0.15f, 4, Fade(GRAY, 0.40f));

        int logY = boxY + 6;
        for (const auto& entry : logEntries) {
            FontManager::DrawTextShadow(entry.text.c_str(), 18.0f, (float)logY, 14.0f, Fade(entry.color, 0.95f));
            logY += 22;
        }
    }

    // -------------------------------------------------------------------------
    // 4. Interaktives Skilltree-Overlay (über allen anderen Elementen)
    // -------------------------------------------------------------------------
    skillTreeUI_.Draw(uiTribe, mouseScreen);
}

