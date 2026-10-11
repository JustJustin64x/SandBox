#include "ui/main_menu.h"
#include "common.h"

MainMenu::MainMenu() {
    // Buttons mittig im Fenster (bei 1920x1080 Auflösung) platzieren
    // Format: { X, Y, Breite, Hoehe }
    float centerX = (float)SCREEN_WIDTH / 2.0f;
    startButton = { centerX - 120.0f, 320.0f, 240.0f, 55.0f };
    exitButton  = { centerX - 120.0f, 400.0f, 240.0f, 55.0f };
}

MenuAction MainMenu::Update() {
    Vector2 mousePos = GetMousePosition();

    // Prüfen, ob die linke Maustaste gedrückt wurde
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (CheckCollisionPointRec(mousePos, startButton)) {
            return MenuAction::START_GAME;
        }
        if (CheckCollisionPointRec(mousePos, exitButton)) {
            return MenuAction::EXIT_GAME;
        }
    }

    return MenuAction::NONE;
}

void MainMenu::Draw() const {
    Vector2 mousePos = GetMousePosition();

    // 1. Hintergrund zeichnen (z. B. dunkles Steingrau passend zur Steinzeit)
    ClearBackground(Color{ 30, 32, 36, 255 });

    int screenW = SCREEN_WIDTH;

    // 2. Spieltitel & Untertitel
    int titleW = MeasureText("PRIMITIVE SANDBOX", 48);
    DrawText("PRIMITIVE SANDBOX", (screenW - titleW) / 2, 160, 48, RAYWHITE);
    int subW = MeasureText("A Stone Age Evolution Simulator", 20);
    DrawText("A Stone Age Evolution Simulator", (screenW - subW) / 2, 220, 20, LIGHTGRAY);

    // 3. Start-Button (Farbe wechselt bei Mausberührung / Hover)
    bool hoverStart = CheckCollisionPointRec(mousePos, startButton);
    DrawRectangleRec(startButton, hoverStart ? Color{ 80, 140, 80, 255 } : Color{ 50, 100, 50, 255 });
    DrawRectangleLinesEx(startButton, 2.0f, RAYWHITE);
    DrawText("SPIEL STARTEN", (int)startButton.x + 35, (int)startButton.y + 18, 20, RAYWHITE);

    // 4. Beenden-Button
    bool hoverExit = CheckCollisionPointRec(mousePos, exitButton);
    DrawRectangleRec(exitButton, hoverExit ? Color{ 160, 60, 60, 255 } : Color{ 110, 40, 40, 255 });
    DrawRectangleLinesEx(exitButton, 2.0f, RAYWHITE);
    DrawText("BEENDEN", (int)exitButton.x + 70, (int)exitButton.y + 18, 20, RAYWHITE);
}