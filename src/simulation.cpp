#include "simulation.h"
#include "config.h"

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
      camera_(screenWidth, screenHeight, world_.CenterPosition(), 2.2f),
      tribe_(world_.CenterPosition()) {

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

    // 2. Ressourcen in der Welt verteilen
    resources_.Populate(world_, tribe_.CampPosition());
    tribe_.InitWorld(world_);

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

    eventLog_.Add("Steinzeit-Stamm gegründet. Willkommen in der Steinzeit!", { 255, 230, 100, 255 });
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

    eventLog_.Add(TextFormat("%s (%s, %s) dem Stamm beigetreten", RoleDefaultNames(role, id), RoleName(role), clanName.c_str()),
                  clan ? clan->bannerColor : RoleColor(role));
}

void Simulation::SpawnAnimal(Vector2 worldPos) {
    if (!world_.IsWalkable(worldPos)) return;
    int idx = resources_.SpawnAnimal(world_, worldPos);
    if (idx >= 0) {
        eventLog_.Add("Ein neues Wildtier ist aufgetaucht!", { 220, 180, 120, 255 });
    }
}

void Simulation::SetCurrentAction(SelectedAction action) {
    currentAction_ = action;
    isPainting_ = false;
    switch (action) {
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
            break;
    }
}

void Simulation::HandleInput() {
    // Geschwindigkeit (F1-F3)
    if (IsKeyPressed(KEY_F1)) timeSpeed_ = 1.0f;
    if (IsKeyPressed(KEY_F2)) timeSpeed_ = 2.0f;
    if (IsKeyPressed(KEY_F3)) timeSpeed_ = 4.0f;

    // Skilltree / Technologiebaum umschalten mit Taste 'K'
    if (IsKeyPressed(KEY_K)) {
        skillTreeUI_.Toggle();
        CancelActiveInput();
    }

    Vector2 mouseScreen = GetMousePosition();

    // Wenn der Skilltree geöffnet ist, Eingaben exklusiv an das Overlay leiten
    if (skillTreeUI_.IsOpen()) {
        skillTreeUI_.Update(tribe_, mouseScreen, IsMouseButtonPressed(MOUSE_BUTTON_LEFT));
        return;
    }

    // Klick auf den Skilltree-Button in der oberen Leiste
    Rectangle btnSkillTree = { (float)SCREEN_WIDTH - 240.0f, 10.0f, 130.0f, 28.0f };
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouseScreen, btnSkillTree)) {
        skillTreeUI_.Toggle();
        CancelActiveInput();
        return;
    }

    // Hütte oder Baustelle manuell anfordern mit Taste 'B'
    if (IsKeyPressed(KEY_B)) {
        int site = tribe_.EnsureConstructionSite(world_, eventLog_);
        if (site < 0) {
            eventLog_.Add("Nicht genug Ressourcen oder kein freier Bauplatz für neues Gebäude!", RED);
        }
    }

    Vector2 mouseWorld = camera_.ScreenToWorld(mouseScreen);

    bool outOfVirtualScreen = (mouseScreen.x < 0.0f || mouseScreen.x > (float)SCREEN_WIDTH ||
                               mouseScreen.y < 0.0f || mouseScreen.y > (float)SCREEN_HEIGHT);
    bool inTopBar = (mouseScreen.y <= 48);
    bool inBottomHUD = (mouseScreen.y >= SCREEN_HEIGHT - 36 && mouseScreen.x <= 360);
    bool inInspector = false;
    if (selectedCavemanId_ != -1) {
        float panelW = 320.0f;
        float panelH = 430.0f;
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
            if (currentAction_ != SelectedAction::SPAWN_HUNTER && currentAction_ != SelectedAction::SPAWN_ANIMAL) {
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
                if (currentAction_ == SelectedAction::SPAWN_HUNTER) {
                    SpawnCaveman(Role::Hunter, mouseWorld);
                } else if (currentAction_ == SelectedAction::SPAWN_ANIMAL) {
                    SpawnAnimal(mouseWorld);
                } else {
                    isPainting_ = true;
                    int tx = World::ToTile(mouseWorld.x);
                    int ty = World::ToTile(mouseWorld.y);
                    world_.SetTile(tx, ty, selectedTile_);
                }
            }
        }
    } else if (isPainting_ && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        if (canInteractWithWorld) {
            int tx = World::ToTile(mouseWorld.x);
            int ty = World::ToTile(mouseWorld.y);
            world_.SetTile(tx, ty, selectedTile_);
        }
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        isPainting_ = false;
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

        // 1. Ressourcen updaten
        resources_.Update(world_, tribe_.CampPosition(), step);

        // 2. Stamm & Gebäude & Soziales updaten
        int population = GetAliveCount();
        tribe_.Update(world_, cavemen_, population, step, eventLog_);
        tribe_.Hierarchy().UpdateLeadership(tribe_.Relationships(), cavemen_, eventLog_);

        // 3. Steinzeitmenschen mit Utility AI & Soziologie updaten
        WorldContext ctx{ world_, tribe_, resources_, eventLog_, population, timeOfDay, &cavemen_ };

        for (auto& c : cavemen_) {
            c.Update(ctx, step);
        }

        remaining -= step;
    }
}

