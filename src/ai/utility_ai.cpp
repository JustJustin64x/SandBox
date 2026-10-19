#include "utility_ai.h"
#include "caveman.h"
#include "tribe.h"
#include "building.h"
#include "resource_node.h"
#include "relationship.h"
#include "social_structure.h"
#include "social_actions.h"
#include "poi.h"
#include "skill_tree.h"
#include "config.h"

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

    // 3. Aufwärmen (bei Nacht / Kälte)
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

    // 8. Nahrung sammeln (Beeren)
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

    // 11. Wandern / Verweilen (Fallback)
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

float UtilityAI::ScoreEat(const Caveman& agent, const WorldContext& ctx, std::string& outReason) {
    const Stockpile& stock = ctx.tribe.Stock();
    if (stock.food <= 0) return 0.0f;

    float hunger = agent.Hunger();
    if (hunger < 25.0f) return 0.0f;

    // Altruistische Figuren lassen anderen bei knappen Vorräten eher den Vortritt
    float altruismFactor = 1.0f - (agent.GetPersonality().altruism * 0.2f);
    float urgency = std::pow(hunger / 100.0f, 2.2f) * altruismFactor;
    float dist = DistanceFactor(agent.Position(), ctx.tribe.StockpilePosition());
    float score = std::clamp(urgency * 1.3f * dist, 0.0f, 1.0f);

    outReason = TextFormat("Essen (Score: %.2f - Hunger bei %.0f%%)", score, hunger);
    return score;
}

float UtilityAI::ScoreSleep(const Caveman& agent, const WorldContext& ctx, int& outHutId, std::string& outReason) {
    float tiredness = agent.Tiredness();
    float ap = agent.Ap();

    if (tiredness < 35.0f && ap > 40.0f) return 0.0f;

    float tirednessNorm = tiredness / 100.0f;
    float apExhaustion = (100.0f - ap) / 100.0f;
    float urgency = std::max(std::pow(tirednessNorm, 1.8f), apExhaustion * 0.9f);

    outHutId = ctx.tribe.Buildings().FindBestShelter(agent.Position());
    float shelterBonus = (outHutId >= 0) ? 1.15f : 0.85f;

    if (agent.Hunger() >= cfg::kHungerCritical) urgency *= 0.3f;

    float score = std::clamp(urgency * shelterBonus, 0.0f, 0.98f);
    outReason = TextFormat("Schlafen (Score: %.2f - Müdigkeit %.0f%%)", score, tiredness);
    return score;
}

float UtilityAI::ScoreWarmUp(const Caveman& agent, const WorldContext& ctx, int& outShelterId, std::string& outReason) {
    float warmth = agent.Warmth();
    if (warmth > 60.0f) return 0.0f;

    float coldness = (100.0f - warmth) / 100.0f;
    float urgency = std::pow(coldness, 2.0f);

    outShelterId = ctx.tribe.Buildings().FindBestShelter(agent.Position());
    Vector2 targetPos = (outShelterId >= 0) ? ctx.tribe.Buildings().Get(outShelterId)->position : ctx.tribe.CampPosition();
    float dist = DistanceFactor(agent.Position(), targetPos);

    float score = std::clamp(urgency * 1.25f * dist, 0.0f, 0.99f);
    outReason = TextFormat("Aufwärmen (Score: %.2f - Wärme nur %.0f%%)", score, warmth);
    return score;
}

