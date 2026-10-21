#include "utility_ai.h"
#include "caveman.h"
#include "world.h"
#include "tribe.h"
#include "building.h"
#include "resource_node.h"
#include "relationship.h"
#include "social_structure.h"
#include "social_actions.h"
#include "poi.h"
#include "skill_tree.h"
#include "config.h"
#include "world/environmental_system.h"

#include <cmath>
#include <algorithm>

float UtilityAI::DistanceFactor(Vector2 from, Vector2 to, float maxRange) {
    float dist = Vector2Distance(from, to);
    if (dist <= 16.0f) return 1.0f;
    if (dist >= maxRange) return 0.2f;
    return 1.0f - (dist / maxRange) * 0.8f;
}

bool UtilityAI::CanPerformAction(const Caveman& agent, CavemanState state, const WorldContext& ctx, int targetId) {
    // 0. Kinder (Role::Child oder Alter < 16) dürfen unter keinen Umständen arbeiten, bauen, forschen, jagen oder kämpfen!
    if (agent.GetRole() == Role::Child || agent.AgeYears() < 16.0f) {
        if (state == CavemanState::Building || state == CavemanState::Crafting ||
            state == CavemanState::Hunting || state == CavemanState::GatheringFood ||
            state == CavemanState::GatheringMaterials || state == CavemanState::Expedition ||
            state == CavemanState::BorderRaid) {
            return false;
        }
    }

    // 1. Harte Überlebens-Sperre: Wer verhungert (>= 75%), erfriert (<= 20%) oder lebensgefährlich verletzt ist (<= 35 HP),
    // darf KEINE nicht-überlebenswichtigen Arbeiten (Bauen, Craften, Expedition, Wandern, Raiden, Materialien sammeln) ausführen!
    bool criticalSurvival = (agent.Hunger() >= 75.0f || agent.Warmth() <= 20.0f || agent.Hp() <= 35.0f);
    if (criticalSurvival) {
        if (state == CavemanState::Building || state == CavemanState::Crafting ||
            state == CavemanState::Expedition || state == CavemanState::Wandering ||
            state == CavemanState::BorderRaid || state == CavemanState::GatheringMaterials) {
            return false;
        }
    }

    // 2. Ziel-Sperre durch Cooldown/Blacklist
    if (targetId >= 0 && (agent.IsTargetBlacklisted(targetId) || agent.IsNodeBlacklisted(targetId))) {
        return false;
    }

    // 3. Zustandsspezifische Vorbedingungen
    switch (state) {
        case CavemanState::Building: {
            if (agent.GetRole() == Role::Child) return false;
            int bId = (targetId >= 0) ? targetId : ctx.tribe.Buildings().FindActiveConstructionSite(agent.Position());
            if (bId < 0) {
                bId = ctx.tribe.Buildings().FindRepairSite(agent.Position());
            }
            if (bId < 0 || agent.IsTargetBlacklisted(bId)) return false;
            const Building* b = ctx.tribe.Buildings().Get(bId);
            if (!b || (!b->NeedsWork() && !b->NeedsRepair())) return false;
            if (b->builders >= b->maxBuilders) return false;

            // Material-Verfügbarkeit prüfen: Bei Neubau oder Upgrade muss mindestens 1 Einheit des benötigten Materials
            // vorhanden sein oder der Baufortschritt ist bereits im Gang (> 0%)
            bool isRepair = (b->finished && !b->underUpgrade && b->hp < b->maxHp - 8.0f);
            if (!isRepair) {
                ResourceCost cost = b->underUpgrade ? b->GetUpgradeCost() : b->GetBuildCost();
                const Stockpile& stock = ctx.tribe.Stock();
                int neededTypes = (cost.wood > 0 ? 1 : 0) + (cost.stone > 0 ? 1 : 0) +
                                  (cost.planks > 0 ? 1 : 0) + (cost.bricks > 0 ? 1 : 0);
                int availableTypes = ((cost.wood > 0 && stock.wood > 0) ? 1 : 0) +
                                     ((cost.stone > 0 && stock.stone > 0) ? 1 : 0) +
                                     ((cost.planks > 0 && stock.planks > 0) ? 1 : 0) +
                                     ((cost.bricks > 0 && stock.bricks > 0) ? 1 : 0);
                if (neededTypes > 0 && availableTypes == 0 && b->progress < 1.0f) {
                    return false;
                }
            }
            return true;
        }

        case CavemanState::Crafting: {
            if (agent.GetRole() == Role::Child) return false;
            int benchId = (targetId >= 0) ? targetId : ctx.tribe.Buildings().FindCraftBench(agent.Position());
            if (benchId < 0 || agent.IsTargetBlacklisted(benchId)) return false;
            const Building* b = ctx.tribe.Buildings().Get(benchId);
            if (!b || !b->finished) return false;
            return true;
        }

        case CavemanState::Expedition: {
            if (agent.GetRole() == Role::Child) return false;
            if (agent.Hp() < 45.0f || agent.Hunger() > 60.0f || agent.Warmth() < 35.0f || agent.Tiredness() > 65.0f) {
                return false;
            }
            if (targetId >= 0 && agent.IsTargetBlacklisted(targetId)) return false;
            return true;
        }

        case CavemanState::Hunting: {
            if (agent.GetRole() == Role::Child) return false;
            const RoleTraits& traits = GetRoleTraits(agent.GetRole());
            if (!traits.canHunt) return false;
            if (targetId >= 0 && (agent.IsTargetBlacklisted(targetId) || agent.IsNodeBlacklisted(targetId))) return false;
            return true;
        }

        case CavemanState::GatheringFood: {
            if (agent.GetRole() == Role::Child) return false;
            if (targetId >= 0 && (agent.IsTargetBlacklisted(targetId) || agent.IsNodeBlacklisted(targetId))) return false;
            return true;
        }

        case CavemanState::GatheringMaterials: {
            if (agent.GetRole() == Role::Child) return false;
            if (targetId >= 0 && (agent.IsTargetBlacklisted(targetId) || agent.IsNodeBlacklisted(targetId))) return false;
            return true;
        }

        case CavemanState::Sleeping: {
            bool isNight = (ctx.timeOfDay > 0.72f || ctx.timeOfDay < 0.22f);
            if (isNight) {
                if (agent.Tiredness() < 45.0f) return false;
            } else {
                if (agent.Tiredness() < 70.0f) return false;
            }
            return true;
        }

        case CavemanState::Eating: {
            const Stockpile& stock = ctx.tribe.Stock();
            if (stock.food <= 0 && stock.cookedFood <= 0) return false;
            if (agent.Hunger() < 25.0f) return false;
            return true;
        }

        case CavemanState::BorderRaid: {
            if (agent.GetRole() == Role::Child) return false;
            if (agent.Hp() < 50.0f) return false;
            return true;
        }

        default:
            break;
    }

    return true;
}

