#include "caveman.h"
#include "config.h"
#include "event_log.h"
#include "sprite_sheet.h"
#include "tribe.h"
#include "world.h"

#include <algorithm>
#include <utility>

namespace {

// Prioritäten der Absichten: Eine laufende Tätigkeit wird nur von einer
// Absicht mit HÖHERER Priorität unterbrochen.
constexpr int kPrioIdle      = 0;
constexpr int kPrioMaterials = 10;
constexpr int kPrioBuild     = 15;
constexpr int kPrioProvision = 20;
constexpr int kPrioHunger    = 40;
constexpr int kPrioSleep     = 50;

constexpr float kBlockDuration = 3.0f;   // so lange wird ein gescheiterter Zustand gemieden

const RoleTraits kRoleTraits[4] = {
    //  speed work  build yield hunger tired attack canHunt canBuild canMat row
    {  44.0f, 1.0f, 1.0f, 1.0f, 1.1f,  1.0f, 10.0f, true,   true,    true,  0 },   // Jäger
    {  40.0f, 1.0f, 1.0f, 1.3f, 1.0f,  1.0f,  3.0f, false,  true,    true,  1 },   // Sammlerin
    {  28.0f, 0.7f, 1.3f, 0.7f, 0.8f,  1.3f,  2.0f, false,  true,    true,  2 },   // Ältester
    {  36.0f, 0.5f, 0.0f, 0.4f, 0.8f,  1.1f,  1.0f, false,  false,   false, 3 },   // Kind
};

} // namespace

// ============================================================================
//  Namen, Farben, Eigenschaften
// ============================================================================
const char* RoleName(Role role) {
    switch (role) {
        case Role::Hunter:   return "Jäger";
        case Role::Gatherer: return "Sammlerin";
        case Role::Elder:    return "Ältester";
        case Role::Child:    return "Kind";
    }
    return "?";
}

const char* StateName(CavemanState state) {
    switch (state) {
        case CavemanState::Idle:               return "IDLE";
        case CavemanState::Wandering:          return "WANDERING";
        case CavemanState::Hunting:            return "HUNTING";
        case CavemanState::GatheringFood:      return "GATHERING_FOOD";
        case CavemanState::GatheringMaterials: return "GATHERING_MATERIALS";
        case CavemanState::Building:           return "BUILDING";
        case CavemanState::Sleeping:           return "SLEEPING";
        case CavemanState::Eating:             return "EATING";
        case CavemanState::Dead:               return "DEAD";
    }
    return "?";
}

Color RoleColor(Role role) {
    switch (role) {
        case Role::Hunter:   return { 230, 90, 70, 255 };
        case Role::Gatherer: return { 240, 170, 60, 255 };
        case Role::Elder:    return { 170, 140, 230, 255 };
        case Role::Child:    return { 110, 200, 230, 255 };
    }
    return WHITE;
}

const RoleTraits& GetRoleTraits(Role role) {
    return kRoleTraits[(int)role];
}

// ============================================================================
//  Konstruktion
// ============================================================================
Caveman::Caveman(int id, std::string name, Role role, Vector2 position)
    : id_(id), name_(std::move(name)), role_(role), position_(position), targetPosition_(position) {
    // Leicht unterschiedliche Startwerte, damit nicht alle gleichzeitig handeln
    hunger_     = (float)GetRandomValue(5, 35);
    tiredness_  = (float)GetRandomValue(0, 30);
    ap_         = (float)GetRandomValue(80, 100);
    facingLeft_ = GetRandomValue(0, 1) == 1;
    thinkTimer_ = (float)GetRandomValue(0, 50) / 100.0f;
    animTime_   = (float)GetRandomValue(0, 100) / 100.0f;
}

