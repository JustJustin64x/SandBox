#ifndef SIMULATION_H
#define SIMULATION_H

// --- Core ---
#include "core/common.h"
#include "core/camera.h"
#include "core/event_log.h"

// --- World & Environment ---
#include "world/world.h"
#include "world/resource_node.h"
#include "world/environmental_system.h"

// --- Entities & Tribe ---
#include "entities/caveman.h"
#include "entities/tribe.h"

// --- Graphics & UI ---
#include "gfx/sprite_sheet.h"
#include "skill_tree_ui.h"

#include <vector>
#include <memory>

// Aktionen/Werkzeuge, die der Spieler auswählen kann (auch via TAB-Menü)
enum class SelectedAction {
    NONE = 0,           // Normaler Cursor (Beobachten & Figuren anklicken)
    TERRAIN_GRASS,
    TERRAIN_WATER,
    TERRAIN_FOREST,
    SPAWN_HUNTER,
    SPAWN_ANIMAL,
    IGNITE_FIRE,        // Feuer / Lauffeuer legen
    GRAVITY_ANOMALY     // Antigravitation / Schwerelosigkeit
};

class Simulation {
public:
    Simulation(int worldWidth, int worldHeight, int screenWidth, int screenHeight);
    ~Simulation() = default;

    void Update(float dt);
    void DrawWorld() const;
    void DrawUI() const;

    bool IsPaused() const { return isPaused_; }
    void SetPaused(bool paused) { isPaused_ = paused; }

    bool IsSkillTreeOpen() const { return skillTreeUI_.IsOpen(); }
    void SetSkillTreeOpen(bool open) { skillTreeUI_.SetOpen(open); }
    void ToggleSkillTree() { skillTreeUI_.Toggle(); }

    SelectedAction GetCurrentAction() const { return currentAction_; }
    void SetCurrentAction(SelectedAction action);
    void CycleWeather();
    const EnvironmentalSystem& Env() const { return env_; }

    // Bricht aktives Zeichnen ab (z.B. wenn ein Menü geöffnet oder geschlossen wird)
    void CancelActiveInput() { isPainting_ = false; }

private:
    void HandleInput();
    void SpawnCaveman(Role role, Vector2 worldPos);
    void SpawnChildFromParents(Caveman& parentA, Caveman& parentB, Vector2 spawnPos);
    void SpawnAnimal(Vector2 worldPos);
    void UpdateDemographicsAndFactions(float dt);
    void DrawRelationshipLines(const Caveman& selected) const;
    int GetAliveCount() const;

    World world_;
    EnvironmentalSystem env_;
    CameraController camera_;
    Tribe tribe_;
    ResourceManager resources_;
    std::vector<Caveman> cavemen_;
    SpriteSheet spriteSheet_;
    EventLog eventLog_;
    SkillTreeUI skillTreeUI_;

    bool isPaused_ = false;
    bool showTerritoryOverlay_ = false;
    float timeSpeed_ = 1.0f;
    float totalSimTime_ = 0.0f;
    float demographicTimer_ = 0.0f;
    float schismTimer_ = 0.0f;
    int nextCavemanId_ = 1;

    // Aktive Aktion & Kachel-Auswahl (Standard: Normaler Cursor ohne Werkzeug)
    SelectedAction currentAction_ = SelectedAction::NONE;
    TileType selectedTile_ = TILE_GRASS;
    bool isPainting_ = false;

    // Selektion / Inspektor
    int selectedCavemanId_ = -1;
};

#endif // SIMULATION_H