float UtilityAI::ScoreSocial(const Caveman& agent, const WorldContext& ctx, int& outTargetId, std::string& outReason) {
    if (!ctx.allCavemen) return 0.0f;

    float bestScore = 0.0f;
    int bestTargetId = -1;
    std::string bestReason = "";

    for (const auto& other : *ctx.allCavemen) {
        if (other.Id() == agent.Id() || other.IsDead()) continue;

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
    outNodeId = ctx.resources.FindBest(ResourceType::Bush, agent.Position());
    if (outNodeId < 0) return 0.0f;

    const auto* node = ctx.resources.Get(outNodeId);
    if (!node) return 0.0f;

    const Stockpile& stock = ctx.tribe.Stock();
    int reserveTarget = ctx.population * cfg::kFoodReservePerMember;
    float colonyNeed = (stock.food < reserveTarget) ?
        (float)(reserveTarget - stock.food) / (float)reserveTarget : 0.1f;

    float personalNeed = agent.Hunger() / 100.0f;
    float roleAffinity = (agent.GetRole() == Role::Gatherer) ? 1.4f :
                         (agent.GetRole() == Role::Hunter) ? 0.6f : 1.0f;

    float dist = DistanceFactor(agent.Position(), node->position);
    float score = std::clamp((colonyNeed * 0.6f + personalNeed * 0.4f) * roleAffinity * dist, 0.0f, 0.92f);

    outReason = TextFormat("Beeren sammeln (Score: %.2f - Vorrat: %d/%d)", score, stock.food, reserveTarget);
    return score;
}

float UtilityAI::ScoreHunt(const Caveman& agent, const WorldContext& ctx, int& outNodeId, std::string& outReason) {
    const RoleTraits& traits = GetRoleTraits(agent.GetRole());
    if (!traits.canHunt) return 0.0f;

    outNodeId = ctx.resources.FindBest(ResourceType::Animal, agent.Position());
    if (outNodeId < 0) return 0.0f;

    const auto* node = ctx.resources.Get(outNodeId);
    if (!node) return 0.0f;

    const Stockpile& stock = ctx.tribe.Stock();
    int reserveTarget = ctx.population * cfg::kFoodReservePerMember;
    float colonyNeed = (stock.food < reserveTarget) ?
        (float)(reserveTarget - stock.food) / (float)reserveTarget : 0.15f;

    float roleAffinity = (agent.GetRole() == Role::Hunter) ? 1.5f : 0.7f;
    float dist = DistanceFactor(agent.Position(), node->position);

    float score = std::clamp(colonyNeed * 1.1f * roleAffinity * dist, 0.0f, 0.95f);
    outReason = TextFormat("Jagen (Score: %.2f - Fleischbedarf)", score);
    return score;
}

float UtilityAI::ScoreGatherWood(const Caveman& agent, const WorldContext& ctx, int& outNodeId, std::string& outReason) {
    outNodeId = ctx.resources.FindBest(ResourceType::Tree, agent.Position());
    if (outNodeId < 0) return 0.0f;

    const auto* node = ctx.resources.Get(outNodeId);
    if (!node) return 0.0f;

    const Stockpile& stock = ctx.tribe.Stock();
    bool needsHuts = ctx.tribe.NeedsMoreHuts(ctx.population);
    bool hasSite = ctx.tribe.Buildings().FindActiveConstructionSite(agent.Position()) >= 0;

    float need = 0.15f;
    if (hasSite || needsHuts) need += 0.5f;
    if (stock.wood < 8) need += 0.3f;

    float dist = DistanceFactor(agent.Position(), node->position);
    float roleAffinity = (agent.GetRole() == Role::Gatherer || agent.GetRole() == Role::Elder) ? 1.2f : 0.9f;

    float score = std::clamp(need * roleAffinity * dist, 0.0f, 0.88f);
    outReason = TextFormat("Holzhacken (Score: %.2f - Holz: %d)", score, stock.wood);
    return score;
}

float UtilityAI::ScoreGatherStone(const Caveman& agent, const WorldContext& ctx, int& outNodeId, std::string& outReason) {
    outNodeId = ctx.resources.FindBest(ResourceType::Rock, agent.Position());
    if (outNodeId < 0) return 0.0f;

    const auto* node = ctx.resources.Get(outNodeId);
    if (!node) return 0.0f;

    const Stockpile& stock = ctx.tribe.Stock();
    bool needsHuts = ctx.tribe.NeedsMoreHuts(ctx.population);
    bool hasSite = ctx.tribe.Buildings().FindActiveConstructionSite(agent.Position()) >= 0;

    float need = 0.1f;
    if (hasSite || needsHuts) need += 0.45f;
    if (stock.stone < 6) need += 0.35f;

    float dist = DistanceFactor(agent.Position(), node->position);
    float roleAffinity = (agent.GetRole() == Role::Gatherer) ? 1.1f : 0.9f;

    float score = std::clamp(need * roleAffinity * dist, 0.0f, 0.86f);
    outReason = TextFormat("Steine sammeln (Score: %.2f - Stein: %d)", score, stock.stone);
    return score;
}

float UtilityAI::ScoreBuild(const Caveman& agent, const WorldContext& ctx, int& outBuildingId, std::string& outReason) {
    const RoleTraits& traits = GetRoleTraits(agent.GetRole());
    if (!traits.canBuild) return 0.0f;

    outBuildingId = ctx.tribe.Buildings().FindActiveConstructionSite(agent.Position());
    if (outBuildingId < 0) return 0.0f;

    const Building* b = ctx.tribe.Buildings().Get(outBuildingId);
    if (!b) return 0.0f;

    float basePriority = 0.65f;
    if (b->type == BuildingType::TownCenter) basePriority = 0.82f;
    if (ctx.tribe.NeedsMoreHuts(ctx.population) && b->type == BuildingType::Hut) basePriority = 0.78f;

    float dist = DistanceFactor(agent.Position(), b->position);
    float score = std::clamp(basePriority * traits.buildEfficiency * dist, 0.0f, 0.94f);

    outReason = TextFormat("Bauen (Score: %.2f - %s)", score, b->DisplayName().c_str());
    return score;
}

float UtilityAI::ScoreCraft(const Caveman& agent, const WorldContext& ctx, int& outBenchId, std::string& outReason) {
    if (!ctx.tribe.Age().CanCraftRefined()) return 0.0f;

    outBenchId = ctx.tribe.Buildings().FindCraftBench(agent.Position());
    if (outBenchId < 0) return 0.0f;

    const Stockpile& stock = ctx.tribe.Stock();
    bool canPlanks = stock.wood >= 2 && stock.planks < 15;
    bool canBricks = stock.stone >= 2 && stock.bricks < 10;
    if (!canPlanks && !canBricks) return 0.0f;

    const Building* b = ctx.tribe.Buildings().Get(outBenchId);
    if (!b) return 0.0f;

    float dist = DistanceFactor(agent.Position(), b->position);
    float score = std::clamp(0.68f * dist, 0.0f, 0.85f);

    outReason = TextFormat("Veredeln (Score: %.2f - Werkbank)", score);
    return score;
}

float UtilityAI::ScoreWander(const Caveman& agent, const WorldContext& ctx, std::string& outReason) {
    (void)ctx;
    float baseScore = (agent.GetRole() == Role::Child) ? 0.22f : 0.08f;
    outReason = TextFormat("Erkunden / Verweilen (Score: %.2f)", baseScore);
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