// ============================================================================
//  Haupt-Update
// ============================================================================
void Caveman::Update(WorldContext& ctx, float dt) {
    if (state_ == CavemanState::Dead) return;   // Tote werden nicht mehr aktualisiert

    animTime_ += dt;
    damageFlash_  = std::max(0.0f, damageFlash_ - dt);
    blockedTimer_ = std::max(0.0f, blockedTimer_ - dt);

    // 1) Bedürfnisse verändern sich über die Zeit
    UpdateNeeds(ctx, dt);
    if (hp_ <= 0.0f) { Die(ctx); return; }

    // 2) "Denken": regelmäßig oder sobald die aktuelle Aufgabe erledigt ist
    thinkTimer_ -= dt;
    if (taskDone_ || thinkTimer_ <= 0.0f) {
        thinkTimer_ = cfg::kThinkInterval;
        const Intent intent = Think(ctx);
        if (taskDone_ || (intent.priority > currentPriority_ && IsInterruptible())) ApplyIntent(intent, ctx);
    }

    // 3) Aktuellen Zustand ausführen
    isMoving_ = false;
    switch (state_) {
        case CavemanState::Idle:               UpdateIdle(dt); break;
        case CavemanState::Wandering:          UpdateWandering(ctx, dt); break;
        case CavemanState::Hunting:            UpdateHunting(ctx, dt); break;
        case CavemanState::GatheringFood:
        case CavemanState::GatheringMaterials: UpdateHarvesting(ctx, dt); break;
        case CavemanState::Building:           UpdateBuilding(ctx, dt); break;
        case CavemanState::Sleeping:           UpdateSleeping(ctx, dt); break;
        case CavemanState::Eating:             UpdateEating(ctx, dt); break;
        case CavemanState::Dead:               break;
    }

    if (hp_ <= 0.0f) Die(ctx);   // z. B. durch Gegenangriff eines Wildtiers
}

void Caveman::UpdateNeeds(WorldContext& ctx, float dt) {
    (void)ctx;
    const RoleTraits& t = GetRoleTraits(role_);
    const bool asleep = state_ == CavemanState::Sleeping && phase_ == TaskPhase::Working;
    const bool working = phase_ == TaskPhase::Working &&
                         (state_ == CavemanState::Hunting || state_ == CavemanState::GatheringFood ||
                          state_ == CavemanState::GatheringMaterials || state_ == CavemanState::Building);

    // Hunger steigt stetig (im Schlaf langsamer)
    const float hungerRate = cfg::kHungerPerSecond * t.hungerRate * (asleep ? cfg::kSleepHungerFactor : 1.0f);
    hunger_ = std::min(cfg::kMaxStat, hunger_ + hungerRate * dt);

    // Müdigkeit steigt im Wachzustand (bei Arbeit schneller)
    if (!asleep) {
        const float tiredRate = cfg::kTirednessPerSecond + (working ? cfg::kWorkTirednessPerSecond : 0.0f);
        tiredness_ = std::min(cfg::kMaxStat, tiredness_ + tiredRate * t.tirednessRate * dt);
    }

    // Keine Nahrung mehr im Körper -> HP sinken
    if (hunger_ >= cfg::kMaxStat) {
        hp_ -= cfg::kStarvationDamagePerSecond * dt;
        lastDamageCause_ = "Hunger";
    }
}

void Caveman::TakeDamage(float amount, const char* cause, WorldContext& ctx) {
    if (IsDead() || amount <= 0.0f) return;
    hp_ -= amount;
    lastDamageCause_ = cause;
    damageFlash_ = 0.25f;
    if (hp_ > 0.0f) {
        ctx.log.Add(TextFormat("%s wurde verletzt (%s, -%d HP)", name_.c_str(), cause, (int)amount),
                    { 255, 160, 120, 255 });
    }
}

// ============================================================================
//  "Denk"-Logik: wählt die wichtigste Absicht nach Prioritäten
// ============================================================================
bool Caveman::IsBlocked(CavemanState state) const {
    return blockedTimer_ > 0.0f && blockedState_ == state;
}

