#ifndef COMMON_H
#define COMMON_H

// Gemeinsame Includes und globale Konstanten für das gesamte Projekt.
#include "raylib.h"
#include "raymath.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

constexpr int SCREEN_WIDTH  = 1920;
constexpr int SCREEN_HEIGHT = 1080;
constexpr int TARGET_FPS    = 60;

constexpr int DEFAULT_WORLD_WIDTH  = 160;   // in Kacheln
constexpr int DEFAULT_WORLD_HEIGHT = 100;   // in Kacheln
constexpr int TILE_SIZE            = 16;    // Pixel pro Kachel

#endif // COMMON_H