UtilityDecision UtilityAI::Evaluate(const Caveman& agent, const WorldContext& ctx) {
    UtilityDecision best;
    best.state = CavemanState::Idle;
    best.score = -1.0f;
    bool isChild = (agent.GetRole() == Role::Child || agent.AgeYears() < 16.0f);
    const BrainWeights& brain = agent.GetBrainWeights();

    // 0. Panik / Flucht vor Feuer & Strom (höchste Überlebenspriorität, moduliert durch Raubtier-/Gefahrenvorsicht)
    {
        std::string reason;
        float score = ScoreFlee(agent, ctx, reason);
        if (score > 0.0f) {
            score *= (0.75f + brain.Get(BrainGene::PredatorCaution) * 0.30f);
            if (score > best.score) {
                best = { CavemanState::Fleeing, ResourceType::Bush, -1, -1, -1, score, reason };
            }
        }
    }

    // 1. Essen (Moduliert durch genetische & erlernte Nahrungsvorsicht)
    if (CanPerformAction(agent, CavemanState::Eating, ctx)) {
        std::string reason;
        float score = ScoreEat(agent, ctx, reason);
        score *= brain.Get(BrainGene::FoodCaution);
        if (score > best.score) {
            best = { CavemanState::Eating, ResourceType::Bush, -1, -1, -1, score, reason };
        }
    }

    // 2. Schlafen (Harte Schranke: tagsüber nur ab Müdigkeit >= 70%, nachts ab >= 45%)
    if (CanPerformAction(agent, CavemanState::Sleeping, ctx)) {
        int hutId = -1;
        std::string reason;
        float score = ScoreSleep(agent, ctx, hutId, reason);
        score *= brain.Get(BrainGene::SleepRest);
        if (score > best.score) {
            best = { CavemanState::Sleeping, ResourceType::Bush, hutId, -1, -1, score, reason };
        }
    }

    // 3. Aufwärmen / Schutz bei Sturm & Kälte (Moduliert durch Wärmepriorität)
    if (CanPerformAction(agent, CavemanState::WarmingUp, ctx)) {
        int shelterId = -1;
        std::string reason;
        float score = ScoreWarmUp(agent, ctx, shelterId, reason);
        score *= brain.Get(BrainGene::WarmthPriority);
        if (score > best.score) {
            best = { CavemanState::WarmingUp, ResourceType::Bush, shelterId, -1, -1, score, reason };
        }
    }

    // 4. Soziales Handeln (Gespräch, Klatsch, Flirten, Teilen, Kinderfürsorge/Lehre)
    {
        int targetCharId = -1;
        std::string reason;
        float score = ScoreSocial(agent, ctx, targetCharId, reason);
        score *= brain.Get(BrainGene::Social);
        if (score > best.score && targetCharId >= 0) {
            best = { CavemanState::Socializing, ResourceType::Bush, -1, -1, targetCharId, score, reason };
        }
    }

    // Arbeits-, Bau-, Jagd- und Expeditions-Aktionen sind für Kinder STRIKT verboten!
    if (!isChild) {
        // 5. Forschung & Expedition (POIs erkunden mit Ausrüstung)
        if (CanPerformAction(agent, CavemanState::Expedition, ctx)) {
            int poiId = -1;
            std::string reason;
            float score = ScoreExpedition(agent, ctx, poiId, reason);
            score *= brain.Get(BrainGene::Expedition);
            if (score > best.score && poiId >= 0) {
                best = { CavemanState::Expedition, ResourceType::Bush, -1, -1, -1, score, reason, poiId };
            }
        }

        // 6. Bauen / Reparieren (Moduliert durch Bautrieb)
        if (CanPerformAction(agent, CavemanState::Building, ctx)) {
            int buildingId = -1;
            std::string reason;
            float score = ScoreBuild(agent, ctx, buildingId, reason);
            score *= brain.Get(BrainGene::Build);
            if (score > best.score && buildingId >= 0) {
                best = { CavemanState::Building, ResourceType::Tree, buildingId, -1, -1, score, reason };
            }
        }

        // 6b. Veredeln an der Werkbank (Moduliert durch Handwerkstrieb)
        if (CanPerformAction(agent, CavemanState::Crafting, ctx)) {
            int benchId = -1;
            std::string reason;
            float score = ScoreCraft(agent, ctx, benchId, reason);
            score *= brain.Get(BrainGene::Craft);
            if (score > best.score && benchId >= 0) {
                best = { CavemanState::Crafting, ResourceType::Tree, benchId, -1, -1, score, reason };
            }
        }

        // 7. Jagd (Wildtiere, moduliert durch Jagdlust & Raubtiervorsicht)
        if (CanPerformAction(agent, CavemanState::Hunting, ctx)) {
            int nodeId = -1;
            std::string reason;
            float score = ScoreHunt(agent, ctx, nodeId, reason);
            score *= brain.Get(BrainGene::Hunt);
            if (score > best.score && nodeId >= 0) {
                best = { CavemanState::Hunting, ResourceType::Animal, -1, nodeId, -1, score, reason };
            }
        }

        // 8a. Neolithische Landwirtschaft & Tierzucht (Ackerfeld & Tiergehege bestellen/ernten)
        if (CanPerformAction(agent, CavemanState::GatheringFood, ctx)) {
            int farmId = ctx.tribe.Buildings().FindFarmOrPenNeedingWork(agent.Position());
            if (farmId >= 0) {
                if (const Building* b = ctx.tribe.Buildings().Get(farmId)) {
                    float roleAff = (agent.GetRole() == Role::Gatherer || agent.GetRole() == Role::Elder) ? 1.25f : 0.90f;
                    float dist = DistanceFactor(agent.Position(), b->position);
                    float basePrio = b->IsHarvestReady() ? 0.86f : 0.68f;
                    float farmScore = std::clamp(basePrio * roleAff * dist, 0.0f, 0.93f);
                    farmScore *= brain.Get(BrainGene::GatherFood);
                    if (farmScore > best.score) {
                        std::string farmReason = (b->type == BuildingType::FarmPlot)
                            ? (b->IsHarvestReady() ? "Getreide ernten" : "Ackerfeld bestellen")
                            : (b->IsHarvestReady() ? "Tiergehege versorgen & scheren" : "Herde füttern");
                        best = { CavemanState::GatheringFood, ResourceType::Bush, farmId, -1, -1, farmScore, farmReason };
                    }
                }
            }
        }

        // 8b. Nahrung sammeln (Wildbeeren)
        if (CanPerformAction(agent, CavemanState::GatheringFood, ctx)) {
            int nodeId = -1;
            std::string reason;
            float score = ScoreGatherFood(agent, ctx, nodeId, reason);
            score *= brain.Get(BrainGene::GatherFood);
            if (score > best.score && nodeId >= 0) {
                best = { CavemanState::GatheringFood, ResourceType::Bush, -1, nodeId, -1, score, reason };
            }
        }

        // 9. Holz beschaffen
        if (CanPerformAction(agent, CavemanState::GatheringMaterials, ctx)) {
            int nodeId = -1;
            std::string reason;
            float score = ScoreGatherWood(agent, ctx, nodeId, reason);
            score *= brain.Get(BrainGene::GatherWood);
            if (score > best.score && nodeId >= 0) {
                best = { CavemanState::GatheringMaterials, ResourceType::Tree, -1, nodeId, -1, score, reason };
            }
        }

        // 10. Stein beschaffen
        if (CanPerformAction(agent, CavemanState::GatheringMaterials, ctx)) {
            int nodeId = -1;
            std::string reason;
            float score = ScoreGatherStone(agent, ctx, nodeId, reason);
            score *= brain.Get(BrainGene::GatherStone);
            if (score > best.score && nodeId >= 0) {
                best = { CavemanState::GatheringMaterials, ResourceType::Rock, -1, nodeId, -1, score, reason };
            }
        }

        // 11. Grenzkampf, Grenzpatrouille & Kachel-Eroberung (Phase 4: Border Raid)
        if (CanPerformAction(agent, CavemanState::BorderRaid, ctx)) {
            int tileX = -1, tileY = -1;
            std::string reason;
            float score = ScoreBorderRaid(agent, ctx, tileX, tileY, reason);
            score *= brain.Get(BrainGene::BorderRaid);
            if (score > best.score && tileX >= 0) {
                best = { CavemanState::BorderRaid, ResourceType::Bush, -1, tileX, tileY, score, reason };
            }
        }
    }

    // 12. Wandern / Verweilen (Fallback)
    if (CanPerformAction(agent, CavemanState::Wandering, ctx)) {
        std::string reason;
        float score = ScoreWander(agent, ctx, reason);
        if (score > best.score) {
            best = { CavemanState::Wandering, ResourceType::Bush, -1, -1, -1, score, reason };
        }
    }

    // Task-Commitment Hysterese-Bonus (+0.20f) für die aktuell laufende valide Aktion
    if (!agent.IsTaskDone() && agent.GetTaskCommitmentTimer() > 0.0f && agent.State() != CavemanState::Idle) {
        if (best.state == agent.State()) {
            best.score += 0.20f;
        }
    }

    return best;
}