Caveman::Intent Caveman::Think(const WorldContext& ctx) const {
    const RoleTraits& t = GetRoleTraits(role_);
    const Stockpile& stock = ctx.tribe.Stock();

    const bool starving   = hunger_ >= cfg::kHungerCritical;
    const bool hungry     = hunger_ > cfg::kHungerThreshold;
    const bool needsSleep = tiredness_ > cfg::kTirednessThreshold || ap_ < cfg::kApMinForWork;

    // 1) Müdigkeit > 80 (oder AP verbraucht): Schlafplatz/Hütte suchen und schlafen.
    //    Ausnahme: kurz vor dem Verhungern geht Essen vor.
    if (needsSleep && !starving && !IsBlocked(CavemanState::Sleeping))
        return { CavemanState::Sleeping, kPrioSleep, ResourceType::Bush };

    // 2) Hunger > 70: Lager prüfen, sonst Nahrung beschaffen
    if (hungry) {
        if (stock.food > 0 && !IsBlocked(CavemanState::Eating))
            return { CavemanState::Eating, kPrioHunger, ResourceType::Bush };
        if (t.canHunt && !IsBlocked(CavemanState::Hunting))
            return { CavemanState::Hunting, kPrioHunger, ResourceType::Animal };
        // Sammler suchen Beeren. Älteste & Kinder können nicht jagen und sammeln ebenfalls.
        if (!IsBlocked(CavemanState::GatheringFood))
            return { CavemanState::GatheringFood, kPrioHunger, ResourceType::Bush };
    }

    // 3) (Erweiterung) Vorrat anlegen, damit der Stamm nicht erst bei Hunger losläuft
    const int reserveTarget = ctx.population * cfg::kFoodReservePerMember;
    if (stock.food < reserveTarget) {
        if (role_ == Role::Hunter && !IsBlocked(CavemanState::Hunting))
            return { CavemanState::Hunting, kPrioProvision, ResourceType::Animal };
        if (role_ == Role::Gatherer && !IsBlocked(CavemanState::GatheringFood))
            return { CavemanState::GatheringFood, kPrioProvision, ResourceType::Bush };
    }

    // 4) Holz & Stein im Lager und keine dringenden Bedürfnisse: Hütte bauen
    if (t.canBuild && !IsBlocked(CavemanState::Building)) {
        if (ctx.tribe.HasConstructionSite() ||
            (ctx.tribe.CanAffordHut() && ctx.tribe.NeedsMoreHuts(ctx.population)))
            return { CavemanState::Building, kPrioBuild, ResourceType::Tree };
    }

    // 5) (Erweiterung) Fehlendes Baumaterial besorgen, solange Schlafplätze fehlen
    if (t.canGatherMaterials && !ctx.tribe.HasConstructionSite() &&
        ctx.tribe.NeedsMoreHuts(ctx.population) && !IsBlocked(CavemanState::GatheringMaterials)) {
        const float woodNeed  = (float)std::max(0, cfg::kHutWoodCost - stock.wood) / (float)cfg::kHutWoodCost;
        const float stoneNeed = (float)std::max(0, cfg::kHutStoneCost - stock.stone) / (float)cfg::kHutStoneCost;
        if (woodNeed > 0.0f || stoneNeed > 0.0f) {
            return { CavemanState::GatheringMaterials, kPrioMaterials,
                     woodNeed >= stoneNeed ? ResourceType::Tree : ResourceType::Rock };
        }
    }

    // 6) Ansonsten: umherwandern oder am Lager verweilen
    const bool wander = GetRandomValue(0, 99) < (role_ == Role::Child ? 75 : 55);
    return { wander ? CavemanState::Wandering : CavemanState::Idle, kPrioIdle, ResourceType::Bush };
}

bool Caveman::IsInterruptible() const {
    // Beute/Material erst abliefern, Kampf erst zu Ende bringen
    if (carryAmount_ > 0) return false;
    if (state_ == CavemanState::Hunting && phase_ == TaskPhase::Working) return false;
    return true;
}

void Caveman::ApplyIntent(const Intent& intent, WorldContext& ctx) {
    // Gleicher Zustand läuft schon -> nur Priorität anpassen, Ziel behalten
    if (!taskDone_ && intent.state == state_) {
        currentPriority_ = intent.priority;
        return;
    }
    if (EnterState(intent.state, intent.resource, ctx)) {
        currentPriority_ = intent.priority;
        return;
    }
    // Kein Ziel erreichbar -> diesen Zustand kurz meiden und kurz rasten
    blockedState_ = intent.state;
    blockedTimer_ = kBlockDuration;
    EnterState(CavemanState::Idle, ResourceType::Bush, ctx);
    stateTimer_ = 1.0f;
    currentPriority_ = kPrioIdle;
}

// ============================================================================
//  Zustandswechsel
// ============================================================================
Vector2 Caveman::PersonalOffset(float radius) const {
    // Goldener Winkel -> jeder Mensch bekommt einen eigenen Platz um ein Ziel herum
    const float angle = (float)id_ * 2.39996f;
    return { std::cos(angle) * radius, std::sin(angle) * radius * 0.6f };
}

