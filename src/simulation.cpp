#include "simulation.h"
#include "config.h"
#include "font_manager.h"

#include <algorithm>
#include <cmath>

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
      camera_(screenWidth, screenHeight, world_.CenterPosition(), 2.2f),
      tribe_(world_.CenterPosition()) {

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

    // 2. Ressourcen & Phase-1-Umweltphänomene in der Welt verteilen
    resources_.Populate(world_, tribe_.CampPosition());
    tribe_.InitWorld(world_);
    env_.InitDemoPhenomena(world_);

    // 3. Startbevölkerung um das Lagerfeuer spawnen
    Vector2 camp = tribe_.CampPosition();
    for (int i = 0; i < cfg::kStartHunters; ++i) {
        Vector2 pos;
        if (world_.FindRandomWalkablePosition(camp, 35.0f, pos, 30)) {
            SpawnCaveman(Role::Hunter, pos);
        }
    }
    for (int i = 0; i < cfg::kStartGatherers; ++i) {
        Vector2 pos;
        if (world_.FindRandomWalkablePosition(camp, 35.0f, pos, 30)) {
            SpawnCaveman(Role::Gatherer, pos);
        }
    }
    for (int i = 0; i < cfg::kStartElders; ++i) {
        Vector2 pos;
        if (world_.FindRandomWalkablePosition(camp, 30.0f, pos, 30)) {
            SpawnCaveman(Role::Elder, pos);
        }
    }
    for (int i = 0; i < cfg::kStartChildren; ++i) {
        Vector2 pos;
        if (world_.FindRandomWalkablePosition(camp, 30.0f, pos, 30)) {
            SpawnCaveman(Role::Child, pos);
        }
    }

    eventLog_.Add("Steinzeit-Stamm gegründet. Phase 1 (Physik & Umwelt) aktiv!", { 255, 230, 100, 255 });
}

int Simulation::GetAliveCount() const {
    int alive = 0;
    for (const auto& c : cavemen_) {
        if (!c.IsDead()) ++alive;
    }
    return alive;
}

void Simulation::SpawnCaveman(Role role, Vector2 worldPos) {
    if (!world_.IsWalkable(worldPos)) return;
    int id = nextCavemanId_++;
    cavemen_.emplace_back(id, RoleDefaultNames(role, id), role, worldPos);

    // Soziologie: Clanzuweisung & Hierarchie
    tribe_.Hierarchy().AssignToClan(id, (int)role);
    int clanId = tribe_.Hierarchy().GetClanId(id);
    const Clan* clan = tribe_.Hierarchy().GetClan(clanId);
    std::string clanName = clan ? clan->name : "Stamm";

    SocialRank rank = tribe_.Hierarchy().GetRank(id);
    Vector2 homeCamp = tribe_.Hierarchy().GetFactionCampForMember(id, tribe_.CampPosition());
    cavemen_.back().SyncSocialVisuals(clanId, clan ? clan->bannerColor : WHITE, (int)rank, homeCamp);

    eventLog_.Add(TextFormat("%s (%s, %s) dem Stamm beigetreten", RoleDefaultNames(role, id), RoleName(role), clanName.c_str()),
                  clan ? clan->bannerColor : RoleColor(role));
}