// ============================================================================
//  Einzelbewertungen
// ============================================================================

float UtilityAI::ScoreFlee(const Caveman& agent, const WorldContext& ctx, std::string& outReason) {
    if (!ctx.env) return 0.0f;
    Vector2 hazardCenter = { 0.0f, 0.0f };
    if (ctx.env->IsHazardNear(agent.Position(), 36.0f, hazardCenter)) {
        outReason = "Flieht vor Feuer & Gefahren!";
        return 1.95f;
    }
    return 0.0f;
}

float UtilityAI::ScoreEat(const Caveman& agent, const WorldContext& ctx, std::string& outReason) {
    const Stockpile& stock = ctx.tribe.Stock();
    if (stock.food <= 0 && stock.cookedFood <= 0) return 0.0f;

    float hunger = agent.Hunger();
    if (hunger < 25.0f) return 0.0f;

    // Altruistische Figuren lassen anderen bei knappen Vorräten eher den Vortritt, außer bei akutem Hunger
    float altruismFactor = (hunger >= 75.0f) ? 1.0f : (1.0f - (agent.GetPersonality().altruism * 0.2f));
    float urgency = std::pow(hunger / 100.0f, 2.0f) * altruismFactor;
    float dist = DistanceFactor(agent.Position(), ctx.tribe.StockpilePosition());

    // Bei hohem Hunger (Lebensgefahr) darf Distanz die Notwendigkeit zur Nahrungsaufnahme nicht ersticken!
    if (hunger >= 65.0f) {
        float blend = std::clamp((hunger - 65.0f) / 35.0f, 0.0f, 1.0f);
        dist = dist * (1.0f - blend) + 1.0f * blend;
        urgency *= (1.0f + blend * 0.85f); // Bis zu 1.85x Score-Boost
    }

    float score = std::clamp(urgency * 1.35f * dist, 0.0f, 1.95f);

    outReason = (stock.cookedFood > 0)
        ? TextFormat("Warme Mahlzeit essen (Hunger: %.0f%%)", hunger)
        : TextFormat("Rohkost essen (Hunger: %.0f%%)", hunger);
    return score;
}

float UtilityAI::ScoreSleep(const Caveman& agent, const WorldContext& ctx, int& outHutId, std::string& outReason) {
    float tiredness = agent.Tiredness();
    float ap = agent.Ap();

    bool isNight = (ctx.timeOfDay > 0.72f || ctx.timeOfDay < 0.22f);
    if (isNight) {
        if (tiredness < 45.0f) return 0.0f;
    } else {
        if (tiredness < 70.0f) return 0.0f;
    }

    float tirednessNorm = tiredness / 100.0f;
    float apExhaustion = (100.0f - ap) / 100.0f;
    float urgency = std::max(std::pow(tirednessNorm, 1.8f), apExhaustion * 0.9f);

    outHutId = ctx.tribe.Buildings().FindBestShelter(agent.Position());
    float shelterBonus = (outHutId >= 0) ? 1.15f : 0.85f;
    if (ctx.env && (ctx.env->GetWeather() == WeatherType::Storm || ctx.env->GetWeather() == WeatherType::Rain)) {
        shelterBonus *= 1.18f;
    }

    if (agent.Hunger() >= cfg::kHungerCritical) urgency *= 0.3f;

    float score = std::clamp(urgency * shelterBonus, 0.0f, 0.98f);
    outReason = (outHutId >= 0)
        ? TextFormat("Schlafen in Hütte (Müdigkeit: %.0f%%)", tiredness)
        : TextFormat("Am Feuer ausruhen (Müdigkeit: %.0f%%)", tiredness);
    return score;
}