bool Caveman::EnterState(CavemanState newState, ResourceType resource, WorldContext& ctx) {
    ExitState(ctx);
    state_ = newState;
    phase_ = TaskPhase::MovingToTarget;
    taskDone_ = false;
    stateTimer_ = 0.0f;
    workTimer_ = 0.0f;
    repathTimer_ = 1.0f;
    taskResource_ = resource;

    switch (newState) {
        case CavemanState::Idle:
            stateTimer_ = (float)GetRandomValue(20, 50) / 10.0f;
            targetPosition_ = position_;
            return true;

        case CavemanState::Wandering: {
            // In Lagernähe bleiben, damit der Stamm zusammenhält
            const float radius = (role_ == Role::Child ? 6.0f : 12.0f) * TILE_SIZE;
            Vector2 dest;
            if (!ctx.world.FindRandomWalkablePosition(ctx.tribe.CampPosition(), radius, dest, 20)) return false;
            stateTimer_ = 25.0f;
            return SetDestination(ctx, dest);
        }

        case CavemanState::Hunting: {
            const int idx = ctx.resources.FindBest(ResourceType::Animal, position_);
            const ResourceNode* prey = ctx.resources.Get(idx);
            if (!prey || !SetDestination(ctx, prey->position)) return false;
            targetNode_ = idx;
            targetGeneration_ = prey->generation;
            ctx.resources.Claim(idx);
            return true;
        }

        case CavemanState::GatheringFood:
        case CavemanState::GatheringMaterials: {
            if (newState == CavemanState::GatheringFood) taskResource_ = ResourceType::Bush;
            const int idx = ctx.resources.FindBest(taskResource_, position_);
            const ResourceNode* node = ctx.resources.Get(idx);
            if (!node || !SetDestination(ctx, Vector2Add(node->position, PersonalOffset(5.0f)))) return false;
            targetNode_ = idx;
            targetGeneration_ = node->generation;
            ctx.resources.Claim(idx);
            return true;
        }

        case CavemanState::Building: {
            const int site = ctx.tribe.EnsureConstructionSite(ctx.world, ctx.log);
            Hut* hut = ctx.tribe.GetHut(site);
            if (!hut || !SetDestination(ctx, Vector2Add(hut->position, PersonalOffset(13.0f)))) return false;
            targetHut_ = site;
            hut->builders++;
            return true;
        }

        case CavemanState::Eating:
            if (ctx.tribe.Stock().food <= 0) return false;
            return SetDestination(ctx, Vector2Add(ctx.tribe.StockpilePosition(), PersonalOffset(10.0f)));

        case CavemanState::Sleeping: {
            // Nächste Hütte mit freiem Platz, sonst unter freiem Himmel am Feuer
            Vector2 dest;
            const int hutIdx = ctx.tribe.FindSleepingHut(position_);
            if (Hut* hut = ctx.tribe.GetHut(hutIdx)) {
                const float slotX = -8.0f + 8.0f * (float)(hut->sleepers % cfg::kHutCapacity);
                dest = { hut->position.x + slotX, hut->position.y + 7.0f };
                hut->sleepers++;
                targetHut_ = hutIdx;
                sleepingInHut_ = true;
            } else {
                dest = Vector2Add(ctx.tribe.CampPosition(), PersonalOffset(28.0f));
            }
            if (!SetDestination(ctx, dest)) {
                path_.clear();
                phase_ = TaskPhase::Working;   // kein Weg -> an Ort und Stelle schlafen
            }
            return true;
        }

        case CavemanState::Dead:
            return false;
    }
    return false;
}

void Caveman::ExitState(WorldContext& ctx) {
    ReleaseNode(ctx);
    if (Hut* hut = ctx.tribe.GetHut(targetHut_)) {
        if (state_ == CavemanState::Building) hut->builders = std::max(0, hut->builders - 1);
        if (state_ == CavemanState::Sleeping && sleepingInHut_) hut->sleepers = std::max(0, hut->sleepers - 1);
    }
    targetHut_ = -1;
    sleepingInHut_ = false;
    path_.clear();
    pathIndex_ = 0;
}

void Caveman::ReleaseNode(WorldContext& ctx) {
    if (targetNode_ >= 0) ctx.resources.Release(targetNode_);
    targetNode_ = -1;
}

void Caveman::Die(WorldContext& ctx) {
    if (state_ == CavemanState::Dead) return;
    ExitState(ctx);
    carryType_ = CarryType::None;
    carryAmount_ = 0;
    hp_ = 0.0f;
    state_ = CavemanState::Dead;
    taskDone_ = false;

    const bool starved = std::string(lastDamageCause_) == "Hunger";
    ctx.log.Add(TextFormat("%s (%s) %s", name_.c_str(), RoleName(role_),
                           starved ? "ist verhungert." : TextFormat("wurde getötet (%s).", lastDamageCause_)),
                { 255, 80, 80, 255 });
}

