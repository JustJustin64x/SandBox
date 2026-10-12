#pragma once

// Gemeinsame Includes und globale Konstanten für das gesamte Projekt.
#include "raylib.h"
#include "raymath.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

// Feste logische (virtuelle) Auflösung des Spiels
constexpr int SCREEN_WIDTH  = 1920;
constexpr int SCREEN_HEIGHT = 1080;
constexpr int VIRTUAL_SCREEN_WIDTH  = SCREEN_WIDTH;
constexpr int VIRTUAL_SCREEN_HEIGHT = SCREEN_HEIGHT;
constexpr int TARGET_FPS    = 60;

constexpr int DEFAULT_WORLD_WIDTH  = 160;   // in Kacheln
constexpr int DEFAULT_WORLD_HEIGHT = 100;   // in Kacheln
constexpr int TILE_SIZE            = 16;    // Pixel pro Kachel

