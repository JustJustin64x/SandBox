#include "caveman.h"
#include "config.h"
#include "event_log.h"
#include "sprite_sheet.h"
#include "tribe.h"
#include "building.h"
#include "world.h"
#include "world/environmental_system.h"
#include "utility_ai.h"
#include "relationship.h"
#include "social_structure.h"
#include "social_actions.h"
#include "font_manager.h"

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
        case CavemanState::Fleeing:            return "FLEEING (PANIK)";
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

const char* ToolTierName(ToolTier t) {
    switch (t) {
        case ToolTier::BareHands:    return "Bloße Hände";
        case ToolTier::FlintHandaxe: return "Faustkeil (Stein)";
        case ToolTier::HaftedAxe:    return "Steinaxt";
        case ToolTier::BronzeTool:   return "Bronzewerkzeug";
        case ToolTier::IronTool:     return "Eisenwerkzeug";
    }
    return "?";
}

const char* WeaponTierName(WeaponTier w) {
    switch (w) {
        case WeaponTier::WoodClub:     return "Holzknüppel";
        case WeaponTier::FlintSpear:   return "Feuersteinspeer";
        case WeaponTier::CompositeBow: return "Kompositbogen";
        case WeaponTier::MetalBlade:   return "Metallklinge";
    }
    return "?";
}

const char* ContainerTierName(ContainerTier c) {
    switch (c) {
        case ContainerTier::None:        return "Keine";
        case ContainerTier::WovenBasket: return "Tragekorb (2x)";
        case ContainerTier::Handcart:    return "Handkarren (3x)";
    }
    return "?";
}