// ============================================================================
//  Zustands-Handler
// ============================================================================
void Caveman::UpdateIdle(float dt) {
    stateTimer_ -= dt;
    if (stateTimer_ <= 0.0f) FinishTask();
}

void Caveman::UpdateWandering(WorldContext& ctx, float dt) {
    stateTimer_ -= dt;
    if (MoveAlongPath(ctx, dt) != MoveResult::Moving || stateTimer_ <= 0.0f) FinishTask();
}

void Caveman::UpdateHunting(WorldContext& ctx, float dt) {
    if (phase_ == TaskPhase::Returning) { UpdateReturning(ctx, dt); return; }

    ResourceNode* prey = ctx.resources.Get(targetNode_);
    if (!prey || !ctx.resources.IsValidTarget(targetNode_, targetGeneration_)) {
        FinishTask();   // Beute ist weg (z. B. von einem anderen Jäger erlegt)
        return;
    }

    if (phase_ == TaskPhase::MovingToTarget) {
        if (Vector2Distance(position_, prey->position) <= cfg::kInteractRange) {
            phase_ = TaskPhase::Working;
            prey->engaged = true;
            path_.clear();
            workTimer_ = 0.0f;
            return;
        }
        // Das Tier bewegt sich -> Pfad regelmäßig aktualisieren
        repathTimer_ -= dt;
        if (repathTimer_ <= 0.0f) {
            repathTimer_ = 1.0f;
            if (Vector2Distance(prey->position, targetPosition_) > (float)TILE_SIZE &&
                !SetDestination(ctx, prey->position)) {
                FinishTask();
                return;
            }
        }
        const MoveResult r = MoveAlongPath(ctx, dt);
        if (r == MoveResult::Failed) FinishTask();
        else if (r == MoveResult::Arrived && !SetDestination(ctx, prey->position)) FinishTask();
        return;
    }

    // Working: Kampf. Kostet AP, das Tier kann zurückschlagen.
    const RoleTraits& t = GetRoleTraits(role_);
    prey->engaged = true;
    facingLeft_ = prey->position.x < position_.x;
    prey->hp -= t.attackDamage * WorkFactor(t.workEfficiency) * dt;
    SpendAp(cfg::kApCostHunt * dt);

    workTimer_ += dt;
    if (workTimer_ >= 1.0f) {
        workTimer_ -= 1.0f;
        if (GetRandomValue(0, 99) < cfg::kAnimalCounterChance)
            TakeDamage((float)GetRandomValue(cfg::kAnimalDamageMin, cfg::kAnimalDamageMax), "Wildtier", ctx);
    }

    if (prey->hp <= 0.0f) {
        const int meat = ctx.resources.KillAnimal(targetNode_);
        ReleaseNode(ctx);
        ctx.log.Add(TextFormat("%s hat ein Wildtier erlegt (+%d Fleisch)", name_.c_str(), meat),
                    { 230, 150, 100, 255 });
        StartReturning(ctx, CarryType::Food, meat);
    }
}

void Caveman::UpdateHarvesting(WorldContext& ctx, float dt) {
    if (phase_ == TaskPhase::Returning) { UpdateReturning(ctx, dt); return; }

    if (!ctx.resources.IsValidTarget(targetNode_, targetGeneration_)) {
        FinishTask();   // erschöpft -> beim nächsten Denken neues Ziel
        return;
    }

    if (phase_ == TaskPhase::MovingToTarget) {
        const MoveResult r = MoveAlongPath(ctx, dt);
        if (r == MoveResult::Failed) {
            FinishTask();
        } else if (r == MoveResult::Arrived) {
            phase_ = TaskPhase::Working;
            workTimer_ = 0.0f;
            if (const ResourceNode* node = ctx.resources.Get(targetNode_)) facingLeft_ = node->position.x < position_.x;
        }
        return;
    }

    // Working: ernten
    float duration = cfg::kGatherFoodDuration;
    float apCost = cfg::kApCostGather;
    int yield = cfg::kBerryYield;
    CarryType carry = CarryType::Food;
    if (taskResource_ == ResourceType::Tree) {
        duration = cfg::kChopWoodDuration; apCost = cfg::kApCostMaterials; yield = cfg::kWoodYield; carry = CarryType::Wood;
    } else if (taskResource_ == ResourceType::Rock) {
        duration = cfg::kMineStoneDuration; apCost = cfg::kApCostMaterials; yield = cfg::kStoneYield; carry = CarryType::Stone;
    }

    const RoleTraits& t = GetRoleTraits(role_);
    workTimer_ += WorkFactor(t.workEfficiency) * dt;
    SpendAp(apCost * dt);

    if (workTimer_ >= duration) {
        const int wanted = std::max(1, (int)std::lround((float)yield * t.gatherYield));
        const int got = ctx.resources.Harvest(targetNode_, wanted);
        ReleaseNode(ctx);
        if (got <= 0) { FinishTask(); return; }
        StartReturning(ctx, carry, got);
    }
}

