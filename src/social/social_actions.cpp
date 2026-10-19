#include "social_actions.h"
#include "caveman.h"
#include "tribe.h"
#include "relationship.h"
#include "social_structure.h"
#include "event_log.h"
#include "config.h"

#include <cmath>
#include <algorithm>

float SocialActionEvaluator::ScoreSocialize(const Caveman& agent, const Caveman& target, const WorldContext& ctx, std::string& outReason) {
    if (agent.Id() == target.Id() || target.IsDead()) return 0.0f;

    const RelationshipManager& rels = ctx.tribe.Relationships();
    float aff = rels.GetAffinity(agent.Id(), target.Id());

    // Feinde werden beim Plaudern gemieden
    if (aff < -20.0f) return 0.0f;

    float dist = Vector2Distance(agent.Position(), target.Position());
    if (dist > 180.0f) return 0.0f; // Nur in der Nähe

    float sociability = (agent.GetPersonality().sociability + 1.0f) * 0.5f; // 0..1
    float socialNeed = agent.SocialNeed() / 100.0f;

    // Nepotismus-Vorteil (gleicher Clan)
    bool sameClan = ctx.tribe.Hierarchy().AreSameClan(agent.Id(), target.Id());
    float clanBonus = sameClan ? 1.25f : 1.0f;

    // Freunde bevorzugt
    float affFactor = (aff + 50.0f) / 150.0f; // 0..1
    float distFactor = 1.0f - (dist / 180.0f) * 0.5f;

    float score = std::clamp((sociability * 0.4f + socialNeed * 0.6f) * affFactor * clanBonus * distFactor, 0.0f, 0.85f);
    outReason = TextFormat("Gespräch suchen mit %s (Score: %.2f)", target.Name().c_str(), score);
    return score;
}

float SocialActionEvaluator::ScoreFlirt(const Caveman& agent, const Caveman& target, const WorldContext& ctx, std::string& outReason) {
    if (agent.Id() == target.Id() || target.IsDead()) return 0.0f;
    if (agent.GetRole() == Role::Child || target.GetRole() == Role::Child) return 0.0f; // Keine Kinder

    const RelationshipManager& rels = ctx.tribe.Relationships();
    if (rels.GetPartner(agent.Id()) >= 0 || rels.GetPartner(target.Id()) >= 0) return 0.0f; // Bereits vergeben

    float aff = rels.GetAffinity(agent.Id(), target.Id());
    if (aff < 30.0f) return 0.0f; // Mindestsympathie

    float dist = Vector2Distance(agent.Position(), target.Position());
    if (dist > 140.0f) return 0.0f;

    float score = std::clamp((aff / 100.0f) * 0.88f, 0.0f, 0.90f);
    outReason = TextFormat("Um %s werben / flirten (Score: %.2f)", target.Name().c_str(), score);
    return score;
}

float SocialActionEvaluator::ScoreShareFood(const Caveman& agent, const Caveman& target, const WorldContext& ctx, std::string& outReason) {
    if (agent.Id() == target.Id() || target.IsDead()) return 0.0f;
    if (target.Hunger() < 50.0f) return 0.0f; // Target hat gar keinen großen Hunger
    if (ctx.tribe.Stock().food <= 2) return 0.0f; // Kolonievorrat zu knapp

    const RelationshipManager& rels = ctx.tribe.Relationships();
    float aff = rels.GetAffinity(agent.Id(), target.Id());
    if (aff < -10.0f) return 0.0f; // Feinden wird Nahrung aktiv verweigert!

    float altruism = (agent.GetPersonality().altruism + 1.0f) * 0.5f; // 0..1
    float urgency = target.Hunger() / 100.0f;

    // Beziehungs-Priorität: Partner > Kinder > Freunde > Clan
    float bondMultiplier = 0.8f;
    if (rels.GetPartner(agent.Id()) == target.Id()) {
        bondMultiplier = 2.2f;
    } else if (ctx.tribe.Hierarchy().AreKin(agent.Id(), target.Id())) {
        bondMultiplier = 1.8f;
    } else if (aff > 40.0f) {
        bondMultiplier = 1.4f;
    } else if (ctx.tribe.Hierarchy().AreSameClan(agent.Id(), target.Id())) {
        bondMultiplier = 1.15f;
    }

    float score = std::clamp(altruism * urgency * bondMultiplier * 0.7f, 0.0f, 0.94f);
    outReason = TextFormat("Nahrung teilen mit %s (Score: %.2f)", target.Name().c_str(), score);
    return score;
}

