#include "ui/main_menu.h"
#include "core/common.h"
#include "font_manager.h"

MainMenu::MainMenu() {
    // Buttons mittig im Fenster (bei 1920x1080 Auflösung) platzieren
    // Format: { X, Y, Breite, Hoehe }
    float centerX = (float)SCREEN_WIDTH / 2.0f;
    startButton = { centerX - 130.0f, 320.0f, 260.0f, 60.0f };
    exitButton  = { centerX - 130.0f, 410.0f, 260.0f, 60.0f };
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
    ClearBackground(Color{ 24, 26, 30, 255 });

    float screenW = (float)SCREEN_WIDTH;

    // 2. Spieltitel & Untertitel mit sauberen HD-Schriften
    FontManager::DrawTextCenteredShadow("PRIMITIVE SANDBOX", screenW * 0.5f, 150.0f, 54.0f, GOLD);
    FontManager::DrawTextCenteredShadow("Stone Age Evolution & Survival - Autonome Lebenssimulation",
                                        screenW * 0.5f, 225.0f, 22.0f, LIGHTGRAY);

    // 3. Start-Button (Farbe wechselt bei Mausberührung / Hover)
    bool hoverStart = CheckCollisionPointRec(mousePos, startButton);
    DrawRectangleRec(startButton, hoverStart ? Color{ 60, 160, 80, 255 } : Color{ 40, 120, 55, 255 });
    DrawRectangleLinesEx(startButton, 2.0f, RAYWHITE);
    FontManager::DrawTextCenteredShadow("SPIEL STARTEN", startButton.x + startButton.width * 0.5f,
                                        startButton.y + (startButton.height - 24.0f) * 0.5f, 24.0f, RAYWHITE);

    // 4. Beenden-Button
    bool hoverExit = CheckCollisionPointRec(mousePos, exitButton);
    DrawRectangleRec(exitButton, hoverExit ? Color{ 180, 50, 50, 255 } : Color{ 130, 35, 35, 255 });
    DrawRectangleLinesEx(exitButton, 2.0f, RAYWHITE);
    FontManager::DrawTextCenteredShadow("BEENDEN", exitButton.x + exitButton.width * 0.5f,
                                        exitButton.y + (exitButton.height - 24.0f) * 0.5f, 24.0f, RAYWHITE);
}