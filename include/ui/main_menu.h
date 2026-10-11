#pragma once
#include "raylib.h"

// Welche Aktion hat der Spieler im Menü ausgelöst?
enum class MenuAction {
    NONE,           // Noch nichts angeklickt (im Menü bleiben)
    START_GAME,     // Spiel starten gewählt
    EXIT_GAME       // Spiel beenden gewählt
};

class MainMenu {
public:
    MainMenu();

    // Verarbeitet Mauseingaben & Klicks
    MenuAction Update();

    // Zeichnet Titel, Buttons und Effekte
    void Draw() const;

private:
    Rectangle startButton;
    Rectangle exitButton;
};