float SocialActionEvaluator::ScoreDispute(const Caveman& agent, const Caveman& target, const WorldContext& ctx, std::string& outReason) {
    if (agent.Id() == target.Id() || target.IsDead()) return 0.0f;

    const RelationshipManager& rels = ctx.tribe.Relationships();
    float aff = rels.GetAffinity(agent.Id(), target.Id());
    if (aff > -15.0f) return 0.0f; // Nur mit Rivalen oder Feinden

    float aggression = (agent.GetPersonality().aggression + 1.0f) * 0.5f;
    float frustration = (agent.Hunger() > 60.0f || agent.Warmth() < 40.0f) ? 1.4f : 1.0f;
    float hate = std::abs(aff) / 100.0f;

    float score = std::clamp(aggression * hate * frustration * 0.65f, 0.0f, 0.86f);
    outReason = TextFormat("Streit suchen mit Rivale %s (Score: %.2f)", target.Name().c_str(), score);
    return score;
}

// ============================================================================
//  Ausführung
// ============================================================================

void SocialActionEvaluator::ExecuteSocialize(Caveman& agent, Caveman& target, WorldContext& ctx) {
    RelationshipManager& rels = ctx.tribe.Relationships();
    rels.ApplyInteraction(agent.Id(), target.Id(), MemoryType::SharedWork, 0.0f);
    rels.ApplyInteraction(target.Id(), agent.Id(), MemoryType::SharedWork, 0.0f);

    agent.ResetSocialNeed();
    target.ResetSocialNeed();
    agent.TriggerEmote(SocialEmote::Chat, 3.2f);
    target.TriggerEmote(SocialEmote::Chat, 3.2f);

    // Klatsch & Tratsch: Agent teilt Meinung über eine zufällige dritte Person
    int sampleThirdId = (agent.Id() % 3 == 0) ? 1 : 2;
    if (sampleThirdId != agent.Id() && sampleThirdId != target.Id()) {
        float agentOpinion = rels.GetAffinity(agent.Id(), sampleThirdId);
        float targetTrustInAgent = rels.GetTrust(target.Id(), agent.Id()) / 100.0f;

        // Verschiebe Targets Meinung über die dritte Person
        if (std::abs(agentOpinion) > 25.0f) {
            RelationshipEdge& e = rels.GetOrCreate(target.Id(), sampleThirdId);
            e.affinity = std::clamp(e.affinity + (agentOpinion > 0 ? 5.0f : -5.0f) * targetTrustInAgent, -100.0f, 100.0f);
        }
    }
}

void SocialActionEvaluator::ExecuteFlirt(Caveman& agent, Caveman& target, WorldContext& ctx) {
    RelationshipManager& rels = ctx.tribe.Relationships();
    rels.ApplyInteraction(agent.Id(), target.Id(), MemoryType::Flirted, 0.0f);
    rels.ApplyInteraction(target.Id(), agent.Id(), MemoryType::Flirted, 0.0f);

    agent.ResetSocialNeed();
    target.ResetSocialNeed();
    agent.TriggerEmote(SocialEmote::Heart, 3.8f);
    target.TriggerEmote(SocialEmote::Heart, 3.8f);

    float affA = rels.GetAffinity(agent.Id(), target.Id());
    float affB = rels.GetAffinity(target.Id(), agent.Id());

    // Ab 55 gegenseitiger Zuneigung wird ein Paar daraus!
    if (affA >= 55.0f && affB >= 55.0f) {
        rels.FormPartnership(agent.Id(), target.Id(), ctx.log, agent.Name(), target.Name());
        ctx.tribe.Hierarchy().GetOrCreateFamily(agent.Id(), target.Id());
    }
}

void SocialActionEvaluator::ExecuteShareFood(Caveman& agent, Caveman& target, WorldContext& ctx) {
    if (ctx.tribe.Stock().food > 0) {
        ctx.tribe.Stock().food--;
        target.Feed(30.0f);

        agent.TriggerEmote(SocialEmote::ShareFood, 3.4f);
        target.TriggerEmote(SocialEmote::ShareFood, 3.4f);

        RelationshipManager& rels = ctx.tribe.Relationships();
        rels.ApplyInteraction(target.Id(), agent.Id(), MemoryType::SharedFood, 0.0f);

        ctx.log.Add(TextFormat("%s teilt Nahrung mit %s (+16 Sympathie)", agent.Name().c_str(), target.Name().c_str()),
                    { 130, 230, 150, 255 });
    }
}

void SocialActionEvaluator::ExecuteDispute(Caveman& agent, Caveman& target, WorldContext& ctx) {
    RelationshipManager& rels = ctx.tribe.Relationships();
    rels.ApplyInteraction(agent.Id(), target.Id(), MemoryType::Insulted, 0.0f);
    rels.ApplyInteraction(target.Id(), agent.Id(), MemoryType::Insulted, 0.0f);

    agent.TriggerEmote(SocialEmote::Dispute, 3.5f);
    target.TriggerEmote(SocialEmote::Dispute, 3.5f);

    ctx.log.Add(TextFormat("Streit & Sticheleien zwischen %s und %s!", agent.Name().c_str(), target.Name().c_str()),
                { 240, 120, 100, 255 });
}