void Caveman::UpdateBuilding(WorldContext& ctx, float dt) {
    Hut* hut = ctx.tribe.GetHut(targetHut_);
    if (!hut || hut->finished) { FinishTask(); return; }

    if (phase_ == TaskPhase::MovingToTarget) {
        const MoveResult r = MoveAlongPath(ctx, dt);
        if (r == MoveResult::Failed) FinishTask();
        else if (r == MoveResult::Arrived) phase_ = TaskPhase::Working;
        return;
    }

    // Working: AP in Baufortschritt umwandeln
    facingLeft_ = hut->position.x < position_.x;
    if (ap_ <= 0.0f) { FinishTask(); return; }

    const RoleTraits& t = GetRoleTraits(role_);
    SpendAp(cfg::kApCostBuild * dt);
    if (ctx.tribe.AddBuildProgress(targetHut_, cfg::kBuildRatePerSecond * t.buildEfficiency * dt, ctx.log))
        FinishTask();
}

void Caveman::UpdateSleeping(WorldContext& ctx, float dt) {
    if (phase_ == TaskPhase::MovingToTarget) {
        if (MoveAlongPath(ctx, dt) != MoveResult::Moving) phase_ = TaskPhase::Working;   // angekommen -> hinlegen
        return;
    }

    // Schlafen: Müdigkeit sinkt, AP regenerieren (in Hütten schneller)
    const float bonus = sleepingInHut_ ? cfg::kHutSleepBonus : 1.0f;
    tiredness_ = std::max(0.0f, tiredness_ - cfg::kSleepTirednessRecovery * bonus * dt);
    ap_ = std::min(cfg::kMaxStat, ap_ + cfg::kSleepApRegen * bonus * dt);
    if (hunger_ < cfg::kHungerThreshold) hp_ = std::min(cfg::kMaxStat, hp_ + cfg::kSleepHpRegen * dt);

    const bool rested = tiredness_ <= cfg::kWakeTiredness && ap_ >= cfg::kWakeAp;
    if (rested || hunger_ >= cfg::kHungerCritical) FinishTask();   // ausgeschlafen oder Hunger weckt
}

void Caveman::UpdateEating(WorldContext& ctx, float dt) {
    if (phase_ == TaskPhase::MovingToTarget) {
        const MoveResult r = MoveAlongPath(ctx, dt);
        if (r == MoveResult::Failed) FinishTask();
        else if (r == MoveResult::Arrived) { phase_ = TaskPhase::Working; workTimer_ = 0.0f; }
        return;
    }

    workTimer_ += dt;
    if (workTimer_ >= cfg::kEatInterval) {
        workTimer_ -= cfg::kEatInterval;
        Stockpile& stock = ctx.tribe.Stock();
        if (stock.food <= 0) { FinishTask(); return; }   // jemand war schneller
        stock.food--;
        hunger_ = std::max(0.0f, hunger_ - cfg::kHungerPerFood);
    }
    if (hunger_ <= cfg::kSatedHunger) FinishTask();
}

void Caveman::UpdateReturning(WorldContext& ctx, float dt) {
    // Bei Erfolg ODER wenn kein Weg mehr existiert: abliefern
    if (MoveAlongPath(ctx, dt) != MoveResult::Moving) {
        DeliverCarry(ctx);
        FinishTask();
    }
}

// ============================================================================
//  Bewegung & Hilfsfunktionen
// ============================================================================
bool Caveman::SetDestination(WorldContext& ctx, Vector2 target) {
    targetPosition_ = target;
    path_.clear();
    pathIndex_ = 0;
    if (Vector2Distance(position_, target) < 1.0f) return true;
    return ctx.world.FindPath(position_, target, path_);
}

