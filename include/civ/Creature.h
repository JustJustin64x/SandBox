#pragma once

#include "core/common.h"
#include "CivilizationTech.h"

#include <string>
#include <memory>

// Forward Declarations zur Vermeidung zirkulärer Abhängigkeiten
class Settlement;
class World;
class EventLog;
class SpriteSheet;
struct Task;

// =============================================================================
// ZUSTÄNDE DER FINITE STATE MACHINE (FSM)
// =============================================================================
enum class CreatureState {
    IDLE,          // Keine dringenden Bedürfnisse, wartet auf Dorf-Aufgaben
    SEEKING_FOOD,  // Dringender Hunger! Geht zum Lager / sammelt Nahrung (Vorrang vor Arbeit)
    WORKING,       // Führt eine vom Dorf zugewiesene Aufgabe aus (Holz, Stein, Bau)
    RESTING,       // Erschöpft! Geht zur Hütte/Lagerplatz und regeneriert Energie
    DEAD           // Gesundheit auf 0 gesunken; inaktiv
};

// =============================================================================
// BALANCING-PARAMETER FÜR KREATUREN
// Diese Werte können hier direkt angepasst werden, um das Spiel auszubalancieren!
// =============================================================================
namespace CreatureConfig {
    constexpr float MAX_HEALTH             = 100.0f; // Maximale Gesundheit
    constexpr float MAX_HUNGER             = 100.0f; // Maximaler Hunger (100 = verhungert)
    constexpr float MAX_ENERGY             = 100.0f; // Maximale Energie/Ausdauer

    // Verbrauchs- und Regenerationsraten pro Sekunde
    constexpr float HUNGER_INCREASE_RATE   = 1.5f;   // Hunger steigt um 1.5 Punkte pro Sekunde
    constexpr float ENERGY_DRAIN_WORKING   = 2.2f;   // Energieverbrauch während der Arbeit
    constexpr float ENERGY_DRAIN_WALKING   = 0.8f;   // Energieverbrauch beim normalen Gehen
    constexpr float ENERGY_RECOVER_RATE    = 12.0f;  // Energiegewinn pro Sekunde beim Schlafen
    constexpr float STARVATION_DAMAGE_RATE = 5.0f;   // LP-Schaden pro Sekunde wenn Hunger == 100

    // Schwellenwerte für Zustandsübergänge (Überleben vor Arbeit)
    constexpr float HUNGER_URGENT_LEVEL    = 70.0f;  // Ab 70 % Hunger wird Arbeit sofort abgebrochen!
    constexpr float HUNGER_SATISFIED_LEVEL = 20.0f;  // Bis zu diesem Wert isst eine Creature
    constexpr float ENERGY_LOW_LEVEL       = 20.0f;  // Ab 20 % Energie sucht Creature einen Schlafplatz
    constexpr float ENERGY_RESTED_LEVEL    = 90.0f;  // Ab 90 % Energie wacht Creature wieder auf

    constexpr float FOOD_HUNGER_REDUCTION  = 40.0f;  // Eine Portion Nahrung senkt Hunger um 40 Punkte
    constexpr int   CARRY_CAPACITY         = 5;      // Maximale Tragekapazität für Rohstoffe
}

// =============================================================================
// KREATUR-KLASSE (CREATURE)
// Hinweis: Creatures kennen keine Epochen! Sie reagieren rein auf ihre
// eigenen Bedürfnisse und Aufgaben, die ihnen das Dorf zuweist.
// =============================================================================
class Creature {
public:
    Creature(int id, const std::string& name, Vector2 startPosition);

    // --- Update & FSM --------------------------------------------------------
    // Führt die Zustandsmaschine und Bewegung für den aktuellen Zeitschritt aus
    void Update(float dt, Settlement& settlement, const World& world, EventLog* log = nullptr);

    // --- Aufgabenverwaltung (Job System) -------------------------------------
    void AssignTask(std::shared_ptr<Task> task);
    void ReleaseTask(Settlement& settlement);
    bool HasTask() const { return currentTask_ != nullptr; }
    std::shared_ptr<Task> CurrentTask() const { return currentTask_; }

    // --- Status & Abfragen ---------------------------------------------------
    int Id() const                 { return id_; }
    const std::string& Name() const { return name_; }
    CreatureState State() const    { return state_; }
    Vector2 Position() const       { return position_; }
    Vector2 TargetPosition() const { return targetPosition_; }

    float Health() const           { return health_; }
    float Hunger() const           { return hunger_; }
    float Energy() const           { return energy_; }
    bool IsDead() const            { return state_ == CreatureState::DEAD; }

    CivResourceType CarriedResource() const { return carriedResource_; }
    int CarriedAmount() const            { return carriedAmount_; }

    // Gibt einen verständlichen deutschen Text für das Inspektionspanel zurück
    std::string ActivityDescription() const;
    static const char* StateToString(CreatureState state);

    // --- Schaden & Heilung ---------------------------------------------------
    void TakeDamage(float amount, const char* reason = nullptr, EventLog* log = nullptr);
    void Heal(float amount);

    // --- Zeichnen ------------------------------------------------------------
    void Draw(const SpriteSheet* spriteSheet, bool isSelected) const;

private:
    // Interne FSM-Entscheidungslogik
    void EvaluateNeedsAndState(Settlement& settlement);
    void ProcessStateBehavior(float dt, Settlement& settlement, const World& world, EventLog* log);
    void MoveTowards(Vector2 target, float speed, float dt);

    int id_ = 0;
    std::string name_;
    CreatureState state_ = CreatureState::IDLE;

    // Grundbedürfnisse
    float health_ = CreatureConfig::MAX_HEALTH;
    float hunger_ = 10.0f; // Startet mit leichtem Hunger
    float energy_ = CreatureConfig::MAX_ENERGY;

    // Positionierung & Bewegung
    Vector2 position_ = { 0.0f, 0.0f };
    Vector2 targetPosition_ = { 0.0f, 0.0f };
    float moveSpeed_ = 48.0f; // Pixel pro Sekunde

    // Aktive Aufgabe & Inventar
    std::shared_ptr<Task> currentTask_ = nullptr;
    CivResourceType carriedResource_ = CivResourceType::FOOD;
    int carriedAmount_ = 0;

    // Timer für Leerlauf-Wanderung
    float idleWanderTimer_ = 0.0f;
};
