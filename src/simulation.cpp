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
      tribe_(world_.CenterPosition()),
      civSim_({ world_.CenterPosition().x + 50.0f, world_.CenterPosition().y }, 5) {

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
    eventLog_.Add(TextFormat("%s (%s) dem Stamm beigetreten", RoleDefaultNames(role, id), RoleName(role)), RoleColor(role));
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

    Vector2 mouseScreen = GetMousePosition();
    Vector2 mouseWorld = camera_.ScreenToWorld(mouseScreen);

    // Prüfen, ob Maus im spielbaren Bereich liegt (nicht in oberer Leiste, nicht im Inspektor-Panel, nicht in Letterbox-Rändern)
    bool outOfVirtualScreen = (mouseScreen.x < 0.0f || mouseScreen.x > (float)SCREEN_WIDTH ||
                               mouseScreen.y < 0.0f || mouseScreen.y > (float)SCREEN_HEIGHT);
    bool inTopBar = (mouseScreen.y <= 48);
    bool inBottomHUD = (mouseScreen.y >= SCREEN_HEIGHT - 36 && mouseScreen.x <= 360);
    bool inInspector = false;
    if (selectedCavemanId_ != -1) {
        float panelW = 280.0f;
        float panelH = 260.0f;
        float panelX = (float)SCREEN_WIDTH - panelW - 16.0f;
        float panelY = 60.0f;
        Rectangle inspRect = { panelX, panelY, panelW, panelH };
        if (CheckCollisionPointRec(mouseScreen, inspRect)) {
            inInspector = true;
        }
    }
    bool canInteractWithWorld = !outOfVirtualScreen && !inTopBar && !inBottomHUD && !inInspector;

    // Linksklick: Entweder Inspektion, Spawnen oder Terrain malen (über TAB-Aktionsmenü gesteuert)
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (canInteractWithWorld) {
            // 1. Wenn kein Spawn-Tool aktiv ist, prüfen ob ein Wesen/Mensch zur Inspektion angeklickt wurde
            bool clickedEntity = false;
            if (currentAction_ != SelectedAction::SPAWN_HUNTER && currentAction_ != SelectedAction::SPAWN_ANIMAL) {
                // Zuerst Zivilisations-Creatures prüfen
                for (const auto& c : civSim_.Creatures()) {
                    if (c.IsDead()) continue;
                    if (Vector2Distance(mouseWorld, c.Position()) < 14.0f) {
                        selectedCreatureId_ = c.Id();
                        selectedCavemanId_ = -1;
                        clickedEntity = true;
                        break;
                    }
                }

                // Danach Cavemen prüfen
                if (!clickedEntity) {
                    for (const auto& c : cavemen_) {
                        if (c.IsDead()) continue;
                        if (Vector2Distance(mouseWorld, c.Position()) < 14.0f) {
                            selectedCavemanId_ = c.Id();
                            selectedCreatureId_ = -1;
                            clickedEntity = true;
                            break;
                        }
                    }
                }
            }

            // 2. Wenn keine Entity angeklickt wurde: Aktion ausführen
            if (!clickedEntity) {
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
        // Kontinuierliches Malen NUR wenn zuvor auf der Welt gestartet wurde!
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

    // Sub-stepping für stabile Logik
    float remaining = effectiveDt;
    while (remaining > 0.0f) {
        float step = std::min(remaining, cfg::kMaxSimStep);

        // 1. Ressourcen updaten (Nachwachsen, Tiere)
        resources_.Update(world_, tribe_.CampPosition(), step);

        // 2. Steinzeitmenschen updaten
        int population = GetAliveCount();
        WorldContext ctx{ world_, tribe_, resources_, eventLog_, population };

        for (auto& c : cavemen_) {
            c.Update(ctx, step);
        }

        // 3. Modulare Zivilisations-Simulation updaten (Creatures, Siedlung, Epochen)
        civSim_.Update(step, world_, eventLog_);

        remaining -= step;
    }
}

void Simulation::DrawWorld() const {
    BeginMode2D(camera_.Get());

    // 1. Kachel-Welt
    world_.Draw(camera_.Get());

    // 2. Ressourcen
    resources_.Draw(camera_.Get());

    // 3. Stamm (Lagerfeuer, Vorratslager, Hütten)
    tribe_.Draw(totalSimTime_);

    // 4. Modulare Zivilisations-Siedlung & Kreaturen zeichnen
    civSim_.DrawWorld(&spriteSheet_, selectedCreatureId_);

    // 5. Steinzeitmenschen Y-sortiert zeichnen (Tiefensortierung)
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

    // 5. Aktions-Vorschau an Mausposition
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
    DrawRectangle(0, 0, screenW, 48, Fade(BLACK, 0.85f));
    DrawRectangleLines(0, 0, screenW, 48, Fade(GRAY, 0.5f));

    DrawText("Stone Age 2D Sandbox", 16, 14, 20, RAYWHITE);

    // Pause & Speed
    if (isPaused_) {
        DrawText("[PAUSIERT]", 260, 16, 18, RED);
    } else {
        DrawText(TextFormat("[SPEED: %.0fx]", timeSpeed_), 260, 16, 18, GREEN);
    }

    // Lager-Vorräte
    const auto& stock = tribe_.Stock();
    int pop = GetAliveCount();
    int sleepCap = tribe_.SleepingCapacity();

    DrawText(TextFormat("Pop: %d", pop), 400, 16, 18, RAYWHITE);
    DrawText(TextFormat("Nahrung: %d", stock.food), 500, 16, 18, (stock.food > 5) ? LIME : RED);
    DrawText(TextFormat("Holz: %d", stock.wood), 640, 16, 18, { 200, 140, 70, 255 });
    DrawText(TextFormat("Stein: %d", stock.stone), 750, 16, 18, { 190, 190, 190, 255 });
    DrawText(TextFormat("Hütten: %d (Betten: %d)", stock.huts, sleepCap), 870, 16, 18, GOLD);

    DrawFPS(screenW - 90, 16);

    // -------------------------------------------------------------------------
    // 2. Inspektor-Panel (rechts)
    // -------------------------------------------------------------------------
    // 2a. Inspektor für selektierte Zivilisations-Creature
    const Creature* selectedCreature = civSim_.GetCreatureById(selectedCreatureId_);
    if (selectedCreature && !selectedCreature->IsDead()) {
        int panelW = 280;
        int panelH = 260;
        int panelX = screenW - panelW - 16;
        int panelY = 60;

        DrawRectangle(panelX, panelY, panelW, panelH, Fade(BLACK, 0.85f));
        DrawRectangleLines(panelX, panelY, panelW, panelH, SKYBLUE);

        DrawText(selectedCreature->Name().c_str(), panelX + 14, panelY + 12, 18, RAYWHITE);
        DrawText(TextFormat("Epoche: %s", civSim_.Tech().CurrentAgeName()), panelX + 14, panelY + 34, 15, GOLD);

        DrawText(TextFormat("Status: %s", Creature::StateToString(selectedCreature->State())), panelX + 14, panelY + 56, 12, YELLOW);
        DrawText(TextFormat("Aktivitaet: %s", selectedCreature->ActivityDescription().c_str()), panelX + 14, panelY + 74, 12, LIGHTGRAY);

        auto drawStatBar = [&](const char* label, float val, float maxVal, int y, Color barColor) {
            DrawText(TextFormat("%s: %.0f/%.0f", label, val, maxVal), panelX + 14, y, 13, RAYWHITE);
            int barX = panelX + 120;
            int barW = 140;
            int barH = 12;
            DrawRectangle(barX, y + 1, barW, barH, Fade(GRAY, 0.4f));
            float pct = std::clamp(val / maxVal, 0.0f, 1.0f);
            DrawRectangle(barX, y + 1, (int)(barW * pct), barH, barColor);
            DrawRectangleLines(barX, y + 1, barW, barH, Fade(WHITE, 0.5f));
        };

        drawStatBar("Gesundheit", selectedCreature->Health(), 100.0f, panelY + 100, (selectedCreature->Health() > 50) ? GREEN : RED);
        drawStatBar("Hunger", selectedCreature->Hunger(), 100.0f, panelY + 124, (selectedCreature->Hunger() > 70) ? RED : ORANGE);
        drawStatBar("Energie", selectedCreature->Energy(), 100.0f, panelY + 148, SKYBLUE);

        if (selectedCreature->CarriedAmount() > 0) {
            DrawText(TextFormat("Traegt: %d x %s", selectedCreature->CarriedAmount(),
                     CivilizationTech::ResourceToString(selectedCreature->CarriedResource())),
                     panelX + 14, panelY + 180, 13, GREEN);
        } else {
            DrawText("Traegt: nichts", panelX + 14, panelY + 180, 13, DARKGRAY);
        }

        DrawText("Fokus: Zivilisations-Creature", panelX + 14, panelY + 230, 11, GRAY);
    }

    // 2b. Inspektor-Panel für selektierten Steinzeitmenschen (rechts)
    const Caveman* selected = nullptr;
    for (const auto& c : cavemen_) {
        if (c.Id() == selectedCavemanId_ && !c.IsDead()) {
            selected = &c;
            break;
        }
    }

    if (selected) {
        int panelW = 280;
        int panelH = 260;
        int panelX = screenW - panelW - 16;
        int panelY = 60;

        DrawRectangle(panelX, panelY, panelW, panelH, Fade(BLACK, 0.85f));
        DrawRectangleLines(panelX, panelY, panelW, panelH, RoleColor(selected->GetRole()));

        DrawText(selected->Name().c_str(), panelX + 14, panelY + 12, 18, RAYWHITE);
        DrawText(RoleName(selected->GetRole()), panelX + 14, panelY + 34, 15, RoleColor(selected->GetRole()));

        DrawText(TextFormat("Status: %s", StateName(selected->State())), panelX + 14, panelY + 56, 14, YELLOW);
        DrawText(TextFormat("Aktion: %s", selected->ActivityText().c_str()), panelX + 14, panelY + 74, 13, LIGHTGRAY);

        auto drawStatBar = [&](const char* label, float val, float maxVal, int y, Color barColor) {
            DrawText(TextFormat("%s: %.0f/%.0f", label, val, maxVal), panelX + 14, y, 13, RAYWHITE);
            int barX = panelX + 120;
            int barW = 140;
            int barH = 12;
            DrawRectangle(barX, y + 1, barW, barH, Fade(GRAY, 0.4f));
            float pct = std::clamp(val / maxVal, 0.0f, 1.0f);
            DrawRectangle(barX, y + 1, (int)(barW * pct), barH, barColor);
            DrawRectangleLines(barX, y + 1, barW, barH, Fade(WHITE, 0.5f));
        };

        drawStatBar("HP", selected->Hp(), 100.0f, panelY + 100, (selected->Hp() > 50) ? GREEN : RED);
        drawStatBar("AP", selected->Ap(), 100.0f, panelY + 124, SKYBLUE);
        drawStatBar("Hunger", selected->Hunger(), 100.0f, panelY + 148, (selected->Hunger() > 70) ? RED : ORANGE);
        drawStatBar("Müdigkeit", selected->Tiredness(), 100.0f, panelY + 172, (selected->Tiredness() > 80) ? PURPLE : BLUE);

        if (selected->Carrying() != CarryType::None) {
            const char* carryName = (selected->Carrying() == CarryType::Food) ? "Nahrung" :
                                    (selected->Carrying() == CarryType::Wood) ? "Holz" : "Stein";
            DrawText(TextFormat("Trägt: %d x %s", selected->CarryAmount(), carryName),
                     panelX + 14, panelY + 200, 14, GREEN);
        } else {
            DrawText("Trägt: nichts", panelX + 14, panelY + 200, 13, DARKGRAY);
        }

        DrawText("Klick auf anderen Menschen zum Wechseln", panelX + 14, panelY + 230, 11, GRAY);
    }

    // -------------------------------------------------------------------------
    // 3. Zivilisations-Status & Epochen-Leiste
    // -------------------------------------------------------------------------
    civSim_.DrawUI(screenW, screenH);

    // -------------------------------------------------------------------------
    // 3. Ereignis-Log (unten links)
    // -------------------------------------------------------------------------
    const auto& logEntries = eventLog_.Entries();
    int logY = screenH - 55 - (int)logEntries.size() * 18;
    for (const auto& entry : logEntries) {
        DrawText(entry.text.c_str(), 16, logY, 14, Fade(entry.color, 0.9f));
        logY += 18;
    }
}