void Simulation::DrawWorld() const {
    BeginMode2D(camera_.Get());

    // 1. Kachel-Welt
    world_.Draw(camera_.Get());

    // 2. Ressourcen
    resources_.Draw(camera_.Get());

    // 3. Entdeckungsorte (Points of Interest)
    tribe_.POIs().Draw(camera_.Get(), totalSimTime_);

    // 4. Stamm & Gebäude
    tribe_.Draw(totalSimTime_);

    // 5. Nacht-Lichtstimmung
    float timeOfDay = std::fmod(totalSimTime_, cfg::kDayCycleDuration) / cfg::kDayCycleDuration;
    bool isNight = (timeOfDay > 0.72f || timeOfDay < 0.22f);
    if (isNight) {
        DrawRectangle(0, 0, world_.Width() * TILE_SIZE, world_.Height() * TILE_SIZE, Fade(Color{ 10, 15, 45, 255 }, 0.22f));
    }

    // 6. Steinzeitmenschen Y-sortiert zeichnen
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

    // 6. Aktions-Vorschau an Mausposition
    Vector2 mouseScreen = GetMousePosition();
    if (mouseScreen.x >= 0 && mouseScreen.x <= SCREEN_WIDTH && mouseScreen.y > 48 && mouseScreen.y < SCREEN_HEIGHT - 36) {
        Vector2 mouseWorld = camera_.ScreenToWorld(mouseScreen);
        if (currentAction_ == SelectedAction::SPAWN_HUNTER) {
            DrawCircleLines((int)mouseWorld.x, (int)mouseWorld.y, 14.0f, PURPLE);
        } else if (currentAction_ == SelectedAction::SPAWN_ANIMAL) {
            DrawCircleLines((int)mouseWorld.x, (int)mouseWorld.y, 14.0f, ORANGE);
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
    // 1. Obere Statusleiste
    // -------------------------------------------------------------------------
    DrawRectangle(0, 0, screenW, 48, Fade(BLACK, 0.88f));
    DrawRectangleLines(0, 0, screenW, 48, Fade(GRAY, 0.5f));

    DrawText("Stone Age 2D Sandbox", 16, 14, 20, RAYWHITE);

    // Zeitalter & Epochen-Pacing
    Era era = tribe_.Age().CurrentEra();
    DrawText(TextFormat("[%s]  %.0fs/%.0fs (M:%d/%d)", EraName(era),
                        tribe_.Age().EraElapsedTime(), tribe_.Age().RequiredEraDuration(era),
                        tribe_.Skills().GetMilestonesCompleted(era), tribe_.Skills().GetMilestonesTotal(era)),
             235, 15, 15, EraColor(era));

    // Tag / Nacht Indikator
    float timeOfDay = std::fmod(totalSimTime_, cfg::kDayCycleDuration) / cfg::kDayCycleDuration;
    bool isNight = (timeOfDay > 0.72f || timeOfDay < 0.22f);
    DrawText(isNight ? "NACHT" : "TAG", 465, 16, 15, isNight ? SKYBLUE : GOLD);

    // Pause & Speed
    if (isPaused_) {
        DrawText("[PAUSE]", 525, 16, 15, RED);
    } else {
        DrawText(TextFormat("[%.0fx]", timeSpeed_), 525, 16, 15, GREEN);
    }

    // Lager-Vorräte (Rohstoffe & Veredelt)
    const auto& stock = tribe_.Stock();
    int pop = GetAliveCount();
    int sleepCap = tribe_.SleepingCapacity();

    DrawText(TextFormat("Pop: %d", pop), 570, 16, 16, RAYWHITE);
    DrawText(TextFormat("Nahrung: %d", stock.food), 650, 16, 16, (stock.food > 5) ? LIME : RED);
    DrawText(TextFormat("Holz: %d", stock.wood), 770, 16, 16, { 200, 140, 70, 255 });
    DrawText(TextFormat("Stein: %d", stock.stone), 860, 16, 16, { 190, 190, 190, 255 });

    if (stock.planks > 0 || stock.bricks > 0 || era >= Era::BronzeAge) {
        DrawText(TextFormat("Balken: %d", stock.planks), 950, 16, 16, { 230, 175, 95, 255 });
        DrawText(TextFormat("Ziegel: %d", stock.bricks), 1055, 16, 16, { 200, 200, 210, 255 });
    }

    DrawText(TextFormat("Betten: %d/%d", pop, sleepCap), 1160, 16, 16, (sleepCap >= pop) ? GOLD : ORANGE);

    // Skilltree-Button
    Vector2 mouseScreen = GetMousePosition();
    Rectangle btnSkillTree = { (float)screenW - 250.0f, 10.0f, 140.0f, 28.0f };
    bool hovTree = CheckCollisionPointRec(mouseScreen, btnSkillTree);
    DrawRectangleRec(btnSkillTree, hovTree ? Color{ 55, 80, 130, 255 } : Color{ 32, 45, 75, 255 });
    DrawRectangleLinesEx(btnSkillTree, 1.2f, GOLD);
    DrawText("[K] Skilltree", (int)btnSkillTree.x + 18, (int)btnSkillTree.y + 6, 15, GOLD);

    DrawFPS(screenW - 90, 16);

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
        int panelW = 320;
        int panelH = 430;
        int panelX = screenW - panelW - 16;
        int panelY = 60;

        DrawRectangle(panelX, panelY, panelW, panelH, Fade(BLACK, 0.90f));
        DrawRectangleLines(panelX, panelY, panelW, panelH, RoleColor(selected->GetRole()));

        // Name & Rolle
        DrawText(selected->Name().c_str(), panelX + 14, panelY + 12, 18, RAYWHITE);
        DrawText(RoleName(selected->GetRole()), panelX + 14, panelY + 34, 15, RoleColor(selected->GetRole()));

        // Clan & Rang
        int clanId = tribe_.Hierarchy().GetClanId(selected->Id());
        const Clan* clan = tribe_.Hierarchy().GetClan(clanId);
        SocialRank rank = tribe_.Hierarchy().GetRank(selected->Id());
        DrawText(TextFormat("%s | %s", clan ? clan->name.c_str() : "Stammlos", SocialRankName(rank)),
                 panelX + 14, panelY + 52, 13, clan ? clan->bannerColor : GOLD);

        // Persönlichkeits-Kurzprofil
        const Personality& p = selected->GetPersonality();
        DrawText(TextFormat("Wesen: %s (Dom: %+.1f, Alt: %+.1f, Soz: %+.1f)",
                            p.GetPrimaryTraitDescription().c_str(), p.dominance, p.altruism, p.sociability),
                 panelX + 14, panelY + 70, 11, LIGHTGRAY);

        // Beziehungen
        int partnerId = tribe_.Relationships().GetPartner(selected->Id());
        auto friends = tribe_.Relationships().GetFriends(selected->Id());
        auto enemies = tribe_.Relationships().GetEnemies(selected->Id());
        DrawText(TextFormat("Partner: %s | Freunde: %d | Rivalen: %d",
                            (partnerId >= 0 ? TextFormat("#%d", partnerId) : "Keiner"),
                            (int)friends.size(), (int)enemies.size()),
                 panelX + 14, panelY + 86, 12, (partnerId >= 0 ? PINK : RAYWHITE));

        // Status & Tätigkeit
        DrawText(TextFormat("Status: %s", StateName(selected->State())), panelX + 14, panelY + 106, 13, YELLOW);
        DrawText(TextFormat("Tätigkeit: %s", selected->ActivityText().c_str()), panelX + 14, panelY + 122, 12, LIGHTGRAY);

        auto drawStatBar = [&](const char* label, float val, float maxVal, int y, Color barColor) {
            DrawText(TextFormat("%s: %.0f/%.0f", label, val, maxVal), panelX + 14, y, 12, RAYWHITE);
            int barX = panelX + 120;
            int barW = 175;
            int barH = 11;
            DrawRectangle(barX, y + 1, barW, barH, Fade(GRAY, 0.4f));
            float pct = std::clamp(val / maxVal, 0.0f, 1.0f);
            DrawRectangle(barX, y + 1, (int)(barW * pct), barH, barColor);
            DrawRectangleLines(barX, y + 1, barW, barH, Fade(WHITE, 0.5f));
        };

        drawStatBar("HP", selected->Hp(), 100.0f, panelY + 144, (selected->Hp() > 50) ? GREEN : RED);
        drawStatBar("AP", selected->Ap(), 100.0f, panelY + 162, SKYBLUE);
        drawStatBar("Hunger", selected->Hunger(), 100.0f, panelY + 180, (selected->Hunger() > 70) ? RED : ORANGE);
        drawStatBar("Wärme", selected->Warmth(), 100.0f, panelY + 198, (selected->Warmth() < 30) ? RED : (selected->Warmth() < 60) ? YELLOW : ORANGE);
        drawStatBar("Müdigkeit", selected->Tiredness(), 100.0f, panelY + 216, (selected->Tiredness() > 80) ? PURPLE : BLUE);
        drawStatBar("Motivation", selected->Morale(), 100.0f, panelY + 234, (selected->Morale() > 60) ? LIME : GOLD);
        drawStatBar("Gemeinschaft", 100.0f - selected->SocialNeed(), 100.0f, panelY + 252, (selected->SocialNeed() > 70) ? ORANGE : GREEN);

        if (selected->Carrying() != CarryType::None) {
            const char* carryName = (selected->Carrying() == CarryType::Food) ? "Nahrung" :
                                    (selected->Carrying() == CarryType::Wood) ? "Holz" :
                                    (selected->Carrying() == CarryType::Stone) ? "Stein" :
                                    (selected->Carrying() == CarryType::Planks) ? "Balken" : "Ziegel";
            DrawText(TextFormat("Trägt: %d x %s", selected->CarryAmount(), carryName),
                     panelX + 14, panelY + 276, 13, GREEN);
        } else {
            DrawText("Trägt: nichts", panelX + 14, panelY + 276, 12, DARKGRAY);
        }

        // --- Utility AI Log & Entscheidungsgrund ---
        DrawLine(panelX + 10, panelY + 298, panelX + panelW - 10, panelY + 298, Fade(GRAY, 0.5f));
        DrawText("KI-Entscheidung (Utility AI):", panelX + 14, panelY + 306, 12, GOLD);
        DrawText(selected->DecisionReason().c_str(), panelX + 14, panelY + 326, 11, RAYWHITE);
        DrawText(TextFormat("Nutzen-Score: %.2f", selected->DecisionScore()), panelX + 14, panelY + 348, 11, SKYBLUE);

        // Letzte prägende Erinnerung
        const auto& memories = selected->Memory().Entries();
        if (!memories.empty()) {
            const auto& lastMem = memories.back();
            DrawText(TextFormat("Erinnerung: %s (zu #%d)", MemoryTypeName(lastMem.type), lastMem.otherId),
                     panelX + 14, panelY + 372, 11, (lastMem.baseImpact >= 0 ? LIME : ORANGE));
        }

        DrawText("Klick auf anderen Menschen zum Wechseln", panelX + 14, panelY + 404, 11, GRAY);
    }

    // -------------------------------------------------------------------------
    // 3. Ereignis-Log (unten links)
    // -------------------------------------------------------------------------
    const auto& logEntries = eventLog_.Entries();
    int logY = screenH - 55 - (int)logEntries.size() * 18;
    for (const auto& entry : logEntries) {
        DrawText(entry.text.c_str(), 16, logY, 14, Fade(entry.color, 0.9f));
        logY += 18;
    }

    // -------------------------------------------------------------------------
    // 4. Interaktives Skilltree-Overlay (über allen anderen Elementen)
    // -------------------------------------------------------------------------
    skillTreeUI_.Draw(tribe_, mouseScreen);
}
