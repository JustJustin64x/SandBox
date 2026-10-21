#include "core/common.h"
#include "simulation.h"
#include "render_manager.h"
#include "font_manager.h"
#include "gfx/asset_manager.h"

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
    // Fenster & Render-Management initialisieren (HiDPI, dynamic resize, Centering)
    RenderManager renderMgr(SCREEN_WIDTH, SCREEN_HEIGHT);
    renderMgr.InitWindowAndContext("Stone Age 2D Sandbox - Primitive Survival & Evolution");
    SetTargetFPS(TARGET_FPS);

    // HD-Font-System initialisieren (bilineares Filtering gegen verpixelten Text)
    FontManager::Init();

    // Zentrales Asset-Management initialisieren (Texturen & Fallback-Pipeline)
    AssetManager::Get().Init();

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

    // Kasten für das TAB-Menü (erweitert für Phase 1: Physik & Umwelt)
    Rectangle tabMenuBox = { SCREEN_WIDTH / 2.0f - 260.0f, SCREEN_HEIGHT / 2.0f - 210.0f, 520.0f, 420.0f };
    Rectangle btnGrass   = { tabMenuBox.x + 30,  tabMenuBox.y + 75,  215, 33 };
    Rectangle btnWater   = { tabMenuBox.x + 30,  tabMenuBox.y + 114, 215, 33 };
    Rectangle btnForest  = { tabMenuBox.x + 30,  tabMenuBox.y + 153, 215, 33 };
    Rectangle btnMan     = { tabMenuBox.x + 30,  tabMenuBox.y + 192, 215, 33 };
    Rectangle btnWoman   = { tabMenuBox.x + 30,  tabMenuBox.y + 231, 215, 33 };
    Rectangle btnAnimal  = { tabMenuBox.x + 30,  tabMenuBox.y + 270, 215, 33 };

    Rectangle btnCursor  = { tabMenuBox.x + 275, tabMenuBox.y + 75,  215, 33 };
    Rectangle btnFire    = { tabMenuBox.x + 275, tabMenuBox.y + 114, 215, 33 };
    Rectangle btnGravity = { tabMenuBox.x + 275, tabMenuBox.y + 153, 215, 33 };
    Rectangle btnWeather = { tabMenuBox.x + 275, tabMenuBox.y + 192, 215, 33 };
    Rectangle btnRegen   = { tabMenuBox.x + 275, tabMenuBox.y + 231, 215, 33 };
    Rectangle btnClose   = { tabMenuBox.x + 160, tabMenuBox.y + 325, 200, 38 };

    // =========================================================================
    // DIE SPIELSCHLEIFE
    // =========================================================================
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        // Auflösungs- & Letterbox-Management aktualisieren
        renderMgr.Update();
        Vector2 mousePos = renderMgr.GetVirtualMousePosition();

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
            // ESC: Pause umschalten (oder Menü / Skilltree / Inspektor / Werkzeug schließen)
            if (IsKeyPressed(KEY_ESCAPE)) {
                if (sim.IsSkillTreeOpen()) {
                    sim.SetSkillTreeOpen(false);
                    ignoreMouseUntilRelease = true;
                    sim.CancelActiveInput();
                } else if (showTabMenu) {
                    showTabMenu = false; // Erst TAB-Menü zu
                    sim.SetPaused(isPaused);
                    ignoreMouseUntilRelease = true;
                    sim.CancelActiveInput();
                } else if (sim.GetCurrentAction() != SelectedAction::NONE) {
                    sim.SetCurrentAction(SelectedAction::NONE);
                    ignoreMouseUntilRelease = true;
                    sim.CancelActiveInput();
                } else if (sim.SelectedCavemanId() != -1) {
                    sim.DeselectCaveman();
                    ignoreMouseUntilRelease = true;
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
                auto selectAndClose = [&](SelectedAction act) {
                    // Klick auf bereits aktives Werkzeug wählt es wieder ab -> Normaler Cursor!
                    if (sim.GetCurrentAction() == act && act != SelectedAction::NONE) {
                        sim.SetCurrentAction(SelectedAction::NONE);
                    } else {
                        sim.SetCurrentAction(act);
                    }
                    showTabMenu = false;
                    sim.SetPaused(isPaused);
                    ignoreMouseUntilRelease = true;
                    sim.CancelActiveInput();
                };

                if (!ignoreMouseUntilRelease && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    if (CheckCollisionPointRec(mousePos, btnCursor))       selectAndClose(SelectedAction::NONE);
                    else if (CheckCollisionPointRec(mousePos, btnGrass))   selectAndClose(SelectedAction::TERRAIN_GRASS);
                    else if (CheckCollisionPointRec(mousePos, btnWater))   selectAndClose(SelectedAction::TERRAIN_WATER);
                    else if (CheckCollisionPointRec(mousePos, btnForest))  selectAndClose(SelectedAction::TERRAIN_FOREST);
                    else if (CheckCollisionPointRec(mousePos, btnMan))     selectAndClose(SelectedAction::SPAWN_MAN);
                    else if (CheckCollisionPointRec(mousePos, btnWoman))   selectAndClose(SelectedAction::SPAWN_WOMAN);
                    else if (CheckCollisionPointRec(mousePos, btnAnimal))  selectAndClose(SelectedAction::SPAWN_ANIMAL);
                    else if (CheckCollisionPointRec(mousePos, btnFire))    selectAndClose(SelectedAction::IGNITE_FIRE);
                    else if (CheckCollisionPointRec(mousePos, btnGravity)) selectAndClose(SelectedAction::GRAVITY_ANOMALY);
                    else if (CheckCollisionPointRec(mousePos, btnWeather)) {
                        sim.CycleWeather();
                        ignoreMouseUntilRelease = true;
                    } else if (CheckCollisionPointRec(mousePos, btnRegen)) {
                        sim.RegenerateWorld(0);
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
                }
            } 
            // WENN DAS SPIEL LÄUFT (weder Pause noch TAB-Menü):
            else if (!isPaused) {
                // Klick auf den "[X Abwählen]"-Button unten links neben der Werkzeuganzeige prüfen
                if (!ignoreMouseUntilRelease && sim.GetCurrentAction() != SelectedAction::NONE &&
                    IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    Rectangle btnDeselect = { 14.0f + 250.0f, (float)SCREEN_HEIGHT - 36.0f, 105.0f, 26.0f };
                    if (CheckCollisionPointRec(mousePos, btnDeselect)) {
                        sim.SetCurrentAction(SelectedAction::NONE);
                        ignoreMouseUntilRelease = true;
                        sim.CancelActiveInput();
                    }
                }

                // Deine Simulation rechnet die Welt weiter
                sim.Update(dt);
            }
        }

        // ---------------------------------------------------------------------
        // B) ZEICHNEN (DRAW)
        // ---------------------------------------------------------------------
        renderMgr.BeginVirtual();

            if (currentState == AppState::MENU) {
                // --- 1. STARTBILDSCHIRM ZEICHNEN ---
                DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Color{ 20, 23, 28, 255 });
                FontManager::DrawTextCenteredShadow("PRIMITIVE SANDBOX", SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT / 4.0f - 15.0f, 52.0f, GOLD);
                FontManager::DrawTextCenteredShadow("Stone Age Evolution & Survival - Autonome Lebenssimulation",
                                                    SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT / 4.0f + 55.0f, 22.0f, LIGHTGRAY);

                // Start-Button
                bool hoverStart = CheckCollisionPointRec(mousePos, startButton);
                DrawRectangleRec(startButton, hoverStart ? Color{ 50, 160, 70, 255 } : Color{ 35, 120, 50, 255 });
                DrawRectangleLinesEx(startButton, 2.0f, RAYWHITE);
                FontManager::DrawTextCenteredShadow("SPIEL STARTEN", startButton.x + startButton.width * 0.5f,
                                                    startButton.y + (startButton.height - 24.0f) * 0.5f, 24.0f, RAYWHITE);

                // Beenden-Button
                bool hoverExit = CheckCollisionPointRec(mousePos, exitButton);
                DrawRectangleRec(exitButton, hoverExit ? Color{ 180, 50, 50, 255 } : Color{ 130, 35, 35, 255 });
                DrawRectangleLinesEx(exitButton, 2.0f, RAYWHITE);
                FontManager::DrawTextCenteredShadow("BEENDEN", exitButton.x + exitButton.width * 0.5f,
                                                    exitButton.y + (exitButton.height - 24.0f) * 0.5f, 24.0f, RAYWHITE);
            } 
            else if (currentState == AppState::PLAYING) {
                // --- 2. DEINE SPIELWELT ZEICHNEN ---
                sim.DrawWorld();
                sim.DrawUI();

                // Statusanzeige für das aktuell gewählte Werkzeug (unten links)
                SelectedAction act = sim.GetCurrentAction();
                if (act != SelectedAction::NONE) {
                    const char* actionName = "";
                    if (act == SelectedAction::TERRAIN_GRASS)   actionName = "Gras malen";
                    if (act == SelectedAction::TERRAIN_WATER)   actionName = "Wasser malen";
                    if (act == SelectedAction::TERRAIN_FOREST)  actionName = "Wald pflanzen";
                    if (act == SelectedAction::SPAWN_MAN || act == SelectedAction::SPAWN_HUNTER) actionName = "Mann erschaffen (♂)";
                    if (act == SelectedAction::SPAWN_WOMAN)     actionName = "Frau erschaffen (♀)";
                    if (act == SelectedAction::SPAWN_ANIMAL)    actionName = "Wildtier spawnen";
                    if (act == SelectedAction::IGNITE_FIRE)     actionName = "Feuer legen";
                    if (act == SelectedAction::GRAVITY_ANOMALY) actionName = "Antigravitation";

                    std::string actionLabel = TextFormat("Werkzeug: %s", actionName);
                    Rectangle boxR = { 14.0f, (float)SCREEN_HEIGHT - 36.0f, 240.0f, 26.0f };
                    DrawRectangleRounded(boxR, 0.35f, 4, Fade(Color{ 18, 22, 28, 255 }, 0.90f));
                    DrawRectangleRoundedLinesEx(boxR, 0.35f, 4, 1.2f, GOLD);
                    FontManager::DrawTextShadow(actionLabel.c_str(), 24.0f, (float)(SCREEN_HEIGHT - 31), 14.0f, GOLD);

                    // Direkt anklickbarer "[X Abwählen]"-Button
                    Rectangle btnDeselect = { 14.0f + 250.0f, (float)SCREEN_HEIGHT - 36.0f, 105.0f, 26.0f };
                    bool hovDesel = CheckCollisionPointRec(mousePos, btnDeselect);
                    DrawRectangleRounded(btnDeselect, 0.35f, 4, hovDesel ? Color{ 175, 50, 50, 245 } : Color{ 120, 35, 35, 225 });
                    DrawRectangleRoundedLinesEx(btnDeselect, 0.35f, 4, 1.2f, RAYWHITE);
                    FontManager::DrawTextCenteredShadow("[X] Abwählen", btnDeselect.x + btnDeselect.width * 0.5f,
                                                        btnDeselect.y + 5.0f, 13.0f, RAYWHITE);
                } else {
                    // Dezenter Steuerungs-Hinweis (unaufdringlich, halbtransparent)
                    const char* hint = "[TAB] Werkzeuge  |  [U] Wetter  |  [K] Skilltree  |  [T] Territorien";
                    float tw = FontManager::MeasureText(hint, 12.0f);
                    Rectangle hintR = { 14.0f, (float)SCREEN_HEIGHT - 28.0f, tw + 20.0f, 20.0f };
                    DrawRectangleRounded(hintR, 0.4f, 4, Fade(BLACK, 0.55f));
                    FontManager::DrawTextShadow(hint, 24.0f, (float)(SCREEN_HEIGHT - 24), 12.0f, Fade(LIGHTGRAY, 0.85f));
                }

                // --- 3. PAUSE-OVERLAY (wenn ESC aktiv ist) ---
                if (isPaused && !showTabMenu) {
                    DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.65f));
                    FontManager::DrawTextCenteredShadow("PAUSE", SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT * 0.5f - 55.0f, 58.0f, RAYWHITE);
                    FontManager::DrawTextCenteredShadow("Drücke ESC zum Fortsetzen", SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT * 0.5f + 18.0f, 22.0f, LIGHTGRAY);
                }

                // --- 4. TAB-AKTIONSMENÜ OVERLAY (wenn TAB aktiv ist) ---
                if (showTabMenu) {
                    DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.60f));
                    DrawRectangleRec(tabMenuBox, Color{ 26, 28, 34, 252 });
                    DrawRectangleLinesEx(tabMenuBox, 2.0f, GOLD);

                    FontManager::DrawTextCenteredShadow("GÖTTLICHE KRÄFTE & SANDBOX",
                                                        tabMenuBox.x + tabMenuBox.width * 0.5f, tabMenuBox.y + 16.0f, 26.0f, GOLD);

                    // Helferfunktion für Menü-Buttons
                    auto drawBtn = [&](Rectangle r, const char* txt, Color col, bool isCurrent) {
                        bool hov = CheckCollisionPointRec(mousePos, r);
                        Color fill = hov ? Fade(col, 0.95f) : Fade(col, 0.72f);
                        DrawRectangleRec(r, fill);
                        DrawRectangleLinesEx(r, isCurrent ? 2.5f : 1.0f, isCurrent ? GOLD : RAYWHITE);
                        FontManager::DrawTextCenteredShadow(txt, r.x + r.width * 0.5f, r.y + (r.height - 18.0f) * 0.5f, 15.0f, RAYWHITE);
                    };

                    FontManager::DrawTextShadow("--- Terrain & Natur ---", tabMenuBox.x + 30.0f, tabMenuBox.y + 54.0f, 15.0f, GRAY);
                    drawBtn(btnGrass,  "1. Gras malen",        Color{ 40, 110, 50, 255 },  act == SelectedAction::TERRAIN_GRASS);
                    drawBtn(btnWater,  "2. Wasser malen",      Color{ 35, 80, 150, 255 },  act == SelectedAction::TERRAIN_WATER);
                    drawBtn(btnForest, "3. Wald pflanzen",     Color{ 20, 80, 35, 255 },   act == SelectedAction::TERRAIN_FOREST);
                    drawBtn(btnMan,    "Mann erschaffen (♂)",  Color{ 55, 90, 160, 255 },  act == SelectedAction::SPAWN_MAN || act == SelectedAction::SPAWN_HUNTER);
                    drawBtn(btnWoman,  "Frau erschaffen (♀)",  Color{ 150, 55, 120, 255 }, act == SelectedAction::SPAWN_WOMAN);
                    drawBtn(btnAnimal, "Wildtier erschaffen",  Color{ 150, 90, 40, 255 },  act == SelectedAction::SPAWN_ANIMAL);

                    FontManager::DrawTextShadow("--- Göttliche Eingriffe ---", tabMenuBox.x + 275.0f, tabMenuBox.y + 54.0f, 15.0f, GOLD);
                    drawBtn(btnCursor,  "Cursor (Beobachten)",       Color{ 45, 95, 135, 255 },  act == SelectedAction::NONE);
                    drawBtn(btnFire,    "Feuer legen (Lauffeuer)",   Color{ 195, 65, 25, 255 },  act == SelectedAction::IGNITE_FIRE);
                    drawBtn(btnGravity, "Antigravitation (Zero-G)",  Color{ 95, 70, 185, 255 },  act == SelectedAction::GRAVITY_ANOMALY);
                    drawBtn(btnWeather, TextFormat("Wetter: %s", WeatherTypeName(sim.Env().GetWeather())), Color{ 60, 95, 130, 255 }, false);
                    drawBtn(btnRegen,   "Neue Welt [R]",             Color{ 160, 110, 40, 255 }, false);

                    // Schließen-Button
                    drawBtn({ tabMenuBox.x + 160, tabMenuBox.y + 325, 200, 38 }, "Schließen", Color{ 70, 70, 80, 255 }, false);

                    const char* hint = "[Rechtsklick] / [Q] im Spiel wählt Werkzeuge sofort ab";
                    FontManager::DrawTextCenteredShadow(hint, tabMenuBox.x + tabMenuBox.width * 0.5f, tabMenuBox.y + 382.0f, 14.0f, LIGHTGRAY);
                }
            }
        renderMgr.EndVirtual();

        // Skaliert auf Fenstergröße und zeichnet Letterbox-Balken
        renderMgr.Present();
    }

    // Aufräumen & Beenden
    AssetManager::Get().UnloadAll();
    FontManager::Shutdown();
    return 0;
}