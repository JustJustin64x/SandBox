#include "core/common.h"
#include "simulation.h"

// -----------------------------------------------------------------------------
// 1. ZUSTÄNDE DER ANWENDUNG
// -----------------------------------------------------------------------------
enum class AppState {
    MENU,       // Hauptmenü / Startbildschirm
    PLAYING     // Spielansicht
};

// -----------------------------------------------------------------------------
// 2. HAUPTPROGRAMM
// -----------------------------------------------------------------------------
int main() {
    // Fenster initialisieren
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Stone Age 2D Sandbox - Primitive Survival & Evolution");
    SetTargetFPS(TARGET_FPS);

    // WICHTIG: Raylib daran hindern, das Fenster bei ESC zu schließen,
    // damit wir ESC als Pause-Taste im Spiel nutzen können!
    SetExitKey(KEY_NULL);

    // Hauptsimulation erzeugen (bestehende Spielwelt)
    Simulation sim(
        DEFAULT_WORLD_WIDTH,
        DEFAULT_WORLD_HEIGHT,
        SCREEN_WIDTH,
        SCREEN_HEIGHT
    );

    // Start-Zustände festlegen
    AppState currentState = AppState::MENU;
    bool isPaused = false;
    bool showTabMenu = false;
    bool ignoreMouseUntilRelease = false;

    // Button-Positionen für das Hauptmenü (mittig)
    float btnWidth = 260.0f;
    float btnHeight = 60.0f;
    float centerX = (SCREEN_WIDTH - btnWidth) / 2.0f;
    Rectangle startButton = { centerX, SCREEN_HEIGHT / 2.0f - 20.0f, btnWidth, btnHeight };
    Rectangle exitButton  = { centerX, SCREEN_HEIGHT / 2.0f + 60.0f, btnWidth, btnHeight };

    // Kasten für das TAB-Menü
    Rectangle tabMenuBox = { SCREEN_WIDTH / 2.0f - 250.0f, SCREEN_HEIGHT / 2.0f - 180.0f, 500.0f, 350.0f };

    // =========================================================================
    // DIE SPIELSCHLEIFE
    // =========================================================================
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        Vector2 mousePos = GetMousePosition();

        // ---------------------------------------------------------------------
        // MAUS-SCHUTZ: Verhindert Klick-Durchgriff (Click-Through)
        // ---------------------------------------------------------------------
        if (ignoreMouseUntilRelease) {
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) || !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                ignoreMouseUntilRelease = false;
            }
        }

        // ---------------------------------------------------------------------
        // A) LOGIK & EINGABEN (UPDATE)
        // ---------------------------------------------------------------------
        if (currentState == AppState::MENU) {
            // Klicks im Startbildschirm auswerten
            if (!ignoreMouseUntilRelease && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (CheckCollisionPointRec(mousePos, startButton)) {
                    currentState = AppState::PLAYING; // Spiel starten!
                    ignoreMouseUntilRelease = true;
                    sim.CancelActiveInput();
                } else if (CheckCollisionPointRec(mousePos, exitButton)) {
                    break; // Schleife beenden -> Spiel schließt
                }
            }
        } 
        else if (currentState == AppState::PLAYING) {
            // ESC: Pause umschalten (oder Menü schließen)
            // Regel: Ist das TAB-Menü gerade offen, soll ESC zuerst das TAB-Menü schließen, ohne sofort zu pausieren.
            if (IsKeyPressed(KEY_ESCAPE)) {
                if (showTabMenu) {
                    showTabMenu = false; // Erst TAB-Menü zu
                    sim.SetPaused(isPaused);
                    ignoreMouseUntilRelease = true;
                    sim.CancelActiveInput();
                } else {
                    isPaused = !isPaused; // Pause an/aus
                    sim.SetPaused(isPaused);
                    ignoreMouseUntilRelease = true;
                    sim.CancelActiveInput();
                }
            }

            // TAB: Aktionsmenü öffnen / schließen
            if (IsKeyPressed(KEY_TAB)) {
                showTabMenu = !showTabMenu;
                if (showTabMenu) {
                    sim.SetPaused(true);
                } else {
                    sim.SetPaused(isPaused);
                }
                ignoreMouseUntilRelease = true;
                sim.CancelActiveInput();
            }

            // Optional: Leertaste schaltet Pause um, wenn TAB-Menü nicht aktiv ist
            if (IsKeyPressed(KEY_SPACE) && !showTabMenu) {
                isPaused = !isPaused;
                sim.SetPaused(isPaused);
                ignoreMouseUntilRelease = true;
                sim.CancelActiveInput();
            }

            // WENN TAB-MENÜ OFFEN IST:
            if (showTabMenu) {
                // Klickbare Buttons im TAB-Menü
                Rectangle btnGrass  = { tabMenuBox.x + 30,  tabMenuBox.y + 70,  200, 40 };
                Rectangle btnWater  = { tabMenuBox.x + 30,  tabMenuBox.y + 120, 200, 40 };
                Rectangle btnForest = { tabMenuBox.x + 30,  tabMenuBox.y + 170, 200, 40 };
                Rectangle btnHunter = { tabMenuBox.x + 260, tabMenuBox.y + 70,  200, 40 };
                Rectangle btnAnimal = { tabMenuBox.x + 260, tabMenuBox.y + 120, 200, 40 };
                Rectangle btnClose  = { tabMenuBox.x + 150, tabMenuBox.y + 240, 200, 36 };

                if (!ignoreMouseUntilRelease && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    if (CheckCollisionPointRec(mousePos, btnGrass)) {
                        sim.SetCurrentAction(SelectedAction::TERRAIN_GRASS);
                        showTabMenu = false;
                        sim.SetPaused(isPaused);
                        ignoreMouseUntilRelease = true;
                        sim.CancelActiveInput();
                    } else if (CheckCollisionPointRec(mousePos, btnWater)) {
                        sim.SetCurrentAction(SelectedAction::TERRAIN_WATER);
                        showTabMenu = false;
                        sim.SetPaused(isPaused);
                        ignoreMouseUntilRelease = true;
                        sim.CancelActiveInput();
                    } else if (CheckCollisionPointRec(mousePos, btnForest)) {
                        sim.SetCurrentAction(SelectedAction::TERRAIN_FOREST);
                        showTabMenu = false;
                        sim.SetPaused(isPaused);
                        ignoreMouseUntilRelease = true;
                        sim.CancelActiveInput();
                    } else if (CheckCollisionPointRec(mousePos, btnHunter)) {
                        sim.SetCurrentAction(SelectedAction::SPAWN_HUNTER);
                        showTabMenu = false;
                        sim.SetPaused(isPaused);
                        ignoreMouseUntilRelease = true;
                        sim.CancelActiveInput();
                    } else if (CheckCollisionPointRec(mousePos, btnAnimal)) {
                        sim.SetCurrentAction(SelectedAction::SPAWN_ANIMAL);
                        showTabMenu = false;
                        sim.SetPaused(isPaused);
                        ignoreMouseUntilRelease = true;
                        sim.CancelActiveInput();
                    } else if (CheckCollisionPointRec(mousePos, btnClose)) {
                        showTabMenu = false;
                        sim.SetPaused(isPaused);
                        ignoreMouseUntilRelease = true;
                        sim.CancelActiveInput();
                    }
                    // Alle weiteren Klicks im TAB-Overlay werden konsumiert (kein Click-Through!)
                }
            } 
            // WENN DAS SPIEL LÄUFT (weder Pause noch TAB-Menü):
            else if (!isPaused) {
                // Deine Simulation rechnet die Welt weiter
                sim.Update(dt);
            }
        }

        // ---------------------------------------------------------------------
        // B) ZEICHNEN (DRAW)
        // ---------------------------------------------------------------------
        BeginDrawing();
            ClearBackground(BLACK);

            if (currentState == AppState::MENU) {
                // --- 1. STARTBILDSCHIRM ZEICHNEN ---
                DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Color{ 22, 24, 28, 255 });
                DrawText("PRIMITIVE SANDBOX", SCREEN_WIDTH / 2 - 240, SCREEN_HEIGHT / 4, 46, GOLD);
                DrawText("Stone Age Evolution & Survival", SCREEN_WIDTH / 2 - 180, SCREEN_HEIGHT / 4 + 60, 22, LIGHTGRAY);

                // Start-Button
                bool hoverStart = CheckCollisionPointRec(mousePos, startButton);
                DrawRectangleRec(startButton, hoverStart ? Color{ 50, 160, 70, 255 } : Color{ 35, 120, 50, 255 });
                DrawRectangleLinesEx(startButton, 2.0f, RAYWHITE);
                int startTextW = MeasureText("SPIEL STARTEN", 22);
                DrawText("SPIEL STARTEN", (int)(startButton.x + (startButton.width - startTextW) / 2), (int)(startButton.y + (startButton.height - 22) / 2), 22, RAYWHITE);

                // Beenden-Button
                bool hoverExit = CheckCollisionPointRec(mousePos, exitButton);
                DrawRectangleRec(exitButton, hoverExit ? Color{ 180, 50, 50, 255 } : Color{ 130, 35, 35, 255 });
                DrawRectangleLinesEx(exitButton, 2.0f, RAYWHITE);
                int exitTextW = MeasureText("BEENDEN", 22);
                DrawText("BEENDEN", (int)(exitButton.x + (exitButton.width - exitTextW) / 2), (int)(exitButton.y + (exitButton.height - 22) / 2), 22, RAYWHITE);
            } 
            else if (currentState == AppState::PLAYING) {
                // --- 2. DEINE SPIELWELT ZEICHNEN ---
                sim.DrawWorld();
                sim.DrawUI();

                // Statusanzeige für das aktuell gewählte Werkzeug (unten links)
                const char* actionName = "Gras malen";
                SelectedAction act = sim.GetCurrentAction();
                if (act == SelectedAction::TERRAIN_WATER)   actionName = "Wasser malen";
                if (act == SelectedAction::TERRAIN_FOREST)  actionName = "Wald pflanzen";
                if (act == SelectedAction::SPAWN_HUNTER)    actionName = "Jäger spawnen";
                if (act == SelectedAction::SPAWN_ANIMAL)    actionName = "Wildtier spawnen";

                const char* actionLabel = TextFormat("Aktion: %s  |  [TAB]: Menü", actionName);
                int labelW = MeasureText(actionLabel, 16);
                DrawRectangle(14, SCREEN_HEIGHT - 38, labelW + 20, 28, Fade(BLACK, 0.75f));
                DrawRectangleLines(14, SCREEN_HEIGHT - 38, labelW + 20, 28, Fade(GOLD, 0.6f));
                DrawText(actionLabel, 24, SCREEN_HEIGHT - 32, 16, GOLD);

                // --- 3. PAUSE-OVERLAY (wenn ESC aktiv ist) ---
                if (isPaused && !showTabMenu) {
                    DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.65f));
                    const char* pauseTitle = "PAUSE";
                    int titleSize = 54;
                    int titleW = MeasureText(pauseTitle, titleSize);
                    DrawText(pauseTitle, (SCREEN_WIDTH - titleW) / 2, SCREEN_HEIGHT / 2 - 50, titleSize, RAYWHITE);

                    const char* pauseSub = "Drücke ESC zum Fortsetzen";
                    int subSize = 22;
                    int subW = MeasureText(pauseSub, subSize);
                    DrawText(pauseSub, (SCREEN_WIDTH - subW) / 2, SCREEN_HEIGHT / 2 + 20, subSize, LIGHTGRAY);
                }

                // --- 4. TAB-AKTIONSMENÜ OVERLAY (wenn TAB aktiv ist) ---
                if (showTabMenu) {
                    DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.60f));
                    DrawRectangleRec(tabMenuBox, Color{ 26, 28, 34, 250 });
                    DrawRectangleLinesEx(tabMenuBox, 2.0f, GOLD);

                    int menuTitleW = MeasureText("AKTIONSMENÜ", 28);
                    DrawText("AKTIONSMENÜ", (int)(tabMenuBox.x + (tabMenuBox.width - menuTitleW) / 2), (int)tabMenuBox.y + 20, 28, GOLD);

                    // Helferfunktion für Menü-Buttons
                    auto drawBtn = [&](Rectangle r, const char* txt, Color col, bool isCurrent) {
                        bool hov = CheckCollisionPointRec(mousePos, r);
                        Color fill = hov ? Fade(col, 0.95f) : Fade(col, 0.70f);
                        DrawRectangleRec(r, fill);
                        DrawRectangleLinesEx(r, isCurrent ? 2.5f : 1.0f, isCurrent ? GOLD : RAYWHITE);
                        int tw = MeasureText(txt, 16);
                        DrawText(txt, (int)(r.x + (r.width - tw) / 2), (int)r.y + 12, 16, RAYWHITE);
                    };

                    DrawText("--- Terrain ---", (int)tabMenuBox.x + 30, (int)tabMenuBox.y + 52, 14, GRAY);
                    drawBtn({ tabMenuBox.x + 30, tabMenuBox.y + 70, 200, 40 }, "1. Gras", Color{ 40, 110, 50, 255 }, act == SelectedAction::TERRAIN_GRASS);
                    drawBtn({ tabMenuBox.x + 30, tabMenuBox.y + 120, 200, 40 }, "2. Wasser", Color{ 35, 80, 150, 255 }, act == SelectedAction::TERRAIN_WATER);
                    drawBtn({ tabMenuBox.x + 30, tabMenuBox.y + 170, 200, 40 }, "3. Wald", Color{ 20, 80, 35, 255 }, act == SelectedAction::TERRAIN_FOREST);

                    DrawText("--- Spawnen ---", (int)tabMenuBox.x + 260, (int)tabMenuBox.y + 52, 14, GRAY);
                    drawBtn({ tabMenuBox.x + 260, tabMenuBox.y + 70, 200, 40 }, "Jäger erschaffen", Color{ 120, 60, 150, 255 }, act == SelectedAction::SPAWN_HUNTER);
                    drawBtn({ tabMenuBox.x + 260, tabMenuBox.y + 120, 200, 40 }, "Wildtier erschaffen", Color{ 150, 90, 40, 255 }, act == SelectedAction::SPAWN_ANIMAL);

                    // Schließen-Button
                    drawBtn({ tabMenuBox.x + 150, tabMenuBox.y + 240, 200, 36 }, "Schließen", Color{ 70, 70, 80, 255 }, false);

                    const char* hint = "[TAB] oder [ESC] zum Schließen";
                    int hintW = MeasureText(hint, 15);
                    DrawText(hint, (int)(tabMenuBox.x + (tabMenuBox.width - hintW) / 2), (int)tabMenuBox.y + 305, 15, LIGHTGRAY);
                }
            }
        EndDrawing();
    }

    // Aufräumen & Beenden
    CloseWindow();
    return 0;
}