Caveman::MoveResult Caveman::MoveAlongPath(WorldContext& ctx, float dt) {
    if (pathIndex_ >= path_.size()) return MoveResult::Arrived;

    const RoleTraits& t = GetRoleTraits(role_);
    float speed = t.moveSpeed;
    if (hunger_ >= cfg::kMaxStat) speed *= 0.6f;   // geschwächt
    if (ap_ <= 0.0f) speed *= 0.8f;

    float remaining = speed * dt;
    while (remaining > 0.0f && pathIndex_ < path_.size()) {
        const Vector2 waypoint = path_[pathIndex_];
        const Vector2 delta = Vector2Subtract(waypoint, position_);
        const float dist = Vector2Length(delta);
        if (dist > 0.001f && std::fabs(delta.x) / dist > 0.2f) facingLeft_ = delta.x < 0.0f;

        if (dist <= remaining) {
            position_ = waypoint;
            remaining -= dist;
            ++pathIndex_;
            continue;
        }

        const Vector2 next = Vector2Add(position_, Vector2Scale(delta, remaining / dist));
        if (!ctx.world.IsWalkable(next) && ctx.world.IsWalkable(position_)) {
            // Gelände wurde (z. B. vom Spieler) verändert -> neu planen
            if (!SetDestination(ctx, targetPosition_)) return MoveResult::Failed;
            isMoving_ = true;
            return MoveResult::Moving;
        }
        position_ = next;
        remaining = 0.0f;
    }

    isMoving_ = true;
    return pathIndex_ >= path_.size() ? MoveResult::Arrived : MoveResult::Moving;
}

void Caveman::StartReturning(WorldContext& ctx, CarryType type, int amount) {
    carryType_ = type;
    carryAmount_ = amount;
    phase_ = TaskPhase::Returning;
    if (!SetDestination(ctx, Vector2Add(ctx.tribe.StockpilePosition(), PersonalOffset(8.0f)))) {
        DeliverCarry(ctx);
        FinishTask();
    }
}

void Caveman::DeliverCarry(WorldContext& ctx) {
    Stockpile& stock = ctx.tribe.Stock();
    switch (carryType_) {
        case CarryType::Food:  stock.food  += carryAmount_; break;
        case CarryType::Wood:  stock.wood  += carryAmount_; break;
        case CarryType::Stone: stock.stone += carryAmount_; break;
        case CarryType::None:  break;
    }
    carryType_ = CarryType::None;
    carryAmount_ = 0;
}

float Caveman::WorkFactor(float efficiency) const {
    return efficiency * (ap_ > 0.0f ? 1.0f : cfg::kLowApWorkFactor);
}

void Caveman::SpendAp(float amount) {
    ap_ = std::max(0.0f, ap_ - amount);
}

std::string Caveman::ActivityText() const {
    const bool moving = phase_ == TaskPhase::MovingToTarget;
    const bool returning = phase_ == TaskPhase::Returning;
    switch (state_) {
        case CavemanState::Idle:      return "Rastet";
        case CavemanState::Wandering: return "Streift umher";
        case CavemanState::Hunting:
            return moving ? "Pirscht sich an" : returning ? "Bringt Fleisch heim" : "Kämpft mit Wildtier";
        case CavemanState::GatheringFood:
            return moving ? "Sucht Beeren" : returning ? "Bringt Beeren heim" : "Pflückt Beeren";
        case CavemanState::GatheringMaterials:
            if (taskResource_ == ResourceType::Rock)
                return moving ? "Geht zum Fels" : returning ? "Trägt Stein zum Lager" : "Schlägt Stein";
            return moving ? "Geht zum Baum" : returning ? "Trägt Holz zum Lager" : "Fällt Holz";
        case CavemanState::Building:  return moving ? "Geht zur Baustelle" : "Baut Hütte";
        case CavemanState::Sleeping:
            if (moving) return sleepingInHut_ ? "Geht zur Hütte" : "Sucht Schlafplatz";
            return sleepingInHut_ ? "Schläft (Hütte)" : "Schläft (draußen)";
        case CavemanState::Eating:    return moving ? "Geht zum Lager" : "Isst";
        case CavemanState::Dead:      return "Tot";
    }
    return "";
}

