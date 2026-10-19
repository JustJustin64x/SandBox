#include "caveman.h"
#include "config.h"
#include "event_log.h"
#include "sprite_sheet.h"
#include "tribe.h"
#include "building.h"
#include "world.h"
#include "utility_ai.h"
#include "relationship.h"
#include "social_structure.h"
#include "social_actions.h"

#include <algorithm>
#include <utility>

namespace {

constexpr float kBlockDuration = 3.0f;

const RoleTraits kRoleTraits[4] = {
    //  speed work  build craft yield hunger tired attack canHunt canBuild canCraft canMat row
    {  44.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.1f,  1.0f, 10.0f, true,   true,    true,    true,  0 },   // Jäger
    {  40.0f, 1.0f, 1.0f, 1.0f, 1.3f, 1.0f,  1.0f,  3.0f, false,  true,    true,    true,  1 },   // Sammlerin
    {  28.0f, 0.7f, 1.3f, 1.5f, 0.7f, 0.8f,  1.3f,  2.0f, false,  true,    true,    true,  2 },   // Ältester
    {  36.0f, 0.5f, 0.0f, 0.0f, 0.4f, 0.8f,  1.1f,  1.0f, false,  false,   false,   false, 3 },   // Kind
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
        case CavemanState::Crafting:           return "CRAFTING";
        case CavemanState::WarmingUp:          return "WARMING_UP";
        case CavemanState::Socializing:        return "SOCIALIZING";
        case CavemanState::Expedition:         return "EXPEDITION";
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
    hunger_     = (float)GetRandomValue(5, 30);
    warmth_     = (float)GetRandomValue(85, 100);
    tiredness_  = (float)GetRandomValue(0, 25);
    ap_         = (float)GetRandomValue(85, 100);
    morale_     = (float)GetRandomValue(70, 85);
    socialNeed_ = (float)GetRandomValue(20, 50);

    // Unveränderliches psychologisches Profil
    personality_ = Personality::GenerateRandom((int)role_);

    facingLeft_ = GetRandomValue(0, 1) == 1;
    thinkTimer_ = (float)GetRandomValue(0, 40) / 100.0f;
    animTime_   = (float)GetRandomValue(0, 100) / 100.0f;
}

// ============================================================================
//  Haupt-Update
// ============================================================================
void Caveman::Update(WorldContext& ctx, float dt) {
    if (state_ == CavemanState::Dead) return;

    animTime_ += dt;
    damageFlash_  = std::max(0.0f, damageFlash_ - dt);
    blockedTimer_ = std::max(0.0f, blockedTimer_ - dt);

    // 1) Bedürfnisse & Gedächtnis aktualisieren
    UpdateNeeds(ctx, dt);
    if (hp_ <= 0.0f) { Die(ctx); return; }

    // 2) Situativ-logisches Denken (Utility AI)
    thinkTimer_ -= dt;
    if (taskDone_ || thinkTimer_ <= 0.0f) {
        thinkTimer_ = cfg::kThinkInterval;
        Think(ctx);
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
        case CavemanState::Crafting:           UpdateCrafting(ctx, dt); break;
        case CavemanState::WarmingUp:          UpdateWarmingUp(ctx, dt); break;
        case CavemanState::Socializing:        UpdateSocializing(ctx, dt); break;
        case CavemanState::Expedition:         UpdateExpedition(ctx, dt); break;
        case CavemanState::Sleeping:           UpdateSleeping(ctx, dt); break;
        case CavemanState::Eating:             UpdateEating(ctx, dt); break;
        case CavemanState::Dead:               break;
    }

    if (hp_ <= 0.0f) Die(ctx);
}

void Caveman::UpdateNeeds(WorldContext& ctx, float dt) {
    const RoleTraits& t = GetRoleTraits(role_);
    const bool asleep = (state_ == CavemanState::Sleeping && phase_ == TaskPhase::Working);
    const bool working = (phase_ == TaskPhase::Working &&
                          (state_ == CavemanState::Hunting || state_ == CavemanState::GatheringFood ||
                           state_ == CavemanState::GatheringMaterials || state_ == CavemanState::Building ||
                           state_ == CavemanState::Crafting));

    // 1. Hunger
    const float hungerRate = cfg::kHungerPerSecond * t.hungerRate * (asleep ? cfg::kSleepHungerFactor : 1.0f);
    hunger_ = std::min(cfg::kMaxStat, hunger_ + hungerRate * dt);
    if (hunger_ >= cfg::kMaxStat) {
        TakeDamage(cfg::kStarvationDamagePerSecond * dt, "Hunger", ctx);
    }

    // 2. Müdigkeit
    if (!asleep) {
        const float tiredRate = cfg::kTirednessPerSecond + (working ? cfg::kWorkTirednessPerSecond : 0.0f);
        tiredness_ = std::min(cfg::kMaxStat, tiredness_ + tiredRate * t.tirednessRate * dt);
    }

    // 3. Wärme / Kälte
    float ambient = ctx.tribe.Buildings().GetWarmthAt(position_);
    if (sleepingInShelter_) ambient += cfg::kWarmthRecoverInShelter;

    if (ambient > 0.0f) {
        warmth_ = std::min(100.0f, warmth_ + ambient * dt);
    } else {
        bool isNight = (ctx.timeOfDay > 0.72f || ctx.timeOfDay < 0.22f);
        float lossRate = isNight ? cfg::kWarmthLossNight : cfg::kWarmthLossDay;
        warmth_ = std::max(0.0f, warmth_ - lossRate * dt);

        if (warmth_ <= 0.0f) {
            TakeDamage(cfg::kColdDamagePerSecond * dt, "Kälte", ctx);
        }
    }

    // 4. Soziales Bedürfnis (Einsamkeit)
    socialNeed_ = std::min(100.0f, socialNeed_ + 0.45f * dt);

    // 5. Arbeitseifer & Motivation (beeinflusst von Persönlichkeit, Bedürfnissen & Beziehung zum Partner)
    float targetMorale = 60.0f;
    if (ctx.tribe.Stock().food > ctx.population * 2) targetMorale += 15.0f;
    if (!ctx.tribe.NeedsMoreHuts(ctx.population))    targetMorale += 15.0f;
    if (ctx.tribe.Relationships().GetPartner(id_) >= 0) targetMorale += 10.0f; // Partnerglück

    if (hunger_ > 60.0f) targetMorale -= 30.0f;
    if (warmth_ < 40.0f) targetMorale -= 30.0f;
    if (tiredness_ > 75.0f) targetMorale -= 20.0f;
    if (socialNeed_ > 80.0f) targetMorale -= 15.0f; // Einsamkeit

    targetMorale = std::clamp(targetMorale, 10.0f, 100.0f);
    morale_ += (targetMorale - morale_) * 0.1f * dt;
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
//  Situatives & logisches Handeln (Utility AI)
// ============================================================================
bool Caveman::IsBlocked(CavemanState state) const {
    return blockedTimer_ > 0.0f && blockedState_ == state;
}

void Caveman::Think(WorldContext& ctx) {
    UtilityDecision dec = UtilityAI::Evaluate(*this, ctx);
    lastDecisionReason_ = dec.reason;
    lastDecisionScore_ = dec.score;

    if (IsBlocked(dec.state)) return;

    if (taskDone_ || (dec.score > lastDecisionScore_ + 0.15f && IsInterruptible())) {
        if (!EnterState(dec.state, dec.resource, dec.targetBuildingId, dec.targetNodeId, dec.targetCharacterId, dec.targetPoiId, ctx)) {
            blockedState_ = dec.state;
            blockedTimer_ = kBlockDuration;
            EnterState(CavemanState::Idle, ResourceType::Bush, -1, -1, -1, -1, ctx);
            stateTimer_ = 1.0f;
        }
    }
}

bool Caveman::IsInterruptible() const {
    if (carryAmount_ > 0) return false;
    if (state_ == CavemanState::Hunting && phase_ == TaskPhase::Working) return false;
    if (state_ == CavemanState::Expedition && phase_ == TaskPhase::Working) return false;
    return true;
}

// ============================================================================
//  Zustandswechsel
// ============================================================================
bool Caveman::EnterState(CavemanState state, ResourceType resource, int targetBuildingId,
                         int targetNodeId, int targetCharacterId, int targetPoiId, WorldContext& ctx) {
    ExitState(ctx);

    state_ = state;
    taskResource_ = resource;
    phase_ = TaskPhase::MovingToTarget;
    taskDone_ = false;
    stateTimer_ = 0.0f;
    workTimer_ = 0.0f;

    switch (state) {
        case CavemanState::Idle: {
            stateTimer_ = (float)GetRandomValue(15, 30) / 10.0f;
            return true;
        }
        case CavemanState::Wandering: {
            stateTimer_ = (float)GetRandomValue(40, 80) / 10.0f;
            Vector2 dest;
            if (ctx.world.FindRandomWalkablePosition(position_, 60.0f, dest, 20)) {
                return SetDestination(ctx, dest);
            }
            return false;
        }
        case CavemanState::Hunting: {
            targetNode_ = targetNodeId;
            ctx.resources.Claim(targetNode_);
            ResourceNode* prey = ctx.resources.Get(targetNode_);
            if (!prey || !prey->IsAvailable()) return false;
            targetGeneration_ = prey->generation;
            repathTimer_ = 0.5f;
            return SetDestination(ctx, prey->position);
        }
        case CavemanState::GatheringFood:
        case CavemanState::GatheringMaterials: {
            targetNode_ = targetNodeId;
            ctx.resources.Claim(targetNode_);
            ResourceNode* node = ctx.resources.Get(targetNode_);
            if (!node || !node->IsAvailable()) return false;
            targetGeneration_ = node->generation;
            return SetDestination(ctx, node->position);
        }
        case CavemanState::Building: {
            targetBuilding_ = targetBuildingId;
            Building* b = ctx.tribe.Buildings().Get(targetBuilding_);
            if (!b || !b->NeedsWork()) return false;
            b->builders++;
            return SetDestination(ctx, b->position);
        }
        case CavemanState::Crafting: {
            targetBuilding_ = targetBuildingId;
            Building* b = ctx.tribe.Buildings().Get(targetBuilding_);
            if (!b || b->type != BuildingType::CraftBench || !b->finished) return false;
            b->occupants++;
            return SetDestination(ctx, b->position);
        }
        case CavemanState::WarmingUp: {
            targetBuilding_ = targetBuildingId;
            Vector2 dest = ctx.tribe.CampPosition();
            if (Building* b = ctx.tribe.Buildings().Get(targetBuilding_)) {
                dest = b->position;
            }
            return SetDestination(ctx, dest);
        }
        case CavemanState::Socializing: {
            targetSocialPartner_ = targetCharacterId;
            if (ctx.allCavemen) {
                for (const auto& c : *ctx.allCavemen) {
                    if (c.Id() == targetSocialPartner_ && !c.IsDead()) {
                        return SetDestination(ctx, c.Position());
                    }
                }
            }
            return false;
        }
        case CavemanState::Expedition: {
            targetPoi_ = targetPoiId;
            PointOfInterest* poi = ctx.tribe.POIs().Get(targetPoi_);
            if (!poi || !poi->CanBeExplored()) return false;

            carryingTorch_ = poi->gear.torch;
            if (poi->gear.torch && ctx.tribe.Stock().torches > 0) ctx.tribe.Stock().torches--;
            if (poi->gear.provisions && ctx.tribe.Stock().food > 0) ctx.tribe.Stock().food--;

            poi->assignedExplorerId = id_;
            ctx.log.Add(TextFormat("%s bricht zur Expedition nach '%s' auf!", name_.c_str(), poi->name.c_str()), GOLD);
            return SetDestination(ctx, poi->position);
        }
        case CavemanState::Sleeping: {
            targetBuilding_ = targetBuildingId;
            if (Building* b = ctx.tribe.Buildings().Get(targetBuilding_)) {
                b->occupants++;
                sleepingInShelter_ = true;
                return SetDestination(ctx, b->position);
            }
            sleepingInShelter_ = false;
            Vector2 spot;
            if (ctx.world.FindRandomWalkablePosition(ctx.tribe.CampPosition(), 24.0f, spot, 15)) {
                return SetDestination(ctx, spot);
            }
            return SetDestination(ctx, ctx.tribe.CampPosition());
        }
        case CavemanState::Eating: {
            return SetDestination(ctx, ctx.tribe.StockpilePosition());
        }
        case CavemanState::Dead:
            return true;
    }
    return false;
}

void Caveman::ExitState(WorldContext& ctx) {
    ReleaseNode(ctx);
    ReleaseBuilding(ctx);
    targetSocialPartner_ = -1;
    if (targetPoi_ >= 0) {
        if (PointOfInterest* poi = ctx.tribe.POIs().Get(targetPoi_)) {
            if (poi->assignedExplorerId == id_ && poi->state != POIState::Explored) {
                poi->assignedExplorerId = -1;
                if (poi->state == POIState::Exploring) poi->state = POIState::Discovered;
            }
        }
    }
    targetPoi_ = -1;
    carryingTorch_ = false;
}

void Caveman::ReleaseBuilding(WorldContext& ctx) {
    if (targetBuilding_ >= 0) {
        if (Building* b = ctx.tribe.Buildings().Get(targetBuilding_)) {
            if (state_ == CavemanState::Building && b->builders > 0) b->builders--;
            if ((state_ == CavemanState::Sleeping || state_ == CavemanState::Crafting) && b->occupants > 0) b->occupants--;
        }
    }
    targetBuilding_ = -1;
    sleepingInShelter_ = false;
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

    // Wenn Figur einen Lebenspartner hat: Trauer und Partner auflösen
    int partnerId = ctx.tribe.Relationships().GetPartner(id_);
    if (partnerId >= 0) {
        ctx.tribe.Relationships().BreakPartnership(id_, partnerId);
        ctx.log.Add(TextFormat("%s trauert tief um den verstorbenen Lebenspartner %s!",
                               name_.c_str(), RoleName(role_)), { 200, 100, 180, 255 });
    }

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
        FinishTask();
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

    // Kampf
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
        FinishTask();
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
    Building* b = ctx.tribe.Buildings().Get(targetBuilding_);
    if (!b || !b->NeedsWork()) { FinishTask(); return; }

    if (phase_ == TaskPhase::MovingToTarget) {
        const MoveResult r = MoveAlongPath(ctx, dt);
        if (r == MoveResult::Failed) FinishTask();
        else if (r == MoveResult::Arrived) phase_ = TaskPhase::Working;
        return;
    }

    facingLeft_ = b->position.x < position_.x;
    if (ap_ <= 0.0f) { FinishTask(); return; }

    const RoleTraits& t = GetRoleTraits(role_);
    SpendAp(cfg::kApCostBuild * dt);
    float progress = cfg::kBuildRatePerSecond * t.buildEfficiency * dt;
    if (ctx.tribe.Buildings().AddBuildProgress(targetBuilding_, progress, ctx.tribe.Stock(), ctx.log)) {
        FinishTask();
    }
}

void Caveman::UpdateCrafting(WorldContext& ctx, float dt) {
    Building* b = ctx.tribe.Buildings().Get(targetBuilding_);
    if (!b || b->type != BuildingType::CraftBench || !b->finished) { FinishTask(); return; }

    if (phase_ == TaskPhase::MovingToTarget) {
        const MoveResult r = MoveAlongPath(ctx, dt);
        if (r == MoveResult::Failed) FinishTask();
        else if (r == MoveResult::Arrived) { phase_ = TaskPhase::Working; workTimer_ = 0.0f; }
        return;
    }

    facingLeft_ = b->position.x < position_.x;
    SpendAp(cfg::kApCostCraft * dt);
    workTimer_ += dt;

    if (workTimer_ >= cfg::kCraftDuration) {
        workTimer_ = 0.0f;
        if (ctx.tribe.CanCraftPlanks() && ctx.tribe.Stock().planks < 15) {
            ctx.tribe.CraftPlank();
            ctx.log.Add(TextFormat("%s hat 1 Holzbalken an der Werkbank hergestellt!", name_.c_str()), { 230, 160, 90, 255 });
            FinishTask();
        } else if (ctx.tribe.CanCraftBricks() && ctx.tribe.Stock().bricks < 10) {
            ctx.tribe.CraftBrick();
            ctx.log.Add(TextFormat("%s hat 1 Steinziegel an der Werkbank behauen!", name_.c_str()), { 180, 180, 195, 255 });
            FinishTask();
        } else {
            FinishTask();
        }
    }
}

void Caveman::UpdateWarmingUp(WorldContext& ctx, float dt) {
    if (phase_ == TaskPhase::MovingToTarget) {
        const MoveResult r = MoveAlongPath(ctx, dt);
        if (r == MoveResult::Failed) FinishTask();
        else if (r == MoveResult::Arrived) phase_ = TaskPhase::Working;
        return;
    }

    float ambient = ctx.tribe.Buildings().GetWarmthAt(position_);
    warmth_ = std::min(100.0f, warmth_ + std::max(ambient, cfg::kWarmthRecoverNearFire) * dt);
    ap_ = std::min(100.0f, ap_ + 3.0f * dt);

    if (warmth_ >= 90.0f) {
        FinishTask();
    }
}

void Caveman::UpdateSocializing(WorldContext& ctx, float dt) {
    if (!ctx.allCavemen) { FinishTask(); return; }

    Caveman* partner = nullptr;
    for (auto& c : *ctx.allCavemen) {
        if (c.Id() == targetSocialPartner_ && !c.IsDead()) {
            partner = &c;
            break;
        }
    }

    if (!partner) { FinishTask(); return; }

    if (phase_ == TaskPhase::MovingToTarget) {
        if (Vector2Distance(position_, partner->Position()) <= cfg::kInteractRange * 1.8f) {
            phase_ = TaskPhase::Working;
            workTimer_ = 0.0f;
            return;
        }

        repathTimer_ -= dt;
        if (repathTimer_ <= 0.0f) {
            repathTimer_ = 0.8f;
            SetDestination(ctx, partner->Position());
        }

        MoveResult r = MoveAlongPath(ctx, dt);
        if (r == MoveResult::Failed) FinishTask();
        return;
    }

    // Interaktion durchführen (Reden, Klatsch, Flirten)
    facingLeft_ = partner->Position().x < position_.x;
    workTimer_ += dt;

    if (workTimer_ >= 1.4f) {
        SocialActionEvaluator::ExecuteSocialize(*this, *partner, ctx);
        morale_ = std::min(100.0f, morale_ + 10.0f);
        partner->morale_ = std::min(100.0f, partner->morale_ + 10.0f);
        FinishTask();
    }
}

void Caveman::UpdateExpedition(WorldContext& ctx, float dt) {
    PointOfInterest* poi = ctx.tribe.POIs().Get(targetPoi_);
    if (!poi) { FinishTask(); return; }

    if (phase_ == TaskPhase::MovingToTarget) {
        MoveResult r = MoveAlongPath(ctx, dt);
        float distToPoi = Vector2Distance(position_, poi->position);
        if (r == MoveResult::Arrived || distToPoi <= 22.0f) {
            phase_ = TaskPhase::Working;
            workTimer_ = 0.0f;
            poi->state = POIState::Exploring;
            poi->assignedExplorerId = id_;
            ctx.log.Add(TextFormat("%s hat %s erreicht und erforscht den Ort!", name_.c_str(), poi->name.c_str()), POIColor(poi->type));
        } else if (r == MoveResult::Failed) {
            FinishTask();
        }
        return;
    }

    if (phase_ == TaskPhase::Working) {
        facingLeft_ = poi->position.x < position_.x;
        SpendAp(cfg::kApCostMaterials * 0.4f * dt);
        workTimer_ += dt * WorkFactor(1.0f);
        poi->explorationProgress = std::clamp(workTimer_ / cfg::kExpeditionDuration, 0.0f, 1.0f);

        if (workTimer_ >= cfg::kExpeditionDuration) {
            poi->explorationProgress = 1.0f;
            carryType_ = CarryType::Discovery;
            carryAmount_ = 1;
            phase_ = TaskPhase::Returning;
            ctx.log.Add(TextFormat("%s hat neue Erkenntnisse in '%s' geborgen und kehrt zurück!", name_.c_str(), poi->name.c_str()), GOLD);
            SetDestination(ctx, ctx.tribe.CampPosition());
        }
        return;
    }

    if (phase_ == TaskPhase::Returning) {
        MoveResult r = MoveAlongPath(ctx, dt);
        float distToCamp = Vector2Distance(position_, ctx.tribe.CampPosition());
        if (r == MoveResult::Arrived || distToCamp <= 24.0f) {
            poi->state = POIState::Explored;
            poi->assignedExplorerId = -1;

            // Assoziierte Technologie im Skilltree freischalten
            for (const auto& node : ctx.tribe.Skills().AllNodes()) {
                if (node.requiresPOI && node.targetPOIType == poi->type) {
                    ctx.tribe.Skills().CompleteNode(node.id, ctx.log);
                }
            }

            morale_ = std::min(100.0f, morale_ + cfg::kExpeditionMoraleBoost);
            memory_.Add(MemoryType::SavedInCombat, id_, 20.0f);

            carryType_ = CarryType::None;
            carryAmount_ = 0;
            carryingTorch_ = false;
            targetPoi_ = -1;
            FinishTask();
        } else if (r == MoveResult::Failed) {
            FinishTask();
        }
    }
}

void Caveman::UpdateSleeping(WorldContext& ctx, float dt) {
    if (phase_ == TaskPhase::MovingToTarget) {
        if (MoveAlongPath(ctx, dt) != MoveResult::Moving) phase_ = TaskPhase::Working;
        return;
    }

    const float bonus = sleepingInShelter_ ? cfg::kHutSleepBonus : 1.0f;
    tiredness_ = std::max(0.0f, tiredness_ - cfg::kSleepTirednessRecovery * bonus * dt);
    ap_ = std::min(cfg::kMaxStat, ap_ + cfg::kSleepApRegen * bonus * dt);
    if (hunger_ < cfg::kHungerThreshold) hp_ = std::min(cfg::kMaxStat, hp_ + cfg::kSleepHpRegen * dt);

    const bool rested = tiredness_ <= cfg::kWakeTiredness && ap_ >= cfg::kWakeAp;
    if (rested || hunger_ >= cfg::kHungerCritical) FinishTask();
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
        if (stock.food <= 0) { FinishTask(); return; }
        stock.food--;
        hunger_ = std::max(0.0f, hunger_ - cfg::kHungerPerFood);
    }
    if (hunger_ <= cfg::kSatedHunger) FinishTask();
}

void Caveman::UpdateReturning(WorldContext& ctx, float dt) {
    const MoveResult r = MoveAlongPath(ctx, dt);
    if (r == MoveResult::Arrived || r == MoveResult::Failed) {
        DeliverCarry(ctx);
        FinishTask();
    }
}

void Caveman::StartReturning(WorldContext& ctx, CarryType type, int amount) {
    carryType_ = type;
    carryAmount_ = amount;
    phase_ = TaskPhase::Returning;
    SetDestination(ctx, ctx.tribe.StockpilePosition());
}

void Caveman::DeliverCarry(WorldContext& ctx) {
    if (carryAmount_ <= 0) { carryType_ = CarryType::None; return; }
    Stockpile& stock = ctx.tribe.Stock();
    switch (carryType_) {
        case CarryType::Food:   stock.food += carryAmount_; break;
        case CarryType::Wood:   stock.wood += carryAmount_; break;
        case CarryType::Stone:  stock.stone += carryAmount_; break;
        case CarryType::Planks: stock.planks += carryAmount_; break;
        case CarryType::Bricks: stock.bricks += carryAmount_; break;
        case CarryType::Tools:  stock.tools += carryAmount_; break;
        case CarryType::Discovery:
        case CarryType::None:   break;
    }
    carryType_ = CarryType::None;
    carryAmount_ = 0;
}

// ============================================================================
//  Bewegung & Pathfinding
// ============================================================================
bool Caveman::SetDestination(WorldContext& ctx, Vector2 target) {
    targetPosition_ = target;
    pathIndex_ = 0;
    return ctx.world.FindPath(position_, target, path_);
}

Caveman::MoveResult Caveman::MoveAlongPath(WorldContext& ctx, float dt) {
    (void)ctx;
    if (path_.empty() || pathIndex_ >= path_.size()) return MoveResult::Arrived;

    const RoleTraits& t = GetRoleTraits(role_);
    const float speed = t.moveSpeed;
    float step = speed * dt;
    isMoving_ = true;

    while (step > 0.0f && pathIndex_ < path_.size()) {
        const Vector2 next = path_[pathIndex_];
        const float dist = Vector2Distance(position_, next);

        if (dist <= step) {
            position_ = next;
            step -= dist;
            pathIndex_++;
        } else {
            const Vector2 dir = Vector2Normalize(Vector2Subtract(next, position_));
            position_ = Vector2Add(position_, Vector2Scale(dir, step));
            facingLeft_ = dir.x < 0.0f;
            step = 0.0f;
        }
    }

    if (pathIndex_ >= path_.size()) {
        path_.clear();
        pathIndex_ = 0;
        return MoveResult::Arrived;
    }
    return MoveResult::Moving;
}

float Caveman::WorkFactor(float efficiency) const {
    return efficiency * (ap_ <= 0.0f ? cfg::kLowApWorkFactor : 1.0f);
}

void Caveman::SpendAp(float amount) {
    ap_ = std::max(0.0f, ap_ - amount);
}

Vector2 Caveman::PersonalOffset(float radius) const {
    const float a = (float)(id_ * 73 % 360) / 180.0f * PI;
    return { std::cos(a) * radius, std::sin(a) * radius * 0.5f };
}

std::string Caveman::ActivityText() const {
    switch (state_) {
        case CavemanState::Idle:               return "Verweilt am Ort";
        case CavemanState::Wandering:          return "Erkundet die Gegend";
        case CavemanState::Hunting:            return "Auf der Jagd nach Wild";
        case CavemanState::GatheringFood:      return "Sammelt Beeren";
        case CavemanState::GatheringMaterials: return (taskResource_ == ResourceType::Tree) ? "Fällt Holz" : "Schlägt Steine";
        case CavemanState::Building:           return "Errichtet Siedlungsbau";
        case CavemanState::Crafting:           return "Veredelt Güter an Werkbank";
        case CavemanState::WarmingUp:          return "Wärmt sich am Feuer auf";
        case CavemanState::Socializing:        return "Im Gespräch mit Stammesmitglied";
        case CavemanState::Expedition:         return (phase_ == TaskPhase::Working) ? "Erforscht Entdeckungsort" : (phase_ == TaskPhase::Returning) ? "Bringt Forschungswissen zurück" : "Auf Expedition zu POI";
        case CavemanState::Sleeping:           return sleepingInShelter_ ? "Schläft in Unterkunft" : "Schläft im Freien";
        case CavemanState::Eating:             return "Isst aus dem Vorrat";
        case CavemanState::Dead:               return "Verstorben";
    }
    return "?";
}

std::string Caveman::GetSocialSummary(const Tribe& tribe) const {
    int clanId = tribe.Hierarchy().GetClanId(id_);
    const Clan* clan = tribe.Hierarchy().GetClan(clanId);
    std::string clanName = clan ? clan->name : "Stammlos";
    SocialRank rank = tribe.Hierarchy().GetRank(id_);

    int partner = tribe.Relationships().GetPartner(id_);
    std::string partnerStr = (partner >= 0) ? TextFormat("Partner: #%d", partner) : "Kein Partner";

    auto friends = tribe.Relationships().GetFriends(id_);
    auto enemies = tribe.Relationships().GetEnemies(id_);

    return TextFormat("%s | %s | %s | Wesen: %s | Freunde: %d | Rivalen: %d",
                      clanName.c_str(), SocialRankName(rank), partnerStr.c_str(),
                      personality_.GetPrimaryTraitDescription().c_str(),
                      (int)friends.size(), (int)enemies.size());
}

// ============================================================================
//  Zeichnen
// ============================================================================
void Caveman::Draw(const SpriteSheet& sheet, bool highlighted) const {
    if (IsDead()) {
        DrawLineEx({ position_.x - 4, position_.y - 5 }, { position_.x + 4, position_.y + 5 }, 1.5f, Fade(RED, 0.7f));
        DrawLineEx({ position_.x + 4, position_.y - 5 }, { position_.x - 4, position_.y + 5 }, 1.5f, Fade(RED, 0.7f));
        return;
    }

    // Fackellicht bei Nacht/Expedition
    if (carryingTorch_) {
        DrawCircleGradient(position_, cfg::kExpeditionTorchLight,
                           Fade(ORANGE, 0.25f), Fade(YELLOW, 0.0f));
    }

    const RoleTraits& t = GetRoleTraits(role_);
    int frame = 0;
    if (isMoving_) {
        frame = 1 + (int)(animTime_ * cfg::kWalkAnimFps) % 4;
    }

    Color tint = (damageFlash_ > 0.0f) ? RED : WHITE;
    sheet.Draw(t.spriteRow, frame, position_, facingLeft_, tint);

    // Expeditions-Rucksack
    if (state_ == CavemanState::Expedition) {
        int packX = (int)position_.x + (facingLeft_ ? 3 : -7);
        DrawRectangle(packX, (int)position_.y - 8, 4, 6, Color{ 115, 75, 45, 255 });
        DrawRectangleLines(packX, (int)position_.y - 8, 4, 6, Color{ 70, 45, 25, 255 });
    }

    // Getragene Fackel
    if (carryingTorch_) {
        Vector2 torchHand = { position_.x + (facingLeft_ ? -8.0f : 8.0f), position_.y - 4.0f };
        DrawLineEx(torchHand, { torchHand.x, torchHand.y - 7.0f }, 1.5f, BROWN);
        DrawCircleV({ torchHand.x, torchHand.y - 8.0f }, 2.0f, ORANGE);
        DrawPixel((int)torchHand.x, (int)torchHand.y - 8, YELLOW);
    }

    // Geborgenes Forschungswissen / Kristall
    if (carryType_ == CarryType::Discovery) {
        DrawCircle((int)position_.x, (int)position_.y - 15, 3.5f, GOLD);
        DrawCircleLines((int)position_.x, (int)position_.y - 15, 3.5f, RAYWHITE);
    }

    if (highlighted) {
        DrawCircleLines((int)position_.x, (int)position_.y, 13.0f, GOLD);
    }
}

void Caveman::DrawDebugPath() const {
    if (path_.empty() || pathIndex_ >= path_.size()) return;
    for (size_t i = pathIndex_; i < path_.size() - 1; ++i) {
        DrawLineEx(path_[i], path_[i + 1], 1.2f, Fade(RoleColor(role_), 0.6f));
    }
}
