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
    static float ScoreShareFood(const Caveman& agent, const Caveman& target, const WorldContext& ctx, std::string& outReason);
    static float ScoreDispute(const Caveman& agent, const Caveman& target, const WorldContext& ctx, std::string& outReason);

    // Ausführung sozialer Handlungen
    static void ExecuteSocialize(Caveman& agent, Caveman& target, WorldContext& ctx);
    static void ExecuteFlirt(Caveman& agent, Caveman& target, WorldContext& ctx);
    static void ExecuteShareFood(Caveman& agent, Caveman& target, WorldContext& ctx);
    static void ExecuteDispute(Caveman& agent, Caveman& target, WorldContext& ctx);
};

#endif // SOCIAL_ACTIONS_H

