#include "caveman.h"
#include "config.h"
#include "event_log.h"
#include "sprite_sheet.h"
#include "gfx/sprite_animator.h"
#include "gfx/asset_manager.h"
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
#include <queue>
#include <cstring>
#include <cmath>

namespace {

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
        case CavemanState::BorderRaid:         return "BORDER_RAID";
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
Caveman::Caveman(int id, std::string name, Role role, Vector2 position, Gender gender)
    : id_(id), name_(std::move(name)), role_(role), gender_(gender), position_(position), targetPosition_(position), homeCampPos_(position) {
    if (role_ == Role::Gatherer && gender == Gender::Male) {
        gender_ = Gender::Female;
    }
    hunger_     = (float)GetRandomValue(5, 30);
    warmth_     = (float)GetRandomValue(85, 100);
    tiredness_  = (float)GetRandomValue(0, 25);
    ap_         = (float)GetRandomValue(85, 100);
    morale_     = (float)GetRandomValue(70, 85);
    socialNeed_ = (float)GetRandomValue(20, 50);

    // Lebensalter je nach Startrolle initialisieren
    if (role_ == Role::Child) {
        ageYears_ = (float)GetRandomValue(4, 9);
    } else if (role_ == Role::Elder) {
        ageYears_ = (float)GetRandomValue(56, 62);
        knowledgeBonus_ = 0.15f;
    } else {
        ageYears_ = (float)GetRandomValue(18, 28);
    }
    maxLifespan_ = (float)GetRandomValue(76, 88);

    // Unveränderliches psychologisches Profil & Evolutionäres Gehirn-Genom
    personality_ = Personality::GenerateRandom((int)role_);
    genome_ = BrainGenome::GenerateRandom();
    brain_.InitFromGenome(genome_);

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

void Caveman::GrowUp(WorldContext& ctx) {
    // Genetische Persönlichkeit bestimmt Berufung beim Heranwachsen
    bool hunterTrait = (personality_.dominance + personality_.aggression * 0.8f) >
                       (personality_.altruism + personality_.sociability * 0.5f);
    role_ = hunterTrait ? Role::Hunter : Role::Gatherer;

    // AUFTRAG PHASE 1.4: Namens-Korrektur beim Auswachsen
    // Aktualisiere den Namen! Nutze std::string::replace oder finde das Wort "Kind" im String this->name.
    // Ersetze "Kind" durch "Jäger" (wenn role == Hunter) oder "Sammlerin" / "Sammler" (wenn role == Gatherer).
    // Z. B. muss "Kind #36" im Code sauber zu "Jäger #36" umgeschrieben werden.
    size_t pos = name_.find("Kind");
    if (pos != std::string::npos) {
        if (role_ == Role::Hunter) {
            std::string rep = (gender_ == Gender::Female) ? "Jägerin" : "Jäger";
            name_.replace(pos, 4, rep);
        } else {
            std::string rep = (gender_ == Gender::Female) ? "Sammlerin" : "Sammler";
            name_.replace(pos, 4, rep);
        }
    } else {
        if (role_ == Role::Hunter) {
            name_ = ((gender_ == Gender::Female) ? "Jägerin #" : "Jäger #") + std::to_string(id_);
        } else {
            name_ = ((gender_ == Gender::Female) ? "Sammlerin #" : "Sammler #") + std::to_string(id_);
        }
    }

    TriggerEmote(SocialEmote::Crown, 4.0f);
    ctx.log.Add(TextFormat("%s ist herangewachsen (16 J.) und wirkt nun als %s!",
                           name_.c_str(), RoleName(role_)), RoleColor(role_));
}

// ============================================================================
//  Haupt-Update
// ============================================================================
void Caveman::Update(WorldContext& ctx, float dt) {
    if (state_ == CavemanState::Dead) return;

    animTime_ += dt;
    damageFlash_  = std::max(0.0f, damageFlash_ - dt);
    blockedTimer_ = std::max(0.0f, blockedTimer_ - dt);
    taskCommitmentTimer_ = std::max(0.0f, taskCommitmentTimer_ - dt);
    pregnancyCooldown_ = std::max(0.0f, pregnancyCooldown_ - dt);
    if (isPregnant_) {
        pregnancyTimer_ += dt;
    }
    if (teachCooldown > 0.0f) teachCooldown -= dt;
    if (teachCooldown < 0.0f) teachCooldown = 0.0f;
    for (auto it = blacklistedNodes_.begin(); it != blacklistedNodes_.end();) {
        it->second -= dt;
        if (it->second <= 0.0f) {
            it = blacklistedNodes_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = blacklistedTargets_.begin(); it != blacklistedTargets_.end();) {
        it->second -= dt;
        if (it->second <= 0.0f) {
            it = blacklistedTargets_.erase(it);
        } else {
            ++it;
        }
    }
    if (emoteTimer_ > 0.0f) {
        emoteTimer_ -= dt;
        if (emoteTimer_ <= 0.0f) activeEmote_ = SocialEmote::None;
    }

    // --- Notfall-Flucht-KI für Ertrinken und Verbrennen (AUFTRAG PHASE 3: Hard Overrides) ---
    const int curTx = World::ToTile(position_.x);
    const int curTy = World::ToTile(position_.y);
    const WaterType wt = ctx.world.GetWaterType(curTx, curTy);
    const TileType tt = ctx.world.GetTile(curTx, curTy);
    isSwimming_ = (wt != WaterType::None) || (tt == TILE_SHALLOW_WATER || tt == TILE_DEEP_WATER);

    // 1. Emergency Water Escape: Ertrinken verhindern bei erschöpfter Ausdauer (AP <= 10.0f)
    if (isSwimming_ && ap_ <= 10.0f) {
        ExecuteEmergencyWaterEscape(ctx.world, dt);
        // Falls im tiefen Wasser ohne AP: minimaler Ertrinkungsschaden getaktet alle 1.5s
        if (ap_ <= 0.0f && (tt == TILE_DEEP_WATER || wt == WaterType::Saltwater)) {
            drownDamageTimer_ += dt;
            if (drownDamageTimer_ >= 1.5f) {
                drownDamageTimer_ = 0.0f;
                TakeDamage(8.0f, "Ertrinken", ctx);
            }
        }
        if (hp_ <= 0.0f) Die(ctx);
        return;
    }

    // 2. Emergency Fire Escape: Sofortige Flucht von brennenden Kacheln
    Tile currentTile = ctx.world.GetTile(position_.x, position_.y);
    if (currentTile.isOnFire || ctx.world.IsTileOnFire(curTx, curTy)) {
        ExecuteEmergencyFireEscape(ctx.world, dt);
        if (ctx.env) {
            const TileEnvState& envSt = ctx.env->GetState(curTx, curTy);
            if (envSt.isBurning) {
                envDamageTimer_ += dt;
                if (envDamageTimer_ >= 1.0f) {
                    envDamageTimer_ = 0.0f;
                    TakeDamage(std::max(4.0f, 10.0f * envSt.fireIntensity), "Feuer", ctx);
                }
            }
        }
        if (hp_ <= 0.0f) Die(ctx);
        return;
    } else if (movementSpeedMultiplier_ == 2.0f && state_ != CavemanState::Fleeing) {
        movementSpeedMultiplier_ = 1.0f;
    }

    // 0) Demografie & Lebenszyklus (Kind -> Jäger/Sammler -> Ältester -> Alterstod)
    ageYears_ += dt * cfg::kAgeRate; // ca. 1.8 Lebensjahre pro Spielminute (ausbalancierte Generationen)
    if (role_ == Role::Child && ageYears_ >= 16.0f) {
        GrowUp(ctx);
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
        case CavemanState::BorderRaid:         UpdateBorderRaid(ctx, dt); break;
        case CavemanState::Sleeping:           UpdateSleeping(ctx, dt); break;
        case CavemanState::Eating:             UpdateEating(ctx, dt); break;
        case CavemanState::Dead:               break;
    }

    // Sanfte Separation-Force (Abstoßung) gegen Figuren-Stacking im Lager (BLOCK 2.1)
    if (ctx.allCavemen && state_ != CavemanState::Dead) {
        for (const auto& other : *ctx.allCavemen) {
            if (other.Id() == id_ || !other.IsAlive()) continue;
            float dist = Vector2Distance(position_, other.Position());
            if (dist < 8.0f) {
                Vector2 diff = Vector2Subtract(position_, other.Position());
                float len = Vector2Length(diff);
                Vector2 dir;
                if (len < 0.001f) {
                    float angle = (float)(id_ * 45 % 360) * (PI / 180.0f);
                    dir = { std::cos(angle), std::sin(angle) };
                } else {
                    dir = Vector2Scale(diff, 1.0f / len);
                }
                float push = (8.0f - dist) * 12.0f * dt;
                Vector2 cand = Vector2Add(position_, Vector2Scale(dir, push));
                if (ctx.world.IsWalkable(cand)) {
                    position_ = cand;
                }
            }
        }
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
        starvationTimer_ += dt;
        if (starvationTimer_ >= 2.5f) {
            starvationTimer_ = 0.0f;
            TakeDamage(4.0f, "Hunger", ctx);
        }
    } else {
        starvationTimer_ = 0.0f;
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
        coldDamageTimer_ = 0.0f;
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
            coldDamageTimer_ += dt;
            if (coldDamageTimer_ >= 2.5f) {
                coldDamageTimer_ = 0.0f;
                TakeDamage(4.0f, "Kälte", ctx);
            }
        } else {
            coldDamageTimer_ = 0.0f;
        }
    }

    // 3b. Direkter Umweltschaden (Feuer & Starkstrom im Wasser) getaktet alle 1.0s
    if (ctx.env) {
        const int tx = World::ToTile(position_.x);
        const int ty = World::ToTile(position_.y);
        const TileEnvState& envSt = ctx.env->GetState(tx, ty);
        if (envSt.isBurning || envSt.electrifiedTimer > 0.0f) {
            envDamageTimer_ += dt;
            if (envDamageTimer_ >= 1.0f) {
                envDamageTimer_ = 0.0f;
                if (envSt.isBurning) {
                    TakeDamage(std::max(4.0f, 10.0f * envSt.fireIntensity), "Feuer", ctx);
                } else if (envSt.electrifiedTimer > 0.0f) {
                    TakeDamage(8.0f, "Starkstrom", ctx);
                }
            }
        } else {
            envDamageTimer_ = 0.0f;
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

        // Wasser-Notfall-Rettung (BLOCK 3.5): Sobald AP < 25% beim Schwimmen erreicht wird,
        // bewege die Figur per direktem Vektor-Move auf die nächstgelegene Landkachel zu!
        if (ap_ < 25.0f) {
            int safeX = curTx, safeY = curTy;
            bool foundLand = false;
            float bestDistSq = 9999.0f;
            for (int dy = -10; dy <= 10; ++dy) {
                for (int dx = -10; dx <= 10; ++dx) {
                    int nx = curTx + dx;
                    int ny = curTy + dy;
                    if (!ctx.world.IsInBounds(nx, ny)) continue;
                    TileType t = ctx.world.GetTile(nx, ny);
                    WaterType w = ctx.world.GetWaterType(nx, ny);
                    if (t != TILE_DEEP_WATER && t != TILE_SHALLOW_WATER && w == WaterType::None && ctx.world.IsWalkable(World::TileCenter(nx, ny))) {
                        float dsq = (float)(dx * dx + dy * dy);
                        if (dsq < bestDistSq) {
                            bestDistSq = dsq;
                            safeX = nx; safeY = ny;
                            foundLand = true;
                        }
                    }
                }
            }
            if (foundLand) {
                Vector2 landPos = World::TileCenter(safeX, safeY);
                Vector2 dir = Vector2Subtract(landPos, position_);
                float len = Vector2Length(dir);
                if (len > 0.5f) {
                    dir = Vector2Scale(dir, 1.0f / len);
                    float escapeSpeed = 24.0f; // Direkter Notfall-Schwimmzug Richtung Land
                    position_ = Vector2Add(position_, Vector2Scale(dir, escapeSpeed * dt));
                }
            }
        }

        // Ertrinken: Fällt AP im tiefen Wasser auf 0, verliert der Caveman alle 1.5s spürbar HP
        if (ap_ <= 0.0f && (tt == TILE_DEEP_WATER || wt == WaterType::Saltwater)) {
            drownDamageTimer_ += dt;
            if (drownDamageTimer_ >= 1.5f) {
                drownDamageTimer_ = 0.0f;
                TakeDamage(8.0f, "Ertrinken", ctx);
            }
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
        } else {
            drownDamageTimer_ = 0.0f;
        }
    } else {
        drownDamageTimer_ = 0.0f;
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

    // 6. Loyalität zum Stamm (beeinflusst von Moral, Bedürfnissen und Clan-Wohlstand)
    float targetLoyalty = 75.0f;
    if (socialRank_ == (int)SocialRank::Chief) {
        targetLoyalty = 100.0f;
    } else {
        if (morale_ > 65.0f) targetLoyalty += 15.0f;
        else if (morale_ < 35.0f) targetLoyalty -= 35.0f;

        if (hunger_ > 75.0f) targetLoyalty -= 20.0f;
        if (warmth_ < 35.0f) targetLoyalty -= 15.0f;
        if (personality_.dominance > 0.35f) targetLoyalty -= 15.0f; // Machthungrige neigen eher zur Rebellion

        int chiefId = ctx.tribe.Hierarchy().GetChiefId();
        if (chiefId >= 0 && chiefId != id_) {
            float respectToChief = ctx.tribe.Relationships().GetRespect(id_, chiefId);
            if (respectToChief < 0.0f) targetLoyalty -= 20.0f;
            else if (respectToChief > 50.0f) targetLoyalty += 15.0f;
        }
    }
    targetLoyalty = std::clamp(targetLoyalty, 5.0f, 100.0f);
    loyalty_ += (targetLoyalty - loyalty_) * 0.05f * dt;

    // 7. Passive Lebensregeneration bei guter Verfassung (satt & warm)
    if (hunger_ < 50.0f && warmth_ > 50.0f && hp_ < cfg::kMaxStat) {
        hp_ = std::min(cfg::kMaxStat, hp_ + 0.6f * dt);
    }
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
    int intDamage = (int)std::round(amount);
    if (hp_ > 0.0f && intDamage > 0) {
        ctx.log.Add(TextFormat("%s wurde verletzt (%s, -%d HP)", name_.c_str(), cause, intDamage),
                    { 255, 160, 120, 255 });
    }
}

void Caveman::TakeDamageNonLethal(float amount) {
    if (IsDead() || amount <= 0.0f) return;
    hp_ = std::max(20.0f, hp_ - amount);
    damageFlash_ = 0.25f;
}

bool Caveman::IsTargetBlacklisted(int targetId) const {
    auto it = blacklistedTargets_.find(targetId);
    return it != blacklistedTargets_.end() && it->second > 0.0f;
}

void Caveman::BlacklistTarget(int targetId, float duration) {
    if (targetId >= 0) {
        blacklistedTargets_[targetId] = duration;
    }
}

bool Caveman::IsNodeBlacklisted(int nodeId) const {
    if (IsTargetBlacklisted(nodeId)) return true;
    auto it = blacklistedNodes_.find(nodeId);
    return it != blacklistedNodes_.end() && it->second > 0.0f;
}

void Caveman::BlacklistNode(int nodeId, float duration) {
    if (nodeId >= 0) {
        blacklistedNodes_[nodeId] = duration;
        blacklistedTargets_[nodeId] = duration;
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

    // Task-Commitment (Hysterese): Nicht zwischen Aufgaben hin- und herflippen!
    // Wenn die Figur aktiv an einer Aufgabe arbeitet und der Commitment-Timer noch läuft,
    // bleibt sie bei der aktuellen Aufgabe, außer es liegt ein Notfall (HP < 25 oder akuter Hunger/Kälte) vor.
    bool emergency = (hp_ < 25.0f || hunger_ >= 85.0f || warmth_ <= 20.0f);
    if (!taskDone_ && taskCommitmentTimer_ > 0.0f && !emergency) {
        return;
    }

    if (taskDone_ || (dec.score > lastDecisionScore_ + 0.20f && IsInterruptible())) {
        if (!EnterState(dec.state, dec.resource, dec.targetBuildingId, dec.targetNodeId, dec.targetCharacterId, dec.targetPoiId, ctx)) {
            // Fehlgeschlagenes Betreten: Ziel für 15s sperren, nicht denselben Auftrag im nächsten Frame annehmen!
            int failedTarget = (dec.targetBuildingId >= 0) ? dec.targetBuildingId :
                               ((dec.targetPoiId >= 0) ? dec.targetPoiId : dec.targetNodeId);
            if (failedTarget >= 0) {
                BlacklistTarget(failedTarget, 15.0f);
            }
            thinkTimer_ = 0.5f;
            EnterState(CavemanState::Idle, ResourceType::Bush, -1, -1, -1, -1, ctx);
            stateTimer_ = 0.5f;
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

    // Snapshot der Vitalwerte vor Beginn der Aufgabe für Reinforcement Learning
    taskStartState_ = state;
    taskStartHp_ = hp_;
    taskStartHunger_ = hunger_;
    taskStartWarmth_ = warmth_;
    taskStartTiredness_ = tiredness_;

    switch (state) {
        case CavemanState::Idle: {
            stateTimer_ = (float)GetRandomValue(15, 30) / 10.0f;
            return true;
        }
        case CavemanState::Wandering: {
            stateTimer_ = (float)GetRandomValue(40, 80) / 10.0f;
            Vector2 dest;
            for (int att = 0; att < 12; ++att) {
                if (ctx.world.FindRandomWalkablePosition(position_, 55.0f, dest, 10)) {
                    int tx = World::ToTile(dest.x), ty = World::ToTile(dest.y);
                    int owner = ctx.world.GetOwnerTribeId(tx, ty);
                    if (owner == clanId_ || owner == -1) {
                        return SetDestination(ctx, dest);
                    }
                }
            }
            if (ctx.world.FindRandomWalkablePosition(ctx.tribe.CampPosition(), 35.0f, dest, 15)) {
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
            ResourceNode* prey = ctx.resources.Get(targetNodeId);
            if (!prey || !prey->IsAvailable()) {
                BlacklistNode(targetNodeId, 10.0f);
                return false;
            }
            if (!SetDestination(ctx, prey->position)) {
                BlacklistNode(targetNodeId, 15.0f);
                return false;
            }
            targetNode_ = targetNodeId;
            ctx.resources.Claim(targetNode_);
            targetGeneration_ = prey->generation;
            repathTimer_ = 0.5f;
            taskCommitmentTimer_ = 3.5f;
            return true;
        }
        case CavemanState::GatheringFood:
        case CavemanState::GatheringMaterials: {
            if (state == CavemanState::GatheringFood && targetBuildingId >= 0) {
                targetBuilding_ = targetBuildingId;
                Building* b = ctx.tribe.Buildings().Get(targetBuilding_);
                if (!b || !b->NeedsFarmTending()) return false;
                if (!SetDestination(ctx, b->position)) return false;
                b->occupants++;
                taskCommitmentTimer_ = 3.5f;
                return true;
            }
            ResourceNode* node = ctx.resources.Get(targetNodeId);
            if (!node || !node->IsAvailable()) {
                BlacklistNode(targetNodeId, 10.0f);
                return false;
            }
            if (!SetDestination(ctx, node->position)) {
                BlacklistNode(targetNodeId, 15.0f);
                return false;
            }
            targetNode_ = targetNodeId;
            ctx.resources.Claim(targetNode_);
            targetGeneration_ = node->generation;
            taskCommitmentTimer_ = 3.5f;
            return true;
        }
        case CavemanState::Building: {
            targetBuilding_ = targetBuildingId;
            Building* b = ctx.tribe.Buildings().Get(targetBuilding_);
            if (!b || (!b->NeedsWork() && !b->NeedsRepair())) {
                BlacklistTarget(targetBuildingId, 10.0f);
                return false;
            }
            if (!SetDestination(ctx, b->position)) {
                BlacklistTarget(targetBuildingId, 15.0f);
                return false;
            }
            b->builders++;
            taskCommitmentTimer_ = 3.5f;
            return true;
        }
        case CavemanState::Crafting: {
            targetBuilding_ = targetBuildingId;
            Building* b = ctx.tribe.Buildings().Get(targetBuilding_);
            if (!b || (b->type != BuildingType::CraftBench && b->type != BuildingType::Kiln) || !b->finished) {
                BlacklistTarget(targetBuildingId, 10.0f);
                return false;
            }
            if (!SetDestination(ctx, b->position)) {
                BlacklistTarget(targetBuildingId, 15.0f);
                return false;
            }
            b->occupants++;
            taskCommitmentTimer_ = 3.5f;
            return true;
        }
        case CavemanState::WarmingUp: {
            targetBuilding_ = targetBuildingId;
            Vector2 dest = ctx.tribe.CampPosition();
            if (Building* b = ctx.tribe.Buildings().Get(targetBuilding_)) {
                dest = b->position;
            }
            // Verteile wärmende/wartende Figuren kreisförmig auf die 8 Nachbarkacheln um das Feuer
            float angle = (float)(id_ % 8) * (2.0f * PI / 8.0f);
            Vector2 offset = { std::cos(angle) * (TILE_SIZE * 1.15f), std::sin(angle) * (TILE_SIZE * 1.15f) };
            Vector2 targetPos = Vector2Add(dest, offset);
            if (ctx.world.IsWalkable(targetPos)) {
                dest = targetPos;
            }
            if (!SetDestination(ctx, dest)) return false;
            taskCommitmentTimer_ = 3.5f;
            return true;
        }
        case CavemanState::Socializing: {
            targetSocialPartner_ = targetCharacterId;
            if (ctx.allCavemen) {
                for (const auto& c : *ctx.allCavemen) {
                    if (c.Id() == targetSocialPartner_ && !c.IsDead()) {
                        taskCommitmentTimer_ = 3.5f;
                        return SetDestination(ctx, c.Position());
                    }
                }
            }
            return false;
        }
        case CavemanState::Expedition: {
            targetPoi_ = targetPoiId;
            PointOfInterest* poi = ctx.tribe.POIs().Get(targetPoi_);
            if (!poi || !poi->CanBeExplored()) {
                BlacklistTarget(targetPoiId, 10.0f);
                return false;
            }

            if (!SetDestination(ctx, poi->position)) {
                BlacklistTarget(targetPoiId, 15.0f);
                return false;
            }

            carryingTorch_ = poi->gear.torch;
            if (poi->gear.torch && ctx.tribe.Stock().torches > 0) ctx.tribe.Stock().torches--;
            if (poi->gear.provisions && ctx.tribe.Stock().food > 0) ctx.tribe.Stock().food--;

            poi->assignedExplorerId = id_;
            taskCommitmentTimer_ = 3.5f;
            ctx.log.Add(TextFormat("%s bricht zur Expedition nach '%s' auf!", name_.c_str(), poi->name.c_str()), GOLD);
            return true;
        }
        case CavemanState::BorderRaid: {
            stateTimer_ = 6.0f;
            int tx = targetNodeId;
            int ty = targetCharacterId;
            if (ctx.world.IsInBounds(tx, ty)) {
                taskCommitmentTimer_ = 3.5f;
                return SetDestination(ctx, World::TileCenter(tx, ty));
            }
            return false;
        }
        case CavemanState::Sleeping: {
            targetBuilding_ = targetBuildingId;
            Building* b = ctx.tribe.Buildings().Get(targetBuilding_);
            if (b && b->type == BuildingType::Hut && b->IsAvailableShelter()) {
                if (SetDestination(ctx, b->position)) {
                    b->occupants++;
                    sleepingInShelter_ = true;
                    taskCommitmentTimer_ = 3.5f;
                    return true;
                }
                BlacklistTarget(targetBuildingId, 10.0f);
            }
            targetBuilding_ = -1;
            sleepingInShelter_ = false;
            Vector2 camp = ctx.tribe.CampPosition();
            Vector2 spot = camp;
            bool foundSpot = false;
            int ctxCamp = World::ToTile(camp.x);
            int ctyCamp = World::ToTile(camp.y);
            for (int att = 0; att < 24; ++att) {
                float dist = (float)GetRandomValue(24, 40); // 1.5 - 2.5 Kacheln
                float angle = (float)GetRandomValue(0, 360) * DEG2RAD;
                Vector2 candidate = { camp.x + std::cos(angle) * dist, camp.y + std::sin(angle) * dist };
                int tx = World::ToTile(candidate.x);
                int ty = World::ToTile(candidate.y);
                // Niemals direkt auf der Feuer-Kachel schlafen!
                if ((tx != ctxCamp || ty != ctyCamp) && ctx.world.IsWalkableTile(tx, ty)) {
                    spot = World::TileCenter(tx, ty);
                    foundSpot = true;
                    break;
                }
            }
            if (!foundSpot) {
                int dxOffsets[] = { 1, -1, 0, 0, 1, -1, 1, -1 };
                int dyOffsets[] = { 0, 0, 1, -1, 1, 1, -1, -1 };
                for (int i = 0; i < 8; ++i) {
                    int nx = ctxCamp + dxOffsets[i];
                    int ny = ctyCamp + dyOffsets[i];
                    if (ctx.world.IsWalkableTile(nx, ny)) {
                        spot = World::TileCenter(nx, ny);
                        foundSpot = true;
                        break;
                    }
                }
            }
            taskCommitmentTimer_ = 3.5f;
            return SetDestination(ctx, spot);
        }
        case CavemanState::Eating: {
            taskCommitmentTimer_ = 3.5f;
            return SetDestination(ctx, ctx.tribe.StockpilePosition());
        }
        case CavemanState::Dead:
            return true;
    }
    return false;
}

void Caveman::ExitState(WorldContext& ctx) {
    EvaluateActionOutcomeAndReward(ctx);
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

void Caveman::EvaluateActionOutcomeAndReward(WorldContext& ctx) {
    if (taskStartState_ == CavemanState::Idle || taskStartState_ == CavemanState::Dead) return;

    // Vitalwert-Deltas seit Beginn der Aufgabe
    float deltaHp = hp_ - taskStartHp_;
    float deltaHunger = hunger_ - taskStartHunger_;     // Negativ = Hunger verringert (gut!)
    float deltaWarmth = warmth_ - taskStartWarmth_;     // Positiv = Aufgewärmt (gut!)
    float deltaTiredness = tiredness_ - taskStartTiredness_; // Negativ = Erholt (gut!)

    // Effektive Lernrate: Alpha * Neuroplastizität * Stammesfaktor
    float plasticity = genome_.GetGene(BrainGene::Neuroplasticity);
    float tribeMult = ctx.tribe.CognitiveMultiplier();
    float alphaEff = 0.05f * plasticity * tribeMult;

    switch (taskStartState_) {
        case CavemanState::Eating: {
            if (deltaHunger < -15.0f || hunger_ < 25.0f) {
                brain_.ApplyReward(BrainGene::FoodCaution, +1.0f, alphaEff, "Satt gegessen (+Hungervorsorge)");
            }
            break;
        }
        case CavemanState::Sleeping: {
            if (deltaTiredness < -25.0f || tiredness_ < 25.0f) {
                brain_.ApplyReward(BrainGene::SleepRest, +0.9f, alphaEff, "Ausgeruht & erholt (+Schlafpriorität)");
            }
            if (deltaWarmth < -25.0f) {
                brain_.ApplyReward(BrainGene::WarmthPriority, +1.2f, alphaEff, "Kälte im Schlaf gespürt (+Wärme)");
            }
            break;
        }
        case CavemanState::WarmingUp: {
            if (deltaWarmth > +20.0f || warmth_ > 75.0f) {
                brain_.ApplyReward(BrainGene::WarmthPriority, +1.1f, alphaEff, "Am Feuer aufgewärmt (+Wärmepriorität)");
            }
            break;
        }
        case CavemanState::GatheringFood: {
            if (deltaHp < -6.0f) {
                // Bei Nahrungssuche fast erfroren oder verhungert
                brain_.ApplyReward(BrainGene::GatherFood, -1.0f, alphaEff, "Erschöpfung bei Nahrungssuche");
                brain_.ApplyReward(BrainGene::FoodCaution, +1.4f, alphaEff, "Früher Hunger erkannt (+Vorsicht)");
            } else if (carryAmount_ > 0 || deltaHunger < -10.0f) {
                brain_.ApplyReward(BrainGene::GatherFood, +0.9f, alphaEff, "Ertragreiche Ernte (+Sammellust)");
            }
            break;
        }
        case CavemanState::Hunting: {
            if (deltaHp < -20.0f) {
                // Schwere Jagdverletzung erlitten
                brain_.ApplyReward(BrainGene::Hunt, -1.2f, alphaEff, "Schwere Jagdverletzung (-Jagdtrieb)");
                brain_.ApplyReward(BrainGene::PredatorCaution, +1.8f, alphaEff, "Furcht vor Raubtieren gelernt (+Vorsicht)");
            } else if (carryAmount_ > 0 || deltaHunger < -10.0f) {
                brain_.ApplyReward(BrainGene::Hunt, +1.1f, alphaEff, "Erfolgreiche Jagdbeute (+Jagdlust)");
            }
            break;
        }
        case CavemanState::Building: {
            if (deltaHp < -6.0f) {
                // Trotz Kälte/Hunger gebaut und Schaden genommen!
                brain_.ApplyReward(BrainGene::Build, -1.5f, alphaEff, "Schaden beim Bauen erlitten (-Bautrieb)");
                if (hunger_ > 75.0f) {
                    brain_.ApplyReward(BrainGene::FoodCaution, +1.8f, alphaEff, "Hunger vor Bauarbeit priorisieren");
                }
                if (warmth_ < 25.0f) {
                    brain_.ApplyReward(BrainGene::WarmthPriority, +1.8f, alphaEff, "Frost vor Bauarbeit meiden");
                }
            } else {
                brain_.ApplyReward(BrainGene::Build, +0.7f, alphaEff, "Siedlung ausgebaut (+Bautrieb)");
            }
            break;
        }
        case CavemanState::Crafting: {
            if (deltaHp < -6.0f) {
                brain_.ApplyReward(BrainGene::Craft, -1.3f, alphaEff, "Schaden beim Handwerk (-Handwerk)");
                brain_.ApplyReward(BrainGene::FoodCaution, +1.4f, alphaEff, "Lebensbedürfnisse wichtiger als Handwerk");
            } else {
                brain_.ApplyReward(BrainGene::Craft, +0.8f, alphaEff, "Werkzeug gefertigt (+Handwerk)");
            }
            break;
        }
        case CavemanState::GatheringMaterials: {
            if (deltaHp < -6.0f) {
                brain_.ApplyReward(taskResource_ == ResourceType::Rock ? BrainGene::GatherStone : BrainGene::GatherWood,
                                   -1.1f, alphaEff, "Überanstrengung bei Rohstoffsuche");
                brain_.ApplyReward(BrainGene::FoodCaution, +1.3f, alphaEff, "Früher auf Nahrung achten");
            } else if (carryAmount_ > 0) {
                brain_.ApplyReward(taskResource_ == ResourceType::Rock ? BrainGene::GatherStone : BrainGene::GatherWood,
                                   +0.8f, alphaEff, "Rohstoffe ins Lager gebracht");
            }
            break;
        }
        case CavemanState::Expedition: {
            if (deltaHp < -15.0f) {
                brain_.ApplyReward(BrainGene::Expedition, -1.3f, alphaEff, "Gefahren der Wildnis gespürt (-Leichtsinn)");
                brain_.ApplyReward(BrainGene::PredatorCaution, +1.4f, alphaEff, "Vorsicht bei Expeditionen");
            } else {
                brain_.ApplyReward(BrainGene::Expedition, +1.2f, alphaEff, "Neues Territorium erkundet (+Entdecker)");
            }
            break;
        }
        case CavemanState::BorderRaid: {
            if (deltaHp < -15.0f) {
                brain_.ApplyReward(BrainGene::BorderRaid, -1.1f, alphaEff, "Grenzkampf-Verletzung erlitten");
                brain_.ApplyReward(BrainGene::PredatorCaution, +1.2f, alphaEff, "Grenzkampf-Vorsicht");
            } else {
                brain_.ApplyReward(BrainGene::BorderRaid, +0.9f, alphaEff, "Grenzland verteidigt (+Wachsamkeit)");
            }
            break;
        }
        case CavemanState::Socializing: {
            brain_.ApplyReward(BrainGene::Social, +0.6f, alphaEff, "Sozialer Zusammenhalt gestärkt");
            break;
        }
        default:
            break;
    }
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
    DeathCause cause = DeathCause::OldAge;
    std::string ldc = lastDamageCause_;
    if (ldc == "Hunger") cause = DeathCause::Starvation;
    else if (ldc == "Kälte") cause = DeathCause::Hypothermia;
    else if (ldc == "Wildtier") cause = DeathCause::AnimalAttack;
    else if (ldc == "Streit") cause = DeathCause::CombatDuel;
    else if (ldc == "Ertrinken") cause = DeathCause::Drowning;
    else if (ldc == "MineCollapse" || ldc == "Minenunglück") cause = DeathCause::MineCollapse;
    else if (ldc == "Altersschwäche") cause = DeathCause::OldAge;
    else if (ldc == "Feuer") cause = DeathCause::Hypothermia;
    Die(cause, ctx);
}

void Caveman::Die(DeathCause cause, WorldContext& ctx, const char* killerName) {
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

    std::string clanName = ctx.tribe.Name();
    switch (cause) {
        case DeathCause::Starvation:
            ctx.log.Add(TextFormat("%s (%s) ist den Hungertod gestorben!", name_.c_str(), clanName.c_str()), { 255, 80, 80, 255 });
            break;
        case DeathCause::Hypothermia:
            ctx.log.Add(TextFormat("%s (%s) ist an Unterkühlung erfroren!", name_.c_str(), clanName.c_str()), { 120, 180, 255, 255 });
            break;
        case DeathCause::AnimalAttack:
            ctx.log.Add(TextFormat("%s (%s) wurde von einem Wildtier getötet!", name_.c_str(), clanName.c_str()), { 255, 90, 60, 255 });
            break;
        case DeathCause::CombatDuel:
            ctx.log.Add(TextFormat("%s (%s) ist im rituellen Stammes-Zweikampf gefallen!", name_.c_str(), clanName.c_str()), { 255, 120, 120, 255 });
            break;
        case DeathCause::War:
            ctx.log.Add(TextFormat("%s (%s) fiel im Kampf gegen %s!", name_.c_str(), clanName.c_str(), (killerName && killerName[0]) ? killerName : "einen feindlichen Stamm"), { 255, 60, 60, 255 });
            break;
        case DeathCause::Drowning:
            ctx.log.Add(TextFormat("%s (%s) ist im tiefen Wasser ertrunken!", name_.c_str(), clanName.c_str()), { 80, 150, 255, 255 });
            break;
        case DeathCause::MineCollapse:
            ctx.log.Add(TextFormat("MINEN-UNGLÜCK: Ein tiefer Stollen ist für %s (%s) eingestürzt!", name_.c_str(), clanName.c_str()), { 255, 140, 50, 255 });
            break;
        case DeathCause::OldAge:
            ctx.log.Add(TextFormat("%s (%s) ist friedlich an Altersschwäche verstorben (%.0f J.).", name_.c_str(), clanName.c_str(), ageYears_), { 200, 200, 210, 255 });
            break;
    }

    // Meilenstein E: Ahnengrab am äußeren Friedhofsrand des Dorfes anlegen (BLOCK 4.2)
    Vector2 graveSpot = position_;
    if (!ctx.tribe.FindGraveyardSpot(ctx.world, graveSpot)) {
        ctx.tribe.FindBuildingSpotAround(ctx.world, ctx.tribe.CampPosition(), graveSpot);
    }
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

    // Nahkampf-Reichweite auf 1.6 Kacheln (25.6px) bzw. Fernkampf-Distanz bei Speer (36px) und Bogen (56px)
    float attackRange = (weaponTier_ == WeaponTier::CompositeBow) ? 56.0f :
                        (weaponTier_ == WeaponTier::FlintSpear)   ? 36.0f : 25.6f;

    if (phase_ == TaskPhase::MovingToTarget) {
        float distToPrey = Vector2Distance(position_, prey->position);
        if (distToPrey <= attackRange) {
            phase_ = TaskPhase::Working;
            prey->engaged = true;
            path_.clear();
            workTimer_ = 0.0f;
            return;
        }
        repathTimer_ -= dt;
        if (repathTimer_ <= 0.0f) {
            repathTimer_ = 0.6f;
            if (Vector2Distance(prey->position, targetPosition_) > (float)TILE_SIZE) {
                SetDestination(ctx, prey->position);
            }
        }
        const MoveResult r = MoveAlongPath(ctx, dt);
        if (r == MoveResult::Failed) {
            if (distToPrey > attackRange * 1.5f) {
                BlacklistTarget(targetNode_, 15.0f);
                BlacklistNode(targetNode_, 15.0f);
                ReleaseNode(ctx);
                thinkTimer_ = 0.5f;
                FinishTask();
            } else {
                phase_ = TaskPhase::Working;
                prey->engaged = true;
                path_.clear();
                workTimer_ = 0.0f;
            }
        } else if (r == MoveResult::Arrived) {
            if (distToPrey <= attackRange) {
                phase_ = TaskPhase::Working;
                prey->engaged = true;
                path_.clear();
                workTimer_ = 0.0f;
            } else {
                SetDestination(ctx, prey->position);
            }
        }
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
        if (ctx.tribe.Skills().AddPractice("animal_husbandry", 16.0f, ctx.log, name_) ||
            ctx.tribe.Skills().AddPractice("tailoring", 16.0f, ctx.log, name_) ||
            ctx.tribe.Skills().AddPractice("torches", 10.0f, ctx.log, name_)) {
            TriggerEmote(SocialEmote::Idea, 4.0f);
        }
        ctx.log.Add(TextFormat("%s hat ein Wildtier erlegt (+%d Fleisch, +1 Fell, +1 Knochen)", name_.c_str(), meat),
                    { 230, 150, 100, 255 });
        int carryTotal = meat * GetCarryCapacityMultiplier();
        if (hunger_ >= 70.0f && carryTotal >= 1) {
            hunger_ = std::max(0.0f, hunger_ - cfg::kHungerPerFood);
            carryTotal--;
        }
        if (carryTotal > 0) {
            StartReturning(ctx, CarryType::Food, carryTotal);
        } else {
            FinishTask();
        }
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
                    if (hunger_ >= 70.0f && harvested >= 1) {
                        hunger_ = std::max(0.0f, hunger_ - cfg::kHungerPerFood);
                        harvested--;
                    }
                    if (harvested > 0) {
                        StartReturning(ctx, CarryType::Food, harvested);
                    } else {
                        FinishTask();
                    }
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
                    if (ctx.tribe.Skills().AddPractice("animal_husbandry", 20.0f, ctx.log, name_) ||
                        ctx.tribe.Skills().AddPractice("tailoring", 18.0f, ctx.log, name_) ||
                        ctx.tribe.Skills().AddPractice("agriculture", 16.0f, ctx.log, name_)) {
                        TriggerEmote(SocialEmote::Idea, 4.0f);
                    }
                    ctx.log.Add(TextFormat("%s versorgt das Tiergehege (+%d Nahrung, +2 Wolle/Fell, +1 Knochen)!",
                                           name_.c_str(), meatAndMilk),
                                { 235, 215, 165, 255 });
                    ReleaseBuilding(ctx);
                    if (hunger_ >= 70.0f && meatAndMilk >= 1) {
                        hunger_ = std::max(0.0f, hunger_ - cfg::kHungerPerFood);
                        meatAndMilk--;
                    }
                    if (meatAndMilk > 0) {
                        StartReturning(ctx, CarryType::Food, meatAndMilk);
                    } else {
                        FinishTask();
                    }
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
            BlacklistNode(targetNode_, 15.0f);
            ReleaseNode(ctx);
            thinkTimer_ = 0.5f;
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

        // Bei akutem Hunger (>= 65%) isst die Figur 1 Portion direkt vor Ort, um nicht auf dem Rückweg zu verhungern
        if (carry == CarryType::Food && hunger_ >= 65.0f && got >= 1) {
            hunger_ = std::max(0.0f, hunger_ - cfg::kHungerPerFood);
            int remaining = got - 1;
            if (remaining > 0) {
                StartReturning(ctx, carry, remaining);
            } else {
                FinishTask();
            }
            return;
        }

        StartReturning(ctx, carry, got);
    }
}

void Caveman::UpdateBuilding(WorldContext& ctx, float dt) {
    Building* b = ctx.tribe.Buildings().Get(targetBuilding_);
    if (!b || (!b->NeedsWork() && !b->NeedsRepair())) { FinishTask(); return; }

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
        float sMate  = SocialActionEvaluator::CalculateMateScore(*this, *partner, ctx, dummy);
        float sTeach = SocialActionEvaluator::ScoreTeachChild(*this, *partner, ctx, dummy);
        float sFood  = SocialActionEvaluator::ScoreShareFood(*this, *partner, ctx, dummy);
        float sFlirt = SocialActionEvaluator::ScoreFlirt(*this, *partner, ctx, dummy);
        float sSoc   = SocialActionEvaluator::ScoreSocialize(*this, *partner, ctx, dummy);
        float sDisp  = SocialActionEvaluator::ScoreDispute(*this, *partner, ctx, dummy);

        if (sMate >= sTeach && sMate >= sFood && sMate >= sFlirt && sMate >= sSoc && sMate >= sDisp && sMate > 0.05f) {
            SocialActionEvaluator::ExecuteMating(*this, *partner, ctx);
        } else if (sTeach >= sFood && sTeach >= sFlirt && sTeach >= sSoc && sTeach >= sDisp && sTeach > 0.05f) {
            SocialActionEvaluator::ExecuteTeachChild(*this, *partner, ctx);
        } else if (sFood >= sFlirt && sFood >= sSoc && sFood >= sDisp && sFood > 0.05f) {
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

void Caveman::UpdateBorderRaid(WorldContext& ctx, float dt) {
    stateTimer_ -= dt;

    // 1. Auf dem Weg zum oder am Ziel bewegen
    MoveAlongPath(ctx, dt);

    // 2. Feinde/Verteidiger in Reichweite prüfen (Kampf!)
    Caveman* enemy = nullptr;
    float closestDist = 36.0f;

    if (ctx.allCavemen) {
        for (auto& other : *ctx.allCavemen) {
            if (other.IsDead() || other.ClanId() == clanId_) continue;
            float d = Vector2Distance(position_, other.Position());
            if (d < closestDist) {
                closestDist = d;
                enemy = &other;
            }
        }
    }

    if (enemy) {
        // Grenzkampf!
        TriggerEmote(SocialEmote::Dispute, 2.5f);
        enemy->TriggerEmote(SocialEmote::Dispute, 2.5f);

        // Kampfschaden basierend auf Waffen
        float myDmgMult = 1.0f;
        if (weaponTier_ == WeaponTier::FlintSpear) myDmgMult = 1.6f;
        else if (weaponTier_ == WeaponTier::CompositeBow) myDmgMult = 1.9f;
        else if (weaponTier_ == WeaponTier::MetalBlade) myDmgMult = 2.4f;

        float enemyDmgMult = 1.0f;
        if (enemy->GetWeaponTier() == WeaponTier::FlintSpear) enemyDmgMult = 1.6f;
        else if (enemy->GetWeaponTier() == WeaponTier::CompositeBow) enemyDmgMult = 1.9f;
        else if (enemy->GetWeaponTier() == WeaponTier::MetalBlade) enemyDmgMult = 2.4f;

        float dps = 14.0f * dt;
        enemy->TakeDamage(dps * myDmgMult, "Grenzkampf", ctx);
        TakeDamage(dps * 0.85f * enemyDmgMult, "Grenzkampf", ctx);

        // Bei schwerer Verletzung (< 25 HP) flieht der Krieger zurück
        if (hp_ < 25.0f) {
            EnterState(CavemanState::Fleeing, ResourceType::Bush, -1, -1, -1, -1, ctx);
            return;
        }
    } else {
        // Belagerung feindlicher Grenz-Anker (BorderTotem, Outpost, Watchtower)
        Building* targetAnchor = nullptr;
        Tribe* enemyTribe = nullptr;
        float closestAnchorDist = 48.0f;

        if (ctx.allTribes) {
            for (auto& otherTribe : *ctx.allTribes) {
                if (otherTribe->Id() == clanId_) continue;
                for (auto& b : otherTribe->Buildings().All()) {
                    if (!b.finished || !b.IsTerritoryAnchor()) continue;
                    float d = Vector2Distance(position_, b.position);
                    if (d < closestAnchorDist) {
                        closestAnchorDist = d;
                        targetAnchor = &b;
                        enemyTribe = otherTribe.get();
                    }
                }
            }
        }

        if (targetAnchor && enemyTribe) {
            TriggerEmote(SocialEmote::Dispute, 2.0f);
            float siegeDps = 18.0f * dt;
            if (weaponTier_ == WeaponTier::MetalBlade) siegeDps *= 2.2f;
            else if (weaponTier_ == WeaponTier::FlintSpear) siegeDps *= 1.5f;

            targetAnchor->hp -= siegeDps;
            if (targetAnchor->hp <= 0.0f) {
                std::string bName = BuildingTypeName(targetAnchor->type, targetAnchor->tier);
                ctx.log.Add(TextFormat("BELAGERUNG: %s hat das %s von '%s' zerstört! Dessen Einflussfeld bricht zusammen!",
                                       name_.c_str(), bName.c_str(), enemyTribe->Name().c_str()), RED);
                targetAnchor->finished = false;
                targetAnchor->hp = 0.0f;
                FinishTask();
                return;
            }
        } else {
            // Grenzpatrouille an der Außenlinie: Sichert das Gebiet
            if (stateTimer_ <= 1.0f) {
                TriggerEmote(SocialEmote::Idea, 2.0f);
            }
        }
    }

    if (stateTimer_ <= 0.0f) {
        FinishTask();
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
    if (hunger_ < cfg::kHungerThreshold) {
        hp_ = std::min(cfg::kMaxStat, hp_ + cfg::kSleepHpRegen * (sleepingInShelter_ ? 1.5f : 1.0f) * dt);
    }

    const bool injured = (hp_ < 85.0f);
    const bool rested = tiredness_ <= cfg::kWakeTiredness && ap_ >= cfg::kWakeAp && (!injured || hunger_ > 60.0f);
    if (tiredness_ <= 5.0f || rested || hunger_ >= cfg::kHungerCritical) FinishTask();
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
    bool ignoreTerritory = (state_ == CavemanState::Expedition || state_ == CavemanState::Fleeing || state_ == CavemanState::BorderRaid);
    return ctx.world.FindPath(position_, target, path_, clanId_, ignoreTerritory);
}

bool Caveman::SetDestinationDirect(const World& world, Vector2 target, bool ignoreTerritory) {
    targetPosition_ = target;
    pathIndex_ = 0;
    return world.FindPath(position_, target, path_, clanId_, ignoreTerritory);
}

// ============================================================================
//  Notfall-Flucht-KI für Ertrinken und Verbrennen (AUFTRAG PHASE 3)
// ============================================================================
void Caveman::ExecuteEmergencyWaterEscape(const World& world, float dt) {
    // 1. A*-Pfad leeren und laufende Utility-Aktion hart abbrechen
    path_.clear();
    pathIndex_ = 0;
    state_ = CavemanState::Fleeing;
    phase_ = TaskPhase::MovingToTarget;
    taskDone_ = false;
    targetNode_ = -1;
    targetBuilding_ = -1;
    targetSocialPartner_ = -1;
    targetPoi_ = -1;
    carryingTorch_ = false;
    stateTimer_ = 3.0f;

    const int startTx = World::ToTile(position_.x);
    const int startTy = World::ToTile(position_.y);

    // 2. Greedy BFS: Sucht vom aktuellen Tile maximal 15 Kacheln weit nach dem
    // ersten Tile mit waterType == WaterType::None UND isWalkable == true (Movement-Costs ignoriert)
    int targetTx = -1;
    int targetTy = -1;

    // Statisches Raster für schnellen Visited-Check ohne Heap-Allokation (31x31 für Radius 15)
    bool visited[31][31];
    std::memset(visited, 0, sizeof(visited));

    std::queue<std::pair<int, int>> q;
    visited[15][15] = true;
    q.push({ startTx, startTy });

    const int kDx[8] = { 0, 1, 0, -1, 1, 1, -1, -1 };
    const int kDy[8] = { 1, 0, -1, 0, 1, -1, 1, -1 };

    while (!q.empty()) {
        auto [cx, cy] = q.front();
        q.pop();

        for (int i = 0; i < 8; ++i) {
            int nx = cx + kDx[i];
            int ny = cy + kDy[i];

            int offX = nx - startTx + 15;
            int offY = ny - startTy + 15;
            if (offX < 0 || offX >= 31 || offY < 0 || offY >= 31) continue;
            if (visited[offY][offX]) continue;
            visited[offY][offX] = true;

            if (!world.IsInBounds(nx, ny)) continue;

            WaterType nwt = world.GetWaterType(nx, ny);
            TileType ntt = world.GetTile(nx, ny);
            bool nWalkable = world.IsWalkableTile(nx, ny);

            // Abbruchbedingung: Sicheres Land mit waterType == WaterType::None UND isWalkable == true
            if (nwt == WaterType::None && ntt != TILE_SHALLOW_WATER && ntt != TILE_DEEP_WATER && nWalkable) {
                targetTx = nx;
                targetTy = ny;
                break;
            }

            q.push({ nx, ny });
        }
        if (targetTx != -1) break;
    }

    // Fallback: Falls BFS innerhalb 15 Kacheln kein Land findet, suche nächstgelegenes begehbares Ufer
    if (targetTx == -1) {
        int shoreX = startTx, shoreY = startTy;
        if (world.FindNearestWalkableTile(startTx, startTy, 30, shoreX, shoreY)) {
            targetTx = shoreX;
            targetTy = shoreY;
        }
    }

    // 3. Force-Move: Zwinge die Figur, sich stur in Richtung dieses Ziel-Tiles zu bewegen, bis sie an Land ist
    if (targetTx != -1 && targetTy != -1) {
        Vector2 targetPos = World::TileCenter(targetTx, targetTy);
        targetPosition_ = targetPos;
        Vector2 dir = Vector2Subtract(targetPos, position_);
        float dist = Vector2Length(dir);
        if (dist > 0.5f) {
            dir = Vector2Scale(dir, 1.0f / dist);
            facingLeft_ = (dir.x < 0.0f);
            isMoving_ = true;
            const RoleTraits& t = GetRoleTraits(role_);
            float speed = std::max(28.0f, t.moveSpeed * 0.90f);
            float step = speed * dt;
            if (dist <= step) {
                position_ = targetPos;
            } else {
                position_ = Vector2Add(position_, Vector2Scale(dir, step));
            }
        } else {
            position_ = targetPos;
        }
    }
}

void Caveman::ExecuteEmergencyFireEscape(const World& world, float dt) {
    // 1. Alle Tasks abbrechen
    path_.clear();
    pathIndex_ = 0;
    state_ = CavemanState::Fleeing;
    phase_ = TaskPhase::MovingToTarget;
    taskDone_ = false;
    targetNode_ = -1;
    targetBuilding_ = -1;
    targetSocialPartner_ = -1;
    targetPoi_ = -1;
    carryingTorch_ = false;
    stateTimer_ = 2.0f;

    // Sprint für die Dauer der Flucht (Multiplikator 2.0f)
    movementSpeedMultiplier_ = 2.0f;

    const int curTx = World::ToTile(position_.x);
    const int curTy = World::ToTile(position_.y);

    // 2. Scanne die direkten 8 Nachbarkacheln (Radius 1 bis 3)
    // Finde die erste Kachel mit !isOnFire && isWalkable
    int safeTx = -1;
    int safeTy = -1;

    for (int r = 1; r <= 3 && safeTx == -1; ++r) {
        for (int dy = -r; dy <= r && safeTx == -1; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                // Ringweise Prüfung (nur Rand von Radius r)
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;

                int nx = curTx + dx;
                int ny = curTy + dy;
                if (!world.IsInBounds(nx, ny)) continue;

                TileData td = world.GetTile(World::TileCenter(nx, ny).x, World::TileCenter(nx, ny).y);
                if (!td.isOnFire && !world.IsTileOnFire(nx, ny) && td.isWalkable && td.type != TILE_DEEP_WATER) {
                    safeTx = nx;
                    safeTy = ny;
                    break;
                }
            }
        }
    }

    // Fallback falls gesamter Nahbereich brennt: Suche weiter entferntes sicheres Kachel
    if (safeTx == -1) {
        int outX = curTx, outY = curTy;
        if (world.FindNearestWalkableTile(curTx, curTy, 8, outX, outY)) {
            if (!world.IsTileOnFire(outX, outY)) {
                safeTx = outX;
                safeTy = outY;
            }
        }
    }

    // 3. Bewege die Figur sofort dorthin mit Sprint-Multiplikator 2.0f
    if (safeTx != -1 && safeTy != -1) {
        Vector2 targetPos = World::TileCenter(safeTx, safeTy);
        targetPosition_ = targetPos;
        Vector2 dir = Vector2Subtract(targetPos, position_);
        float dist = Vector2Length(dir);
        if (dist > 0.5f) {
            dir = Vector2Scale(dir, 1.0f / dist);
            facingLeft_ = (dir.x < 0.0f);
            isMoving_ = true;
            const RoleTraits& t = GetRoleTraits(role_);
            float speed = t.moveSpeed * movementSpeedMultiplier_;
            float step = speed * dt;
            if (dist <= step) {
                position_ = targetPos;
                movementSpeedMultiplier_ = 1.0f;
            } else {
                position_ = Vector2Add(position_, Vector2Scale(dir, step));
            }
        } else {
            position_ = targetPos;
            movementSpeedMultiplier_ = 1.0f;
        }
    }
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

    const float speed = t.moveSpeed * envMult * roadMult * waterMult * movementSpeedMultiplier_;
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
        case CavemanState::BorderRaid:         return "Auf Grenzüberfall / Grenzkampf";
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
void Caveman::Draw(const SpriteSheet& sheet, bool highlighted, float visualYOffset, bool isDark) const {
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

    // Fackellicht bei Nacht/Dunkelheit
    if (carryingTorch_ && isDark) {
        DrawCircleGradient(drawPos, cfg::kExpeditionTorchLight,
                           Fade(ORANGE, 0.25f), Fade(YELLOW, 0.0f));
    }

    Color tint = (damageFlash_ > 0.0f) ? RED : WHITE;
    if (isSwimming_) {
        tint = ColorAlphaBlend(tint, Fade(SKYBLUE, 0.35f), WHITE);
    }
    float charScale = (role_ == Role::Child) ? 0.65f : 1.0f; // BLOCK 4.4: Kinder 65% Skalierung

    (void)sheet;
    // AUFTRAG PHASE 4: SpriteAnimator & Layering (Basis-Körper -> Kleidung -> Waffe)
    AnimState animState = AnimState::Idle;
    if (state_ == CavemanState::Sleeping) {
        animState = AnimState::Sleeping;
    } else if (isMoving_) {
        animState = AnimState::Walking;
    } else if (phase_ == TaskPhase::Working || state_ == CavemanState::Crafting ||
               state_ == CavemanState::Building || state_ == CavemanState::Hunting ||
               state_ == CavemanState::GatheringFood || state_ == CavemanState::GatheringMaterials) {
        animState = AnimState::Working;
    }

    std::string baseKey = (role_ == Role::Child) ? "entity_human_child" :
                          ((gender_ == Gender::Female) ? "entity_human_female" : "entity_human_male");

    std::string clothingKey = "";
    switch (clothingTier_) {
        case ClothingTier::RawHide:    clothingKey = "clothing_raw_hide"; break;
        case ClothingTier::SewnFurs:   clothingKey = "clothing_sewn_furs"; break;
        case ClothingTier::WovenTunic: clothingKey = "clothing_woven_tunic"; break;
        case ClothingTier::Armor:      clothingKey = "clothing_armor"; break;
        default: break;
    }

    std::string itemKey = "";
    if (role_ == Role::Hunter) {
        switch (weaponTier_) {
            case WeaponTier::WoodClub:     itemKey = "item_club"; break;
            case WeaponTier::FlintSpear:    itemKey = "item_spear"; break;
            case WeaponTier::CompositeBow: itemKey = "item_bow"; break;
            case WeaponTier::MetalBlade:   itemKey = "item_sword"; break;
        }
    } else if (carryingTorch_ && isDark) {
        itemKey = "item_torch";
    } else if (toolTier_ >= ToolTier::HaftedAxe) {
        itemKey = "item_axe";
    } else if (containerTier_ == ContainerTier::WovenBasket) {
        itemKey = "item_basket";
    }

    // Mehrschichtiges Sprite-Rendering
    SpriteAnimator::Get().DrawLayeredCharacter(baseKey, animState, animTime_, drawPos, facingLeft_,
                                               tint, charScale, clothingKey, clanColor_, itemKey, WHITE);

    if (isSwimming_) {
        // Vordere Wasserwelle verdeckt die Beine beim Schwimmen
        DrawEllipse((int)drawPos.x, (int)drawPos.y - 1, 7, 3, Fade(Color{ 45, 130, 205, 255 }, 0.55f));
    }

    // 2. Sichtbare Kleidung, Tragekörbe & gehaltene Werkzeuge/Waffen ("Show, Don't Tell")
    DrawClothingAndGear(drawPos, isDark);

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

    // Getragene Fackel nur bei Nacht / Dunkelheit
    if (carryingTorch_ && isDark) {
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

void Caveman::DrawClothingAndGear(Vector2 drawPos, bool isDark) const {
    const int cx = (int)drawPos.x;
    const int cy = (int)drawPos.y;

    // 0) Geschlechts- & Altersmerkmale (BLOCK 4.4)
    bool isElder = (role_ == Role::Elder || ageYears_ >= 56.0f);
    if (gender_ == Gender::Female) {
        // FRAUEN: Zwingend KEIN Bart! Schmalere Silhouette, langes Haar am Rücken + Clan-Haarband
        Color hairCol = isElder ? Color{ 220, 225, 235, 255 } : Color{ 85, 52, 28, 255 };
        int hairX = cx + (facingLeft_ ? 3 : -3);
        DrawLineEx({ (float)hairX, (float)(cy - 16) }, { (float)hairX, (float)(cy - 9) }, 2.0f, hairCol);
        DrawCircle((int)hairX, cy - 12, 1.2f, clanColor_); // Haarband in Clan-Farbe
    } else {
        // MÄNNER: Kurzes Haar, Älteste mit sichtbarem weißem/silbergrauem Bart
        if (isElder) {
            Color beardCol = Color{ 220, 225, 235, 255 };
            int beardX = cx + (facingLeft_ ? -3 : 3);
            DrawTriangle({ (float)beardX, (float)(cy - 13) }, { (float)(beardX - 2), (float)(cy - 9) }, { (float)(beardX + 2), (float)(cy - 9) }, beardCol);
        }
    }

    // Älteste: Silbergraues / weißes Haar am Kopf
    if (isElder) {
        DrawRectangle(cx - 3, cy - 18, 6, 2, Color{ 225, 230, 240, 255 });
    }

    // A) Kleidungs-Stufe direkt am Oberkörper der Figur (BLOCK 4.4)
    switch (clothingTier_) {
        case ClothingTier::None:
            // Unbekleidet: Hautfarbener Torso mit kleinem Lendenschurz
            DrawRectangle(cx - 2, cy - 8, 4, 4, Color{ 225, 175, 130, 240 });
            DrawRectangle(cx - 3, cy - 4, 6, 2, Color{ 95, 60, 30, 240 });
            break;
        case ClothingTier::RawHide:
            // Zotteliges dunkelbraunes Rohfell asymmetrisch über einer Schulter
            DrawRectangle(cx - 3, cy - 8, 6, 5, Color{ 102, 66, 38, 230 });
            DrawPixel(cx - 3, cy - 3, Color{ 85, 52, 28, 230 });
            DrawPixel(cx + 2, cy - 3, Color{ 85, 52, 28, 230 });
            break;
        case ClothingTier::SewnFurs:
            // Helles genähtes Leder-/Pelzgewand mit hellem Kragen und Gürtel
            DrawRectangle(cx - 3, cy - 8, 6, 6, Color{ 165, 120, 75, 240 });
            DrawRectangle(cx - 3, cy - 9, 6, 1, Color{ 215, 195, 165, 240 });
            DrawRectangle(cx - 3, cy - 5, 6, 1, Color{ 80, 48, 24, 255 });
            break;
        case ClothingTier::WovenTunic:
            // Gewebte Tunika + Schulterumhang in der leuchtenden Farbe des Clans!
            DrawRectangle(cx - 3, cy - 8, 6, 6, Fade(clanColor_, 0.95f));
            DrawRectangle(cx - 4, cy - 9, 8, 2, clanColor_); // Schulterumhang
            DrawRectangle(cx - 3, cy - 5, 6, 1, Color{ 90, 55, 25, 255 }); // Gürtel
            break;
        case ClothingTier::Armor:
            // Metallischer Brustpanzer + Schulterplatten + kleiner Helm
            DrawRectangle(cx - 4, cy - 8, 8, 6, Color{ 185, 195, 212, 250 });
            DrawRectangleLines(cx - 4, cy - 8, 8, 6, Color{ 110, 120, 138, 255 });
            DrawRectangle(cx - 5, cy - 9, 2, 3, Color{ 210, 220, 235, 255 }); // Schulterplatte links
            DrawRectangle(cx + 3, cy - 9, 2, 3, Color{ 210, 220, 235, 255 }); // Schulterplatte rechts
            DrawRectangle(cx - 3, cy - 16, 6, 3, Color{ 205, 215, 230, 255 }); // Metallhelm
            DrawLineEx({ (float)(cx - 3), (float)(cy - 14) }, { (float)(cx + 3), (float)(cy - 14) }, 1.2f, GOLD);
            break;
    }

    // B) Tragekorb auf dem Rücken (und sichtbares Ladegut beim Transport)
    if (carryAmount_ > 0 && state_ != CavemanState::Expedition) {
        int backX = cx + (facingLeft_ ? 3 : -7);
        int backY = cy - 9;
        DrawRectangle(backX, backY, 4, 6, Color{ 185, 140, 75, 255 });
        DrawRectangleLines(backX, backY, 4, 6, Color{ 120, 85, 40, 255 });
        Color loadCol = (carryType_ == CarryType::Food)   ? Color{ 220, 55, 65, 255 } :
                        (carryType_ == CarryType::Wood)   ? Color{ 135, 85, 40, 255 } :
                        (carryType_ == CarryType::Stone)  ? Color{ 165, 165, 175, 255 } :
                        (carryType_ == CarryType::Planks) ? Color{ 225, 185, 115, 255 } : GOLD;
        DrawCircle(backX + 2, backY - 1, 2.0f, loadCol);
    }

    // C) Gehaltenes Werkzeug oder Jagdwaffe in der Hand (Fackel nur bei Nacht!)
    if (carryingTorch_ && isDark) return;

    // Älteste mit Holzstab, wenn unbewaffnet
    if (isElder && weaponTier_ <= WeaponTier::WoodClub && toolTier_ == ToolTier::BareHands) {
        float staffX = drawPos.x + (facingLeft_ ? -6.0f : 6.0f);
        DrawLineEx({ staffX, (float)(cy - 16) }, { staffX, (float)cy }, 1.5f, Color{ 140, 95, 48, 255 });
        DrawCircle((int)staffX, cy - 16, 1.8f, Color{ 160, 110, 58, 255 });
        return;
    }

    // Kinder tragen keine Waffen/Werkzeuge (BLOCK 4.4)
    if (role_ == Role::Child) return;

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
            case WeaponTier::CompositeBow: {
                // Geschwungener brauner Bogen + Köcher auf dem Rücken
                DrawLineEx({ handX, handY - 6.0f }, { handX + dir * 2.8f, handY }, 1.6f, Color{ 150, 95, 45, 255 });
                DrawLineEx({ handX + dir * 2.8f, handY }, { handX, handY + 6.0f }, 1.6f, Color{ 150, 95, 45, 255 });
                DrawLineEx({ handX, handY - 6.0f }, { handX, handY + 6.0f }, 1.0f, Fade(RAYWHITE, 0.8f));
                int quiverX = cx + (facingLeft_ ? 3 : -5);
                DrawRectangle(quiverX, cy - 14, 3, 8, Color{ 120, 80, 42, 255 });
                DrawLineEx({ (float)quiverX, (float)(cy - 16) }, { (float)(quiverX + 2), (float)(cy - 13) }, 1.0f, RAYWHITE);
                break;
            }
            case WeaponTier::MetalBlade: {
                // Eisenschwert mit Parierstange + Rundschild mit Clan-Farbe
                DrawLineEx({ handX, handY + 2.0f }, { handX + dir * 6.0f, handY - 8.0f }, 2.0f, Color{ 220, 230, 245, 255 });
                DrawLineEx({ handX + dir * 1.5f, handY - 1.0f }, { handX + dir * 1.5f, handY + 4.0f }, 1.5f, GOLD);
                float shieldX = drawPos.x + (facingLeft_ ? 5.0f : -5.0f);
                DrawCircleV({ shieldX, (float)(cy - 6) }, 3.5f, clanColor_);
                DrawCircleLines((int)shieldX, cy - 6, 3.5f, Color{ 210, 215, 225, 255 });
                DrawCircleV({ shieldX, (float)(cy - 6) }, 1.0f, GOLD);
                break;
            }
        }
        return;
    }

    // Sonstiges Werkzeug in der Hand (Faustkeil, Steinaxt, Bronze-/Eisenwerkzeug)
    switch (toolTier_) {
        case ToolTier::BareHands:
            break;
        case ToolTier::FlintHandaxe:
            DrawTriangle({ handX, handY - 3.0f }, { handX - 2.0f, handY + 1.0f }, { handX + 2.0f, handY + 1.0f }, Color{ 150, 155, 165, 255 });
            break;
        case ToolTier::HaftedAxe:
            DrawLineEx({ handX, handY + 2.0f }, { handX, handY - 5.0f }, 1.3f, Color{ 140, 90, 45, 255 });
            DrawRectangle((int)(handX + (facingLeft_ ? -3.0f : 0.0f)), (int)(handY - 5.0f), 3, 2, Color{ 170, 175, 185, 255 });
            break;
        case ToolTier::BronzeTool:
            DrawLineEx({ handX, handY + 2.0f }, { handX, handY - 5.0f }, 1.4f, Color{ 140, 90, 45, 255 });
            DrawRectangle((int)(handX + (facingLeft_ ? -4.0f : 0.0f)), (int)(handY - 6.0f), 4, 3, Color{ 235, 175, 75, 255 });
            DrawCircle((int)(handX + (facingLeft_ ? -2.0f : 2.0f)), (int)(handY - 5.0f), 1.0f, GOLD);
            break;
        case ToolTier::IronTool: {
            DrawLineEx({ handX, handY + 2.0f }, { handX + dir * 5.0f, handY - 7.0f }, 1.8f, Color{ 215, 225, 240, 255 });
            DrawLineEx({ handX + dir * 1.5f, handY - 1.0f }, { handX + dir * 1.5f, handY + 3.0f }, 1.4f, GOLD);
            float shieldX = drawPos.x + (facingLeft_ ? 4.5f : -4.5f);
            DrawCircleV({ shieldX, (float)(cy - 6) }, 3.2f, clanColor_);
            DrawCircleLines((int)shieldX, cy - 6, 3.2f, RAYWHITE);
            break;
        }
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

    // A2) Bau- / Handwerks-Animation (Hammer schlägt im Takt mit Funken)
    if (state_ == CavemanState::Building && phase_ == TaskPhase::Working) {
        float strike = std::sin(animTime_ * 12.0f);
        DrawCircleV({ ex, ey }, 5.5f, Fade(BLACK, 0.65f));
        float hx = ex + strike * 1.5f;
        float hy = ey - 1.0f + std::abs(strike) * 1.0f;
        DrawLineEx({ hx - 2.5f, hy + 2.5f }, { hx + 2.0f, hy - 2.0f }, 1.5f, Color{ 140, 95, 45, 255 });
        DrawRectangle((int)(hx + 1.0f), (int)(hy - 3.5f), 3, 2, LIGHTGRAY);
        if (strike > 0.6f) {
            DrawPixel((int)(hx + 3.0f), (int)(hy - 3.0f), GOLD);
        }
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
            case SocialEmote::Vote:
                // Blaue Wahlurne mit Einwurfschlitz & Stimmzettel
                DrawRectangleRounded({ ex - 4.5f, ey - 3.5f, 9.0f, 7.0f }, 0.25f, 3, Color{ 60, 130, 220, 255 });
                DrawRectangleLines((int)(ex - 4.5f), (int)(ey - 3.5f), 9, 7, RAYWHITE);
                DrawLineEx({ ex - 2.5f, ey - 1.5f }, { ex + 2.5f, ey - 1.5f }, 1.2f, WHITE);
                DrawPixel((int)ex, (int)ey + 1, GOLD);
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
    } else if (state_ == CavemanState::Sleeping && tiredness_ > 0.0f) {
        FontManager::DrawTextCenteredShadow("zZ", ex, ey - 6.0f, 11.0f, Color{ 200, 185, 255, 255 });
    }
}

void Caveman::DrawDebugPath() const {
    if (path_.empty() || pathIndex_ >= path_.size()) return;
    for (size_t i = pathIndex_; i < path_.size() - 1; ++i) {
        DrawLineEx(path_[i], path_[i + 1], 1.2f, Fade(RoleColor(role_), 0.6f));
    }
}
