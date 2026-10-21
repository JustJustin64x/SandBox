#include "social_actions.h"
#include "caveman.h"
#include "tribe.h"
#include "relationship.h"
#include "social_structure.h"
#include "event_log.h"
#include "config.h"
#include "utility_ai.h"

#include <cmath>
#include <algorithm>

bool SocialActionEvaluator::CanBecomePartners(const Caveman* a, const Caveman* b) {
    if (!a || !b) return false;
    if (a->Id() == b->Id()) return false;
    if (a->IsDead() || b->IsDead()) return false;
    if (a->GetRole() == Role::Child || b->GetRole() == Role::Child) return false;
    if (a->AgeYears() < 16.0f || b->AgeYears() < 16.0f) return false;

    // AUFTRAG PHASE 1.2: Inzest-Sperre im Stammbaum
    // Hole a->motherId, a->fatherId, b->motherId, b->fatherId
    int aMom = a->MotherId();
    int aDad = a->FatherId();
    int bMom = b->MotherId();
    int bDad = b->FatherId();

    // a) Sie dieselbe Mutter (> 0 / >= 0) oder denselben Vater (> 0 / >= 0) haben (Geschwister)
    if ((aMom >= 0 && bMom >= 0 && aMom == bMom) ||
        (aDad >= 0 && bDad >= 0 && aDad == bDad)) {
        return false;
    }

    // b) Elternteil-Kind-Beziehung: a->id == b->motherId oder a->id == b->fatherId (und umgekehrt)
    if (a->Id() == bMom || a->Id() == bDad ||
        b->Id() == aMom || b->Id() == aDad) {
        return false;
    }

    return true;
}

bool SocialActionEvaluator::CanBecomePartners(const Caveman& a, const Caveman& b) {
    return CanBecomePartners(&a, &b);
}

bool SocialActionEvaluator::TryReproduce(Caveman* partnerA, Caveman* partnerB, WorldContext& ctx) {
    if (!partnerA || !partnerB) return false;

    // AUFTRAG PHASE 1.1: Strikte Biologie bei Schwangerschaft / Reproduktion
    // Als ALLERERSTES prüfen:
    if (partnerA->GetGender() == partnerB->GetGender()) return false;

    // Keine Kinder / Unmündigen
    if (partnerA->GetRole() == Role::Child || partnerB->GetRole() == Role::Child) return false;
    if (partnerA->AgeYears() < 16.0f || partnerB->AgeYears() < 16.0f) return false;

    // Inzest-Sperre prüfen (AUFTRAG PHASE 1.2)
    if (!CanBecomePartners(partnerA, partnerB)) return false;

    // Mutter (weiblich) und Vater (männlich) ermitteln
    Caveman* mother = (partnerA->GetGender() == Gender::Female) ? partnerA : partnerB;
    Caveman* father = (partnerA->GetGender() == Gender::Male) ? partnerA : partnerB;

    // Schwangerschafts-Cooldown und Zustand prüfen
    if (mother->IsPregnant() || mother->PregnancyCooldown() > 0.0f) return false;

    // Familien-Limit (maximal 3-4 Kinder pro Elternpaar)
    Family* fam = ctx.tribe.Hierarchy().GetOrCreateFamily(partnerA->Id(), partnerB->Id());
    if (fam && fam->childrenIds.size() >= 4) return false;

    // Auslösen der Schwangerschaft
    mother->SetPregnant(true);
    mother->SetPregnancyCooldown(210.0f); // 180 bis 240 Sekunden Cooldown

    mother->TriggerEmote(SocialEmote::Heart, 3.8f);
    father->TriggerEmote(SocialEmote::Heart, 3.8f);

    ctx.log.Add(TextFormat("Liebesglück: %s & %s erwarten Nachwuchs im %s! ❤️",
                           mother->Name().c_str(), father->Name().c_str(), ctx.tribe.Name().c_str()),
                { 255, 140, 190, 255 });
    return true;
}

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
    if (agent.AgeYears() < 16.0f || target.AgeYears() < 16.0f) return 0.0f;

    // AUFTRAG PHASE 1.2: Inzest-Sperre im Stammbaum
    if (!CanBecomePartners(&agent, &target)) return 0.0f;
    if (ctx.tribe.Hierarchy().AreKin(agent.Id(), target.Id())) return 0.0f;

    const RelationshipManager& rels = ctx.tribe.Relationships();
    int partnerA = rels.GetPartner(agent.Id());
    int partnerB = rels.GetPartner(target.Id());

    // Wenn beide bereits fremd vergeben sind, kein Flirt
    if (partnerA >= 0 && partnerA != target.Id()) return 0.0f;
    if (partnerB >= 0 && partnerB != agent.Id()) return 0.0f;

    float aff = rels.GetAffinity(agent.Id(), target.Id());
    if (aff < 25.0f && partnerA != target.Id()) return 0.0f; // Mindestsympathie

    float dist = Vector2Distance(agent.Position(), target.Position());
    if (dist > 180.0f) return 0.0f;

    float distFactor = 1.0f - (dist / 180.0f) * 0.4f;
    float score = std::clamp((aff / 100.0f) * 0.90f * distFactor, 0.0f, 0.95f);
    outReason = TextFormat("Um %s werben / flirten (Score: %.2f)", target.Name().c_str(), score);
    return score;
}