float UtilityAI::ScoreWarmUp(const Caveman& agent, const WorldContext& ctx, int& outShelterId, std::string& outReason) {
    const Stockpile& stock = ctx.tribe.Stock();

    // 1. Feuerhüten (TendFire): Wenn Lagerfeuer oder Ofen fast erloschen ist und Holz im Lager liegt
    int fireNeedingFuel = ctx.tribe.Buildings().FindFireNeedingFuel(agent.Position());
    if (fireNeedingFuel >= 0 && stock.wood >= 1 && agent.GetRole() != Role::Child) {
        if (const Building* fire = ctx.tribe.Buildings().Get(fireNeedingFuel)) {
            float dist = DistanceFactor(agent.Position(), fire->position);
            float basePrio = (fire->fuel <= 0.0f) ? 0.94f : 0.82f;
            if (agent.GetRole() == Role::Elder) basePrio += 0.05f;
            float score = std::clamp(basePrio * dist, 0.0f, 0.96f);
            outShelterId = fireNeedingFuel;
            outReason = (fire->fuel <= 0.0f)
                ? "Feuer neu entzünden!"
                : TextFormat("Brennholz nachlegen (%d%% Glut)", (int)fire->fuel);
            return score;
        }
    }

    // 2. Kochen am Lagerfeuer (CookFood): Älteste & Sammler braten rohe Vorräte zu warmen Mahlzeiten
    if ((agent.GetRole() == Role::Elder || agent.GetRole() == Role::Gatherer) &&
        stock.food >= 2 && stock.cookedFood < std::max(4, ctx.population)) {
        outShelterId = ctx.tribe.Buildings().FindBestShelter(agent.Position());
        Vector2 targetPos = (outShelterId >= 0) ? ctx.tribe.Buildings().Get(outShelterId)->position : ctx.tribe.CampPosition();
        if (ctx.tribe.Buildings().GetWarmthAt(targetPos) > 0.0f) {
            float dist = DistanceFactor(agent.Position(), targetPos);
            float cookScore = std::clamp(((agent.GetRole() == Role::Elder) ? 0.74f : 0.60f) * dist, 0.0f, 0.80f);
            if (agent.Warmth() > 60.0f) {
                outReason = "Kocht Mahlzeiten am Feuer";
                return cookScore;
            }
        }
    }

    // 2b. Spirituelle Andacht am Monument oder Ahnengrab, wenn die Moral sinkt (Meilenstein E)
    if (agent.Morale() < 55.0f && agent.Warmth() > 55.0f) {
        int monId = ctx.tribe.Buildings().FindMonumentOrGrave(agent.Position());
        if (monId >= 0) {
            if (const Building* mon = ctx.tribe.Buildings().Get(monId)) {
                float dist = DistanceFactor(agent.Position(), mon->position);
                float monScore = std::clamp((0.76f + (55.0f - agent.Morale()) * 0.004f) * dist, 0.0f, 0.85f);
                outShelterId = monId;
                outReason = TextFormat("Andacht an %s", mon->DisplayName().c_str());
                return monScore;
            }
        }
    }

    // 3. Eigenes Aufwärmen & Schutz bei Sturm / Kälte
    float warmth = agent.Warmth();
    bool badWeather = ctx.env && (ctx.env->GetWeather() == WeatherType::Storm || ctx.env->GetWeather() == WeatherType::Rain);
    float threshold = badWeather ? 72.0f : 60.0f;
    if (warmth > threshold) return 0.0f;

    float coldness = (100.0f - warmth) / 100.0f;
    float urgency = std::pow(coldness, 1.85f) + (badWeather ? 0.18f : 0.0f);

    outShelterId = ctx.tribe.Buildings().FindBestShelter(agent.Position());
    Vector2 targetPos = (outShelterId >= 0) ? ctx.tribe.Buildings().Get(outShelterId)->position : ctx.tribe.CampPosition();
    float dist = DistanceFactor(agent.Position(), targetPos);

    // Bei akuter Unterkühlung (< 45%) darf weite Entfernung das Aufsuchen des Feuers nicht verhindern
    if (warmth < 45.0f) {
        float blend = std::clamp((45.0f - warmth) / 45.0f, 0.0f, 1.0f);
        dist = dist * (1.0f - blend) + 1.0f * blend;
        urgency *= (1.0f + blend * 0.90f);
    }

    float score = std::clamp(urgency * 1.35f * dist, 0.0f, 1.95f);
    outReason = badWeather
        ? TextFormat("Schutz vor Unwetter (Wärme: %.0f%%)", warmth)
        : TextFormat("Aufwärmen am Feuer (Wärme: %.0f%%)", warmth);
    return score;
}

float UtilityAI::CalculateMateScore(const Caveman& agent, const Caveman& target, const WorldContext& ctx, std::string& outReason) {
    if (agent.Id() == target.Id() || target.IsDead()) return 0.0f;

    // Nur Erwachsene (AUFTRAG PHASE 1.3: isAdult)
    bool isAdult = (agent.GetRole() != Role::Child && agent.AgeYears() >= 16.0f &&
                    target.GetRole() != Role::Child && target.AgeYears() >= 16.0f);

    // Schwangerschaftsprüfung (AUFTRAG PHASE 1.3: !isPregnant)
    bool isPregnant = agent.IsPregnant() || target.IsPregnant() ||
                      agent.PregnancyCooldown() > 0.0f || target.PregnancyCooldown() > 0.0f;

    const RelationshipManager& rels = ctx.tribe.Relationships();
    float sympathy = rels.GetAffinity(agent.Id(), target.Id());

    // AUFTRAG PHASE 1.3: Senke die Bedingung für eine Paarung: if (sympathy > 40.0f && isAdult && !isPregnant)
    if (!(sympathy > 40.0f && isAdult && !isPregnant)) {
        return 0.0f;
    }

    // Inzest-Sperre prüfen (AUFTRAG PHASE 1.2)
    if (!SocialActionEvaluator::CanBecomePartners(&agent, &target)) return 0.0f;

    // Fremd-Partnerschaften ausschließen
    int partnerA = rels.GetPartner(agent.Id());
    int partnerB = rels.GetPartner(target.Id());
    if (partnerA >= 0 && partnerA != target.Id()) return 0.0f;
    if (partnerB >= 0 && partnerB != agent.Id()) return 0.0f;

    // Distanz-Einfluss
    float dist = Vector2Distance(agent.Position(), target.Position());
    if (dist > 250.0f) return 0.0f;
    float distFactor = 1.0f - (dist / 250.0f) * 0.35f;

    // Basis-Score
    float baseScore = 0.35f * distFactor;

    // AUFTRAG PHASE 1.3: Skaliere den Score abhängig von der Stammespopulation:
    // float popFactor = max(0.0f, 1.0f - (tribe->GetPopulation() / 50.0f));
    int pop = (ctx.tribe.GetPopulation() > 0) ? ctx.tribe.GetPopulation() : ctx.population;
    float popFactor = std::max(0.0f, 1.0f - ((float)pop / 50.0f));

    // Überlebens-Notbremse: Bei akutem Hunger (>= 70%) oder Kälte (<= 25%) geht Selbsterhaltung vor
    if (agent.Hunger() >= 70.0f || agent.Warmth() <= 25.0f) {
        return 0.0f;
    }

    // AUFTRAG PHASE 1.3: Addiere einen statischen Boost:
    // return baseScore + (sympathy * 0.01f) + (popFactor * 0.3f);
    float score = baseScore + (sympathy * 0.01f) + (popFactor * 0.3f);
    score = std::clamp(score, 0.0f, 1.45f);

    outReason = TextFormat("Paarung / Fortpflanzung mit %s (Score: %.2f, PopFactor: %.2f)",
                           target.Name().c_str(), score, popFactor);
    return score;
}

