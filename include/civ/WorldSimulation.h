#pragma once

#include "core/common.h"
#include "CivilizationTech.h"
#include "Settlement.h"
#include "Creature.h"

#include <vector>
#include <memory>
#include <string>

// Forward Declarations zur Vermeidung zirkulärer Abhängigkeiten
class World;
class EventLog;
class SpriteSheet;

// =============================================================================
// SIMULATIONS-KOORDINATOR (WORLD SIMULATION)
// Koordiniert das Zusammenspiel aus Creatures, Siedlung und Zivilisations-Epochen.
// Nutzt ein sauberes Ticking-System für rechenintensive Dorf-Entscheidungen.
// =============================================================================
class WorldSimulation {
public:
    WorldSimulation(Vector2 settlementCenter, int initialCreatureCount);

    // --- Haupt-Update (wird im Spiel-Loop aufgerufen) -------------------------
    // dt: Frame-Delta-Zeit
    void Update(float dt, const World& world, EventLog& log);

    // --- Zeichnen ------------------------------------------------------------
    void DrawWorld(const SpriteSheet* spriteSheet, int selectedCreatureId) const;
    void DrawUI(int screenW, int screenH) const;

    // --- Zugriff auf Teilsysteme ---------------------------------------------
    CivilizationTech& Tech()                   { return tech_; }
    const CivilizationTech& Tech() const       { return tech_; }

    Settlement& GetSettlement()                { return settlement_; }
    const Settlement& GetSettlement() const    { return settlement_; }

    std::vector<Creature>& Creatures()         { return creatures_; }
    const std::vector<Creature>& Creatures() const { return creatures_; }

    int LivingCreaturesCount() const;
    void SpawnCreature(Vector2 worldPos, const std::string& customName = "");

    // Inspektion
    const Creature* GetCreatureById(int id) const;

private:
    // --- Sub-Routinen --------------------------------------------------------
    // Feste Ticks für komplexe Entscheidungen (Task-Generierung, Epochen-Prüfung)
    void RunSettlementDecisionTick(const World& world, EventLog& log);

    CivilizationTech tech_;
    Settlement settlement_;
    std::vector<Creature> creatures_;

    // Performance-Optimierung: Fester Takt für Dorf-Entscheidungen
    // Verhindert, dass das Job Board jeden Frame 60-mal pro Sekunde berechnet wird!
    float decisionTimer_ = 0.0f;
    static constexpr float DECISION_INTERVAL = 1.0f; // Alle 1.0 Sekunden ein Dorf-Entscheidungs-Tick

    int nextCreatureId_ = 1;
};