const char* ClothingTierName(ClothingTier cl) {
    switch (cl) {
        case ClothingTier::None:       return "Unbekleidet";
        case ClothingTier::RawHide:    return "Rohfell (-25% Kälte)";
        case ClothingTier::SewnFurs:   return "Pelzkleidung (-50% Kälte)";
        case ClothingTier::WovenTunic: return "Clan-Tunika (-70% Kälte)";
        case ClothingTier::Armor:      return "Rüstung (-70% Kälte, Schutz)";
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
    : id_(id), name_(std::move(name)), role_(role), position_(position), targetPosition_(position), homeCampPos_(position) {
    hunger_     = (float)GetRandomValue(5, 30);
    warmth_     = (float)GetRandomValue(85, 100);
    tiredness_  = (float)GetRandomValue(0, 25);
    ap_         = (float)GetRandomValue(85, 100);
    morale_     = (float)GetRandomValue(70, 85);
    socialNeed_ = (float)GetRandomValue(20, 50);

    // Lebensalter je nach Startrolle initialisieren
    if (role_ == Role::Child) {
        ageYears_ = (float)GetRandomValue(2, 10);
    } else if (role_ == Role::Elder) {
        ageYears_ = (float)GetRandomValue(56, 66);
        knowledgeBonus_ = 0.15f;
    } else {
        ageYears_ = (float)GetRandomValue(18, 35);
    }
    maxLifespan_ = (float)GetRandomValue(74, 86);

    // Unveränderliches psychologisches Profil
    personality_ = Personality::GenerateRandom((int)role_);

    facingLeft_ = GetRandomValue(0, 1) == 1;
    thinkTimer_ = (float)GetRandomValue(0, 40) / 100.0f;
    animTime_   = (float)GetRandomValue(0, 100) / 100.0f;
}

float Caveman::GetColdInsulation() const {
    switch (clothingTier_) {
        case ClothingTier::None:       return 0.0f;
        case ClothingTier::RawHide:    return 0.25f;
        case ClothingTier::SewnFurs:   return 0.50f;
        case ClothingTier::WovenTunic: return 0.70f;
        case ClothingTier::Armor:      return 0.70f;
    }
    return 0.0f;
}

int Caveman::GetCarryCapacityMultiplier() const {
    switch (containerTier_) {
        case ContainerTier::None:        return 1;
        case ContainerTier::WovenBasket: return 2;
        case ContainerTier::Handcart:    return 3;
    }
    return 1;
}

bool Caveman::NeedsGearUpgrade(const Tribe& tribe) const {
    if (role_ == Role::Child) return false;
    const auto& skills = tribe.Skills();
    const auto& stock = tribe.Stock();

    // Werkzeug-Upgrade möglich?
    if (skills.IsUnlocked("bloomery") && toolTier_ < ToolTier::IronTool && stock.stone >= 1) return true;
    if (skills.IsUnlocked("bronze") && toolTier_ < ToolTier::BronzeTool && stock.stone >= 1) return true;
    if (skills.IsUnlocked("carpentry") && toolTier_ < ToolTier::HaftedAxe && stock.wood >= 1 && stock.stone >= 1) return true;
    if (skills.IsUnlocked("flint") && toolTier_ < ToolTier::FlintHandaxe && stock.stone >= 1) return true;

    // Kleidung-Upgrade möglich?
    if (skills.IsUnlocked("tailoring") && clothingTier_ < ClothingTier::SewnFurs && stock.hides >= 1) return true;
    if (skills.IsUnlocked("tailoring") && containerTier_ == ContainerTier::None && stock.wood >= 1) return true;

    return false;
}

void Caveman::TriggerEmote(SocialEmote emote, float duration) {
    activeEmote_ = emote;
    emoteTimer_  = duration;
}

void Caveman::SyncSocialVisuals(int clanId, Color clanColor, int socialRank, Vector2 homeCampPos) {
    if (socialRank == 0 && socialRank_ != 0) {
        TriggerEmote(SocialEmote::Crown, 4.5f);
    }
    clanId_      = clanId;
    clanColor_   = clanColor;
    socialRank_  = socialRank;
    homeCampPos_ = homeCampPos;
}

// ============================================================================
//  Haupt-Update
// ============================================================================
void Caveman::Update(WorldContext& ctx, float dt) {
    if (state_ == CavemanState::Dead) return;

    animTime_ += dt;
    damageFlash_  = std::max(0.0f, damageFlash_ - dt);
    blockedTimer_ = std::max(0.0f, blockedTimer_ - dt);
    if (emoteTimer_ > 0.0f) {
        emoteTimer_ -= dt;
        if (emoteTimer_ <= 0.0f) activeEmote_ = SocialEmote::None;
    }

    // 0) Demografie & Lebenszyklus (Kind -> Jäger/Sammler -> Ältester -> Alterstod)
    ageYears_ += dt * 0.20f; // ca. 12 Lebensjahre pro Spielminute
    if (role_ == Role::Child && ageYears_ >= 16.0f) {
        // Genetische Persönlichkeit bestimmt Berufung beim Heranwachsen
        bool hunterTrait = (personality_.dominance + personality_.aggression * 0.8f) >
                           (personality_.altruism + personality_.sociability * 0.5f);
        role_ = hunterTrait ? Role::Hunter : Role::Gatherer;
        TriggerEmote(SocialEmote::Crown, 4.0f);
        ctx.log.Add(TextFormat("%s ist herangewachsen (16 J.) und wirkt nun als %s!",
                               name_.c_str(), RoleName(role_)), RoleColor(role_));
    } else if ((role_ == Role::Hunter || role_ == Role::Gatherer) && ageYears_ >= 56.0f) {
        role_ = Role::Elder;
        if (ctx.tribe.Hierarchy().GetRank(id_) != SocialRank::Chief) {
            ctx.tribe.Hierarchy().SetRank(id_, SocialRank::Elder);
        }
        TriggerEmote(SocialEmote::Idea, 4.0f);
        ctx.log.Add(TextFormat("%s (56 J.) gehört nun als %s zum Ältestenrat!",
                               name_.c_str(), RoleName(role_)), RoleColor(role_));
    } else if (ageYears_ >= maxLifespan_) {
        lastDamageCause_ = "Altersschwäche";
        Die(ctx);
        return;
    }

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
        case CavemanState::Fleeing:            UpdateFleeing(ctx, dt); break;
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

    // 3. Wärme / Kälte (inkl. Wetter, Regen, Gebirgskälte & Kleidungs-Isolation)
    float ambient = ctx.tribe.Buildings().GetWarmthAt(position_);
    if (sleepingInShelter_) ambient += cfg::kWarmthRecoverInShelter;
    float envDelta = ctx.env ? ctx.env->GetEnvironmentalWarmthDelta(position_) : 0.0f;

    if (ambient > 0.0f) {
        warmth_ = std::min(100.0f, warmth_ + (ambient + std::max(0.0f, envDelta)) * dt);
    } else {
        bool isNight = (ctx.timeOfDay > 0.72f || ctx.timeOfDay < 0.22f);
        float rawLoss = (isNight ? cfg::kWarmthLossNight : cfg::kWarmthLossDay) - envDelta;
        float insulation = GetColdInsulation(); // 0% bis 70% Kälteschutz durch Kleidung
        float lossRate = std::max(0.15f, rawLoss * (1.0f - insulation));
        warmth_ = std::clamp(warmth_ - lossRate * dt, 0.0f, 100.0f);

        // Kälte-Druck fördert Erfindungsgeist für Kleidung ("tailoring") und Fackeln ("torches")
        if (warmth_ < 55.0f && role_ != Role::Child) {
            if (ctx.tribe.Skills().AddPractice("tailoring", 0.35f * dt, ctx.log, name_)) {
                TriggerEmote(SocialEmote::Idea, 4.0f);
            }
            if (ctx.tribe.Skills().AddPractice("torches", 0.25f * dt, ctx.log, name_)) {
                TriggerEmote(SocialEmote::Idea, 4.0f);
            }
        }

        if (warmth_ <= 0.0f) {
            TakeDamage(cfg::kColdDamagePerSecond * dt, "Kälte", ctx);
        }
    }

    // 3b. Direkter Umweltschaden (Feuer & Starkstrom im Wasser)
    if (ctx.env) {
        const int tx = World::ToTile(position_.x);
        const int ty = World::ToTile(position_.y);
        const TileEnvState& envSt = ctx.env->GetState(tx, ty);
        if (envSt.isBurning) {
            TakeDamage(14.0f * std::max(0.4f, envSt.fireIntensity) * dt, "Feuer", ctx);
        } else if (envSt.electrifiedTimer > 0.0f) {
            TakeDamage(16.0f * dt, "Starkstrom", ctx);
        }
    }

    // 3c. Wasser-Physik, Schwimm-Mechanik & Ertrinken (Phase 2)
    const int curTx = World::ToTile(position_.x);
    const int curTy = World::ToTile(position_.y);
    const WaterType wt = ctx.world.GetWaterType(curTx, curTy);
    const TileType tt = ctx.world.GetTile(curTx, curTy);
    const bool inWater = (wt != WaterType::None) || (tt == TILE_SHALLOW_WATER || tt == TILE_DEEP_WATER);
    isSwimming_ = inWater;

    if (isSwimming_) {
        // Schwimmen leert AP deutlich schneller (+50% schneller in Salzwasser)
        float swimApDrain = (tt == TILE_DEEP_WATER) ? 14.0f : 7.0f;
        if (wt == WaterType::Saltwater) swimApDrain *= 1.5f;
        SpendAp(swimApDrain * dt);

        // Ertrinken: Fällt AP im tiefen Wasser auf 0, verliert der Caveman kontinuierlich HP
        if (ap_ <= 0.0f && (tt == TILE_DEEP_WATER || wt == WaterType::Saltwater)) {
            TakeDamage(12.0f * dt, "Ertrinken", ctx);
            // Bricht aktuelle Aufgabe ab und flieht panisch zum nächsten sicheren Ufer
            if (state_ != CavemanState::Fleeing) {
                int safeX = curTx, safeY = curTy;
                if (ctx.world.FindNearestWalkableTile(curTx, curTy, 10, safeX, safeY)) {
                    TileType safeType = ctx.world.GetTile(safeX, safeY);
                    if (safeType != TILE_DEEP_WATER && safeType != TILE_SHALLOW_WATER) {
                        EnterState(CavemanState::Fleeing, ResourceType::Bush, -1, -1, -1, -1, ctx);
                        SetDestination(ctx, World::TileCenter(safeX, safeY));
                    }
                }
            }
        }
    } else {
        // Süßwasser-Interaktion: An Süßwasser-Ufern können Cavemen trinken/sich abkühlen (regeneriert AP und Moral)
        if (ctx.world.IsNearFreshwater(curTx, curTy, 1)) {
            ap_ = std::min(100.0f, ap_ + 6.0f * dt);
            morale_ = std::min(100.0f, morale_ + 2.0f * dt);
        } else {
            // Normale AP-Regeneration an Land
            ap_ = std::min(100.0f, ap_ + cfg::kApRegenPerSecond * dt);
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
    // Rüstung mindert physischen Schaden im Kampf / bei der Jagd um 45%
    if (clothingTier_ == ClothingTier::Armor &&
        (std::string(cause) == "Wildtier" || std::string(cause) == "Streit")) {
        amount *= 0.55f;
    }
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

    // Panik / Flucht vor Feuer oder Starkstrom unterbricht sofort jede laufende Tätigkeit
    if (dec.state == CavemanState::Fleeing) {
        if (state_ != CavemanState::Fleeing) {
            EnterState(CavemanState::Fleeing, ResourceType::Bush, -1, -1, -1, -1, ctx);
        }
        return;
    }

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
    if (state_ == CavemanState::Fleeing) return false;
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
        case CavemanState::Fleeing: {
            stateTimer_ = 2.8f;
            Vector2 hazardCenter = position_;
            if (ctx.env) {
                ctx.env->IsHazardNear(position_, 48.0f, hazardCenter);
            }
            Vector2 away = Vector2Subtract(position_, hazardCenter);
            if (Vector2Length(away) < 1.0f) {
                away = Vector2Subtract(ctx.tribe.CampPosition(), position_);
            }
            if (Vector2Length(away) < 1.0f) away = { 1.0f, 0.0f };
            away = Vector2Normalize(away);
            Vector2 safeSpot = Vector2Add(position_, Vector2Scale(away, 65.0f));
            int tx = World::ToTile(safeSpot.x);
            int ty = World::ToTile(safeSpot.y);
            int outX = tx, outY = ty;
            if (ctx.world.FindNearestWalkableTile(tx, ty, 6, outX, outY)) {
                return SetDestination(ctx, World::TileCenter(outX, outY));
            }
            return SetDestination(ctx, ctx.tribe.CampPosition());
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
            if (state == CavemanState::GatheringFood && targetBuildingId >= 0) {
                targetBuilding_ = targetBuildingId;
                Building* b = ctx.tribe.Buildings().Get(targetBuilding_);
                if (!b || !b->NeedsFarmTending()) return false;
                b->occupants++;
                return SetDestination(ctx, b->position);
            }
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
            if (!b || (b->type != BuildingType::CraftBench && b->type != BuildingType::Kiln) || !b->finished) return false;
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
            if ((state_ == CavemanState::Sleeping || state_ == CavemanState::Crafting ||
                 state_ == CavemanState::GatheringFood) && b->occupants > 0) {
                b->occupants--;
            }
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

    // Meilenstein E: Ahnengrab nahe der Siedlung für das verstorbene Stammesmitglied anlegen
    Vector2 graveSpot = position_;
    ctx.tribe.FindBuildingSpotAround(ctx.world, ctx.tribe.CampPosition(), graveSpot);
    ctx.tribe.Buildings().SpawnGrave(graveSpot, name_, ctx.tribe.Age().EraIndex(), ctx.log);
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

void Caveman::UpdateFleeing(WorldContext& ctx, float dt) {
    stateTimer_ -= dt;
    MoveResult r = MoveAlongPath(ctx, dt);
    if (r != MoveResult::Moving || stateTimer_ <= 0.0f) {
        Vector2 hazardCenter = { 0.0f, 0.0f };
        if (ctx.env && ctx.env->IsHazardNear(position_, 36.0f, hazardCenter)) {
            EnterState(CavemanState::Fleeing, ResourceType::Bush, -1, -1, -1, -1, ctx);
        } else {
            FinishTask();
        }
    }
}

void Caveman::UpdateHunting(WorldContext& ctx, float dt) {
    if (phase_ == TaskPhase::Returning) { UpdateReturning(ctx, dt); return; }

    ResourceNode* prey = ctx.resources.Get(targetNode_);
    if (!prey || !ctx.resources.IsValidTarget(targetNode_, targetGeneration_)) {
        FinishTask();
        return;
    }

    // Fernkampf-Reichweite bei ausgerüstetem Kompositbogen (56px statt 18px!)
    float attackRange = (weaponTier_ == WeaponTier::CompositeBow) ? 56.0f : cfg::kInteractRange;

    if (phase_ == TaskPhase::MovingToTarget) {
        if (Vector2Distance(position_, prey->position) <= attackRange) {
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

    // Kampf (mit Waffen-Multiplikator & Fernkampf-Sicherheit)
    const RoleTraits& t = GetRoleTraits(role_);
    prey->engaged = true;
    facingLeft_ = prey->position.x < position_.x;
    float weaponMult = (weaponTier_ == WeaponTier::MetalBlade)   ? 1.95f :
                       (weaponTier_ == WeaponTier::CompositeBow) ? 1.55f :
                       (weaponTier_ == WeaponTier::FlintSpear)   ? 1.35f : 1.0f;
    prey->hp -= t.attackDamage * weaponMult * WorkFactor(t.workEfficiency) * dt;
    SpendAp(cfg::kApCostHunt * dt);

    workTimer_ += dt;
    if (workTimer_ >= 1.0f) {
        workTimer_ -= 1.0f;
        int counterChance = (weaponTier_ == WeaponTier::CompositeBow) ? 5 : cfg::kAnimalCounterChance;
        if (GetRandomValue(0, 99) < counterChance)
            TakeDamage((float)GetRandomValue(cfg::kAnimalDamageMin, cfg::kAnimalDamageMax), "Wildtier", ctx);
    }

    if (prey->hp <= 0.0f) {
        const int meat = ctx.resources.KillAnimal(targetNode_);
        ReleaseNode(ctx);
        // Jagdbeute liefert neben Fleisch auch Rohfell (hides) und Knochen (bones) für Kleidung & Werkzeuge!
        ctx.tribe.Stock().hides += 1;
        ctx.tribe.Stock().bones += 1;
        if (ctx.tribe.Skills().AddPractice("tailoring", 16.0f, ctx.log, name_) ||
            ctx.tribe.Skills().AddPractice("torches", 10.0f, ctx.log, name_)) {
            TriggerEmote(SocialEmote::Idea, 4.0f);
        }
        ctx.log.Add(TextFormat("%s hat ein Wildtier erlegt (+%d Fleisch, +1 Fell, +1 Knochen)", name_.c_str(), meat),
                    { 230, 150, 100, 255 });
        StartReturning(ctx, CarryType::Food, meat * GetCarryCapacityMultiplier());
    }
}

void Caveman::UpdateHarvesting(WorldContext& ctx, float dt) {
    if (phase_ == TaskPhase::Returning) { UpdateReturning(ctx, dt); return; }

    // A) Neolithische Landwirtschaft & Tierzucht (FarmPlot & AnimalPen)
    if (targetBuilding_ >= 0) {
        Building* b = ctx.tribe.Buildings().Get(targetBuilding_);
        if (!b || !b->NeedsFarmTending()) {
            FinishTask();
            return;
        }

        if (phase_ == TaskPhase::MovingToTarget) {
            const MoveResult r = MoveAlongPath(ctx, dt);
            if (r == MoveResult::Failed) FinishTask();
            else if (r == MoveResult::Arrived) {
                phase_ = TaskPhase::Working;
                workTimer_ = 0.0f;
                facingLeft_ = b->position.x < position_.x;
            }
            return;
        }

        const RoleTraits& t = GetRoleTraits(role_);
        workTimer_ += WorkFactor(t.workEfficiency) * dt;
        SpendAp(cfg::kApCostGather * 0.8f * dt);

        if (workTimer_ >= 2.2f) {
            int capMult = GetCarryCapacityMultiplier();
            if (b->type == BuildingType::FarmPlot) {
                if (b->IsHarvestReady()) {
                    int baseGrain = (toolTier_ >= ToolTier::BronzeTool)   ? 7 :
                                    (toolTier_ >= ToolTier::FlintHandaxe) ? 6 : 4;
                    int harvested = baseGrain * capMult;
                    b->cropGrowth = 12.0f; // Feld abgeerntet & frisch gepflügt
                    DegradeToolDurability(10.0f, ctx);
                    bool heureka = ctx.tribe.Skills().AddPractice("agriculture", 22.0f, ctx.log, name_);
                    heureka |= ctx.tribe.Skills().AddPractice("carpentry", 14.0f, ctx.log, name_);
                    heureka |= ctx.tribe.Skills().AddPractice("masonry", 12.0f, ctx.log, name_);
                    if (heureka) {
                        TriggerEmote(SocialEmote::Idea, 4.0f);
                    }
                    ctx.log.Add(TextFormat("%s erntet reifes Getreide vom Ackerfeld (+%d Nahrung)!", name_.c_str(), harvested),
                                { 245, 210, 75, 255 });
                    ReleaseBuilding(ctx);
                    StartReturning(ctx, CarryType::Food, harvested);
                    return;
                } else {
                    b->cropGrowth = std::min(100.0f, b->cropGrowth + 38.0f);
                    ctx.tribe.Skills().AddPractice("agriculture", 10.0f, ctx.log, name_);
                    ctx.log.Add(TextFormat("%s bestellt und bewässert das Ackerfeld.", name_.c_str()), { 150, 220, 120, 255 });
                    FinishTask();
                    return;
                }
            } else if (b->type == BuildingType::AnimalPen) {
                if (b->IsHarvestReady()) {
                    int meatAndMilk = 3 * capMult;
                    b->cropGrowth = 18.0f;
                    ctx.tribe.Stock().hides += 2;
                    ctx.tribe.Stock().bones += 1;
                    if (ctx.tribe.Skills().AddPractice("tailoring", 18.0f, ctx.log, name_) ||
                        ctx.tribe.Skills().AddPractice("agriculture", 16.0f, ctx.log, name_)) {
                        TriggerEmote(SocialEmote::Idea, 4.0f);
                    }
                    ctx.log.Add(TextFormat("%s versorgt das Tiergehege (+%d Nahrung, +2 Wolle/Fell, +1 Knochen)!",
                                           name_.c_str(), meatAndMilk),
                                { 235, 215, 165, 255 });
                    ReleaseBuilding(ctx);
                    StartReturning(ctx, CarryType::Food, meatAndMilk);
                    return;
                } else {
                    b->cropGrowth = std::min(100.0f, b->cropGrowth + 35.0f);
                    ctx.tribe.Skills().AddPractice("agriculture", 8.0f, ctx.log, name_);
                    FinishTask();
                    return;
                }
            }
        }
        return;
    }

    // B) Klassisches Wild-Sammeln & Abbauen (Beeren, Holz, Stein)
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
        int capMult = GetCarryCapacityMultiplier(); // Tragekorb (2x) / Handkarren (3x)
        const int wanted = std::max(1, (int)std::lround((float)yield * t.gatherYield * (float)capMult));
        const int got = ctx.resources.Harvest(targetNode_, wanted);
        ReleaseNode(ctx);
        if (got <= 0) { FinishTask(); return; }

        // Learning-by-Doing Praxis & Werkzeug-Abnutzung beim Abbau
        bool heureka = false;
        if (taskResource_ == ResourceType::Rock) {
            heureka |= ctx.tribe.Skills().AddPractice("flint", 18.0f, ctx.log, name_);
            heureka |= ctx.tribe.Skills().AddPractice("masonry", 12.0f, ctx.log, name_);
            DegradeToolDurability(16.0f, ctx);
        } else if (taskResource_ == ResourceType::Tree) {
            heureka |= ctx.tribe.Skills().AddPractice("flint", 12.0f, ctx.log, name_);
            heureka |= ctx.tribe.Skills().AddPractice("carpentry", 14.0f, ctx.log, name_);
            heureka |= ctx.tribe.Skills().AddPractice("torches", 12.0f, ctx.log, name_);
            DegradeToolDurability(14.0f, ctx);
        } else {
            // Wildpflanzen & Beeren sammeln: Führt organisch zur Entdeckung von Ackerbau & Saatgut!
            heureka |= ctx.tribe.Skills().AddPractice("agriculture", 16.0f, ctx.log, name_);
            heureka |= ctx.tribe.Skills().AddPractice("tailoring", 8.0f, ctx.log, name_);
        }
        if (heureka) {
            TriggerEmote(SocialEmote::Idea, 4.0f);
        }

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
    DegradeToolDurability(3.5f * dt, ctx);

    // Beim Bauen lernt der Stamm Zimmererkunst, Steinmetzkunst und Wehrbau
    if (ctx.tribe.Skills().AddPractice("carpentry", 3.5f * dt, ctx.log, name_) ||
        ctx.tribe.Skills().AddPractice("masonry", 3.0f * dt, ctx.log, name_) ||
        ctx.tribe.Skills().AddPractice("defense", 3.5f * dt, ctx.log, name_)) {
        TriggerEmote(SocialEmote::Idea, 4.0f);
    }

    float progress = cfg::kBuildRatePerSecond * WorkFactor(t.buildEfficiency) * dt;
    if (ctx.tribe.Buildings().AddBuildProgress(targetBuilding_, progress, ctx.tribe.Stock(), ctx.log)) {
        FinishTask();
    }
}

void Caveman::UpdateCrafting(WorldContext& ctx, float dt) {
    Building* b = ctx.tribe.Buildings().Get(targetBuilding_);
    if (!b || (b->type != BuildingType::CraftBench && b->type != BuildingType::Kiln) || !b->finished) { FinishTask(); return; }

    if (phase_ == TaskPhase::MovingToTarget) {
        const MoveResult r = MoveAlongPath(ctx, dt);
        if (r == MoveResult::Failed) FinishTask();
        else if (r == MoveResult::Arrived) { phase_ = TaskPhase::Working; workTimer_ = 0.0f; }
        return;
    }

    facingLeft_ = b->position.x < position_.x;
    SpendAp(cfg::kApCostCraft * dt);
    workTimer_ += WorkFactor(1.0f) * dt;

    if (workTimer_ >= cfg::kCraftDuration) {
        workTimer_ = 0.0f;

        // 0. Falls am Lehmofen/Rennofen (Kiln) gearbeitet wird: zuerst Feuerholz nachlegen & Kochen/Brennen
        if (b->type == BuildingType::Kiln && b->fuel < 80.0f && ctx.tribe.Stock().wood >= 1) {
            ctx.tribe.Stock().wood--;
            b->fuel = std::min(100.0f, b->fuel + 65.0f);
            ctx.log.Add(TextFormat("%s befeuert den %s mit frischem Holz (%d%% Glut).",
                                   name_.c_str(), BuildingTypeName(b->type, b->tier), (int)b->fuel),
                        { 255, 175, 80, 255 });
        }

        // 1. Eigene Ausrüstung, Werkzeuge, Kleidung & Tragekorb an der Werkbank / Schmelze herstellen
        AutoUpgradeGearFromTribe(ctx, true);

        // 2. Am aktiven Lehmofen/Rennofen: Warme Mahlzeiten backen oder Tonziegel mit doppelter Ausbeute brennen
        if (b->type == BuildingType::Kiln && b->HasActiveFire() &&
            ctx.tribe.Stock().food >= 1 && ctx.tribe.Stock().cookedFood < std::max(8, ctx.population * 3)) {
            ctx.tribe.Stock().food--;
            ctx.tribe.Stock().cookedFood += 2;
            ctx.log.Add(TextFormat("%s hat im %s 2 nahrhafte Mahlzeiten gebacken!", name_.c_str(), BuildingTypeName(b->type, b->tier)),
                        { 255, 195, 95, 255 });
        }
        // 3. Expeditions-Fackeln & Spitzhacken für den Stamm auf Vorrat fertigen
        else if (ctx.tribe.Skills().IsUnlocked("torches") && ctx.tribe.Stock().torches < 3 && ctx.tribe.CanCraftTorch()) {
            ctx.tribe.CraftTorch();
            ctx.log.Add(TextFormat("%s hat 1 Harzfackel für Expeditionen gefertigt!", name_.c_str()), { 255, 190, 90, 255 });
        } else if (ctx.tribe.Skills().IsUnlocked("flint") && ctx.tribe.Stock().pickaxes < 2 && ctx.tribe.CanCraftPickaxe()) {
            ctx.tribe.CraftPickaxe();
            ctx.log.Add(TextFormat("%s hat 1 Steinspitzhacke geschäftet!", name_.c_str()), { 210, 210, 225, 255 });
        }
        // 4. Baumaterialien (Holzbalken & gebrannte Ziegel) veredeln
        else if ((ctx.tribe.Skills().IsUnlocked("carpentry") || ctx.tribe.Age().CanCraftRefined()) &&
                 ctx.tribe.CanCraftPlanks() && ctx.tribe.Stock().planks < 15) {
            ctx.tribe.CraftPlank();
            ctx.log.Add(TextFormat("%s hat 1 Holzbalken an der Werkbank hergestellt!", name_.c_str()), { 230, 160, 90, 255 });
        } else if ((ctx.tribe.Skills().IsUnlocked("masonry") || b->type == BuildingType::Kiln || ctx.tribe.Age().CurrentEra() >= Era::IronAge) &&
                   ctx.tribe.CanCraftBricks() && ctx.tribe.Stock().bricks < 12) {
            ctx.tribe.CraftBrick();
            if (b->type == BuildingType::Kiln && b->HasActiveFire()) {
                ctx.tribe.Stock().bricks++; // Lehmofen brennt doppelt so viele Ziegel!
                ctx.log.Add(TextFormat("%s hat 2 harte Tonziegel im %s gebrannt!", name_.c_str(), BuildingTypeName(b->type, b->tier)),
                            { 215, 130, 85, 255 });
            } else {
                ctx.log.Add(TextFormat("%s hat 1 Steinziegel an der Werkbank behauen!", name_.c_str()), { 180, 180, 195, 255 });
            }
        }

        // 5. Aktive Handwerks-Forschung ("Learning by Doing" an der Werkbank / am Ofen)
        bool heureka = false;
        heureka |= ctx.tribe.Skills().AddPractice("flint",     20.0f, ctx.log, name_);
        heureka |= ctx.tribe.Skills().AddPractice("tailoring", 20.0f, ctx.log, name_);
        heureka |= ctx.tribe.Skills().AddPractice("torches",   20.0f, ctx.log, name_);
        heureka |= ctx.tribe.Skills().AddPractice("carpentry", 20.0f, ctx.log, name_);
        heureka |= ctx.tribe.Skills().AddPractice("bronze",    (b->type == BuildingType::Kiln) ? 34.0f : 22.0f, ctx.log, name_);
        heureka |= ctx.tribe.Skills().AddPractice("masonry",   (b->type == BuildingType::Kiln) ? 30.0f : 20.0f, ctx.log, name_);
        heureka |= ctx.tribe.Skills().AddPractice("defense",   20.0f, ctx.log, name_);
        if (heureka) {
            TriggerEmote(SocialEmote::Idea, 4.2f);
        }
        FinishTask();
    }
}

void Caveman::UpdateWarmingUp(WorldContext& ctx, float dt) {
    if (phase_ == TaskPhase::MovingToTarget) {
        const MoveResult r = MoveAlongPath(ctx, dt);
        if (r == MoveResult::Failed) FinishTask();
        else if (r == MoveResult::Arrived) { phase_ = TaskPhase::Working; workTimer_ = 0.0f; }
        return;
    }

    Building* fire = ctx.tribe.Buildings().Get(targetBuilding_);

    // 0. Spirituelle Andacht am Monument / Ahnengrab (Meilenstein E)
    if (fire && (fire->type == BuildingType::Monument || fire->type == BuildingType::Grave)) {
        morale_ = std::min(100.0f, morale_ + 22.0f * dt);
        ap_ = std::min(100.0f, ap_ + 6.0f * dt);
        warmth_ = std::min(100.0f, warmth_ + 8.0f * dt);
        if (Clan* clan = ctx.tribe.Hierarchy().GetClanMutable(ctx.tribe.Hierarchy().GetClanId(id_))) {
            clan->cohesion = std::clamp(clan->cohesion + 1.5f * dt, 0.0f, 100.0f);
        }
        if (ctx.tribe.Skills().AddPractice("masonry", 3.5f * dt, ctx.log, name_)) {
            TriggerEmote(SocialEmote::Idea, 3.8f);
        }
        if (morale_ >= 92.0f) {
            FinishTask();
        }
        return;
    }

    if (!fire) {
        // Fallback: nächste Feuerstelle suchen
        int fireId = ctx.tribe.Buildings().FindFireNeedingFuel(position_);
        if (fireId >= 0) fire = ctx.tribe.Buildings().Get(fireId);
    }

    // 1. Feuerhüten (TendFire): Falls das Feuer erloschen oder schwach ist, sofort Holz nachlegen!
    if (fire && (fire->type == BuildingType::TownCenter || fire->type == BuildingType::Kiln) &&
        fire->fuel < 80.0f && ctx.tribe.Stock().wood >= 1) {
        bool wasExtinguished = (fire->fuel <= 0.0f);
        ctx.tribe.Stock().wood--;
        fire->fuel = std::min(100.0f, fire->fuel + 65.0f);
        if (ctx.tribe.Skills().AddPractice("torches", 18.0f, ctx.log, name_)) {
            TriggerEmote(SocialEmote::Idea, 4.0f);
        }
        if (wasExtinguished) {
            ctx.log.Add(TextFormat("%s hat das erloschene %s mit Feuerstein neu entzündet!",
                                   name_.c_str(), BuildingTypeName(fire->type, fire->tier)),
                        { 255, 190, 75, 255 });
        } else {
            ctx.log.Add(TextFormat("%s legt Brennholz ins %s nach (%d%% Glut).",
                                   name_.c_str(), BuildingTypeName(fire->type, fire->tier), (int)fire->fuel),
                        { 245, 175, 85, 255 });
        }
    }

    // 2. Wärme-Regeneration (nur wenn eine aktive Feuerquelle in der Nähe brennt!)
    float ambient = ctx.tribe.Buildings().GetWarmthAt(position_);
    if (ambient > 0.0f) {
        warmth_ = std::min(100.0f, warmth_ + std::max(ambient, cfg::kWarmthRecoverNearFire) * dt);
    } else {
        // Feuer ist kalt und kein Brennholz im Lager -> Aufgabe abbrechen, damit Holz gesammelt wird!
        if (ctx.tribe.Stock().wood <= 0) {
            FinishTask();
            return;
        }
    }
    ap_ = std::min(100.0f, ap_ + 3.0f * dt);

    // 3. Kochen am Lagerfeuer (CookFood): Rohe Nahrung am offenen Feuer zu warmen Mahlzeiten braten
    workTimer_ += dt;
    if (workTimer_ >= 2.2f) {
        workTimer_ = 0.0f;
        if (ambient > 0.0f && ctx.tribe.Stock().food >= 1 &&
            ctx.tribe.Stock().cookedFood < std::max(6, ctx.population * 2)) {
            ctx.tribe.Stock().food--;
            ctx.tribe.Stock().cookedFood += 2;
            ctx.log.Add(TextFormat("%s brät am Feuer 2 warme Mahlzeiten!", name_.c_str()), { 255, 195, 95, 255 });
            if (ctx.tribe.Skills().AddPractice("torches", 12.0f, ctx.log, name_)) {
                TriggerEmote(SocialEmote::Idea, 3.8f);
            }
        }
    }

    // 4. Am Lagerfeuer ziehen sich Frierende ein Rohfell über & Älteste geben Wissen weiter
    AutoUpgradeGearFromTribe(ctx, false);
    if (role_ == Role::Elder) {
        if (ctx.tribe.Skills().AddPractice("torches", 2.0f * dt, ctx.log, name_) ||
            ctx.tribe.Skills().AddPractice("tailoring", 2.0f * dt, ctx.log, name_)) {
            TriggerEmote(SocialEmote::Idea, 3.8f);
        }
    }

    if (warmth_ >= 90.0f && (!fire || !fire->NeedsFuel() || ctx.tribe.Stock().wood <= 0)) {
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

    // Interaktion durchführen (Situativ: Teilen, Flirten, Streit, Lehre oder Gespräch)
    facingLeft_ = partner->Position().x < position_.x;
    workTimer_ += dt;

    if (workTimer_ >= 1.4f) {
        std::string dummy;
        float sSoc   = SocialActionEvaluator::ScoreSocialize(*this, *partner, ctx, dummy);
        float sFlirt = SocialActionEvaluator::ScoreFlirt(*this, *partner, ctx, dummy);
        float sFood  = SocialActionEvaluator::ScoreShareFood(*this, *partner, ctx, dummy);
        float sDisp  = SocialActionEvaluator::ScoreDispute(*this, *partner, ctx, dummy);

        if (sFood >= sFlirt && sFood >= sSoc && sFood >= sDisp && sFood > 0.05f) {
            SocialActionEvaluator::ExecuteShareFood(*this, *partner, ctx);
        } else if (sFlirt >= sSoc && sFlirt >= sDisp && sFlirt > 0.05f) {
            SocialActionEvaluator::ExecuteFlirt(*this, *partner, ctx);
        } else if (sDisp >= sSoc && sDisp > 0.05f) {
            SocialActionEvaluator::ExecuteDispute(*this, *partner, ctx);
        } else {
            SocialActionEvaluator::ExecuteSocialize(*this, *partner, ctx);
        }

        // Wissensweitergabe ("Teach"): Älteste unterrichten Kinder und Stammesmitglieder
        if (role_ == Role::Elder || partner->GetRole() == Role::Elder) {
            Caveman& student = (role_ == Role::Elder) ? *partner : *this;
            student.AddKnowledgeBonus(0.05f);
            student.TriggerEmote(SocialEmote::Idea, 3.2f);
            int focusId = ctx.tribe.Skills().GetFocusNode();
            if (const TechNode* fn = ctx.tribe.Skills().GetNode(focusId)) {
                ctx.tribe.Skills().AddPractice(fn->key, 9.0f, ctx.log, name_);
            }
        }

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
            TriggerEmote(SocialEmote::Idea, 4.5f);
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
                    ctx.tribe.Skills().CompleteNode(node.id, ctx.log, name_);
                }
            }
            TriggerEmote(SocialEmote::Idea, 4.5f);

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
        if (stock.cookedFood > 0) {
            // Gebratene / gekochte Mahlzeit: +45% nahrhafter, wärmt innerlich & hebt die Moral!
            stock.cookedFood--;
            hunger_ = std::max(0.0f, hunger_ - cfg::kHungerPerFood * 1.45f);
            warmth_ = std::min(100.0f, warmth_ + 14.0f);
            morale_ = std::min(100.0f, morale_ + 8.0f);
        } else if (stock.food > 0) {
            stock.food--;
            hunger_ = std::max(0.0f, hunger_ - cfg::kHungerPerFood);
        } else {
            FinishTask();
            return;
        }
        AutoUpgradeGearFromTribe(ctx, false);
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

    // Beim Abliefern am Lagerplatz automatisch verfügbares Basis-Werkzeug / Fell anlegen
    AutoUpgradeGearFromTribe(ctx, false);
}

// ============================================================================
//  Bewegung & Pathfinding (inkl. Trampelpfade & Straßen-Bonus)
// ============================================================================
bool Caveman::SetDestination(WorldContext& ctx, Vector2 target) {
    targetPosition_ = target;
    pathIndex_ = 0;
    return ctx.world.FindPath(position_, target, path_);
}

Caveman::MoveResult Caveman::MoveAlongPath(WorldContext& ctx, float dt) {
    if (path_.empty() || pathIndex_ >= path_.size()) return MoveResult::Arrived;

    const RoleTraits& t = GetRoleTraits(role_);
    float envMult = ctx.env ? ctx.env->GetMovementSpeedMultiplier(position_) : 1.0f;
    float roadMult = ctx.world.GetRoadSpeedMultiplier(position_); // +12% auf Trampelpfad, +25% auf Straße
    if (state_ == CavemanState::Fleeing) envMult *= 1.35f;

    // Geschwindigkeitsreduktion im Wasser (55% bei Furt/flach, 30% bei tiefem Wasser)
    float waterMult = 1.0f;
    int curTx = World::ToTile(position_.x);
    int curTy = World::ToTile(position_.y);
    TileType tt = ctx.world.GetTile(curTx, curTy);
    WaterType wt = ctx.world.GetWaterType(curTx, curTy);
    if (wt != WaterType::None || tt == TILE_SHALLOW_WATER || tt == TILE_DEEP_WATER) {
        if (tt == TILE_DEEP_WATER || ctx.world.GetDistanceToShore(curTx, curTy) > 1) {
            waterMult = 0.30f;
        } else {
            waterMult = 0.55f;
        }
    }

    const float speed = t.moveSpeed * envMult * roadMult * waterMult;
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

    // Schrittfrequenz hinterlässt Spuren: Gras -> Trampelpfad -> Gepflasterte Straße (ab Steinmetzkunst)
    ctx.world.AddFootTraffic(World::ToTile(position_.x), World::ToTile(position_.y),
                             2.2f * dt, ctx.tribe.Skills().IsUnlocked("masonry"));

    // Physische Ressourcen-Drops auf dem Boden automatisch aufheben
    if (ctx.env && state_ != CavemanState::Fleeing) {
        int dropIdx = ctx.env->FindNearestGroundDrop(position_, 13.0f);
        if (dropIdx >= 0) {
            DropItemType dType;
            int dAmt = 0;
            if (ctx.env->CollectGroundDrop(dropIdx, dType, dAmt)) {
                Stockpile& stock = ctx.tribe.Stock();
                if (dType == DropItemType::WoodLog || dType == DropItemType::CharredWood) stock.wood += dAmt;
                else if (dType == DropItemType::StoneChunk) stock.stone += dAmt;
                else if (dType == DropItemType::BerryFood) stock.food += dAmt;
            }
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
    float toolMult = 1.0f;
    switch (toolTier_) {
        case ToolTier::BareHands:    toolMult = 0.80f; break;
        case ToolTier::FlintHandaxe: toolMult = 1.15f; break;
        case ToolTier::HaftedAxe:    toolMult = 1.40f; break;
        case ToolTier::BronzeTool:   toolMult = 1.75f; break;
        case ToolTier::IronTool:     toolMult = 2.15f; break;
    }
    return efficiency * toolMult * (1.0f + knowledgeBonus_) * (ap_ <= 0.0f ? cfg::kLowApWorkFactor : 1.0f);
}

void Caveman::SpendAp(float amount) {
    ap_ = std::max(0.0f, ap_ - amount);
}

void Caveman::AutoUpgradeGearFromTribe(WorldContext& ctx, bool atBench) {
    if (role_ == Role::Child) return;
    Stockpile& stock = ctx.tribe.Stock();
    const SkillTree& skills = ctx.tribe.Skills();

    // 1. Kleidung: Rohfell geht sofort am Lager, genähte Kleidung/Tunika/Rüstung bevorzugt an Werkbank
    if (clothingTier_ == ClothingTier::None && stock.hides >= 1) {
        stock.hides--;
        clothingTier_ = skills.IsUnlocked("tailoring") ? ClothingTier::SewnFurs : ClothingTier::RawHide;
        ctx.log.Add(TextFormat("%s trägt nun %s!", name_.c_str(), ClothingTierName(clothingTier_)), { 215, 175, 120, 255 });
    } else if (atBench && skills.IsUnlocked("bloomery") && clothingTier_ < ClothingTier::Armor && stock.hides >= 1 && stock.stone >= 1) {
        stock.hides--;
        stock.stone--;
        clothingTier_ = ClothingTier::Armor;
        ctx.log.Add(TextFormat("%s hat sich eine %s geschmiedet!", name_.c_str(), ClothingTierName(clothingTier_)), { 200, 215, 240, 255 });
    } else if (atBench && skills.IsUnlocked("bronze") && clothingTier_ < ClothingTier::WovenTunic && stock.hides >= 1) {
        stock.hides--;
        clothingTier_ = ClothingTier::WovenTunic;
        ctx.log.Add(TextFormat("%s trägt nun eine %s!", name_.c_str(), ClothingTierName(clothingTier_)), clanColor_);
    } else if (skills.IsUnlocked("tailoring") && clothingTier_ < ClothingTier::SewnFurs && stock.hides >= 1) {
        stock.hides--;
        clothingTier_ = ClothingTier::SewnFurs;
        ctx.log.Add(TextFormat("%s hat %s genäht!", name_.c_str(), ClothingTierName(clothingTier_)), { 220, 180, 125, 255 });
    }

    // 2. Tragekorb (verdoppelt Sammel-Traglast)
    if (containerTier_ == ContainerTier::None && skills.IsUnlocked("tailoring") && stock.wood >= 1) {
        stock.wood--;
        containerTier_ = ContainerTier::WovenBasket;
        ctx.log.Add(TextFormat("%s nutzt nun einen %s!", name_.c_str(), ContainerTierName(containerTier_)), { 215, 185, 110, 255 });
    }

    // 3. Werkzeuge (Faustkeil geht direkt mit Flint; höhere Stufen an der Werkbank)
    ToolTier desiredTool = toolTier_;
    if (atBench && skills.IsUnlocked("bloomery") && stock.stone >= 1) desiredTool = ToolTier::IronTool;
    else if (atBench && skills.IsUnlocked("bronze") && stock.stone >= 1) desiredTool = ToolTier::BronzeTool;
    else if (atBench && skills.IsUnlocked("carpentry") && stock.wood >= 1 && stock.stone >= 1) desiredTool = ToolTier::HaftedAxe;
    else if (skills.IsUnlocked("flint") && stock.stone >= 1 && toolTier_ == ToolTier::BareHands) desiredTool = ToolTier::FlintHandaxe;

    if (desiredTool > toolTier_) {
        stock.stone = std::max(0, stock.stone - 1);
        if (desiredTool >= ToolTier::HaftedAxe && stock.wood > 0) stock.wood--;
        toolTier_ = desiredTool;
        toolDurability_ = 100.0f;
        stock.tools++;
        ctx.log.Add(TextFormat("%s rüstet %s aus!", name_.c_str(), ToolTierName(toolTier_)), { 190, 225, 255, 255 });
    }

    // 4. Jagdwaffen (Speer -> Kompositbogen -> Metallklinge)
    if (role_ == Role::Hunter) {
        if (skills.IsUnlocked("bloomery") && weaponTier_ < WeaponTier::MetalBlade && stock.stone >= 1) {
            stock.stone--;
            weaponTier_ = WeaponTier::MetalBlade;
            ctx.log.Add(TextFormat("%s führt nun eine %s!", name_.c_str(), WeaponTierName(weaponTier_)), GOLD);
        } else if (skills.IsUnlocked("torches") && weaponTier_ < WeaponTier::CompositeBow && stock.wood >= 1 && stock.bones >= 1) {
            stock.wood--;
            stock.bones--;
            weaponTier_ = WeaponTier::CompositeBow;
            ctx.log.Add(TextFormat("%s jagt nun im Fernkampf mit %s!", name_.c_str(), WeaponTierName(weaponTier_)), { 245, 200, 110, 255 });
        } else if (skills.IsUnlocked("flint") && weaponTier_ < WeaponTier::FlintSpear && stock.wood >= 1) {
            stock.wood--;
            weaponTier_ = WeaponTier::FlintSpear;
            ctx.log.Add(TextFormat("%s führt nun einen %s!", name_.c_str(), WeaponTierName(weaponTier_)), { 230, 180, 120, 255 });
        }
    }
}

void Caveman::DegradeToolDurability(float amount, WorldContext& ctx) {
    if (toolTier_ == ToolTier::BareHands) return;
    // Höherwertige Werkzeuge nutzen sich langsamer ab
    float resist = (toolTier_ == ToolTier::IronTool)   ? 0.35f :
                   (toolTier_ == ToolTier::BronzeTool) ? 0.55f :
                   (toolTier_ == ToolTier::HaftedAxe)  ? 0.75f : 1.0f;
    toolDurability_ -= amount * resist;
    if (toolDurability_ <= 0.0f) {
        ctx.log.Add(TextFormat("Werkzeug von %s (%s) ist zerbrochen!", name_.c_str(), ToolTierName(toolTier_)),
                    { 230, 150, 110, 255 });
        toolTier_ = ToolTier::BareHands;
        toolDurability_ = 0.0f;
        ctx.tribe.Stock().tools = std::max(0, ctx.tribe.Stock().tools - 1);
    }
}

Vector2 Caveman::PersonalOffset(float radius) const {
    const float a = (float)(id_ * 73 % 360) / 180.0f * PI;
    return { std::cos(a) * radius, std::sin(a) * radius * 0.5f };
}

std::string Caveman::ActivityText() const {
    switch (state_) {
        case CavemanState::Idle:               return "Verweilt am Ort";
        case CavemanState::Wandering:          return "Erkundet die Gegend";
        case CavemanState::Fleeing:            return "Flieht in Panik vor Gefahr!";
        case CavemanState::Hunting:            return "Auf der Jagd nach Wild";
        case CavemanState::GatheringFood:      return "Sammelt Beeren";
        case CavemanState::GatheringMaterials: return (taskResource_ == ResourceType::Tree) ? "Fällt Holz" : "Schlägt Steine";
        case CavemanState::Building:           return "Errichtet / repariert Bauwerk";
        case CavemanState::Crafting:           return "Veredelt Güter an Werkbank";
        case CavemanState::WarmingUp:          return "Sucht Wärme & Schutz";
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
//  Zeichnen ("Show, Don't Tell": Clan-Farbe, Rang-Abzeichen & Floating Emotes)
// ============================================================================
void Caveman::Draw(const SpriteSheet& sheet, bool highlighted, float visualYOffset) const {
    Vector2 drawPos = { position_.x, position_.y + visualYOffset };

    if (IsDead()) {
        DrawLineEx({ drawPos.x - 4, drawPos.y - 5 }, { drawPos.x + 4, drawPos.y + 5 }, 1.5f, Fade(RED, 0.7f));
        DrawLineEx({ drawPos.x + 4, drawPos.y - 5 }, { drawPos.x - 4, drawPos.y + 5 }, 1.5f, Fade(RED, 0.7f));
        return;
    }

    // Wenn am Schwimmen: Wasserwellen & Eintauch-Effekt
    if (isSwimming_) {
        float ripple = std::sin(animTime_ * 7.0f);
        DrawEllipse((int)drawPos.x, (int)drawPos.y - 1, 8 + (int)(ripple * 2.0f), 4, Fade(SKYBLUE, 0.45f));
        DrawEllipseLines((int)drawPos.x, (int)drawPos.y - 1, 9 + (int)(ripple * 2.0f), 4, Fade(WHITE, 0.65f));
    } else {
        // 1. Clan-Zugehörigkeits-Ring am Boden (Bär = Rotbraun, Wolf = Blau, Rabe = Violett)
        DrawEllipse((int)drawPos.x, (int)drawPos.y + 1, 7, 3, Fade(clanColor_, 0.55f));
        DrawEllipseLines((int)drawPos.x, (int)drawPos.y + 1, 7, 3, Fade(clanColor_, 0.90f));
    }

    // Fackellicht bei Nacht/Expedition
    if (carryingTorch_) {
        DrawCircleGradient(drawPos, cfg::kExpeditionTorchLight,
                           Fade(ORANGE, 0.25f), Fade(YELLOW, 0.0f));
    }

    const RoleTraits& t = GetRoleTraits(role_);
    int frame = 0;
    if (isMoving_) {
        frame = 1 + (int)(animTime_ * cfg::kWalkAnimFps) % 4;
    }

    Color tint = (damageFlash_ > 0.0f) ? RED : WHITE;
    if (isSwimming_) {
        tint = ColorAlphaBlend(tint, Fade(SKYBLUE, 0.35f), WHITE);
    }
    sheet.Draw(t.spriteRow, frame, drawPos, facingLeft_, tint);

    if (isSwimming_) {
        // Vordere Wasserwelle verdeckt die Beine beim Schwimmen
        DrawEllipse((int)drawPos.x, (int)drawPos.y - 1, 7, 3, Fade(Color{ 45, 130, 205, 255 }, 0.55f));
    }

    // 2. Sichtbare Kleidung, Tragekörbe & gehaltene Werkzeuge/Waffen ("Show, Don't Tell")
    DrawClothingAndGear(drawPos);

    // 2b. Clan-Schulter-Abzeichen (kleines farbiges Wimpel-Dreieck)
    float side = facingLeft_ ? 4.0f : -4.0f;
    DrawCircleV({ drawPos.x + side, drawPos.y - 7.0f }, 2.0f, clanColor_);

    // 3. Sozialer Rang direkt am Kopf (Häuptlings-Krone / Ältesten-Reif)
    if (socialRank_ == 0) { // Chief (Häuptling)
        float cx = drawPos.x;
        float cy = drawPos.y - 19.0f;
        DrawRectangle((int)(cx - 4.0f), (int)cy, 8, 2, GOLD);
        DrawTriangle({ cx - 3.5f, cy - 3.0f }, { cx - 4.5f, cy }, { cx - 1.5f, cy }, GOLD);
        DrawTriangle({ cx,        cy - 4.5f }, { cx - 2.0f, cy }, { cx + 2.0f, cy }, YELLOW);
        DrawTriangle({ cx + 3.5f, cy - 3.0f }, { cx + 1.5f, cy }, { cx + 4.5f, cy }, GOLD);
    } else if (socialRank_ == 1) { // Elder (Ratsältester)
        DrawRectangle((int)(drawPos.x - 3.5f), (int)(drawPos.y - 18.0f), 7, 2, Color{ 210, 215, 245, 255 });
        DrawPixel((int)drawPos.x, (int)(drawPos.y - 18.0f), GOLD);
    }

    // 4. Floating Emotes & kritische Bedürfnis-Icons über dem Kopf
    DrawFloatingEmote(drawPos);

    // Expeditions-Rucksack
    if (state_ == CavemanState::Expedition) {
        int packX = (int)drawPos.x + (facingLeft_ ? 3 : -7);
        DrawRectangle(packX, (int)drawPos.y - 8, 4, 6, Color{ 115, 75, 45, 255 });
        DrawRectangleLines(packX, (int)drawPos.y - 8, 4, 6, Color{ 70, 45, 25, 255 });
    }

    // Getragene Fackel
    if (carryingTorch_) {
        Vector2 torchHand = { drawPos.x + (facingLeft_ ? -8.0f : 8.0f), drawPos.y - 4.0f };
        DrawLineEx(torchHand, { torchHand.x, torchHand.y - 7.0f }, 1.5f, BROWN);
        DrawCircleV({ torchHand.x, torchHand.y - 8.0f }, 2.0f, ORANGE);
        DrawPixel((int)torchHand.x, (int)torchHand.y - 8, YELLOW);
    }

    // Geborgenes Forschungswissen / Kristall
    if (carryType_ == CarryType::Discovery) {
        DrawCircle((int)drawPos.x, (int)drawPos.y - 15, 3.5f, GOLD);
        DrawCircleLines((int)drawPos.x, (int)drawPos.y - 15, 3.5f, RAYWHITE);
    }

    if (highlighted) {
        DrawCircleLines((int)drawPos.x, (int)drawPos.y, 13.0f, GOLD);
    }
}

void Caveman::DrawClothingAndGear(Vector2 drawPos) const {
    const int cx = (int)drawPos.x;
    const int cy = (int)drawPos.y;

    // A) Kleidungs-Stufe direkt am Oberkörper der Figur
    switch (clothingTier_) {
        case ClothingTier::None:
            break;
        case ClothingTier::RawHide:
            // Zotteliges dunkelbraunes Rohfell schräg über dem Torso
            DrawRectangle(cx - 3, cy - 8, 6, 5, Color{ 102, 66, 38, 230 });
            DrawPixel(cx - 3, cy - 3, Color{ 85, 52, 28, 230 });
            DrawPixel(cx + 2, cy - 3, Color{ 85, 52, 28, 230 });
            break;
        case ClothingTier::SewnFurs:
            // Helles genähtes Leder-/Pelzgewand mit hellem Kragen
            DrawRectangle(cx - 3, cy - 8, 6, 6, Color{ 158, 114, 70, 240 });
            DrawRectangle(cx - 3, cy - 9, 6, 1, Color{ 215, 195, 165, 240 });
            break;
        case ClothingTier::WovenTunic:
            // Gewebte Tunika in der Clan-Farbe mit Ledergürtel
            DrawRectangle(cx - 3, cy - 8, 6, 6, Fade(clanColor_, 0.90f));
            DrawRectangle(cx - 3, cy - 5, 6, 1, Color{ 95, 60, 30, 245 });
            break;
        case ClothingTier::Armor:
            // Metallischer Brustpanzer + kleiner Helmreif
            DrawRectangle(cx - 4, cy - 8, 8, 6, Color{ 182, 192, 208, 245 });
            DrawRectangleLines(cx - 4, cy - 8, 8, 6, Color{ 110, 120, 138, 255 });
            DrawRectangle(cx - 3, cy - 15, 6, 2, Color{ 195, 205, 220, 245 });
            break;
    }

    // B) Tragekorb auf dem Rücken (und sichtbares Ladegut beim Rückweg)
    if (containerTier_ != ContainerTier::None && state_ != CavemanState::Expedition) {
        int backX = cx + (facingLeft_ ? 3 : -6);
        int backY = cy - 8;
        DrawRectangle(backX, backY, 3, 5, Color{ 195, 152, 82, 245 });
        DrawRectangleLines(backX, backY, 3, 5, Color{ 130, 95, 45, 245 });
        if (carryAmount_ > 0) {
            Color loadCol = (carryType_ == CarryType::Food)  ? Color{ 220, 55, 65, 255 } :
                            (carryType_ == CarryType::Wood)  ? Color{ 135, 85, 40, 255 } :
                            (carryType_ == CarryType::Stone) ? Color{ 165, 165, 175, 255 } : GOLD;
            DrawCircle(backX + 1, backY - 1, 1.8f, loadCol);
        }
    }

    // C) Gehaltenes Werkzeug oder Jagdwaffe in der vorderen Hand
    if (carryingTorch_) return; // Fackel wird separat gezeichnet

    float handX = drawPos.x + (facingLeft_ ? -6.5f : 6.5f);
    float handY = drawPos.y - 5.0f;
    float dir = facingLeft_ ? -1.0f : 1.0f;

    // Jäger im Jagdmodus oder mit höherer Waffe zeigen ihre Waffe
    if (state_ == CavemanState::Hunting || (role_ == Role::Hunter && weaponTier_ > WeaponTier::WoodClub)) {
        switch (weaponTier_) {
            case WeaponTier::WoodClub:
                DrawLineEx({ handX, handY + 1.0f }, { handX + dir * 3.0f, handY - 5.0f }, 2.0f, Color{ 130, 82, 38, 255 });
                break;
            case WeaponTier::FlintSpear:
                DrawLineEx({ handX - dir * 1.0f, handY + 3.0f }, { handX + dir * 4.0f, handY - 6.0f }, 1.3f, Color{ 145, 95, 48, 255 });
                DrawCircleV({ handX + dir * 4.5f, handY - 6.8f }, 1.6f, Color{ 185, 195, 210, 255 });
                break;
            case WeaponTier::CompositeBow:
                // Geschwungener Bogen + Sehne
                DrawLineEx({ handX, handY - 5.0f }, { handX + dir * 2.5f, handY }, 1.4f, Color{ 165, 110, 50, 255 });
                DrawLineEx({ handX + dir * 2.5f, handY }, { handX, handY + 5.0f }, 1.4f, Color{ 165, 110, 50, 255 });
                DrawLineEx({ handX, handY - 5.0f }, { handX, handY + 5.0f }, 1.0f, Fade(RAYWHITE, 0.75f));
                break;
            case WeaponTier::MetalBlade:
                DrawLineEx({ handX, handY + 1.0f }, { handX + dir * 4.0f, handY - 6.0f }, 1.8f, Color{ 210, 225, 245, 255 });
                DrawPixel((int)handX, (int)handY + 1, GOLD);
                break;
        }
        return;
    }

    // Sonstiges Werkzeug in der Hand (Faustkeil, Steinaxt, Bronze-/Eisenwerkzeug)
    switch (toolTier_) {
        case ToolTier::BareHands:
            break;
        case ToolTier::FlintHandaxe:
            DrawCircleV({ handX, handY - 1.0f }, 2.0f, Color{ 160, 168, 180, 255 });
            break;
        case ToolTier::HaftedAxe:
            DrawLineEx({ handX, handY + 2.0f }, { handX, handY - 5.0f }, 1.3f, Color{ 140, 90, 45, 255 });
            DrawRectangle((int)(handX + (facingLeft_ ? -3.0f : 0.0f)), (int)(handY - 5.0f), 3, 2, Color{ 170, 175, 185, 255 });
            break;
        case ToolTier::BronzeTool:
            DrawLineEx({ handX, handY + 2.0f }, { handX, handY - 5.0f }, 1.3f, Color{ 140, 90, 45, 255 });
            DrawRectangle((int)(handX + (facingLeft_ ? -3.0f : 0.0f)), (int)(handY - 5.0f), 3, 2, Color{ 225, 155, 70, 255 });
            break;
        case ToolTier::IronTool:
            DrawLineEx({ handX, handY + 2.0f }, { handX, handY - 5.0f }, 1.3f, Color{ 140, 90, 45, 255 });
            DrawRectangle((int)(handX + (facingLeft_ ? -3.0f : 0.0f)), (int)(handY - 5.0f), 3, 2, Color{ 205, 220, 240, 255 });
            break;
    }
}

void Caveman::DrawFloatingEmote(Vector2 drawPos) const {
    float bob = std::sin(animTime_ * 5.5f) * 1.6f;
    float ex = drawPos.x;
    float ey = drawPos.y - 26.0f + bob;

    // A) Panik bei Flucht hat höchste visuelle Priorität
    if (state_ == CavemanState::Fleeing) {
        DrawCircleV({ ex, ey }, 6.0f, Fade(BLACK, 0.75f));
        FontManager::DrawTextCenteredShadow("!", ex, ey - 7.0f, 13.0f, RED);
        return;
    }

    // B) Aktives Sozial-Emote (Streit, Flirt/Herz, Teilen, Gespräch, Krone, Idee)
    if (activeEmote_ != SocialEmote::None) {
        DrawCircleV({ ex, ey }, 5.5f, Fade(BLACK, 0.65f));
        switch (activeEmote_) {
            case SocialEmote::Heart:
                // Pinkes Pixel-Herz (Flirt / Partnerschaft / Geburt)
                DrawCircleV({ ex - 1.8f, ey - 1.0f }, 2.2f, PINK);
                DrawCircleV({ ex + 1.8f, ey - 1.0f }, 2.2f, PINK);
                DrawTriangle({ ex - 3.6f, ey - 0.2f }, { ex, ey + 3.8f }, { ex + 3.6f, ey - 0.2f }, PINK);
                break;
            case SocialEmote::Chat:
                // Weiße Sprechblase mit Punkten
                DrawRectangleRounded({ ex - 4.5f, ey - 3.5f, 9.0f, 6.5f }, 0.4f, 4, RAYWHITE);
                DrawPixel((int)ex - 2, (int)ey - 1, DARKGRAY);
                DrawPixel((int)ex,     (int)ey - 1, DARKGRAY);
                DrawPixel((int)ex + 2, (int)ey - 1, DARKGRAY);
                break;
            case SocialEmote::ShareFood:
                // Grüner Kreis + Rote Beere (Teilen / Altruismus)
                DrawCircleLines((int)ex, (int)ey, 4.5f, LIME);
                DrawCircleV({ ex, ey }, 2.5f, Color{ 230, 50, 70, 255 });
                break;
            case SocialEmote::Dispute:
                // Rote gekreuzte Konflikt-Klingen (Streit / Rivalität)
                DrawLineEx({ ex - 3.5f, ey - 3.5f }, { ex + 3.5f, ey + 3.5f }, 1.8f, RED);
                DrawLineEx({ ex + 3.5f, ey - 3.5f }, { ex - 3.5f, ey + 3.5f }, 1.8f, ORANGE);
                break;
            case SocialEmote::Crown:
                DrawCircleV({ ex, ey }, 3.2f, GOLD);
                DrawCircleLines((int)ex, (int)ey, 4.5f, YELLOW);
                break;
            case SocialEmote::Idea:
                DrawCircleV({ ex, ey - 0.5f }, 3.0f, YELLOW);
                DrawRectangle((int)ex - 1, (int)ey + 2, 2, 2, LIGHTGRAY);
                break;
            default:
                break;
        }
        return;
    }

    // C) Kritische Bedürfnis-Zustände ("Show, Don't Tell" für Hunger, Frieren, Erschöpfung)
    if (hunger_ >= 72.0f) {
        // Pulsierendes Hunger-Icon (rote Fleischkeule)
        DrawCircleV({ ex, ey }, 4.8f, Fade(BLACK, 0.65f));
        DrawCircleV({ ex - 1.0f, ey - 0.5f }, 2.6f, Color{ 235, 85, 60, 255 });
        DrawLineEx({ ex + 1.0f, ey + 1.0f }, { ex + 3.5f, ey + 3.0f }, 1.5f, RAYWHITE);
    } else if (warmth_ <= 35.0f) {
        // Blaues Kälte-/Eiskristall-Icon
        DrawCircleV({ ex, ey }, 4.8f, Fade(BLACK, 0.65f));
        DrawLineEx({ ex - 3.5f, ey }, { ex + 3.5f, ey }, 1.3f, SKYBLUE);
        DrawLineEx({ ex, ey - 3.5f }, { ex, ey + 3.5f }, 1.3f, SKYBLUE);
        DrawCircleV({ ex, ey }, 1.5f, WHITE);
    } else if (tiredness_ >= 82.0f || state_ == CavemanState::Sleeping) {
        FontManager::DrawTextCenteredShadow("zZ", ex, ey - 6.0f, 11.0f, Color{ 200, 185, 255, 255 });
    }
}

void Caveman::DrawDebugPath() const {
    if (path_.empty() || pathIndex_ >= path_.size()) return;
    for (size_t i = pathIndex_; i < path_.size() - 1; ++i) {
        DrawLineEx(path_[i], path_[i + 1], 1.2f, Fade(RoleColor(role_), 0.6f));
    }
}
