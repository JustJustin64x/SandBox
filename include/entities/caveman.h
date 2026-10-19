#ifndef CAVEMAN_H
#define CAVEMAN_H

#include "common.h"
#include "resource_node.h"
#include "personality.h"

#include <string>
#include <vector>

class World;
class Tribe;
class EventLog;
class SpriteSheet;
class EnvironmentalSystem;

// Rolle bestimmt Fähigkeiten und Sprite-Zeile.
enum class Role { Hunter = 0, Gatherer = 1, Elder = 2, Child = 3 };

// Zustände der State-Machine.
enum class CavemanState {
    Idle,
    Wandering,
    Fleeing,              // Neu (Phase 1): Panische Flucht vor Feuer, Strom oder Explosionen
    Hunting,
    GatheringFood,
    GatheringMaterials,   // Holz/Stein beschaffen
    Building,             // Bauen oder Reparieren
    Crafting,             // Veredeln an der Werkbank
    WarmingUp,            // Aufwärmen am Feuer oder in einer Hütte
    Socializing,          // Gespräche, Klatsch, Flirten, soziale Bindung
    Expedition,           // Forschungsexpedition zu einem POI mit Ausrüstung
    Sleeping,
    Eating,
    Dead
};

// Unterphase einer Tätigkeit (hingehen -> arbeiten -> zurückbringen).
enum class TaskPhase { MovingToTarget, Working, Returning };

enum class CarryType { None, Food, Wood, Stone, Planks, Bricks, Tools, Discovery };

// Visuelle Floating-Emotes ("Show, Don't Tell" über den Köpfen der Figuren)
enum class SocialEmote {
    None = 0,
    Chat,         // Sprechblase (Gespräch / Klatsch)
    Heart,        // Pinkes Herz (Flirt / Lebenspartner / Nachwuchs)
    ShareFood,    // Grünes Nahrungs-Icon (Futter geteilt)
    Dispute,      // Rote Blitze / Konflikt (Streit / Rivale)
    Crown,        // Goldene Krone (Aufstieg zum Häuptling)
    Idea          // Goldener Stern / Glühbirne (Heureka-Entdeckung / Bauplan)
};

// Werkzeug-Stufen (Abbaugeschwindigkeit & freigeschaltete Rohstoffe)
enum class ToolTier {
    BareHands = 0,   // Bloße Hände (0.75x Tempo)
    FlintHandaxe,    // Faustkeil / Schaber (1.15x Tempo)
    HaftedAxe,       // Geschäftete Steinaxt / Hacke (1.40x Tempo)
    BronzeTool,      // Bronzewerkzeug (1.75x Tempo)
    IronTool         // Eisenwerkzeug (2.15x Tempo)
};

// Jagd- & Kampfwaffen
enum class WeaponTier {
    WoodClub = 0,    // Holzknüppel (Nahkampf)
    FlintSpear,      // Feuersteinspeer (erhöhter Schaden)
    CompositeBow,    // Bogen & Knochenpfeile (Fernkampf-Jagd!)
    MetalBlade       // Bronze-/Eisenschwert & Speer
};

// Tragehilfen (vervielfachen Traglast beim Sammeln)
enum class ContainerTier {
    None = 0,        // Bloße Arme (1x Traglast)
    WovenBasket,     // Geflochtener Korb (2x Traglast)
    Handcart         // Handkarren (3x Traglast)
};

// Kleidung & Rüstung (sichtbar am Körper + Kälte-Isolation)
enum class ClothingTier {
    None = 0,        // Unbekleidet / Lendenschurz (0% Kälteschutz)
    RawHide,         // Umgehängtes Rohfell (-25% Wärmeverlust)
    SewnFurs,        // Genähte Pelz-/Lederkleidung (-50% Wärmeverlust)
    WovenTunic,      // Gewebte Tunika in Clan-Farbe (-70% Wärmeverlust)
    Armor            // Leder-/Metallrüstung (-70% Wärmeverlust, -45% Kampfschaden)
};

const char* RoleName(Role role);
const char* StateName(CavemanState state);
const char* ToolTierName(ToolTier t);
const char* WeaponTierName(WeaponTier w);
const char* ContainerTierName(ContainerTier c);
const char* ClothingTierName(ClothingTier cl);
Color RoleColor(Role role);

