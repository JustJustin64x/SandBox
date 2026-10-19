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

Tribe& Simulation::CurrentUiTribe() {
    if (selectedCavemanId_ != -1) {
        for (const auto& c : cavemen_) {
            if (c.Id() == selectedCavemanId_) {
                int idx = std::clamp(c.ClanId() - 1, 0, (int)tribes_.size() - 1);
                return *tribes_[idx];
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
                int idx = std::clamp(c.ClanId() - 1, 0, (int)tribes_.size() - 1);
                return *tribes_[idx];
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

void Simulation::SpawnCaveman(Role role, Vector2 worldPos, int preferredClanId, Color clanColor) {
    if (!world_.IsWalkable(worldPos)) return;
    int id = nextCavemanId_++;
    cavemen_.emplace_back(id, RoleDefaultNames(role, id), role, worldPos);

    int tribeIdx = std::clamp(preferredClanId - 1, 0, (int)tribes_.size() - 1);
    Tribe& tr = *tribes_[tribeIdx];

    tr.Hierarchy().AssignToClan(id, (int)role, preferredClanId);
    int clanId = preferredClanId > 0 ? preferredClanId : tr.Hierarchy().GetClanId(id);
    const Clan* clan = tr.Hierarchy().GetClan(clanId);
    std::string clanName = clan ? clan->name : tr.Name();
    Color bColor = (clanColor.a > 0) ? clanColor : (clan ? clan->bannerColor : tr.FactionColor());

    SocialRank rank = tr.Hierarchy().GetRank(id);
    Vector2 homeCamp = tr.CampPosition();
    cavemen_.back().SyncSocialVisuals(clanId, bColor, (int)rank, homeCamp);

    eventLog_.Add(TextFormat("%s (%s) ist dem %s beigetreten", RoleDefaultNames(role, id), RoleName(role), clanName.c_str()),
                  bColor);
}

void Simulation::SpawnChildFromParents(Caveman& parentA, Caveman& parentB, Vector2 spawnPos) {
    int id = nextCavemanId_++;
    Personality childPers = Personality::Inherit(parentA.GetPersonality(), parentB.GetPersonality());
    cavemen_.emplace_back(id, RoleDefaultNames(Role::Child, id), Role::Child, spawnPos);
    cavemen_.back().SetPersonality(childPers);

    int parentClanId = parentA.ClanId();
    int tIdx = std::clamp(parentClanId - 1, 0, (int)tribes_.size() - 1);
    Tribe& tr = *tribes_[tIdx];

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

    // Finde 4 strategisch ideale Standorte für die unabhängigen Stämme (Phase 3)
    std::vector<Vector2> spawnLocs = FindTribeSpawnLocations(4);

    struct TribeDef {
        int id;
        const char* name;
        Color color;
    };
    static const TribeDef kDefs[4] = {
        { 1, "Sonnen-Clan",   Color{ 215, 50,  50,  255 } }, // Karmesinrot
        { 2, "Flusswächter",  Color{ 45,  120, 225, 255 } }, // Kobaltblau
        { 3, "Waldläufer",    Color{ 40,  175, 75,  255 } }, // Smaragdgrün
        { 4, "Felsenvolk",    Color{ 225, 170, 35,  255 } }  // Ockergelb
    };

    for (int i = 0; i < 4; ++i) {
        auto tr = std::make_unique<Tribe>(spawnLocs[i]);
        tr->SetIdentity(kDefs[i].id, kDefs[i].name, kDefs[i].color);
        tr->InitWorld(world_);
        tribes_.push_back(std::move(tr));
    }

    // Ressourcen & Umweltphänomene initialisieren
    resources_.Populate(world_, tribes_[0]->CampPosition());
    env_.InitDemoPhenomena(world_);

    // 8-10 Cavemen pro Stamm eng geklüngelt (2-4 Kacheln) um das jeweilige Lagerfeuer spawnen
    for (size_t t = 0; t < tribes_.size(); ++t) {
        Vector2 camp = tribes_[t]->CampPosition();
        int clanId = tribes_[t]->Id();
        Color cColor = tribes_[t]->FactionColor();

        // 4 Jäger
        for (int i = 0; i < 4; ++i) {
            Vector2 pos;
            if (world_.FindRandomWalkablePosition(camp, 45.0f, pos, 30)) {
                SpawnCaveman(Role::Hunter, pos, clanId, cColor);
            }
        }
        // 4 Sammler
        for (int i = 0; i < 4; ++i) {
            Vector2 pos;
            if (world_.FindRandomWalkablePosition(camp, 45.0f, pos, 30)) {
                SpawnCaveman(Role::Gatherer, pos, clanId, cColor);
            }
        }
        // 1 Ältester
        Vector2 pos;
        if (world_.FindRandomWalkablePosition(camp, 35.0f, pos, 30)) {
            SpawnCaveman(Role::Elder, pos, clanId, cColor);
        }
    }

    // Zusätzliche Wildtiere
    for (int i = 0; i < cfg::kAnimalCount; ++i) {
        Vector2 pos;
        if (world_.FindRandomWalkablePosition(world_.CenterPosition(), 220.0f, pos, 30)) {
            SpawnAnimal(pos);
        }
    }

    camera_.SetTarget(tribes_[0]->CampPosition());
    eventLog_.Add(TextFormat("4 unabhängige Zivilisationen gegründet! Seed: %u", seed), GOLD);
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

    // Zivilisationen durchschalten (Tasten 1..4)
    if (IsKeyPressed(KEY_ONE) && tribes_.size() > 0)   { activeTribeIndex_ = 0; camera_.SetTarget(tribes_[0]->CampPosition()); }
    if (IsKeyPressed(KEY_TWO) && tribes_.size() > 1)   { activeTribeIndex_ = 1; camera_.SetTarget(tribes_[1]->CampPosition()); }
    if (IsKeyPressed(KEY_THREE) && tribes_.size() > 2) { activeTribeIndex_ = 2; camera_.SetTarget(tribes_[2]->CampPosition()); }
    if (IsKeyPressed(KEY_FOUR) && tribes_.size() > 3)  { activeTribeIndex_ = 3; camera_.SetTarget(tribes_[3]->CampPosition()); }

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
            bool isPointSpawnTool = (currentAction_ == SelectedAction::SPAWN_HUNTER ||
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
                    case SelectedAction::SPAWN_HUNTER:
                        SpawnCaveman(Role::Hunter, mouseWorld);
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
        int tIdx = std::clamp(c.ClanId() - 1, 0, (int)tribes_.size() - 1);
        Tribe& tr = *tribes_[tIdx];
        SocialRank rank = tr.Hierarchy().GetRank(c.Id());
        c.SyncSocialVisuals(tr.Id(), tr.FactionColor(), (int)rank, tr.CampPosition());
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

        // 2. Ressourcen updaten
        for (const auto& t : tribes_) {
            resources_.Update(world_, t->CampPosition(), step);
        }

        // 3. Stämme, autonomer Siedlungsplaner & Führung updaten
        int population = GetAliveCount();
        for (auto& t : tribes_) {
            t->Update(world_, cavemen_, population, step, eventLog_);
            t->Hierarchy().UpdateLeadership(t->Relationships(), cavemen_, eventLog_);
        }

        // 4. Demografie (Nachwuchs & Vererbung), Fraktionsspaltung & Clan-Visuals
        UpdateDemographicsAndFactions(step);

        // 5. Steinzeitmenschen mit Utility AI, Soziologie & Umwelt-Kontext updaten
        for (auto& c : cavemen_) {
            int tIdx = std::clamp(c.ClanId() - 1, 0, (int)tribes_.size() - 1);
            Tribe& tr = *tribes_[tIdx];
            WorldContext ctx{ world_, tr, resources_, eventLog_, population, timeOfDay, &cavemen_, &env_ };
            c.Update(ctx, step);
        }

        remaining -= step;
    }
}

void Simulation::DrawRelationshipLines(const Caveman& selected) const {
    float fromYOffset = world_.GetVisualYOffset(selected.Position());
    Vector2 from = { selected.Position().x, selected.Position().y - 8.0f + fromYOffset };
    int selId = selected.Id();
    int tIdx = std::clamp(selected.ClanId() - 1, 0, (int)tribes_.size() - 1);
    const Tribe& tr = *tribes_[tIdx];

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

    for (const auto* c : sortedCavemen) {
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
        if (currentAction_ == SelectedAction::SPAWN_HUNTER) {
            DrawCircleLines((int)mouseWorld.x, (int)mouseWorld.y, 14.0f, PURPLE);
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

    // 4 Zivilisations-Badges zum schnellen Durchschalten der Stämme
    float tabX = 14.0f;
    float tabY = 110.0f;
    Vector2 mouseScreen = GetMousePosition();
    for (size_t i = 0; i < tribes_.size(); ++i) {
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

    // Zeitalter & Kausales 14-Epochen-Modell der aktiven Zivilisation
    Era era = uiTribe.Age().CurrentEra();
    HistoricalEpoch curEpoch = uiTribe.Age().CurrentEpoch();
    FontManager::DrawTextShadow(TextFormat("[%s | %s]  (M:%d/%d)", EraName(era), HistoricalEpochName(curEpoch),
                                           uiTribe.Skills().GetMilestonesCompleted(era), uiTribe.Skills().GetMilestonesTotal(era)),
                                235.0f, 14.0f, 16.0f, EraColor(era));

    // Tag / Nacht + Wetter Indikator
    float timeOfDay = std::fmod(totalSimTime_, cfg::kDayCycleDuration) / cfg::kDayCycleDuration;
    bool isNight = (timeOfDay > 0.72f || timeOfDay < 0.22f);
    FontManager::DrawTextShadow(isNight ? "NACHT" : "TAG", 590.0f, 14.0f, 16.0f, isNight ? SKYBLUE : GOLD);
    FontManager::DrawTextShadow(WeatherTypeName(env_.GetWeather()), 650.0f, 14.0f, 16.0f, WeatherTypeColor(env_.GetWeather()));

    // Pause & Speed
    if (isPaused_) {
        FontManager::DrawTextShadow("[PAUSE]", 715.0f, 14.0f, 16.0f, RED);
    } else {
        FontManager::DrawTextShadow(TextFormat("[%.0fx]", timeSpeed_), 715.0f, 14.0f, 16.0f, GREEN);
    }

    // Lager-Vorräte (Rohstoffe & Veredelt) der aktiven Zivilisation
    const auto& stock = uiTribe.Stock();
    int pop = GetAliveCount();
    int sleepCap = uiTribe.SleepingCapacity();

    FontManager::DrawTextShadow(TextFormat("Pop: %d", pop), 765.0f, 14.0f, 16.0f, RAYWHITE);
    FontManager::DrawTextShadow(TextFormat("Nahrung: %d (+%d)", stock.food, stock.cookedFood), 835.0f, 14.0f, 16.0f,
                                (stock.food + stock.cookedFood > 5) ? LIME : RED);
    FontManager::DrawTextShadow(TextFormat("Holz: %d", stock.wood), 980.0f, 14.0f, 16.0f, Color{ 215, 155, 80, 255 });
    FontManager::DrawTextShadow(TextFormat("Stein: %d", stock.stone), 1060.0f, 14.0f, 16.0f, Color{ 200, 200, 200, 255 });
    FontManager::DrawTextShadow(TextFormat("Fell: %d", stock.hides), 1145.0f, 14.0f, 16.0f, Color{ 225, 180, 125, 255 });

    if (stock.planks > 0 || stock.bricks > 0 || era >= Era::BronzeAge) {
        FontManager::DrawTextShadow(TextFormat("Balken: %d", stock.planks), 1215.0f, 14.0f, 16.0f, Color{ 240, 185, 105, 255 });
        FontManager::DrawTextShadow(TextFormat("Ziegel: %d", stock.bricks), 1305.0f, 14.0f, 16.0f, Color{ 215, 215, 225, 255 });
    }

    FontManager::DrawTextShadow(TextFormat("Betten: %d/%d", pop, sleepCap), 1385.0f, 14.0f, 16.0f, (sleepCap >= pop) ? GOLD : ORANGE);

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
        int panelH = 525;
        int panelX = screenW - panelW - 16;
        int panelY = 60;

        DrawRectangle(panelX, panelY, panelW, panelH, Fade(BLACK, 0.92f));
        DrawRectangleLines(panelX, panelY, panelW, panelH, RoleColor(selected->GetRole()));

        // Name, Rolle & Lebensalter (Stufe 3)
        FontManager::DrawTextShadow(selected->Name().c_str(), panelX + 14.0f, panelY + 12.0f, 22.0f, RAYWHITE);
        FontManager::DrawTextShadow(TextFormat("%s  |  Alter: %.0f / %.0f J.",
                                               RoleName(selected->GetRole()), selected->AgeYears(), selected->MaxLifespan()),
                                    panelX + 14.0f, panelY + 38.0f, 15.0f, RoleColor(selected->GetRole()));

        // Clan, Kohäsion & Rang
        int tIdx = std::clamp(selected->ClanId() - 1, 0, (int)tribes_.size() - 1);
        const Tribe& tr = *tribes_[tIdx];
        int clanId = tr.Hierarchy().GetClanId(selected->Id());
        const Clan* clan = tr.Hierarchy().GetClan(clanId);
        SocialRank rank = tr.Hierarchy().GetRank(selected->Id());
        FontManager::DrawTextShadow(TextFormat("%s (%s) | %s",
                                               tr.Name().c_str(),
                                               clan ? clan->name.c_str() : "Clan",
                                               SocialRankName(rank)),
                                    panelX + 14.0f, panelY + 58.0f, 14.0f, tr.FactionColor());

        // Persönlichkeits-Kurzprofil
        const Personality& p = selected->GetPersonality();
        FontManager::DrawTextShadow(TextFormat("Wesen: %s (Dom: %+.1f, Alt: %+.1f, Soz: %+.1f)",
                                               p.GetPrimaryTraitDescription().c_str(), p.dominance, p.altruism, p.sociability),
                                    panelX + 14.0f, panelY + 78.0f, 13.0f, LIGHTGRAY);

        // Beziehungen
        int partnerId = tr.Relationships().GetPartner(selected->Id());
        auto friends = tr.Relationships().GetFriends(selected->Id());
        auto enemies = tr.Relationships().GetEnemies(selected->Id());
        FontManager::DrawTextShadow(TextFormat("Partner: %s | Freunde: %d | Rivalen: %d",
                                               (partnerId >= 0 ? TextFormat("#%d", partnerId) : "Keiner"),
                                               (int)friends.size(), (int)enemies.size()),
                                    panelX + 14.0f, panelY + 96.0f, 14.0f, (partnerId >= 0 ? PINK : RAYWHITE));

        // Status & Tätigkeit
        FontManager::DrawTextShadow(TextFormat("Status: %s", StateName(selected->State())), panelX + 14.0f, panelY + 118.0f, 15.0f, YELLOW);
        FontManager::DrawTextShadow(TextFormat("Tätigkeit: %s", selected->ActivityText().c_str()), panelX + 14.0f, panelY + 136.0f, 14.0f, LIGHTGRAY);

        auto drawStatBar = [&](const char* label, float val, float maxVal, int y, Color barColor) {
            FontManager::DrawTextShadow(TextFormat("%s: %.0f/%.0f", label, val, maxVal), panelX + 14.0f, (float)y, 13.0f, RAYWHITE);
            int barX = panelX + 130;
            int barW = 185;
            int barH = 12;
            DrawRectangle(barX, y + 2, barW, barH, Fade(GRAY, 0.4f));
            float pct = std::clamp(val / maxVal, 0.0f, 1.0f);
            DrawRectangle(barX, y + 2, (int)(barW * pct), barH, barColor);
            DrawRectangleLines(barX, y + 2, barW, barH, Fade(WHITE, 0.5f));
        };

        drawStatBar("HP", selected->Hp(), 100.0f, panelY + 158, (selected->Hp() > 50) ? GREEN : RED);
        drawStatBar("AP", selected->Ap(), 100.0f, panelY + 176, SKYBLUE);
        drawStatBar("Hunger", selected->Hunger(), 100.0f, panelY + 194, (selected->Hunger() > 70) ? RED : ORANGE);
        drawStatBar("Wärme", selected->Warmth(), 100.0f, panelY + 212, (selected->Warmth() < 30) ? RED : (selected->Warmth() < 60) ? YELLOW : ORANGE);
        drawStatBar("Müdigkeit", selected->Tiredness(), 100.0f, panelY + 230, (selected->Tiredness() > 80) ? PURPLE : BLUE);
        drawStatBar("Motivation", selected->Morale(), 100.0f, panelY + 248, (selected->Morale() > 60) ? LIME : GOLD);
        drawStatBar("Gemeinschaft", 100.0f - selected->SocialNeed(), 100.0f, panelY + 266, (selected->SocialNeed() > 70) ? ORANGE : GREEN);

        // Kleidung & Ausrüstung (Meilenstein A)
        FontManager::DrawTextShadow(TextFormat("Kleidung: %s", ClothingTierName(selected->GetClothing())),
                                    panelX + 14.0f, panelY + 288.0f, 13.0f, Color{ 225, 190, 135, 255 });
        if (selected->GetToolTier() != ToolTier::BareHands) {
            FontManager::DrawTextShadow(TextFormat("Werkzeug: %s (%.0f%%) | Waffe: %s",
                                                   ToolTierName(selected->GetToolTier()), selected->ToolDurability(),
                                                   WeaponTierName(selected->GetWeaponTier())),
                                        panelX + 14.0f, panelY + 306.0f, 13.0f, Color{ 190, 225, 255, 255 });
        } else {
            FontManager::DrawTextShadow(TextFormat("Werkzeug: %s | Waffe: %s",
                                                   ToolTierName(selected->GetToolTier()), WeaponTierName(selected->GetWeaponTier())),
                                        panelX + 14.0f, panelY + 306.0f, 13.0f, LIGHTGRAY);
        }

        if (selected->Carrying() != CarryType::None) {
            const char* carryName = (selected->Carrying() == CarryType::Food) ? "Nahrung" :
                                    (selected->Carrying() == CarryType::Wood) ? "Holz" :
                                    (selected->Carrying() == CarryType::Stone) ? "Stein" :
                                    (selected->Carrying() == CarryType::Planks) ? "Balken" : "Ziegel";
            FontManager::DrawTextShadow(TextFormat("Trägt: %d x %s (%s)", selected->CarryAmount(), carryName, ContainerTierName(selected->GetContainer())),
                                        panelX + 14.0f, panelY + 324.0f, 14.0f, GREEN);
        } else {
            FontManager::DrawTextShadow(TextFormat("Tragehilfe: %s", ContainerTierName(selected->GetContainer())),
                                        panelX + 14.0f, panelY + 324.0f, 13.0f, DARKGRAY);
        }

        // --- Utility AI Log & Entscheidungsgrund ---
        DrawLine(panelX + 10, panelY + 346, panelX + panelW - 10, panelY + 346, Fade(GRAY, 0.5f));
        FontManager::DrawTextShadow("KI-Entscheidung (Utility AI):", panelX + 14.0f, panelY + 354.0f, 14.0f, GOLD);
        FontManager::DrawTextShadow(selected->DecisionReason().c_str(), panelX + 14.0f, panelY + 374.0f, 13.0f, RAYWHITE);
        FontManager::DrawTextShadow(TextFormat("Nutzen-Score: %.2f  |  Wissen: +%.0f%%", selected->DecisionScore(), selected->KnowledgeBonus() * 100.0f),
                                    panelX + 14.0f, panelY + 396.0f, 13.0f, SKYBLUE);

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
    int logY = screenH - 58 - (int)logEntries.size() * 22;
    for (const auto& entry : logEntries) {
        FontManager::DrawTextShadow(entry.text.c_str(), 16.0f, (float)logY, 16.0f, Fade(entry.color, 0.95f));
        logY += 22;
    }

    // -------------------------------------------------------------------------
    // 4. Interaktives Skilltree-Overlay (über allen anderen Elementen)
    // -------------------------------------------------------------------------
    skillTreeUI_.Draw(uiTribe, mouseScreen);
}