float UtilityAI::CalculateMateScore(const Caveman& agent, const WorldContext& ctx, int& outPartnerId, std::string& outReason) {
    if (!ctx.allCavemen) return 0.0f;
    float bestScore = 0.0f;
    int bestId = -1;
    std::string bestReason = "";

    for (const auto& other : *ctx.allCavemen) {
        if (other.Id() == agent.Id() || other.IsDead()) continue;
        if (other.ClanId() != agent.ClanId()) continue;

        std::string reason;
        float score = CalculateMateScore(agent, other, ctx, reason);
        if (score > bestScore) {
            bestScore = score;
            bestId = other.Id();
            bestReason = reason;
        }
    }
    outPartnerId = bestId;
    outReason = bestReason;
    return bestScore;
}

float UtilityAI::CalculateSecedeScore(const Caveman* caveman, const WorldContext& ctx, std::string& outReason) {
    if (!caveman || caveman->IsDead()) return 0.0f;

    // AUFTRAG PHASE 2.2: Gesundheits-Check für Schisma / Secede
    // Eine Abspaltung in die Wildnis ist extrem gefährlich. Eine hungernde oder erfrierende Figur darf diesen Task niemals ausführen.
    if (caveman->Hp() < 70.0f || caveman->Hunger() > 50.0f || caveman->Warmth() < 50.0f) {
        return 0.0f;
    }

    if (caveman->GetRole() == Role::Child || caveman->AgeYears() < 16.0f) {
        return 0.0f;
    }

    // Nur Figuren mit niedriger Loyalität (< 25) und Ehrgeiz streben nach Abspaltung
    if (caveman->Loyalty() >= 25.0f) {
        return 0.0f;
    }

    // Mindestpopulation im Stamm (mind. 10 Stammesmitglieder nötig)
    int pop = (ctx.tribe.GetPopulation() > 0) ? ctx.tribe.GetPopulation() : ctx.population;
    if (pop < 10) {
        return 0.0f;
    }

    float rebellion = (25.0f - caveman->Loyalty()) / 25.0f;
    float dominance = (caveman->GetPersonality().dominance + 1.0f) * 0.5f;
    float aggression = (caveman->GetPersonality().aggression + 1.0f) * 0.5f;

    float score = std::clamp((rebellion * 0.5f + dominance * 0.35f + aggression * 0.15f) * 0.85f, 0.0f, 0.95f);
    outReason = TextFormat("Schisma / Abspaltung vom Stamm planen (Score: %.2f)", score);
    return score;
}

float UtilityAI::CalculateSecedeScore(const Caveman& agent, const WorldContext& ctx, std::string& outReason) {
    return CalculateSecedeScore(&agent, ctx, outReason);
}

float UtilityAI::CalculateFormNewTribeScore(const Caveman* caveman, const WorldContext& ctx, std::string& outReason) {
    return CalculateSecedeScore(caveman, ctx, outReason);
}

float UtilityAI::CalculateFormNewTribeScore(const Caveman& agent, const WorldContext& ctx, std::string& outReason) {
    return CalculateSecedeScore(&agent, ctx, outReason);
}

// AUFTRAG PHASE 4.1: Cooldown für TeachChild & Lehre-Score
float UtilityAI::CalculateTeachChildScore(const Caveman* caveman, const Caveman* target, const WorldContext& ctx, std::string& outReason) {
    if (!caveman || !target) return 0.0f;
    if (caveman->teachCooldown > 0.0f) return 0.0f;
    return SocialActionEvaluator::ScoreTeachChild(*caveman, *target, ctx, outReason);
}

float UtilityAI::CalculateTeachChildScore(const Caveman& caveman, const Caveman& target, const WorldContext& ctx, std::string& outReason) {
    return CalculateTeachChildScore(&caveman, &target, ctx, outReason);
}

float UtilityAI::CalculateTeachChildScore(const Caveman* caveman, const WorldContext& ctx, int& outChildId, std::string& outReason) {
    outChildId = -1;
    if (!caveman) return 0.0f;
    if (caveman->teachCooldown > 0.0f) return 0.0f;
    if (!ctx.allCavemen) return 0.0f;

    float bestScore = 0.0f;
    int bestChildId = -1;
    std::string bestReason;

    for (const auto& other : *ctx.allCavemen) {
        if (other.Id() == caveman->Id() || other.IsDead()) continue;
        if (other.ClanId() != caveman->ClanId()) continue;
        if (other.GetRole() != Role::Child && other.AgeYears() >= 16.0f) continue;

        std::string r;
        float score = CalculateTeachChildScore(caveman, &other, ctx, r);
        if (score > bestScore) {
            bestScore = score;
            bestChildId = other.Id();
            bestReason = std::move(r);
        }
    }

    outChildId = bestChildId;
    outReason = std::move(bestReason);
    return bestScore;
}

float UtilityAI::CalculateTeachChildScore(const Caveman& caveman, const WorldContext& ctx, int& outChildId, std::string& outReason) {
    return CalculateTeachChildScore(&caveman, ctx, outChildId, outReason);
}

float UtilityAI::ScoreSocial(const Caveman& agent, const WorldContext& ctx, int& outTargetId, std::string& outReason) {
    if (!ctx.allCavemen) return 0.0f;

    float bestScore = 0.0f;
    int bestTargetId = -1;
    std::string bestReason = "";

    for (const auto& other : *ctx.allCavemen) {
        if (other.Id() == agent.Id() || other.IsDead()) continue;
        // Territoriale Beschränkung: Alltägliche Sozialaktionen nur innerhalb des eigenen Clans
        if (other.ClanId() != agent.ClanId()) continue;

        std::string reason;

        // AUFTRAG PHASE 1.3: Fortpflanzungs-Priorität prüfen
        float scoreMate = CalculateMateScore(agent, other, ctx, reason);
        if (scoreMate > bestScore) {
            bestScore = scoreMate;
            bestTargetId = other.Id();
            bestReason = reason;
        }

        // AUFTRAG PHASE 4.1: Lehre-Score & Cooldown prüfen
        float scoreTeach = CalculateTeachChildScore(&agent, &other, ctx, reason);
        if (scoreTeach > bestScore) {
            bestScore = scoreTeach;
            bestTargetId = other.Id();
            bestReason = reason;
        }

        float scoreSoc = SocialActionEvaluator::ScoreSocialize(agent, other, ctx, reason);
        if (scoreSoc > bestScore) {
            bestScore = scoreSoc;
            bestTargetId = other.Id();
            bestReason = reason;
        }

        float scoreFlirt = SocialActionEvaluator::ScoreFlirt(agent, other, ctx, reason);
        if (scoreFlirt > bestScore) {
            bestScore = scoreFlirt;
            bestTargetId = other.Id();
            bestReason = reason;
        }

        float scoreFood = SocialActionEvaluator::ScoreShareFood(agent, other, ctx, reason);
        if (scoreFood > bestScore) {
            bestScore = scoreFood;
            bestTargetId = other.Id();
            bestReason = reason;
        }

        float scoreDisp = SocialActionEvaluator::ScoreDispute(agent, other, ctx, reason);
        if (scoreDisp > bestScore) {
            bestScore = scoreDisp;
            bestTargetId = other.Id();
            bestReason = reason;
        }
    }

    outTargetId = bestTargetId;
    outReason = bestReason;
    return bestScore;
}