float SocialActionEvaluator::CalculateMateScore(const Caveman& agent, const Caveman& target, const WorldContext& ctx, std::string& outReason) {
    return UtilityAI::CalculateMateScore(agent, target, ctx, outReason);
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

float SocialActionEvaluator::ScoreTeachChild(const Caveman& agent, const Caveman& target, const WorldContext& ctx, std::string& outReason) {
    if (agent.Id() == target.Id() || target.IsDead()) return 0.0f;
    if (target.GetRole() != Role::Child && target.AgeYears() >= 16.0f) return 0.0f;
    if (agent.GetRole() != Role::Elder && agent.AgeYears() < 30.0f) return 0.0f;

    // Cooldown-Prüfung & Limit (AUFTRAG PHASE 4.1): Lehrer & Kind brauchen Cooldown (90s), Kind max 2 Lektionen
    if (agent.teachCooldown > 0.0f || target.teachCooldown > 0.0f) return 0.0f;
    if (agent.TeachCooldown() > 0.0f || target.TeachCooldown() > 0.0f) return 0.0f;
    if (target.LessonsReceived() >= 2) return 0.0f;

    // Nur im eigenen Clan
    if (!ctx.tribe.Hierarchy().AreSameClan(agent.Id(), target.Id())) return 0.0f;

    float dist = Vector2Distance(agent.Position(), target.Position());
    if (dist > 180.0f) return 0.0f;

    float elderBonus = (agent.GetRole() == Role::Elder) ? 1.4f : 1.1f;
    float distToCamp = Vector2Distance(agent.Position(), ctx.tribe.CampPosition());
    float fireBonus = (distToCamp < 110.0f) ? 1.25f : 0.90f;
    float distFactor = 1.0f - (dist / 180.0f) * 0.4f;

    float score = std::clamp(0.55f * elderBonus * fireBonus * distFactor, 0.0f, 0.92f);
    outReason = TextFormat("Kind %s unterrichten & Wissen weitergeben (Score: %.2f)", target.Name().c_str(), score);
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

    // Ab 40 gegenseitiger Zuneigung wird ein Paar daraus (AUFTRAG PHASE 1.2: Inzest-Sperre)
    if (affA >= 40.0f && affB >= 40.0f) {
        if (CanBecomePartners(&agent, &target)) {
            if (rels.GetPartner(agent.Id()) < 0 && rels.GetPartner(target.Id()) < 0) {
                rels.FormPartnership(agent.Id(), target.Id(), ctx.log, agent.Name(), target.Name());
                ctx.tribe.Hierarchy().GetOrCreateFamily(agent.Id(), target.Id());
            }
        }
    }

    // Biologische Schwangerschaft versuchen (AUFTRAG PHASE 1.1: Prüft strikt verschiedenes Geschlecht)
    TryReproduce(&agent, &target, ctx);
}

void SocialActionEvaluator::ExecuteMating(Caveman& agent, Caveman& partner, WorldContext& ctx) {
    RelationshipManager& rels = ctx.tribe.Relationships();
    rels.ApplyInteraction(agent.Id(), partner.Id(), MemoryType::Flirted, 0.0f);
    rels.ApplyInteraction(partner.Id(), agent.Id(), MemoryType::Flirted, 0.0f);

    agent.ResetSocialNeed();
    partner.ResetSocialNeed();
    agent.TriggerEmote(SocialEmote::Heart, 4.0f);
    partner.TriggerEmote(SocialEmote::Heart, 4.0f);

    // Falls noch nicht formell verpartnert: Ab 40 Zuneigung Partnerschaft schließen
    if (rels.GetPartner(agent.Id()) < 0 && rels.GetPartner(partner.Id()) < 0) {
        if (CanBecomePartners(&agent, &partner)) {
            float affA = rels.GetAffinity(agent.Id(), partner.Id());
            float affB = rels.GetAffinity(partner.Id(), agent.Id());
            if (affA >= 40.0f && affB >= 40.0f) {
                rels.FormPartnership(agent.Id(), partner.Id(), ctx.log, agent.Name(), partner.Name());
                ctx.tribe.Hierarchy().GetOrCreateFamily(agent.Id(), partner.Id());
            }
        }
    }

    // Biologische Schwangerschaft versuchen (AUFTRAG PHASE 1.1)
    TryReproduce(&agent, &partner, ctx);
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

void SocialActionEvaluator::ExecuteTeachChild(Caveman& agent, Caveman& target, WorldContext& ctx) {
    RelationshipManager& rels = ctx.tribe.Relationships();
    rels.ApplyInteraction(target.Id(), agent.Id(), MemoryType::SharedWork, 0.0f);

    agent.ResetSocialNeed();
    target.ResetSocialNeed();
    agent.TriggerEmote(SocialEmote::Idea, 3.5f);
    target.TriggerEmote(SocialEmote::Idea, 4.2f);

    // Kind übernimmt 50% der im Leben gelernten Gehirngewichte des Ältesten
    target.GetBrainWeightsMutable().AssimilateCulture(agent.GetBrainWeights(), 0.50f);

    // Feste Cooldowns (AUFTRAG PHASE 4.1): 90s Blockade für Lehrer und Kind, max 2 Lektionen
    agent.teachCooldown = 90.0f;
    agent.SetTeachCooldown(90.0f);
    target.teachCooldown = 90.0f;
    target.SetTeachCooldown(90.0f);
    target.IncrementLessonsReceived();

    // Respekt und Vertrauen steigen
    RelationshipEdge& e = rels.GetOrCreate(target.Id(), agent.Id());
    e.respect = std::min(100.0f, e.respect + 20.0f);
    e.trust   = std::min(100.0f, e.trust + 15.0f);

    ctx.log.Add(TextFormat("Kulturelle Lehre: %s unterrichtet das Kind %s am Feuer (+50%% Gehirnprägung)!",
                           agent.Name().c_str(), target.Name().c_str()), GOLD);
}

