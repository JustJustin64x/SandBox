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

void Simulation::HandleInput() {
    // Pause umschalten
    if (IsKeyPressed(KEY_SPACE)) {
        isPaused_ = !isPaused_;
    }

    // Geschwindigkeit
    if (IsKeyPressed(KEY_F1)) timeSpeed_ = 1.0f;
    if (IsKeyPressed(KEY_F2)) timeSpeed_ = 2.0f;
    if (IsKeyPressed(KEY_F3)) timeSpeed_ = 4.0f;

    // Kachel-Werkzeug wählen (1 bis 6)
    if (IsKeyPressed(KEY_ONE))   selectedTile_ = TILE_DEEP_WATER;
    if (IsKeyPressed(KEY_TWO))   selectedTile_ = TILE_SHALLOW_WATER;
    if (IsKeyPressed(KEY_THREE)) selectedTile_ = TILE_SAND;
    if (IsKeyPressed(KEY_FOUR))  selectedTile_ = TILE_GRASS;
    if (IsKeyPressed(KEY_FIVE))  selectedTile_ = TILE_FOREST;
    if (IsKeyPressed(KEY_SIX))   selectedTile_ = TILE_MOUNTAIN;

    // Pinselgröße (+/-)
    if (IsKeyPressed(KEY_KP_ADD) || IsKeyPressed(KEY_EQUAL)) {
        if (brushRadius_ < 4) brushRadius_++;
    }
    if (IsKeyPressed(KEY_KP_SUBTRACT) || IsKeyPressed(KEY_MINUS)) {
        if (brushRadius_ > 0) brushRadius_--;
    }

    Vector2 mouseScreen = GetMousePosition();
    Vector2 mouseWorld = camera_.ScreenToWorld(mouseScreen);

    // Klick auf Steinzeitmensch zur Inspektion
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        bool clickedCaveman = false;
        for (const auto& c : cavemen_) {
            if (c.IsDead()) continue;
            if (Vector2Distance(mouseWorld, c.Position()) < 14.0f) {
                selectedCavemanId_ = c.Id();
                clickedCaveman = true;
                break;
            }
        }

        // Wenn kein Caveman angeklickt wurde und nicht im UI-Bereich: Terrain malen
        if (!clickedCaveman && mouseScreen.y > 50 && mouseScreen.y < GetScreenHeight() - 40) {
            int tx = World::ToTile(mouseWorld.x);
            int ty = World::ToTile(mouseWorld.y);
            for (int dy = -brushRadius_; dy <= brushRadius_; ++dy) {
                for (int dx = -brushRadius_; dx <= brushRadius_; ++dx) {
                    world_.SetTile(tx + dx, ty + dy, selectedTile_);
                }
            }
        }
    } else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && mouseScreen.y > 50 && mouseScreen.y < GetScreenHeight() - 40) {
        // Kontinuierliches Terrain-Malen beim Ziehen
        int tx = World::ToTile(mouseWorld.x);
        int ty = World::ToTile(mouseWorld.y);
        for (int dy = -brushRadius_; dy <= brushRadius_; ++dy) {
            for (int dx = -brushRadius_; dx <= brushRadius_; ++dx) {
                world_.SetTile(tx + dx, ty + dy, selectedTile_);
            }
        }
    }

    // Rechtsklick: Entitäten spawnen
    // Shift: Sammlerin, Strg: Ältester, Alt: Kind, T: Tier, sonst: Jäger
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        if (IsKeyDown(KEY_T)) {
            SpawnAnimal(mouseWorld);
        } else if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
            SpawnCaveman(Role::Gatherer, mouseWorld);
        } else if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
            SpawnCaveman(Role::Elder, mouseWorld);
        } else if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) {
            SpawnCaveman(Role::Child, mouseWorld);
        } else {
            SpawnCaveman(Role::Hunter, mouseWorld);
        }
    }

    // Test-Taste 'R': Vorräte aufstocken
    if (IsKeyPressed(KEY_R)) {
        tribe_.Stock().food += 10;
        tribe_.Stock().wood += 10;
        tribe_.Stock().stone += 5;
        eventLog_.Add("Cheat: Vorräte aufgestockt (+10 Nahrung, +10 Holz, +5 Stein)!", GREEN);
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

    // 4. Steinzeitmenschen Y-sortiert zeichnen (Tiefensortierung)
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

    // 5. Pinsel-Vorschau an Mausposition
    Vector2 mouseWorld = camera_.ScreenToWorld(GetMousePosition());
    int tx = World::ToTile(mouseWorld.x);
    int ty = World::ToTile(mouseWorld.y);
    int startX = (tx - brushRadius_) * TILE_SIZE;
    int startY = (ty - brushRadius_) * TILE_SIZE;
    int size = (brushRadius_ * 2 + 1) * TILE_SIZE;
    DrawRectangleLines(startX, startY, size, size, WHITE);

    EndMode2D();
}

void Simulation::DrawUI() const {
    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();

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

    // Pinsel
    const TileProperty& prop = World::GetProperty(selectedTile_);
    DrawText(TextFormat("Pinsel: %s (R:%d)", prop.name, brushRadius_ + 1), 1140, 16, 16, prop.color);

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
    // 3. Ereignis-Log (unten links)
    // -------------------------------------------------------------------------
    const auto& logEntries = eventLog_.Entries();
    int logY = screenH - 55 - (int)logEntries.size() * 18;
    for (const auto& entry : logEntries) {
        DrawText(entry.text.c_str(), 16, logY, 14, Fade(entry.color, 0.9f));
        logY += 18;
    }

    // -------------------------------------------------------------------------
    // 4. Untere Hilfe- und Steuerungsleiste
    // -------------------------------------------------------------------------
    int bottomY = screenH - 32;
    DrawRectangle(0, bottomY, screenW, 32, Fade(BLACK, 0.85f));
    DrawRectangleLines(0, bottomY, screenW, 32, Fade(GRAY, 0.5f));
    DrawText("1-6: Terrain | Links: Malen / Wähle Mensch | Rechts: Jäger (Shift: Sammler, Strg: Ältester, Alt: Kind, T: Tier) | Leertaste: Pause | F1-F3: Tempo | R: Vorräte",
             15, bottomY + 8, 14, LIGHTGRAY);
}

