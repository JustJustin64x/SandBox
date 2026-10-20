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

UtilityDecision UtilityAI::Evaluate(const Caveman& agent, const WorldContext& ctx) {
    UtilityDecision best;
    best.state = CavemanState::Idle;
    best.score = -1.0f;

    // 0. Panik / Flucht vor Feuer & Strom (höchste Überlebenspriorität)
    {
        std::string reason;
        float score = ScoreFlee(agent, ctx, reason);
        if (score > best.score) {
            best = { CavemanState::Fleeing, ResourceType::Bush, -1, -1, -1, score, reason };
        }
    }

    // 1. Essen
    {
        std::string reason;
        float score = ScoreEat(agent, ctx, reason);
        if (score > best.score) {
            best = { CavemanState::Eating, ResourceType::Bush, -1, -1, -1, score, reason };
        }
    }

    // 2. Schlafen
    {
        int hutId = -1;
        std::string reason;
        float score = ScoreSleep(agent, ctx, hutId, reason);
        if (score > best.score) {
            best = { CavemanState::Sleeping, ResourceType::Bush, hutId, -1, -1, score, reason };
        }
    }

    // 3. Aufwärmen / Schutz bei Sturm & Kälte
    {
        int shelterId = -1;
        std::string reason;
        float score = ScoreWarmUp(agent, ctx, shelterId, reason);
        if (score > best.score) {
            best = { CavemanState::WarmingUp, ResourceType::Bush, shelterId, -1, -1, score, reason };
        }
    }

    // 4. Soziales Handeln (Gespräch, Klatsch, Flirten, Teilen)
    {
        int targetCharId = -1;
        std::string reason;
        float score = ScoreSocial(agent, ctx, targetCharId, reason);
        if (score > best.score && targetCharId >= 0) {
            best = { CavemanState::Socializing, ResourceType::Bush, -1, -1, targetCharId, score, reason };
        }
    }

    // 5. Forschung & Expedition (POIs erkunden mit Ausrüstung)
    {
        int poiId = -1;
        std::string reason;
        float score = ScoreExpedition(agent, ctx, poiId, reason);
        if (score > best.score && poiId >= 0) {
            best = { CavemanState::Expedition, ResourceType::Bush, -1, -1, -1, score, reason, poiId };
        }
    }

    // 6. Bauen / Reparieren
    {
        int buildingId = -1;
        std::string reason;
        float score = ScoreBuild(agent, ctx, buildingId, reason);
        if (score > best.score && buildingId >= 0) {
            best = { CavemanState::Building, ResourceType::Tree, buildingId, -1, -1, score, reason };
        }
    }

    // 6. Veredeln an der Werkbank
    {
        int benchId = -1;
        std::string reason;
        float score = ScoreCraft(agent, ctx, benchId, reason);
        if (score > best.score && benchId >= 0) {
            best = { CavemanState::Crafting, ResourceType::Tree, benchId, -1, -1, score, reason };
        }
    }

    // 7. Jagd (Wildtiere)
    {
        int nodeId = -1;
        std::string reason;
        float score = ScoreHunt(agent, ctx, nodeId, reason);
        if (score > best.score && nodeId >= 0) {
            best = { CavemanState::Hunting, ResourceType::Animal, -1, nodeId, -1, score, reason };
        }
    }

    // 8a. Neolithische Landwirtschaft & Tierzucht (Ackerfeld & Tiergehege bestellen/ernten)
    {
        int farmId = ctx.tribe.Buildings().FindFarmOrPenNeedingWork(agent.Position());
        if (farmId >= 0 && agent.GetRole() != Role::Child) {
            if (const Building* b = ctx.tribe.Buildings().Get(farmId)) {
                float roleAff = (agent.GetRole() == Role::Gatherer || agent.GetRole() == Role::Elder) ? 1.25f : 0.90f;
                float dist = DistanceFactor(agent.Position(), b->position);
                float basePrio = b->IsHarvestReady() ? 0.86f : 0.68f;
                float farmScore = std::clamp(basePrio * roleAff * dist, 0.0f, 0.93f);
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
    {
        int nodeId = -1;
        std::string reason;
        float score = ScoreGatherFood(agent, ctx, nodeId, reason);
        if (score > best.score && nodeId >= 0) {
            best = { CavemanState::GatheringFood, ResourceType::Bush, -1, nodeId, -1, score, reason };
        }
    }

    // 9. Holz beschaffen
    {
        int nodeId = -1;
        std::string reason;
        float score = ScoreGatherWood(agent, ctx, nodeId, reason);
        if (score > best.score && nodeId >= 0) {
            best = { CavemanState::GatheringMaterials, ResourceType::Tree, -1, nodeId, -1, score, reason };
        }
    }

    // 10. Stein beschaffen
    {
        int nodeId = -1;
        std::string reason;
        float score = ScoreGatherStone(agent, ctx, nodeId, reason);
        if (score > best.score && nodeId >= 0) {
            best = { CavemanState::GatheringMaterials, ResourceType::Rock, -1, nodeId, -1, score, reason };
        }
    }

    // 11. Grenzkampf, Grenzpatrouille & Kachel-Eroberung (Phase 4: Border Raid)
    {
        int tileX = -1, tileY = -1;
        std::string reason;
        float score = ScoreBorderRaid(agent, ctx, tileX, tileY, reason);
        if (score > best.score && tileX >= 0) {
            best = { CavemanState::BorderRaid, ResourceType::Bush, -1, tileX, tileY, score, reason };
        }
    }

    // 12. Wandern / Verweilen (Fallback)
    {
        std::string reason;
        float score = ScoreWander(agent, ctx, reason);
        if (score > best.score) {
            best = { CavemanState::Wandering, ResourceType::Bush, -1, -1, -1, score, reason };
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
        if (tiredness < 45.0f && ap >= 25.0f) return 0.0f;
    } else {
        if (tiredness < 70.0f && ap >= 15.0f) return 0.0f;
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

    // Rollen-Disziplin: Jäger pflücken KEINE Beeren, solange Vorräte oder Wildtiere existieren!
    if (agent.GetRole() == Role::Hunter) {
        int availAnimals = ctx.resources.CountAvailable(ResourceType::Animal);
        if (totalFood > 0 || availAnimals > 0) {
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
                         (agent.GetRole() == Role::Hunter) ? 0.7f : 1.0f;

    if (agent.Hunger() >= 55.0f || totalFood == 0) {
        roleAffinity = std::max(roleAffinity, 1.4f);
        personalNeed = std::pow(agent.Hunger() / 100.0f, 1.35f) * 1.3f;
    }

    float dist = DistanceFactor(agent.Position(), node->position);
    float score = std::clamp((colonyNeed * 0.5f + personalNeed * 0.65f) * roleAffinity * dist, 0.0f, 1.6f);

    outReason = TextFormat("Beeren pflücken (Vorrat: %d, Hunger: %.0f%%)", totalFood, agent.Hunger());
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
    outReason = TextFormat("Sucht Wildtiere (Fleisch & Vorrat: %d)", totalFood);
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

    outBuildingId = ctx.tribe.Buildings().FindActiveConstructionSite(agent.Position());
    if (outBuildingId < 0) return 0.0f;

    const Building* b = ctx.tribe.Buildings().Get(outBuildingId);
    if (!b) return 0.0f;

    bool isRepair = (b->finished && !b->underUpgrade && b->hp < b->maxHp);
    float basePriority = isRepair ? 0.90f : 0.76f;
    if (b->type == BuildingType::TownCenter) basePriority = 0.88f;
    if (ctx.tribe.NeedsMoreHuts(ctx.population) && b->type == BuildingType::Hut) basePriority = std::max(basePriority, 0.85f);

    float dist = DistanceFactor(agent.Position(), b->position);
    float score = std::clamp(basePriority * traits.buildEfficiency * dist, 0.0f, 0.95f);

    outReason = isRepair
        ? TextFormat("Repariert %s", b->DisplayName().c_str())
        : TextFormat("Baut %s", b->DisplayName().c_str());
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

    // Passenden POI suchen (entweder fokussiert oder der nächste bereite)
    int bestPoiId = ctx.tribe.POIs().FindBestForExpedition(agent.Position(), ctx.tribe.Stock(), targetPoiId);
    if (bestPoiId < 0) return 0.0f;

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