void Simulation::SpawnChildFromParents(Caveman& parentA, Caveman& parentB, Vector2 spawnPos) {
    int id = nextCavemanId_++;
    Personality childPers = Personality::Inherit(parentA.GetPersonality(), parentB.GetPersonality());
    cavemen_.emplace_back(id, RoleDefaultNames(Role::Child, id), Role::Child, spawnPos);
    cavemen_.back().SetPersonality(childPers);

    int parentClanId = tribe_.Hierarchy().GetClanId(parentA.Id());
    tribe_.Hierarchy().AssignToClan(id, (int)Role::Child, parentClanId);

    Family* fam = tribe_.Hierarchy().GetOrCreateFamily(parentA.Id(), parentB.Id());
    if (fam) {
        fam->childrenIds.push_back(id);
    }

    // Hohe familiäre Bindung zwischen Eltern und Kind anlegen
    auto setKinEdge = [&](int from, int to) {
        RelationshipEdge& e = tribe_.Relationships().GetOrCreate(from, to);
        e.affinity = 78.0f;
        e.trust = 85.0f;
        e.respect = 60.0f;
        e.RecalculateTag(true, false);
    };
    setKinEdge(parentA.Id(), id);
    setKinEdge(id, parentA.Id());
    setKinEdge(parentB.Id(), id);
    setKinEdge(id, parentB.Id());

    const Clan* clan = tribe_.Hierarchy().GetClan(parentClanId);
    Vector2 homeCamp = tribe_.Hierarchy().GetFactionCampForMember(id, tribe_.CampPosition());
    cavemen_.back().SyncSocialVisuals(parentClanId, clan ? clan->bannerColor : WHITE, (int)SocialRank::Worker, homeCamp);

    parentA.TriggerEmote(SocialEmote::Heart, 3.0f);
    parentB.TriggerEmote(SocialEmote::Heart, 3.0f);
    cavemen_.back().TriggerEmote(SocialEmote::Heart, 3.0f);

    eventLog_.Add(TextFormat("Nachwuchs! %s & %s bekommen %s (Erbe: %s)!",
                             parentA.Name().c_str(), parentB.Name().c_str(),
                             cavemen_.back().Name().c_str(),
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

    // Wenn der Skilltree geöffnet ist, Eingaben exklusiv an das Overlay leiten
    if (skillTreeUI_.IsOpen()) {
        skillTreeUI_.Update(tribe_, mouseScreen, IsMouseButtonPressed(MOUSE_BUTTON_LEFT));
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
        int site = tribe_.EnsureConstructionSite(world_, eventLog_);
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
                    if (Vector2Distance(mouseWorld, c.Position()) < 14.0f) {
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
                        int tx = World::ToTile(mouseWorld.x);
                        int ty = World::ToTile(mouseWorld.y);
                        world_.SetTile(tx, ty, selectedTile_);
                        break;
                    }
                }
            }
        }
    } else if (isPainting_ && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && currentAction_ != SelectedAction::NONE) {
        if (canInteractWithWorld) {
            int tx = World::ToTile(mouseWorld.x);
            int ty = World::ToTile(mouseWorld.y);
            if (currentAction_ == SelectedAction::IGNITE_FIRE) {
                env_.IgniteTile(world_, tx, ty, 0.9f);
            } else {
                world_.SetTile(tx, ty, selectedTile_);
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
        int clanId = tribe_.Hierarchy().GetClanId(c.Id());
        const Clan* clan = tribe_.Hierarchy().GetClan(clanId);
        SocialRank rank = tribe_.Hierarchy().GetRank(c.Id());
        Vector2 homeCamp = tribe_.Hierarchy().GetFactionCampForMember(c.Id(), tribe_.CampPosition());
        c.SyncSocialVisuals(clanId, clan ? clan->bannerColor : WHITE, (int)rank, homeCamp);
    }

    int pop = GetAliveCount();

    // 2. Stufe 3: Demografie, Fortpflanzung & genetische Vererbung
    demographicTimer_ += dt;
    if (demographicTimer_ >= 18.0f) {
        demographicTimer_ = 0.0f;

        // Wenn genug Nahrung vorhanden ist und die Bevölkerung unter dem Limit liegt, können Partner Nachwuchs zeugen
        if (pop < 26 && tribe_.Stock().food >= std::max(8, pop + 2) &&
            tribe_.Buildings().GetBuildingCount(BuildingType::Hut, true) > 0) {
            for (size_t i = 0; i < cavemen_.size(); ++i) {
                if (cavemen_[i].IsDead() || cavemen_[i].GetRole() == Role::Child) continue;
                if (cavemen_[i].Hunger() > 65.0f) continue;

                int partnerId = tribe_.Relationships().GetPartner(cavemen_[i].Id());
                if (partnerId < 0 || partnerId < cavemen_[i].Id()) continue; // Nur einmal pro Paar prüfen

                int partnerIdx = -1;
                for (size_t j = 0; j < cavemen_.size(); ++j) {
                    if (!cavemen_[j].IsDead() && cavemen_[j].Id() == partnerId && cavemen_[j].GetRole() != Role::Child) {
                        partnerIdx = (int)j;
                        break;
                    }
                }
                if (partnerIdx < 0) continue;

                // Prüfen, wie viele lebende Kinder die Familie bereits hat (max 2 kleine Kinder gleichzeitig)
                Family* fam = tribe_.Hierarchy().GetOrCreateFamily(cavemen_[i].Id(), partnerId);
                int activeChildCount = 0;
                if (fam) {
                    for (int cid : fam->childrenIds) {
                        for (const auto& ch : cavemen_) {
                            if (!ch.IsDead() && ch.Id() == cid && ch.GetRole() == Role::Child) {
                                activeChildCount++;
                            }
                        }
                    }
                }
                if (activeChildCount >= 2) continue;

                // Geburtsort: Nächste fertige Hütte oder nahe bei den Eltern
                Vector2 spawnPos = cavemen_[i].Position();
                int hutId = tribe_.Buildings().FindBestShelter(spawnPos);
                if (hutId >= 0) {
                    if (const Building* hut = tribe_.Buildings().Get(hutId)) {
                        world_.FindRandomWalkablePosition(hut->position, 16.0f, spawnPos, 15);
                    }
                }

                tribe_.Stock().food = std::max(0, tribe_.Stock().food - 3);
                SpawnChildFromParents(cavemen_[i], cavemen_[partnerIdx], spawnPos);
                break; // Maximal 1 Geburt pro Prüf-Intervall
            }
        }
    }

    // 3. Stufe 4: Dynamische Fraktionen & Stammesspaltung (Schisma)
    schismTimer_ += dt;
    if (schismTimer_ >= 24.0f) {
        schismTimer_ = 0.0f;

        int rebelClanId = -1;
        int rebelLeaderId = -1;
        if (tribe_.Hierarchy().CheckForSchism(tribe_.Relationships(), cavemen_, rebelClanId, rebelLeaderId)) {
            const Clan* rebelClan = tribe_.Hierarchy().GetClan(rebelClanId);
            // Suche einen freien Siedlungsplatz in ~45-70 Tiles Entfernung vom Hauptlager
            Vector2 newCampPos = { 0.0f, 0.0f };
            bool foundSpot = false;
            for (int attempt = 0; attempt < 90; ++attempt) {
                float angle = (float)GetRandomValue(0, 3600) / 3600.0f * 2.0f * PI;
                float dist = (float)GetRandomValue(38 * TILE_SIZE, 64 * TILE_SIZE);
                Vector2 candidate = {
                    tribe_.CampPosition().x + std::cos(angle) * dist,
                    tribe_.CampPosition().y + std::sin(angle) * dist
                };
                if (!world_.IsWalkable(candidate)) continue;
                if (candidate.x < 12 * TILE_SIZE || candidate.x > (world_.Width() - 12) * TILE_SIZE ||
                    candidate.y < 12 * TILE_SIZE || candidate.y > (world_.Height() - 12) * TILE_SIZE) {
                    continue;
                }
                bool tooClose = false;
                for (const auto& s : tribe_.Hierarchy().GetSplinterSettlements()) {
                    if (Vector2Distance(candidate, s.centerPos) < 35.0f * TILE_SIZE) {
                        tooClose = true;
                        break;
                    }
                }
                if (!tooClose) {
                    newCampPos = candidate;
                    foundSpot = true;
                    break;
                }
            }

            if (foundSpot && rebelClan) {
                // Kostenloses neues Stammeszentrum (Feuerkreis) für die abgespaltene Fraktion errichten
                Stockpile freeGrant;
                freeGrant.wood = 10;
                freeGrant.stone = 10;
                int tcId = tribe_.Buildings().PlaceBuilding(BuildingType::TownCenter, newCampPos, freeGrant, eventLog_);
                if (Building* tc = tribe_.Buildings().Get(tcId)) {
                    tc->finished = true;
                    tc->progress = 100.0f;
                    tc->warmthRadius = 55.0f;
                    tc->capacity = 2;
                }

                SplinterSettlement splinter;
                splinter.clanId = rebelClanId;
                splinter.factionName = "Freier " + rebelClan->name;
                splinter.bannerColor = rebelClan->bannerColor;
                splinter.centerPos = newCampPos;
                splinter.leaderId = rebelLeaderId;
                splinter.townCenterBuildingId = tcId;
                splinter.territoryRadius = 110.0f;
                tribe_.Hierarchy().RegisterSplinterSettlement(splinter);

                for (auto& c : cavemen_) {
                    if (!c.IsDead() && tribe_.Hierarchy().GetClanId(c.Id()) == rebelClanId) {
                        c.TriggerEmote(c.Id() == rebelLeaderId ? SocialEmote::Crown : SocialEmote::Dispute, 4.0f);
                    }
                }

                eventLog_.Add(TextFormat("STAMMESSPALTUNG! Der %s bricht unter Häuptling #%d auf und gründet eine eigene Siedlung!",
                                         rebelClan->name.c_str(), rebelLeaderId),
                              rebelClan->bannerColor);
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
        env_.Update(world_, resources_, tribe_.Buildings(), cavemen_, eventLog_, timeOfDay, step);
        world_.UpdateRoads(step);

        // 2. Ressourcen updaten
        resources_.Update(world_, tribe_.CampPosition(), step);

        // 3. Stamm, autonomer Siedlungsplaner & Führung updaten
        int population = GetAliveCount();
        tribe_.Update(world_, cavemen_, population, step, eventLog_);
        tribe_.Hierarchy().UpdateLeadership(tribe_.Relationships(), cavemen_, eventLog_);

        // 4. Demografie (Nachwuchs & Vererbung), Fraktionsspaltung & Clan-Visuals
        UpdateDemographicsAndFactions(step);

        // 5. Steinzeitmenschen mit Utility AI, Soziologie & Umwelt-Kontext updaten
        WorldContext ctx{ world_, tribe_, resources_, eventLog_, population, timeOfDay, &cavemen_, &env_ };

        for (auto& c : cavemen_) {
            c.Update(ctx, step);
        }

        remaining -= step;
    }
}

void Simulation::DrawRelationshipLines(const Caveman& selected) const {
    Vector2 from = { selected.Position().x, selected.Position().y - 8.0f };
    int selId = selected.Id();

    for (const auto& other : cavemen_) {
        if (other.IsDead() || other.Id() == selId) continue;

        const RelationshipEdge* edge = tribe_.Relationships().Get(selId, other.Id());
        RelationshipTag tag = edge ? edge->tag : RelationshipTag::Stranger;
        bool isPartner = (tribe_.Relationships().GetPartner(selId) == other.Id());
        bool isKin = tribe_.Hierarchy().AreKin(selId, other.Id());

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
            Vector2 to = { other.Position().x, other.Position().y - 8.0f };
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
    if (showTerritoryOverlay_) {
        tribe_.DrawTerritoryOverlay(world_, totalSimTime_);
    }
    env_.DrawTileOverlays(world_, camera_.Get(), totalSimTime_);

    // 2. Ressourcen (inkl. verkohlter Stümpfe & fliehender Tiere)
    resources_.Draw(camera_.Get());

    // 3. Entdeckungsorte (Points of Interest)
    tribe_.POIs().Draw(camera_.Get(), totalSimTime_);

    // 4. Stamm & Gebäude
    tribe_.Draw(totalSimTime_);

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

    // 7. Steinzeitmenschen Y-sortiert zeichnen
    std::vector<const Caveman*> sortedCavemen;
    sortedCavemen.reserve(cavemen_.size());
    for (const auto& c : cavemen_) {
        if (!c.IsDead()) sortedCavemen.push_back(&c);
    }
    std::sort(sortedCavemen.begin(), sortedCavemen.end(), [](const Caveman* a, const Caveman* b) {
        return a->Position().y < b->Position().y;
    });

    for (const auto* c : sortedCavemen) {
        bool highlighted = (c->Id() == selectedCavemanId_);
        if (highlighted) {
            c->DrawDebugPath();
        }
        c->Draw(spriteSheet_, highlighted);
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

    // Autonomer Siedlungsplaner & God-Sim Status oben links unter der Statusleiste
    DrawRectangle(14, 56, 360, 48, Fade(BLACK, 0.82f));
    DrawRectangleLines(14, 56, 360, 48, Fade(GOLD, 0.65f));
    FontManager::DrawTextShadow("Gott-Instanz: Autonome Menschheitssimulation", 22.0f, 61.0f, 15.0f, GOLD);
    const char* planText = tribe_.CurrentPlan().active
        ? TextFormat("Siedlungsplan: %s", tribe_.CurrentPlan().description.c_str())
        : "Siedlungsplan: Vollständig ausgebaut / Autonom aktiv";
    FontManager::DrawTextShadow(planText, 22.0f, 81.0f, 14.0f, RAYWHITE);

    // -------------------------------------------------------------------------
    // 1. Obere Statusleiste
    // -------------------------------------------------------------------------
    DrawRectangle(0, 0, screenW, 50, Fade(BLACK, 0.90f));
    DrawRectangleLines(0, 0, screenW, 50, Fade(GRAY, 0.5f));

    FontManager::DrawTextShadow("PRIMITIVE SANDBOX", 16.0f, 12.0f, 22.0f, RAYWHITE);

    // Zeitalter & Kausales 14-Epochen-Modell
    Era era = tribe_.Age().CurrentEra();
    HistoricalEpoch curEpoch = tribe_.Age().CurrentEpoch();
    FontManager::DrawTextShadow(TextFormat("[%s | %s]  (M:%d/%d)", EraName(era), HistoricalEpochName(curEpoch),
                                           tribe_.Skills().GetMilestonesCompleted(era), tribe_.Skills().GetMilestonesTotal(era)),
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

    // Lager-Vorräte (Rohstoffe & Veredelt)
    const auto& stock = tribe_.Stock();
    int pop = GetAliveCount();
    int sleepCap = tribe_.SleepingCapacity();

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
    Vector2 mouseScreen = GetMousePosition();
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
        int clanId = tribe_.Hierarchy().GetClanId(selected->Id());
        const Clan* clan = tribe_.Hierarchy().GetClan(clanId);
        SocialRank rank = tribe_.Hierarchy().GetRank(selected->Id());
        FontManager::DrawTextShadow(TextFormat("%s (Kohäsion %.0f%%) | %s",
                                               clan ? clan->name.c_str() : "Stammlos",
                                               clan ? clan->cohesion : 100.0f,
                                               SocialRankName(rank)),
                                    panelX + 14.0f, panelY + 58.0f, 14.0f, clan ? clan->bannerColor : GOLD);

        // Persönlichkeits-Kurzprofil
        const Personality& p = selected->GetPersonality();
        FontManager::DrawTextShadow(TextFormat("Wesen: %s (Dom: %+.1f, Alt: %+.1f, Soz: %+.1f)",
                                               p.GetPrimaryTraitDescription().c_str(), p.dominance, p.altruism, p.sociability),
                                    panelX + 14.0f, panelY + 78.0f, 13.0f, LIGHTGRAY);

        // Beziehungen
        int partnerId = tribe_.Relationships().GetPartner(selected->Id());
        auto friends = tribe_.Relationships().GetFriends(selected->Id());
        auto enemies = tribe_.Relationships().GetEnemies(selected->Id());
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
    skillTreeUI_.Draw(tribe_, mouseScreen);
}