// ============================================================================
//  Zeichnen
// ============================================================================
void Caveman::Draw(const SpriteSheet& sheet, bool highlighted) const {
    if (IsDead()) return;   // Tote werden nicht gezeichnet

    const RoleTraits& t = GetRoleTraits(role_);
    const bool asleep = state_ == CavemanState::Sleeping && phase_ == TaskPhase::Working;
    const bool working = phase_ == TaskPhase::Working && !asleep;
    const float x = position_.x, y = position_.y;
    const float spriteHeight = sheet.Config().drawHeight * (role_ == Role::Child ? 0.8f : 1.0f);

    DrawEllipse((int)x, (int)y, 5, 2, Fade(BLACK, 0.3f));
    if (highlighted) DrawEllipseLines((int)x, (int)y, 8, 3, YELLOW);

    const Color tint = damageFlash_ > 0.0f ? Color{ 255, 110, 110, 255 } : WHITE;

    if (sheet.IsLoaded()) {
        // Pose (Spalte) aus Zustand + Bewegung bestimmen; Zeile = Rolle
        const int row = t.spriteRow;
        int col = sheet.IdleColumn();
        if (asleep) {
            col = sheet.SleepColumn();
        } else if (isMoving_) {
            col = sheet.WalkColumn(row, (int)(animTime_ * cfg::kWalkAnimFps));   // Spalten 1-3 abwechselnd
        } else if (working) {
            const bool alt = std::fmod(animTime_, cfg::kWorkAnimInterval * 2.0f) < cfg::kWorkAnimInterval;
            switch (state_) {
                case CavemanState::Hunting:            col = alt ? sheet.ActionColumn() : sheet.IdleColumn(); break;
                case CavemanState::GatheringFood:      col = sheet.ActionColumn(); break;
                case CavemanState::GatheringMaterials: col = alt ? sheet.ActionColumn() : sheet.CraftColumn(); break;
                case CavemanState::Building:           col = alt ? sheet.CraftColumn() : sheet.IdleColumn(); break;
                case CavemanState::Eating:
                    col = role_ == Role::Elder ? sheet.ActionColumn() : sheet.IdleColumn(); break;
                default: break;
            }
        }
        sheet.Draw(row, col, position_, facingLeft_, tint);
    } else {
        // Fallback ohne Sprite-Sheet: farbige Figur
        const Color body = RoleColor(role_);
        if (asleep) {
            DrawEllipse((int)x, (int)(y - 2), 6, 2, body);
        } else {
            const float bob = isMoving_ ? std::fabs(std::sin(animTime_ * 10.0f)) * 1.5f : 0.0f;
            DrawRectangleRec({ x - 2.5f, y - spriteHeight * 0.65f - bob, 5.0f, spriteHeight * 0.65f }, body);
            DrawCircleV({ x, y - spriteHeight * 0.75f - bob }, 3.0f, { 240, 190, 150, 255 });
        }
    }

    // --- Overlays --------------------------------------------------------------
    const float headY = asleep ? y - 9.0f : y - spriteHeight - 2.0f;

    if (carryAmount_ > 0) {
        const Vector2 c = { x, headY - 2.0f };
        switch (carryType_) {
            case CarryType::Food:  DrawCircleV(c, 2.0f, { 210, 50, 60, 255 }); break;
            case CarryType::Wood:  DrawRectangleRec({ c.x - 3.0f, c.y - 1.0f, 6.0f, 2.2f }, { 130, 85, 40, 255 }); break;
            case CarryType::Stone: DrawCircleV(c, 1.8f, { 160, 158, 155, 255 }); break;
            case CarryType::None:  break;
        }
    }

    if (hp_ < cfg::kMaxStat || highlighted) {
        const float w = 12.0f;
        DrawRectangleRec({ x - w * 0.5f, headY - 6.0f, w, 1.6f }, Fade(BLACK, 0.6f));
        DrawRectangleRec({ x - w * 0.5f, headY - 6.0f, w * hp_ / cfg::kMaxStat, 1.6f },
                         hp_ > 50.0f ? GREEN : (hp_ > 25.0f ? ORANGE : RED));
    }

    if (asleep) {
        const float rise = std::fmod(animTime_, 1.5f) / 1.5f;
        DrawText("z", (int)(x + 4.0f + rise * 3.0f), (int)(headY - 4.0f - rise * 6.0f), 8, Fade(RAYWHITE, 1.0f - rise));
    }
}

void Caveman::DrawDebugPath() const {
    if (IsDead()) return;
    Vector2 prev = position_;
    for (size_t i = pathIndex_; i < path_.size(); ++i) {
        DrawLineEx(prev, path_[i], 1.0f, Fade(YELLOW, 0.7f));
        prev = path_[i];
    }
    DrawCircleLinesV(targetPosition_, 3.0f, YELLOW);
}