float UtilityAI::ScoreGatherFood(const Caveman& agent, const WorldContext& ctx, int& outNodeId, std::string& outReason) {
    const Stockpile& stock = ctx.tribe.Stock();
    int totalFood = stock.food + stock.cookedFood;

    // Rollen-Disziplin: Jäger pflücken KEINE Beeren, solange Vorräte oder Wildtiere existieren,
    // AUSSER bei akutem Hunger (>= 60%), wenn keine Tiere verfügbar sind!
    if (agent.GetRole() == Role::Hunter) {
        int availAnimals = ctx.resources.CountAvailable(ResourceType::Animal);
        if (agent.Hunger() < 60.0f && (totalFood > 0 || availAnimals > 0)) {
            return 0.0f;
        }
    }

    int reserveTarget = ctx.population * cfg::kFoodReservePerMember;
    bool allowNeutral = (totalFood < std::max(4, reserveTarget / 2)) || (agent.Hunger() >= 55.0f);
    outNodeId = ctx.resources.FindBest(ResourceType::Bush, agent.Position(), &ctx.world, agent.ClanId(), allowNeutral,
                                       [&](int id) { return agent.IsNodeBlacklisted(id); });
    if (outNodeId < 0) return 0.0f;

    const auto* node = ctx.resources.Get(outNodeId);
    if (!node) return 0.0f;

    float colonyNeed = (totalFood < reserveTarget) ?
        (float)(reserveTarget - totalFood) / (float)reserveTarget : 0.1f;

    float personalNeed = agent.Hunger() / 100.0f;
    float roleAffinity = (agent.GetRole() == Role::Gatherer) ? 1.4f :
                         (agent.GetRole() == Role::Hunter) ? 0.85f : 1.0f;

    if (agent.Hunger() >= 55.0f || totalFood == 0) {
        roleAffinity = std::max(roleAffinity, 1.4f);
        personalNeed = std::pow(agent.Hunger() / 100.0f, 1.35f) * 1.3f;
    }

    float dist = DistanceFactor(agent.Position(), node->position);
    float score = std::clamp((colonyNeed * 0.5f + personalNeed * 0.65f) * roleAffinity * dist, 0.0f, 1.6f);

    // Überlebens-Notbremse: Sobald Hunger >= 60 oder Stammes-Nahrung < pop * 2
    if (agent.Hunger() >= 60.0f || totalFood < ctx.population * 2) {
        float hungerFactor = std::pow(agent.Hunger() / 100.0f, 1.5f);
        float scarcity = (totalFood < ctx.population * 2)
            ? (float)(ctx.population * 2 - totalFood) / (float)std::max(1, ctx.population * 2) : 0.0f;
        float emergencyBoost = 0.85f + hungerFactor * 0.45f + scarcity * 0.30f;
        score = std::max(score, std::clamp(emergencyBoost * dist, 0.85f, 1.6f));
    }

    outReason = (agent.Hunger() >= 60.0f)
        ? TextFormat("Hunger kritisch (%.0f%%)", agent.Hunger())
        : TextFormat("Beeren pflücken (Vorrat: %d)", totalFood);
    return score;
}

float UtilityAI::ScoreHunt(const Caveman& agent, const WorldContext& ctx, int& outNodeId, std::string& outReason) {
    const RoleTraits& traits = GetRoleTraits(agent.GetRole());
    if (!traits.canHunt) return 0.0f;

    outNodeId = ctx.resources.FindBest(ResourceType::Animal, agent.Position(), &ctx.world, agent.ClanId(), true,
                                       [&](int id) { return agent.IsNodeBlacklisted(id); });
    if (outNodeId < 0) return 0.0f;

    const auto* node = ctx.resources.Get(outNodeId);
    if (!node) return 0.0f;

    const Stockpile& stock = ctx.tribe.Stock();
    int totalFood = stock.food + stock.cookedFood;
    int reserveTarget = ctx.population * cfg::kFoodReservePerMember;
    float colonyNeed = (totalFood < reserveTarget) ?
        (float)(reserveTarget - totalFood) / (float)reserveTarget : 0.35f;

    if (stock.hides < 3 && agent.GetClothing() == ClothingTier::None) {
        colonyNeed = std::max(colonyNeed, 0.65f);
    }

    float roleAffinity = (agent.GetRole() == Role::Hunter) ? 2.4f : 0.7f;
    float dist = DistanceFactor(agent.Position(), node->position);

    float score = std::clamp(colonyNeed * 1.1f * roleAffinity * dist, 0.0f, 1.4f);

    // Kognitive Raubtier-Furcht: Geschwächte Jäger meiden Angriffe auf Wildtiere
    if (agent.Hp() < 65.0f) {
        float caution = agent.GetBrainWeights().Get(BrainGene::PredatorCaution);
        score /= (0.7f + caution * 0.6f);
    }

    // Überlebens-Notbremse: Sobald Hunger >= 60 oder Stammes-Nahrung < pop * 2
    if (agent.Hunger() >= 60.0f || totalFood < ctx.population * 2) {
        float hungerFactor = std::pow(agent.Hunger() / 100.0f, 1.5f);
        float scarcity = (totalFood < ctx.population * 2)
            ? (float)(ctx.population * 2 - totalFood) / (float)std::max(1, ctx.population * 2) : 0.0f;
        float emergencyBoost = 0.85f + hungerFactor * 0.45f + scarcity * 0.30f;
        score = std::max(score, std::clamp(emergencyBoost * dist, 0.85f, 1.6f));
    }

    outReason = (agent.Hunger() >= 60.0f)
        ? TextFormat("Hunger kritisch (%.0f%%)", agent.Hunger())
        : TextFormat("Sucht Wildtiere (Fleisch & Vorrat: %d)", totalFood);
    return score;
}

