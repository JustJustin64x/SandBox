#include "common.h"
#include "simulation.h"

enum class AppState {
        MENU,
        PLAYING,
    };

int main() {

    
    // Raylib-Fenster initialisieren
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Stone Age 2D Sandbox - Primitive Survival & Evolution");
    SetTargetFPS(TARGET_FPS);

    // Hauptsimulation erzeugen
    Simulation sim(
        DEFAULT_WORLD_WIDTH,
        DEFAULT_WORLD_HEIGHT,
        SCREEN_WIDTH,
        SCREEN_HEIGHT
    );
    //Start im Menü
    AppState currentState = AppState::MENU;

    //Button-Abmessungen + Positionen für das Menü
    float btnWidth = 260.0f;
    float btnHeight = 60.0f;
    float centerX = (SCREEN_WIDTH - btnWidth) / 2.0f;

    Rectangle startButton = { centerX, SCREEN_HEIGHT / 2.0f - 20.0f, btnWidth, btnHeight };
    Rectangle exitButton  = { centerX, SCREEN_HEIGHT / 2.0f + 60.0f, btnWidth, btnHeight };
    
 // Spiel-Loop
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        Vector2 mousePos = GetMousePosition();

        // ==========================================
        // 1. LOGIK & EINGABEN (UPDATE)
        // ==========================================
        if (currentState == AppState::MENU) {
            // Prüfen, ob der Spieler links klickt
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                // Klick auf Start-Button -> Wechsel zur Simulation
                if (CheckCollisionPointRec(mousePos, startButton)) {
                    currentState = AppState::PLAYING;
                }
                // Klick auf Beenden-Button -> Loop beenden (Spiel schließt)
                if (CheckCollisionPointRec(mousePos, exitButton)) {
                    break;
                }
            }
        } 
        else if (currentState == AppState::PLAYING) {
            // Nur wenn wir spielen, wird die Simulation weitergerechnet!
            sim.Update(dt);

            // Optional: Mit Taste 'M' oder 'ESC' (falls nicht WindowShouldClose) zurück ins Menü
            if (IsKeyPressed(KEY_M)) {
                currentState = AppState::MENU;
            }
        }

        // ==========================================
        // 2. ZEICHNEN (DRAW)
        // ==========================================
        BeginDrawing();
            ClearBackground(BLACK);

            if (currentState == AppState::MENU) {
                // --- MENÜ ZEICHNEN ---
                
                // Titel & Untertitel
                DrawText("PRIMITIVE SANDBOX", SCREEN_WIDTH / 2 - 240, SCREEN_HEIGHT / 4, 42, GOLD);
                DrawText("Stone Age Evolution & Survival", SCREEN_WIDTH / 2 - 170, SCREEN_HEIGHT / 4 + 55, 20, LIGHTGRAY);

                // Start-Button (wird heller, wenn die Maus darüber schwebt)
                bool hoverStart = CheckCollisionPointRec(mousePos, startButton);
                DrawRectangleRec(startButton, hoverStart ? DARKGREEN : GREEN);
                DrawRectangleLinesEx(startButton, 2.0f, RAYWHITE);
                DrawText("SPIEL STARTEN", (int)startButton.x + 35, (int)startButton.y + 20, 20, RAYWHITE);

                // Beenden-Button
                bool hoverExit = CheckCollisionPointRec(mousePos, exitButton);
                DrawRectangleRec(exitButton, hoverExit ? MAROON : RED);
                DrawRectangleLinesEx(exitButton, 2.0f, RAYWHITE);
                DrawText("BEENDEN", (int)exitButton.x + 75, (int)exitButton.y + 20, 20, RAYWHITE);
            } 
            else if (currentState == AppState::PLAYING) {
                // --- DEINE BESTEHENDE SIMULATION ZEICHNEN ---
                sim.DrawWorld();
                sim.DrawUI();

                // Kleiner Hinweis oben links
                DrawText("[M] Hauptmenue", 10, 10, 16, RAYWHITE);
            }
        EndDrawing();
    }

    CloseWindow();
    return 0;
}

