#ifndef CAVEMAN_H
#define CAVEMAN_H

#include "common.h"
#include "resource_node.h"

#include <string>
#include <vector>

class World;
class Tribe;
class EventLog;
class SpriteSheet;

// Rolle bestimmt Fähigkeiten und Sprite-Zeile.
enum class Role { Hunter = 0, Gatherer = 1, Elder = 2, Child = 3 }; //Rolle der cavemen: Später welcher adden 

// Zustände der State-Machine.
enum class CavemanState {
    Idle,
    Wandering,
    Hunting,
    GatheringFood,
    GatheringMaterials,   // Erweiterung: Holz/Stein für den Hüttenbau beschaffen
    Building,
    Sleeping,
    Eating,
    Dead
};

// Unterphase einer Tätigkeit (hingehen -> arbeiten -> zurückbringen).
enum class TaskPhase { MovingToTarget, Working, Returning };

enum class CarryType { None, Food, Wood, Stone };

const char* RoleName(Role role);
const char* StateName(CavemanState state);
Color RoleColor(Role role);

// Rollenspezifische Eigenschaften.
struct RoleTraits {
    float moveSpeed;          // Pixel pro Sekunde
    float workEfficiency;     // Sammeln/Jagen/Material
    float buildEfficiency;    // Baufortschritt
    float gatherYield;        // Ertragsfaktor
    float hungerRate;         // Faktor auf den Hungeranstieg
    float tirednessRate;      // Faktor auf den Müdigkeitsanstieg
    float attackDamage;       // Schaden pro Sekunde bei der Jagd
    bool canHunt;
    bool canBuild;
    bool canGatherMaterials;
    int spriteRow;            // Zeile im Sprite-Sheet
};
const RoleTraits& GetRoleTraits(Role role);

// Alles, was ein Steinzeitmensch über die Welt wissen/verändern darf.
struct WorldContext {
    World& world;
    Tribe& tribe;
    ResourceManager& resources;
    EventLog& log;
    int population;   // lebende Stammesmitglieder
};

class Caveman {
public:
    Caveman(int id, std::string name, Role role, Vector2 position);

    void Update(WorldContext& ctx, float dt);
    void Draw(const SpriteSheet& sheet, bool highlighted) const;
    void DrawDebugPath() const;
    void TakeDamage(float amount, const char* cause, WorldContext& ctx);

    // --- Abfragen -------------------------------------------------------------
    int Id() const                 { return id_; }
    const std::string& Name() const { return name_; }
    Role GetRole() const           { return role_; }
    CavemanState State() const     { return state_; }
    TaskPhase Phase() const        { return phase_; }
    bool IsDead() const            { return state_ == CavemanState::Dead; }
    Vector2 Position() const       { return position_; }
    Vector2 TargetPosition() const { return targetPosition_; }
    float Hp() const               { return hp_; }
    float Ap() const               { return ap_; }
    float Hunger() const           { return hunger_; }
    float Tiredness() const        { return tiredness_; }
    CarryType Carrying() const     { return carryType_; }
    int CarryAmount() const        { return carryAmount_; }
    std::string ActivityText() const;

private:
    // Ergebnis der "Denk"-Logik
    struct Intent {
        CavemanState state;
        int priority;
        ResourceType resource;
    };

    enum class MoveResult { Moving, Arrived, Failed };

    // --- Bedürfnisse & Entscheidungen -----------------------------------------
    void UpdateNeeds(WorldContext& ctx, float dt);
    Intent Think(const WorldContext& ctx) const;
    void ApplyIntent(const Intent& intent, WorldContext& ctx);
    bool IsInterruptible() const;
    bool IsBlocked(CavemanState state) const;

    // --- Zustandswechsel --------------------------------------------------------
    bool EnterState(CavemanState state, ResourceType resource, WorldContext& ctx);
    void ExitState(WorldContext& ctx);
    void Die(WorldContext& ctx);
    void FinishTask() { taskDone_ = true; }

    // --- Zustands-Handler -------------------------------------------------------
    void UpdateIdle(float dt);
    void UpdateWandering(WorldContext& ctx, float dt);
    void UpdateHunting(WorldContext& ctx, float dt);
    void UpdateHarvesting(WorldContext& ctx, float dt);   // GatheringFood & GatheringMaterials
    void UpdateBuilding(WorldContext& ctx, float dt);
    void UpdateSleeping(WorldContext& ctx, float dt);
    void UpdateEating(WorldContext& ctx, float dt);
    void UpdateReturning(WorldContext& ctx, float dt);

    // --- Bewegung & Hilfen --------------------------------------------------------
    bool SetDestination(WorldContext& ctx, Vector2 target);
    MoveResult MoveAlongPath(WorldContext& ctx, float dt);
    void StartReturning(WorldContext& ctx, CarryType type, int amount);
    void DeliverCarry(WorldContext& ctx);
    void ReleaseNode(WorldContext& ctx);
    float WorkFactor(float efficiency) const;
    void SpendAp(float amount);
    Vector2 PersonalOffset(float radius) const;

    // --- Identität ------------------------------------------------------------
    int id_;
    std::string name_;
    Role role_;

    // --- Position ---------------------------------------------------------------
    Vector2 position_;
    Vector2 targetPosition_;
    std::vector<Vector2> path_;
    size_t pathIndex_ = 0;

    // --- Werte (0..100) -----------------------------------------------------------
    float hp_ = 100.0f;
    float ap_ = 100.0f;
    float hunger_ = 0.0f;      // 0 = satt, 100 = verhungernd
    float tiredness_ = 0.0f;   // 0 = ausgeruht, 100 = erschöpft

    // --- State-Machine ----------------------------------------------------------
    CavemanState state_ = CavemanState::Idle;
    TaskPhase phase_ = TaskPhase::MovingToTarget;
    int currentPriority_ = 0;
    bool taskDone_ = true;
    float thinkTimer_ = 0.0f;
    float stateTimer_ = 0.0f;
    float workTimer_ = 0.0f;
    float repathTimer_ = 0.0f;
    CavemanState blockedState_ = CavemanState::Idle;
    float blockedTimer_ = 0.0f;
    const char* lastDamageCause_ = "Hunger";

    // Ziele
    ResourceType taskResource_ = ResourceType::Bush;
    int targetNode_ = -1;
    unsigned int targetGeneration_ = 0;
    int targetHut_ = -1;
    bool sleepingInHut_ = false;

    // Tragen
    CarryType carryType_ = CarryType::None;
    int carryAmount_ = 0;

    // --- Animation --------------------------------------------------------------
    float animTime_ = 0.0f;
    bool facingLeft_ = false;
    bool isMoving_ = false;
    float damageFlash_ = 0.0f;
};

#endif // CAVEMAN_H
