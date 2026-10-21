#ifndef SOCIAL_ACTIONS_H
#define SOCIAL_ACTIONS_H

#include "common.h"
#include <string>

class Caveman;
struct WorldContext;

class SocialActionEvaluator {
public:
    static float ScoreSocialize(const Caveman& agent, const Caveman& target, const WorldContext& ctx, std::string& outReason);
    static float ScoreFlirt(const Caveman& agent, const Caveman& target, const WorldContext& ctx, std::string& outReason);
    static float CalculateMateScore(const Caveman& agent, const Caveman& target, const WorldContext& ctx, std::string& outReason);
    static float ScoreShareFood(const Caveman& agent, const Caveman& target, const WorldContext& ctx, std::string& outReason);
    static float ScoreDispute(const Caveman& agent, const Caveman& target, const WorldContext& ctx, std::string& outReason);
    static float ScoreTeachChild(const Caveman& agent, const Caveman& target, const WorldContext& ctx, std::string& outReason);

    // Stammbaum- & Partnerschafts-Validierung
    static bool CanBecomePartners(const Caveman* a, const Caveman* b);
    static bool CanBecomePartners(const Caveman& a, const Caveman& b);

    // Biologische Fortpflanzung & Reproduktion
    static bool TryReproduce(Caveman* partnerA, Caveman* partnerB, WorldContext& ctx);

    // Ausführung sozialer Handlungen
    static void ExecuteSocialize(Caveman& agent, Caveman& target, WorldContext& ctx);
    static void ExecuteFlirt(Caveman& agent, Caveman& target, WorldContext& ctx);
    static void ExecuteMating(Caveman& agent, Caveman& partner, WorldContext& ctx);
    static void ExecuteShareFood(Caveman& agent, Caveman& target, WorldContext& ctx);
    static void ExecuteDispute(Caveman& agent, Caveman& target, WorldContext& ctx);
    static void ExecuteTeachChild(Caveman& agent, Caveman& target, WorldContext& ctx);
};

#endif // SOCIAL_ACTIONS_H