// Rollenspezifische Eigenschaften.
struct RoleTraits {
    float moveSpeed;          // Pixel pro Sekunde
    float workEfficiency;     // Sammeln/Jagen/Material
    float buildEfficiency;    // Baufortschritt
    float craftEfficiency;    // Veredelungsgeschwindigkeit
    float gatherYield;        // Ertragsfaktor
    float hungerRate;         // Faktor auf den Hungeranstieg
    float tirednessRate;      // Faktor auf den Müdigkeitsanstieg
    float attackDamage;       // Schaden pro Sekunde bei der Jagd
    bool canHunt;
    bool canBuild;
    bool canCraft;
    bool canGatherMaterials;
    int spriteRow;            // Zeile im Sprite-Sheet
};
const RoleTraits& GetRoleTraits(Role role);

class Caveman;

// Alles, was ein Mensch über die Welt wissen/verändern darf.
struct WorldContext {
    World& world;
    Tribe& tribe;
    ResourceManager& resources;
    EventLog& log;
    int population;       // lebende Stammesmitglieder
    float timeOfDay;      // 0..1 (Tag/Nacht für Kälte & Beleuchtung)
    std::vector<Caveman>* allCavemen = nullptr;
    EnvironmentalSystem* env = nullptr;
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
    bool IsAlive() const           { return state_ != CavemanState::Dead; }
    Vector2 Position() const       { return position_; }
    Vector2 TargetPosition() const { return targetPosition_; }
    float Hp() const               { return hp_; }
    float Ap() const               { return ap_; }
    float Hunger() const           { return hunger_; }
    float Warmth() const           { return warmth_; }
    float Tiredness() const        { return tiredness_; }
    float Morale() const           { return morale_; }
    float SocialNeed() const       { return socialNeed_; }
    float AgeYears() const         { return ageYears_; }
    float MaxLifespan() const      { return maxLifespan_; }
    CarryType Carrying() const     { return carryType_; }
    int CarryAmount() const        { return carryAmount_; }

    // --- Werkzeuge, Waffen, Tragehilfen & Kleidung (Meilenstein A) ------------
    ToolTier GetToolTier() const       { return toolTier_; }
    WeaponTier GetWeaponTier() const   { return weaponTier_; }
    ContainerTier GetContainer() const { return containerTier_; }
    ClothingTier GetClothing() const   { return clothingTier_; }
    float ToolDurability() const       { return toolDurability_; }
    float KnowledgeBonus() const       { return knowledgeBonus_; }
    void AddKnowledgeBonus(float b)    { knowledgeBonus_ = std::min(0.45f, knowledgeBonus_ + b); }
    float GetColdInsulation() const;
    int GetCarryCapacityMultiplier() const;
    bool NeedsGearUpgrade(const Tribe& tribe) const;

    const Personality& GetPersonality() const { return personality_; }
    void SetPersonality(const Personality& p) { personality_ = p; }
    EpisodicMemory& Memory()                  { return memory_; }
    const EpisodicMemory& Memory() const      { return memory_; }

    void ResetSocialNeed()                    { socialNeed_ = 0.0f; }
    void Feed(float amount)                   { hunger_ = std::max(0.0f, hunger_ - amount); }
    void SetTargetSocialPartner(int targetId) { targetSocialPartner_ = targetId; }
    int TargetSocialPartner() const           { return targetSocialPartner_; }
    bool IsCarryingTorch() const              { return carryingTorch_; }
    bool IsOnExpedition() const               { return state_ == CavemanState::Expedition; }

    // --- Visuelles Feedback ("Show, Don't Tell") & Fraktionszugehörigkeit -----
    void TriggerEmote(SocialEmote emote, float duration = 3.2f);
    SocialEmote ActiveEmote() const { return activeEmote_; }
    void SyncSocialVisuals(int clanId, Color clanColor, int socialRank, Vector2 homeCampPos);
    int ClanId() const { return clanId_; }
    Color ClanColor() const { return clanColor_; }
    int RankIndex() const { return socialRank_; }
    Vector2 HomeCampPosition() const { return homeCampPos_; }

    const std::string& DecisionReason() const { return lastDecisionReason_; }
    float DecisionScore() const               { return lastDecisionScore_; }
    std::string ActivityText() const;
    std::string GetSocialSummary(const Tribe& tribe) const;

private:
    enum class MoveResult { Moving, Arrived, Failed };

    // --- Bedürfnisse & Entscheidungen -----------------------------------------
    void UpdateNeeds(WorldContext& ctx, float dt);
    void Think(WorldContext& ctx);
    bool IsInterruptible() const;
    bool IsBlocked(CavemanState state) const;

