#ifndef UTILITY_AI_H
#define UTILITY_AI_H

#include "common.h"
#include "resource_node.h"
#include <string>

#include "caveman.h"

// Ergebnis der Utility-Bewertung
struct UtilityDecision {
    CavemanState state = CavemanState::Idle;
    ResourceType resource = ResourceType::Bush;
    int targetBuildingId = -1;
    int targetNodeId = -1;
    int targetCharacterId = -1;
    int targetPoiId = -1;
    float score = 0.0f;
    std::string reason;

    UtilityDecision() = default;
    UtilityDecision(CavemanState s, ResourceType r, int bId, int nId, int cId, float sc, std::string re, int pId = -1)
        : state(s), resource(r), targetBuildingId(bId), targetNodeId(nId), targetCharacterId(cId), targetPoiId(pId), score(sc), reason(std::move(re)) {}
};

class UtilityAI {
public:
    // Bewertet alle Handlungsoptionen und wählt situativ die beste Aktion
    static UtilityDecision Evaluate(const Caveman& agent, const WorldContext& ctx);

private:
    static float ScoreEat(const Caveman& agent, const WorldContext& ctx, std::string& outReason);
    static float ScoreSleep(const Caveman& agent, const WorldContext& ctx, int& outHutId, std::string& outReason);
    static float ScoreWarmUp(const Caveman& agent, const WorldContext& ctx, int& outShelterId, std::string& outReason);
    static float ScoreSocial(const Caveman& agent, const WorldContext& ctx, int& outTargetId, std::string& outReason);
    static float ScoreExpedition(const Caveman& agent, const WorldContext& ctx, int& outPoiId, std::string& outReason);
    static float ScoreGatherFood(const Caveman& agent, const WorldContext& ctx, int& outNodeId, std::string& outReason);
    static float ScoreHunt(const Caveman& agent, const WorldContext& ctx, int& outNodeId, std::string& outReason);
    static float ScoreGatherWood(const Caveman& agent, const WorldContext& ctx, int& outNodeId, std::string& outReason);
    static float ScoreGatherStone(const Caveman& agent, const WorldContext& ctx, int& outNodeId, std::string& outReason);
    static float ScoreBuild(const Caveman& agent, const WorldContext& ctx, int& outBuildingId, std::string& outReason);
    static float ScoreCraft(const Caveman& agent, const WorldContext& ctx, int& outBenchId, std::string& outReason);
    static float ScoreWander(const Caveman& agent, const WorldContext& ctx, std::string& outReason);

    // Hilfsfunktion: Entfernungsfaktor (1.0 = direkt daneben, fällt sanft ab)
    static float DistanceFactor(Vector2 from, Vector2 to, float maxRange = 600.0f);
};

#endif // UTILITY_AI_H