float UtilityAI::ScoreGatherWood(const Caveman& agent, const WorldContext& ctx, int& outNodeId, std::string& outReason) {
    const Stockpile& stock = ctx.tribe.Stock();
    ResourceCost deficit = ctx.tribe.GetPlannedResourceDeficit();
    bool needsHuts = ctx.tribe.NeedsMoreHuts(ctx.population);
    bool hasSite = ctx.tribe.Buildings().FindActiveConstructionSite(agent.Position()) >= 0;
    bool fireNeedsWood = (ctx.tribe.Buildings().FindFireNeedingFuel(agent.Position()) >= 0 && stock.wood < 3);

    bool allowNeutralWood = (stock.wood < 5) || (deficit.wood > 0) || fireNeedsWood;
    outNodeId = ctx.resources.FindBest(ResourceType::Tree, agent.Position(), &ctx.world, agent.ClanId(), allowNeutralWood,
                                       [&](int id) { return agent.IsNodeBlacklisted(id); });
    if (outNodeId < 0) return 0.0f;

    const auto* node = ctx.resources.Get(outNodeId);
    if (!node) return 0.0f;

    float need = 0.15f;
    if (fireNeedsWood) need += 0.65f;
    else if (deficit.wood > 0 || deficit.planks > 0) need += 0.58f;
    else if (hasSite || needsHuts) need += 0.48f;
    if (stock.wood < 8) need += 0.3f;

    float dist = DistanceFactor(agent.Position(), node->position);
    float roleAffinity = (agent.GetRole() == Role::Gatherer || agent.GetRole() == Role::Elder) ? 1.25f : 0.95f;

    float score = std::clamp(need * roleAffinity * dist, 0.0f, 0.92f);
    if (fireNeedsWood) {
        outReason = "Brennholz für Lagerfeuer beschaffen";
    } else if (deficit.wood > 0) {
        outReason = TextFormat("Holz hacken für %s", ctx.tribe.CurrentPlan().description.c_str());
    } else {
        outReason = TextFormat("Holz hacken (Holzvorrat: %d)", stock.wood);
    }
    return score;
}

float UtilityAI::ScoreGatherStone(const Caveman& agent, const WorldContext& ctx, int& outNodeId, std::string& outReason) {
    const Stockpile& stock = ctx.tribe.Stock();
    ResourceCost deficit = ctx.tribe.GetPlannedResourceDeficit();
    bool allowNeutralStone = (stock.stone < 5 || deficit.stone > 0 || deficit.bricks > 0);
    outNodeId = ctx.resources.FindBest(ResourceType::Rock, agent.Position(), &ctx.world, agent.ClanId(), allowNeutralStone,
                                       [&](int id) { return agent.IsNodeBlacklisted(id); });
    if (outNodeId < 0) return 0.0f;

    const auto* node = ctx.resources.Get(outNodeId);
    if (!node) return 0.0f;

    bool needsHuts = ctx.tribe.NeedsMoreHuts(ctx.population);
    bool hasSite = ctx.tribe.Buildings().FindActiveConstructionSite(agent.Position()) >= 0;

    float need = 0.14f;
    if (deficit.stone > 0 || deficit.bricks > 0) need += 0.55f;
    else if (hasSite || needsHuts) need += 0.45f;
    if (stock.stone < 6) need += 0.35f;
    if (!ctx.tribe.Skills().IsUnlocked("flint") || agent.GetToolTier() == ToolTier::BareHands) {
        need += 0.28f;
    }

    float dist = DistanceFactor(agent.Position(), node->position);
    float roleAffinity = (agent.GetRole() == Role::Gatherer) ? 1.15f : 0.95f;

    float score = std::clamp(need * roleAffinity * dist, 0.0f, 0.88f);
    outReason = (deficit.stone > 0)
        ? TextFormat("Steine schlagen für %s", ctx.tribe.CurrentPlan().description.c_str())
        : TextFormat("Steine & Flint suchen (Stein: %d)", stock.stone);
    return score;
}

float UtilityAI::ScoreBuild(const Caveman& agent, const WorldContext& ctx, int& outBuildingId, std::string& outReason) {
    const RoleTraits& traits = GetRoleTraits(agent.GetRole());
    if (!traits.canBuild) return 0.0f;

    // 1. Erst echte Baustellen / Ausbauten
    outBuildingId = ctx.tribe.Buildings().FindActiveConstructionSite(agent.Position());
    bool isRepair = false;

    // 2. Falls keine Baustelle existiert, prüfe auf dringenden Reparaturbedarf (< 50% HP, max 1 Arbeiter)
    if (outBuildingId < 0) {
        outBuildingId = ctx.tribe.Buildings().FindRepairSite(agent.Position());
        isRepair = (outBuildingId >= 0);
    }

    if (outBuildingId < 0 || agent.IsTargetBlacklisted(outBuildingId)) return 0.0f;

    if (!CanPerformAction(agent, CavemanState::Building, ctx, outBuildingId)) return 0.0f;

    const Building* b = ctx.tribe.Buildings().Get(outBuildingId);
    if (!b) return 0.0f;

    float basePriority = isRepair ? 0.85f : 0.78f;
    if (b->type == BuildingType::TownCenter) basePriority = 0.88f;
    if (ctx.tribe.NeedsMoreHuts(ctx.population) && b->type == BuildingType::Hut) basePriority = std::max(basePriority, 0.85f);

    float dist = DistanceFactor(agent.Position(), b->position);
    float score = std::clamp(basePriority * traits.buildEfficiency * dist, 0.0f, 0.95f);

    outReason = isRepair
        ? TextFormat("Repariert %s (%d%% HP)", b->DisplayName().c_str(), (int)(b->hp / b->maxHp * 100.0f))
        : TextFormat("Baut %s (%d%%)", b->DisplayName().c_str(), (int)b->progress);
    return score;
}

float UtilityAI::ScoreCraft(const Caveman& agent, const WorldContext& ctx, int& outBenchId, std::string& outReason) {
    if (agent.GetRole() == Role::Child) return 0.0f;

    outBenchId = ctx.tribe.Buildings().FindCraftBench(agent.Position());
    if (outBenchId < 0) return 0.0f;

    const Stockpile& stock = ctx.tribe.Stock();
    const SkillTree& skills = ctx.tribe.Skills();
    ResourceCost deficit = ctx.tribe.GetPlannedResourceDeficit();

    bool canPlanks = (skills.IsUnlocked("carpentry") || ctx.tribe.Age().CanCraftRefined()) &&
                     stock.wood >= 2 && (stock.planks < 15 || deficit.planks > 0);
    bool canBricks = (skills.IsUnlocked("masonry") || ctx.tribe.Age().CurrentEra() >= Era::IronAge) &&
                     stock.stone >= 2 && (stock.bricks < 10 || deficit.bricks > 0);
    bool needsGear = agent.NeedsGearUpgrade(ctx.tribe);
    bool needsExpGear = (skills.IsUnlocked("torches") && stock.torches < 3 && stock.wood >= 1) ||
                        (skills.IsUnlocked("flint") && stock.pickaxes < 2 && stock.wood >= 1 && stock.stone >= 1);

    bool canPracticeTech = false;
    for (const auto& n : skills.AllNodes()) {
        if (!n.requiresPOI && n.state == TechState::Ready) {
            canPracticeTech = true;
            break;
        }
    }

    if (!canPlanks && !canBricks && !needsGear && !needsExpGear && !canPracticeTech) return 0.0f;

    const Building* b = ctx.tribe.Buildings().Get(outBenchId);
    if (!b) return 0.0f;

    float priority = 0.62f;
    if (deficit.planks > 0 || deficit.bricks > 0) priority = 0.84f;
    else if (needsGear || needsExpGear) priority = 0.78f;
    else if (agent.GetRole() == Role::Elder && canPracticeTech) priority = 0.76f;

    float dist = DistanceFactor(agent.Position(), b->position);
    float score = std::clamp(priority * dist, 0.0f, 0.89f);

    outReason = needsGear
        ? "Fertigt Ausrüstung an Werkbank"
        : "Tüftelt an der Werkbank";
    return score;
}