    // --- Zustandswechsel --------------------------------------------------------
    bool EnterState(CavemanState state, ResourceType resource, int targetBuildingId,
                    int targetNodeId, int targetCharacterId, int targetPoiId, WorldContext& ctx);
    void ExitState(WorldContext& ctx);
    void Die(WorldContext& ctx);
    void FinishTask() { taskDone_ = true; }

    // --- Zustands-Handler -------------------------------------------------------
    void UpdateIdle(float dt);
    void UpdateWandering(WorldContext& ctx, float dt);
    void UpdateFleeing(WorldContext& ctx, float dt);
    void UpdateHunting(WorldContext& ctx, float dt);
    void UpdateHarvesting(WorldContext& ctx, float dt);
    void UpdateBuilding(WorldContext& ctx, float dt);
    void UpdateCrafting(WorldContext& ctx, float dt);
    void UpdateWarmingUp(WorldContext& ctx, float dt);
    void UpdateSocializing(WorldContext& ctx, float dt);
    void UpdateExpedition(WorldContext& ctx, float dt);
    void UpdateSleeping(WorldContext& ctx, float dt);
    void UpdateEating(WorldContext& ctx, float dt);
    void UpdateReturning(WorldContext& ctx, float dt);

    // --- Bewegung & Hilfen --------------------------------------------------------
    bool SetDestination(WorldContext& ctx, Vector2 target);
    MoveResult MoveAlongPath(WorldContext& ctx, float dt);
    void StartReturning(WorldContext& ctx, CarryType type, int amount);
    void DeliverCarry(WorldContext& ctx);
    void ReleaseNode(WorldContext& ctx);
    void ReleaseBuilding(WorldContext& ctx);
    float WorkFactor(float efficiency) const;
    void SpendAp(float amount);
    void AutoUpgradeGearFromTribe(WorldContext& ctx, bool atBench);
    void DegradeToolDurability(float amount, WorldContext& ctx);
    Vector2 PersonalOffset(float radius) const;
    void DrawFloatingEmote() const;
    void DrawClothingAndGear() const;

    // --- Identität & Demografie -----------------------------------------------
    int id_;
    std::string name_;
    Role role_;
    float ageYears_ = 20.0f;
    float maxLifespan_ = 78.0f;

    // --- Position ---------------------------------------------------------------
    Vector2 position_;
    Vector2 targetPosition_;
    Vector2 homeCampPos_ = { 0.0f, 0.0f };
    std::vector<Vector2> path_;
    size_t pathIndex_ = 0;

    // --- Werte (0..100) -----------------------------------------------------------
    float hp_ = 100.0f;
    float ap_ = 100.0f;
    float hunger_ = 0.0f;       // 0 = satt, 100 = verhungernd
    float warmth_ = 100.0f;     // 100 = warm, 0 = erfrierend
    float tiredness_ = 0.0f;    // 0 = ausgeruht, 100 = erschöpft
    float morale_ = 75.0f;      // 0..100 Motivation / Arbeitseifer
    float socialNeed_ = 35.0f;  // 0..100 Bedürfnis nach Gesellschaft / Zuneigung

    // --- Ausrüstung, Kleidung & Wissen (Meilenstein A) ------------------------
    ToolTier toolTier_ = ToolTier::BareHands;
    WeaponTier weaponTier_ = WeaponTier::WoodClub;
    ContainerTier containerTier_ = ContainerTier::None;
    ClothingTier clothingTier_ = ClothingTier::None;
    float toolDurability_ = 0.0f;
    float knowledgeBonus_ = 0.0f;

    // --- Psychologie & Gedächtnis -----------------------------------------------
    Personality personality_;
    EpisodicMemory memory_;

    // --- Visuelles Feedback ("Show, Don't Tell") & Fraktion ---------------------
    SocialEmote activeEmote_ = SocialEmote::None;
    float emoteTimer_ = 0.0f;
    int clanId_ = 1;
    Color clanColor_ = WHITE;
    int socialRank_ = 3; // Worker

    // --- Utility AI Feedback ----------------------------------------------------
    std::string lastDecisionReason_ = "Erkundet die Gegend";
    float lastDecisionScore_ = 0.1f;

    // --- State-Machine ----------------------------------------------------------
    CavemanState state_ = CavemanState::Idle;
    TaskPhase phase_ = TaskPhase::MovingToTarget;
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
    int targetBuilding_ = -1;
    int targetSocialPartner_ = -1;
    int targetPoi_ = -1;
    bool carryingTorch_ = false;
    bool sleepingInShelter_ = false;

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