float UtilityAI::ScoreWander(const Caveman& agent, const WorldContext& ctx, std::string& outReason) {
    (void)ctx;
    float baseScore = (agent.GetRole() == Role::Child) ? 0.22f : 0.08f;
    outReason = "Erkundet die nähere Umgebung";
    return baseScore;
}

float UtilityAI::ScoreExpedition(const Caveman& agent, const WorldContext& ctx, int& outPoiId, std::string& outReason) {
    outPoiId = -1;

    // Kinder gehen nicht auf gefährliche Expeditionen
    if (agent.GetRole() == Role::Child) return 0.0f;

    // Nur körperlich fitte und gesunde Menschen brechen auf
    if (agent.Hp() < 45.0f || agent.Hunger() > 60.0f || agent.Tiredness() > 65.0f || agent.Warmth() < 35.0f) {
        return 0.0f;
    }

    // Prüfen, ob ein bestimmtes Forschungsziel im Skilltree fokussiert ist
    int focusId = ctx.tribe.Skills().GetFocusNode();
    int targetPoiId = -1;

    if (focusId >= 0) {
        const auto* node = ctx.tribe.Skills().GetNode(focusId);
        if (node && node->requiresPOI) {
            const auto* targetPoi = ctx.tribe.POIs().GetByType(node->targetPOIType);
            if (targetPoi && targetPoi->CanBeExplored() && targetPoi->state != POIState::Explored) {
                targetPoiId = targetPoi->id;
            }
        }
    }

    // Passenden POI suchen (entweder fokussiert oder der nächste bereite der aktuellen Epoche)
    int bestPoiId = ctx.tribe.POIs().FindBestForExpedition(agent.Position(), ctx.tribe.Stock(), targetPoiId, ctx.tribe.Age().CurrentEra());
    if (bestPoiId < 0 || agent.IsTargetBlacklisted(bestPoiId)) return 0.0f;
    if (!CanPerformAction(agent, CavemanState::Expedition, ctx, bestPoiId)) return 0.0f;

    const auto* poi = ctx.tribe.POIs().Get(bestPoiId);
    if (!poi) return 0.0f;

    // Konfliktvermeidung: Nicht zwei Forscher zum selben Ort schicken
    if (poi->assignedExplorerId >= 0 && poi->assignedExplorerId != agent.Id()) {
        return 0.0f;
    }

    outPoiId = bestPoiId;

    // Scoring: Meilensteine und Spielerfokus haben hohe Priorität
    bool isFocus = (targetPoiId == bestPoiId);
    bool isMilestone = (poi->requiredEra == ctx.tribe.Age().CurrentEra());

    float baseScore = isFocus ? 1.55f : (isMilestone ? 1.15f : 0.85f);

    // Persönlichkeit: Mutige (hohe Dominanz), kontaktfreudige Menschen übernehmen gerne Führung
    const auto& p = agent.GetPersonality();
    float persBonus = p.dominance * 0.15f + p.sociability * 0.10f;

    // Entfernungsabzug
    float distFactor = DistanceFactor(agent.Position(), poi->position, 900.0f);
    float moraleFactor = agent.Morale() / 100.0f;

    float finalScore = std::clamp((baseScore + persBonus) * distFactor * (0.65f + moraleFactor * 0.35f), 0.0f, 1.8f);

    outReason = TextFormat("Expedition nach '%s' (Score: %.2f)", poi->name.c_str(), finalScore);
    return finalScore;
}

float UtilityAI::ScoreBorderRaid(const Caveman& agent, const WorldContext& ctx, int& outTileX, int& outTileY, std::string& outReason) {
    if (agent.GetRole() == Role::Child) return 0.0f;
    if (agent.Hp() < 50.0f) return 0.0f;

    auto contacts = ctx.world.GetBorderContacts(agent.ClanId());
    if (contacts.empty()) return 0.0f;

    // Finde den Nachbar-Clan mit höchster Grenzberührung / Reibung
    const BorderContact* bestContact = nullptr;
    int maxBorderTiles = 0;
    for (const auto& bc : contacts) {
        if (bc.sharedBorderTiles > maxBorderTiles && !bc.contactTiles.empty()) {
            maxBorderTiles = bc.sharedBorderTiles;
            bestContact = &bc;
        }
    }
    if (!bestContact || bestContact->contactTiles.empty()) return 0.0f;

    // Faktoren für Grenzaggression (Phase 4.1):
    // 1. Ressourcenknappheit (Hunger, Mangel an Vorräten erhöht Aggression)
    const Stockpile& stock = ctx.tribe.Stock();
    float scarcity = 0.0f;
    if (stock.food < ctx.population * 2) scarcity += 0.35f;
    if (stock.wood < 5) scarcity += 0.20f;
    if (stock.stone < 4) scarcity += 0.20f;

    // 2. Persönlichkeit (Aggression & Dominanz) und Rolle (Jäger patrouillieren/kämpfen vorrangig)
    float aggression = agent.GetPersonality().aggression * 0.35f + agent.GetPersonality().dominance * 0.25f;
    float roleBonus = (agent.GetRole() == Role::Hunter) ? 0.30f : (agent.GetRole() == Role::Elder ? 0.10f : 0.05f);

    // 3. Waffen-Bonus
    float weaponBonus = (agent.GetWeaponTier() >= WeaponTier::FlintSpear) ? 0.15f : 0.0f;

    float score = std::clamp(scarcity * 0.45f + aggression + roleBonus + weaponBonus, 0.0f, 0.85f);
    if (score < 0.45f) return 0.0f;

    // Wähle eine umkämpfte Grenzkachel
    int pickIdx = GetRandomValue(0, (int)bestContact->contactTiles.size() - 1);
    outTileX = bestContact->contactTiles[pickIdx].first;
    outTileY = bestContact->contactTiles[pickIdx].second;

    outReason = TextFormat("Grenzüberfall: Sichert umkämpfte Grenzkachel (%d, %d) gegen Nachbarn! (Score: %.2f)",
                           outTileX, outTileY, score);
    return score;
}

