#include "social_structure.h"
#include "relationship.h"
#include "caveman.h"
#include "tribe.h"
#include "event_log.h"

#include <algorithm>

const char* SocialRankName(SocialRank rank) {
    switch (rank) {
        case SocialRank::Chief:      return "Häuptling";
        case SocialRank::Elder:      return "Ratsältester";
        case SocialRank::Specialist: return "Krieger / Meister";
        case SocialRank::Worker:     return "Stammesmitglied";
        case SocialRank::Outcast:    return "Ausgestoßener";
    }
    return "?";
}

SocialHierarchy::SocialHierarchy() {
    InitClan(1, "Ur-Sippe", Color{ 215, 50, 50, 255 });
}

void SocialHierarchy::InitClan(int clanId, const std::string& name, Color color) {
    for (auto& c : clans_) {
        if (c.id == clanId) {
            c.name = name;
            c.bannerColor = color;
            return;
        }
    }
    Clan newClan;
    newClan.id = clanId;
    newClan.name = name;
    newClan.bannerColor = color;
    newClan.politicsCooldown = 180.0f; // 180s Schutzzeit nach Gründung ohne Rebellionen
    clans_.push_back(newClan);
}

void SocialHierarchy::EnsureClanExists(int clanId, const std::string& defaultName, Color defaultColor) {
    for (const auto& c : clans_) {
        if (c.id == clanId) return;
    }
    Clan newClan;
    newClan.id = clanId;
    newClan.name = defaultName;
    newClan.bannerColor = defaultColor;
    newClan.politicsCooldown = 180.0f;
    clans_.push_back(newClan);
}

void SocialHierarchy::AssignToClan(int characterId, int roleIndex, int preferredClanId) {
    if (preferredClanId > 0) {
        EnsureClanExists(preferredClanId, TextFormat("Clan #%d", preferredClanId));
    } else if (clans_.empty()) {
        EnsureClanExists(1, "Ur-Sippe");
    }

    int targetClanId = preferredClanId > 0 ? preferredClanId : clans_[0].id;
    Clan* targetClan = GetClanMutable(targetClanId);
    if (!targetClan) targetClan = &clans_[0];

    targetClan->memberIds.push_back(characterId);
    memberToClan_[characterId] = targetClan->id;

    // Ränge zuweisen
    SocialRank initialRank = SocialRank::Worker;
    if (roleIndex == 2) {
        initialRank = SocialRank::Elder;
    } else if (roleIndex == 0) {
        initialRank = SocialRank::Specialist;
    }
    ranks_[characterId] = initialRank;

    // Erster Ältester oder Jäger wird Häuptling
    if (kingdom_.monarchId < 0 && (roleIndex == 2 || roleIndex == 0)) {
        kingdom_.monarchId = characterId;
        ranks_[characterId] = SocialRank::Chief;
        targetClan->chiefId = characterId;
    }
}

void SocialHierarchy::SetRank(int characterId, SocialRank rank) {
    ranks_[characterId] = rank;
    if (rank == SocialRank::Chief) {
        kingdom_.monarchId = characterId;
    }
}

SocialRank SocialHierarchy::GetRank(int characterId) const {
    auto it = ranks_.find(characterId);
    if (it != ranks_.end()) return it->second;
    return SocialRank::Worker;
}

int SocialHierarchy::GetClanId(int characterId) const {
    auto it = memberToClan_.find(characterId);
    if (it != memberToClan_.end()) return it->second;
    return 1;
}

int SocialHierarchy::GetChiefIdForClan(int clanId) const {
    const Clan* cl = GetClan(clanId);
    if (cl && cl->chiefId >= 0) {
        auto it = ranks_.find(cl->chiefId);
        if (it != ranks_.end() && it->second == SocialRank::Chief) {
            return cl->chiefId;
        }
    }
    for (const auto& pair : ranks_) {
        if (pair.second == SocialRank::Chief && GetClanId(pair.first) == clanId) {
            return pair.first;
        }
    }
    return kingdom_.monarchId;
}

const Clan* SocialHierarchy::GetClan(int clanId) const {
    for (const auto& c : clans_) {
        if (c.id == clanId) return &c;
    }
    return clans_.empty() ? nullptr : &clans_[0];
}

Clan* SocialHierarchy::GetClanMutable(int clanId) {
    for (auto& c : clans_) {
        if (c.id == clanId) return &c;
    }
    return clans_.empty() ? nullptr : &clans_[0];
}

bool SocialHierarchy::AreSameClan(int idA, int idB) const {
    return GetClanId(idA) == GetClanId(idB);
}

Family* SocialHierarchy::GetOrCreateFamily(int parent1Id, int parent2Id) {
    for (auto& f : families_) {
        if ((f.parent1Id == parent1Id && f.parent2Id == parent2Id) ||
            (f.parent1Id == parent2Id && f.parent2Id == parent1Id)) {
            return &f;
        }
    }

    Family f;
    f.id = nextFamilyId_++;
    f.parent1Id = parent1Id;
    f.parent2Id = parent2Id;
    families_.push_back(f);
    return &families_.back();
}

Family* SocialHierarchy::GetFamily(int familyId) {
    for (auto& f : families_) {
        if (f.id == familyId) return &f;
    }
    return nullptr;
}

const Family* SocialHierarchy::GetFamily(int familyId) const {
    for (const auto& f : families_) {
        if (f.id == familyId) return &f;
    }
    return nullptr;
}

bool SocialHierarchy::AreKin(int idA, int idB) const {
    for (const auto& f : families_) {
        bool aIsParent = (f.parent1Id == idA || f.parent2Id == idA);
        bool bIsParent = (f.parent1Id == idB || f.parent2Id == idB);
        if (aIsParent && bIsParent) return true; // Partner

        bool aIsChild = std::find(f.childrenIds.begin(), f.childrenIds.end(), idA) != f.childrenIds.end();
        bool bIsChild = std::find(f.childrenIds.begin(), f.childrenIds.end(), idB) != f.childrenIds.end();

        if ((aIsParent && bIsChild) || (bIsParent && aIsChild)) return true; // Eltern/Kind
        if (aIsChild && bIsChild) return true; // Geschwister
    }
    return false;
}

void SocialHierarchy::ElectLeader(std::vector<Caveman>& cavemen, const RelationshipManager& rels, EventLog& log, Color clanColor, const std::string& clanName, int targetClanId) {
    if (targetClanId <= 0 && !clans_.empty()) {
        targetClanId = clans_[0].id;
    }

    std::vector<int> candidateIndices;
    float avgDominance = 0.0f;
    float avgSociability = 0.0f;
    int elderCount = 0;

    for (size_t i = 0; i < cavemen.size(); ++i) {
        if (cavemen[i].IsDead() || cavemen[i].GetRole() == Role::Child || cavemen[i].AgeYears() < 16.0f) continue;
        if (targetClanId > 0 && cavemen[i].ClanId() != targetClanId) continue;
        candidateIndices.push_back((int)i);
        avgDominance += cavemen[i].GetPersonality().dominance;
        avgSociability += cavemen[i].GetPersonality().sociability + cavemen[i].GetPersonality().altruism;
        if (cavemen[i].GetRole() == Role::Elder) elderCount++;
    }

    if (candidateIndices.empty()) return;

    if (candidateIndices.size() == 1) {
        Caveman& only = cavemen[candidateIndices[0]];
        SetRank(only.Id(), SocialRank::Chief);
        if (targetClanId > 0) {
            Clan* cl = GetClanMutable(targetClanId);
            if (cl) cl->chiefId = only.Id();
        }
        only.TriggerEmote(SocialEmote::Crown, 4.5f);
        log.Add(TextFormat("%s übernimmt als einziger Erwachsener die Führung im %s!",
                           only.Name().c_str(), clanName.c_str()), clanColor);
        return;
    }

    avgDominance /= (float)candidateIndices.size();
    avgSociability /= (float)candidateIndices.size();

    // 2. Modus der Führungsfindung bestimmen:
    // Pfad A: Schaukampf / Ritual-Duell (Dominanz / Aggression überwiegt)
    // Pfad B: Ältestenrat (Tradition & Erfahrung überwiegt)
    // Pfad C: Freie Stammeswahl (Kooperation & Geselligkeit überwiegt)

    if (avgDominance > 0.08f) {
        // --- PFAD A: RITUAL-ZWEIKAMPF / SCHAUKAMPF ---
        std::sort(candidateIndices.begin(), candidateIndices.end(), [&](int a, int b) {
            float scoreA = cavemen[a].GetPersonality().dominance * 1.5f + cavemen[a].Hp() * 0.2f + (cavemen[a].GetRole() == Role::Hunter ? 15.0f : 0.0f);
            float scoreB = cavemen[b].GetPersonality().dominance * 1.5f + cavemen[b].Hp() * 0.2f + (cavemen[b].GetRole() == Role::Hunter ? 15.0f : 0.0f);
            return scoreA > scoreB;
        });

        int cAIdx = candidateIndices[0];
        int cBIdx = candidateIndices[1];
        Caveman& champA = cavemen[cAIdx];
        Caveman& champB = cavemen[cBIdx];

        champA.TriggerEmote(SocialEmote::Dispute, 3.8f);
        champB.TriggerEmote(SocialEmote::Dispute, 3.8f);

        float combatScoreA = champA.Hp() * 0.4f + champA.Ap() * 0.3f + champA.GetPersonality().dominance * 20.0f;
        float combatScoreB = champB.Hp() * 0.4f + champB.Ap() * 0.3f + champB.GetPersonality().dominance * 20.0f;

        Caveman* winner = &champA;
        Caveman* loser = &champB;
        if (combatScoreB > combatScoreA) {
            winner = &champB;
            loser = &champA;
        }

        loser->TakeDamageNonLethal((float)GetRandomValue(6, 14));
        SetRank(loser->Id(), SocialRank::Specialist);
        SetRank(winner->Id(), SocialRank::Chief);
        if (targetClanId > 0) {
            Clan* cl = GetClanMutable(targetClanId);
            if (cl) cl->chiefId = winner->Id();
        }
        winner->TriggerEmote(SocialEmote::Crown, 5.0f);

        log.Add(TextFormat("Ritual-Zweikampf im %s: %s bezwingt %s im Schaukampf und erringt die Häuptlingskrone!",
                           clanName.c_str(), winner->Name().c_str(), loser->Name().c_str()), clanColor);

    } else if (elderCount >= 1 && (elderCount >= 2 || avgSociability <= avgDominance + 0.3f)) {
        // --- PFAD B: ÄLTESTENRAT ---
        int bestElderIdx = -1;
        float bestWisdom = -10.0f;
        for (int idx : candidateIndices) {
            float wisdom = cavemen[idx].AgeYears() * 1.2f + (cavemen[idx].GetRole() == Role::Elder ? 30.0f : 0.0f);
            if (wisdom > bestWisdom) {
                bestWisdom = wisdom;
                bestElderIdx = idx;
            }
        }

        for (int idx : candidateIndices) {
            if (cavemen[idx].GetRole() == Role::Elder) {
                cavemen[idx].TriggerEmote(SocialEmote::Idea, 3.5f);
            }
        }

        Caveman& elderChief = cavemen[bestElderIdx];
        SetRank(elderChief.Id(), SocialRank::Chief);
        if (targetClanId > 0) {
            Clan* cl = GetClanMutable(targetClanId);
            if (cl) cl->chiefId = elderChief.Id();
        }
        elderChief.TriggerEmote(SocialEmote::Crown, 5.0f);

        const char* article = (elderChief.GetGender() == Gender::Female) ? "Die ehrwürdige" : "Der ehrwürdige";
        log.Add(TextFormat("Der Ältestenrat des %s bestimmt: %s %s (%.0f J.) wird feierlich zum Oberhaupt ernannt!",
                           clanName.c_str(), article, elderChief.Name().c_str(), elderChief.AgeYears()), clanColor);

    } else {
        // --- PFAD C: FREIE STAMMESWAHL ---
        std::unordered_map<int, int> votes;
        for (int idx : candidateIndices) {
            votes[cavemen[idx].Id()] = 0;
            cavemen[idx].TriggerEmote(SocialEmote::Vote, 3.5f);
        }

        for (size_t i = 0; i < cavemen.size(); ++i) {
            if (cavemen[i].IsDead() || (targetClanId > 0 && cavemen[i].ClanId() != targetClanId)) continue;
            int voterId = cavemen[i].Id();
            int bestChoice = cavemen[candidateIndices[0]].Id();
            float bestRep = -999.0f;

            for (int cIdx : candidateIndices) {
                int candId = cavemen[cIdx].Id();
                float rep = rels.GetTrust(voterId, candId) * 1.2f + rels.GetRespect(voterId, candId) * 1.0f;
                if (rep > bestRep) {
                    bestRep = rep;
                    bestChoice = candId;
                }
            }
            votes[bestChoice]++;
        }

        int winnerId = -1;
        int maxVotes = -1;
        for (const auto& pair : votes) {
            if (pair.second > maxVotes) {
                maxVotes = pair.second;
                winnerId = pair.first;
            }
        }

        for (size_t i = 0; i < cavemen.size(); ++i) {
            if (cavemen[i].Id() == winnerId) {
                SetRank(winnerId, SocialRank::Chief);
                if (targetClanId > 0) {
                    Clan* cl = GetClanMutable(targetClanId);
                    if (cl) cl->chiefId = winnerId;
                }
                cavemen[i].TriggerEmote(SocialEmote::Crown, 5.0f);
                log.Add(TextFormat("Freie Stammeswahl im %s: %s gewinnt das Vertrauen mit %d Stimmen und wird zum Häuptling ernannt!",
                                   clanName.c_str(), cavemen[i].Name().c_str(), maxVotes), clanColor);
                break;
            }
        }
    }
}

void SocialHierarchy::UpdateLeadership(const RelationshipManager& rels, std::vector<Caveman>& cavemen, EventLog& log, Color clanColor, const std::string& clanName, int targetClanId) {
    leadershipCheckTimer_ += 1.0f;
    if (leadershipCheckTimer_ < 15.0f) return;
    leadershipCheckTimer_ = 0.0f;

    if (targetClanId <= 0 && !clans_.empty()) {
        targetClanId = clans_[0].id;
    }

    Clan* myClan = (targetClanId > 0) ? GetClanMutable(targetClanId) : (!clans_.empty() ? &clans_[0] : nullptr);
    if (myClan && myClan->politicsCooldown > 0.0f) {
        return;
    }

    int currentLeader = (myClan && myClan->chiefId >= 0) ? myClan->chiefId : kingdom_.monarchId;
    std::string effClanName = !clanName.empty() ? clanName : (myClan ? myClan->name : "Clan");
    Color effColor = (clanColor.a > 0) ? clanColor : (myClan ? myClan->bannerColor : GOLD);

    // Zähle lebende Erwachsene im Zielclan
    int livingAdults = 0;
    for (const auto& c : cavemen) {
        if (!c.IsDead() && c.ClanId() == targetClanId && c.GetRole() != Role::Child && c.AgeYears() >= 16.0f) {
            livingAdults++;
        }
    }

    // 1. Clan-Kohäsion aktualisieren
    int rulingClanId = (currentLeader >= 0) ? GetClanId(currentLeader) : -1;
    for (auto& clan : clans_) {
        int aliveMembers = 0;
        float avgMorale = 0.0f;
        float avgRespectToChief = 0.0f;
        int bestClanLeader = -1;
        float bestDominance = -2.0f;

        for (const auto& c : cavemen) {
            if (c.IsDead()) continue;
            if (GetClanId(c.Id()) != clan.id) continue;
            aliveMembers++;
            avgMorale += c.Morale();
            if (currentLeader >= 0 && c.Id() != currentLeader) {
                avgRespectToChief += rels.GetRespect(c.Id(), currentLeader);
            } else {
                avgRespectToChief += 40.0f;
            }
            if (c.GetRole() != Role::Child && c.GetPersonality().dominance > bestDominance) {
                bestDominance = c.GetPersonality().dominance;
                bestClanLeader = c.Id();
            }
        }

        if (bestClanLeader >= 0) {
            clan.chiefId = bestClanLeader;
        }

        if (aliveMembers > 0) {
            avgMorale /= (float)aliveMembers;
            avgRespectToChief /= (float)aliveMembers;

            float delta = 0.0f;
            if (clan.id == rulingClanId) {
                delta = +1.5f;
            } else {
                delta = (avgMorale - 62.0f) * 0.14f + (avgRespectToChief - 18.0f) * 0.08f - 1.2f;
            }
            clan.cohesion = std::clamp(clan.cohesion + delta, 0.0f, 100.0f);
        }
    }

    // 2. Prüfe, ob der Anführer noch lebt und noch zu diesem Clan gehört
    bool leaderAlive = false;
    if (currentLeader >= 0) {
        for (const auto& c : cavemen) {
            if (c.Id() == currentLeader && !c.IsDead() && (targetClanId <= 0 || c.ClanId() == targetClanId)) {
                leaderAlive = true;
                break;
            }
        }
    }

    if (!leaderAlive) {
        if (myClan) myClan->chiefId = -1;
        kingdom_.monarchId = -1;
        if (livingAdults >= 1) {
            ElectLeader(cavemen, rels, log, effColor, effClanName, targetClanId);
        }
        return;
    }

    // 3. Putsch / Meuterei-Logik (strikt nur ab 4 erwachsenen Clanmitgliedern):
    if (livingAdults < 4) return;

    float totalRespect = 0.0f;
    int count = 0;
    for (const auto& c : cavemen) {
        if (c.IsDead() || c.ClanId() != targetClanId || c.Id() == currentLeader) continue;
        totalRespect += rels.GetRespect(c.Id(), currentLeader);
        count++;
    }

    float avgRespect = (count > 0) ? (totalRespect / (float)count) : 50.0f;
    if (avgRespect < 6.0f) {
        log.Add(TextFormat("Unruhe im %s! Das Volk verweigert dem Häuptling die Gefolgschaft – Neuwahl gefordert!", effClanName.c_str()), { 255, 120, 100, 255 });
        SetRank(currentLeader, SocialRank::Worker);
        if (myClan) {
            myClan->chiefId = -1;
            myClan->politicsCooldown = 180.0f; // 180s Cooldown nach Neuwahl
        }
        kingdom_.monarchId = -1;
        ElectLeader(cavemen, rels, log, effColor, effClanName, targetClanId);
    }
}

void SocialHierarchy::Update(float dt) {
    for (auto& clan : clans_) {
        if (clan.politicsCooldown > 0.0f) {
            clan.politicsCooldown = std::max(0.0f, clan.politicsCooldown - dt);
        }
    }
}

bool SocialHierarchy::CheckLeadershipChallenge(Tribe& tribe, std::vector<Caveman>& cavemen, EventLog& log) {
    Clan* clan = GetClanMutable(tribe.Id());
    if (clan && clan->politicsCooldown > 0.0f) {
        return false;
    }

    // Mindestens 4 erwachsene Clanmitglieder erforderlich für ein Ritualduell
    int livingAdults = 0;
    for (const auto& c : cavemen) {
        if (!c.IsDead() && c.ClanId() == tribe.Id() && c.GetRole() != Role::Child && c.AgeYears() >= 16.0f) {
            livingAdults++;
        }
    }
    if (livingAdults < 4) return false;

    int chiefId = GetChiefIdForClan(tribe.Id());
    if (chiefId < 0) return false;

    Caveman* chief = nullptr;
    for (auto& c : cavemen) {
        if (!c.IsDead() && c.Id() == chiefId && c.ClanId() == tribe.Id()) {
            chief = &c;
            break;
        }
    }
    if (!chief) return false;

    // Suche potenziellen Herausforderer mit hoher Dominanz und geringer Loyalität / geringem Respekt
    Caveman* challenger = nullptr;
    float highestChallengeScore = -100.0f;

    for (auto& c : cavemen) {
        if (c.IsDead() || c.ClanId() != tribe.Id() || c.Id() == chiefId) continue;
        if (c.GetRole() == Role::Child || c.AgeYears() < 16.0f) continue;
        if (c.Hp() < 50.0f) continue; // Zu geschwächt für einen rituellen Kampf

        float dominance = c.GetPersonality().dominance;
        float respect = tribe.Relationships().GetRespect(c.Id(), chiefId);
        float loyalty = c.Loyalty();

        // Bedingung: Machthunger (dominance > 0.30f) und Unzufriedenheit (loyalty < 35.0f oder respect < 15.0f)
        if (dominance > 0.30f && (loyalty < 35.0f || respect < 15.0f)) {
            float score = dominance * 40.0f + (100.0f - loyalty) * 0.5f + (30.0f - respect) * 0.4f + c.Hp() * 0.2f;
            if (score > highestChallengeScore) {
                highestChallengeScore = score;
                challenger = &c;
            }
        }
    }

    if (!challenger) return false;

    // Globaler Politik-Cooldown pro Clan von mindestens 150 Sekunden aktivieren!
    if (clan) {
        clan->politicsCooldown = 150.0f;
    }

    // Physische Bewegung aufeinander zu (Schaukampf in der Mitte)
    Vector2 midPoint = { (challenger->Position().x + chief->Position().x) * 0.5f,
                         (challenger->Position().y + chief->Position().y) * 0.5f };
    challenger->SetPosition({ challenger->Position().x + (midPoint.x - challenger->Position().x) * 0.4f,
                              challenger->Position().y + (midPoint.y - challenger->Position().y) * 0.4f });
    chief->SetPosition({ chief->Position().x + (midPoint.x - chief->Position().x) * 0.4f,
                         chief->Position().y + (midPoint.y - chief->Position().y) * 0.4f });

    // Ankündigung im Event-Log (Phase 3.1)
    log.Add(TextFormat("%s fordert Anführer %s im %s zum Kampf um die Führung heraus!",
                       challenger->Name().c_str(), chief->Name().c_str(), tribe.Name().c_str()), { 255, 215, 0, 255 });

    // Show, Don't Tell: Kampf- und Streit-Emotes
    challenger->TriggerEmote(SocialEmote::Dispute, 4.5f);
    chief->TriggerEmote(SocialEmote::Dispute, 4.5f);

    // Nahe Clan-Mitglieder unterbrechen kurz und schauen im Kreis zu
    for (auto& bystander : cavemen) {
        if (bystander.IsAlive() && bystander.ClanId() == tribe.Id() && bystander.Id() != chief->Id() && bystander.Id() != challenger->Id()) {
            if (Vector2Distance(bystander.Position(), chief->Position()) < 75.0f) {
                bystander.TriggerEmote(SocialEmote::Vote, 3.5f);
            }
        }
    }

    // Realistische Chancen berechnen:
    // Ein 20-jähriger Jäger mit Speer/Knüppel gewinnt ein Duell gegen einen gebrechlichen 68-jährigen Ältesten
    // mit realistischer hoher Wahrscheinlichkeit (80-90%).
    float ageVigorChallenger = (challenger->AgeYears() >= 18.0f && challenger->AgeYears() <= 38.0f) ? 35.0f :
                               (challenger->AgeYears() > 50.0f) ? -(challenger->AgeYears() - 50.0f) * 1.5f : 0.0f;
    float ageVigorChief = (chief->AgeYears() >= 18.0f && chief->AgeYears() <= 38.0f) ? 35.0f :
                          (chief->AgeYears() > 50.0f) ? -(chief->AgeYears() - 50.0f) * 1.5f : 0.0f;

    float roleBonusChallenger = (challenger->GetRole() == Role::Hunter) ? 35.0f :
                                (challenger->GetRole() == Role::Elder) ? -20.0f : 0.0f;
    float roleBonusChief = (chief->GetRole() == Role::Hunter) ? 35.0f :
                           (chief->GetRole() == Role::Elder) ? -20.0f : 0.0f;

    float weaponBonusChallenger = (challenger->GetWeaponTier() == WeaponTier::MetalBlade) ? 35.0f :
                                  (challenger->GetWeaponTier() == WeaponTier::FlintSpear) ? 22.0f :
                                  (challenger->GetWeaponTier() == WeaponTier::CompositeBow) ? 18.0f : 8.0f;
    float weaponBonusChief = (chief->GetWeaponTier() == WeaponTier::MetalBlade) ? 35.0f :
                             (chief->GetWeaponTier() == WeaponTier::FlintSpear) ? 22.0f :
                             (chief->GetWeaponTier() == WeaponTier::CompositeBow) ? 18.0f : 8.0f;

    float combatScoreChallenger = challenger->Hp() * 0.40f + challenger->Ap() * 0.25f +
                                  challenger->GetPersonality().dominance * 20.0f +
                                  challenger->GetPersonality().aggression * 12.0f +
                                  ageVigorChallenger + roleBonusChallenger + weaponBonusChallenger +
                                  (float)GetRandomValue(-8, 8);

    float combatScoreChief = chief->Hp() * 0.40f + chief->Ap() * 0.25f +
                             chief->GetPersonality().dominance * 20.0f +
                             chief->GetPersonality().aggression * 12.0f +
                             ageVigorChief + roleBonusChief + weaponBonusChief +
                             (float)GetRandomValue(-8, 8);

    if (combatScoreChallenger > combatScoreChief) {
        // Herausforderer siegt!
        chief->TakeDamageNonLethal(std::max(16.0f, chief->Hp() * 0.5f));
        SetRank(chief->Id(), (chief->GetRole() == Role::Elder) ? SocialRank::Elder : SocialRank::Specialist);
        SetRank(challenger->Id(), SocialRank::Chief);
        if (clan) clan->chiefId = challenger->Id();

        challenger->TriggerEmote(SocialEmote::Crown, 5.0f);
        challenger->SetLoyalty(100.0f);
        chief->SetLoyalty(25.0f); // Gedemütigter Ex-Häuptling ist unzufrieden

        // Respektverschiebung im Clan
        tribe.Relationships().GetOrCreate(challenger->Id(), chief->Id()).respect = 30.0f;
        tribe.Relationships().GetOrCreate(chief->Id(), challenger->Id()).respect = -25.0f;

        log.Add(TextFormat("[Machtwechsel im %s] %s bezwingt Häuptling %s im rituellen Duell und übernimmt die Herrschaft!",
                           tribe.Name().c_str(), challenger->Name().c_str(), chief->Name().c_str()), tribe.FactionColor());
    } else {
        // Häuptling verteidigt Herrschaft!
        challenger->TakeDamageNonLethal(std::max(18.0f, challenger->Hp() * 0.55f));
        chief->TriggerEmote(SocialEmote::Crown, 4.0f);
        chief->ModifyLoyalty(20.0f);

        challenger->ModifyLoyalty(-35.0f); // Loyalität stürzt ab -> erhöht Chance auf Sezession
        tribe.Relationships().GetOrCreate(challenger->Id(), chief->Id()).respect -= 25.0f;
        tribe.Relationships().GetOrCreate(chief->Id(), challenger->Id()).respect -= 35.0f;

        log.Add(TextFormat("[Machtkampf im %s] Häuptling %s schlägt den Aufständischen %s nieder und festigt seine Macht!",
                           tribe.Name().c_str(), chief->Name().c_str(), challenger->Name().c_str()), tribe.FactionColor());
    }

    return true;
}

bool SocialHierarchy::CheckForSchism(const RelationshipManager& rels, const std::vector<Caveman>& cavemen,
                                     int& outRebelClanId, int& outRebelLeaderId) {
    outRebelClanId = -1;
    outRebelLeaderId = -1;

    int rulingClanId = (kingdom_.monarchId >= 0) ? GetClanId(kingdom_.monarchId) : -1;

    for (const auto& clan : clans_) {
        if (clan.id == rulingClanId) continue;

        // Prüfen, ob dieser Clan sich bereits abgespalten hat
        bool alreadySplit = false;
        for (const auto& s : splinterSettlements_) {
            if (s.clanId == clan.id) {
                alreadySplit = true;
                break;
            }
        }
        if (alreadySplit) continue;

        // Zähle lebende erwachsene Clan-Mitglieder und suche den ehrgeizigsten Anführer
        int adultCount = 0;
        int candidateLeaderId = -1;
        float bestAmbition = -10.0f;
        int rivalriesWithRulingClan = 0;

        for (const auto& c : cavemen) {
            if (c.IsDead() || GetClanId(c.Id()) != clan.id) continue;
            if (c.GetRole() != Role::Child) {
                adultCount++;
                float ambition = c.GetPersonality().dominance * 1.4f + c.GetPersonality().aggression * 0.8f;
                if (ambition > bestAmbition) {
                    bestAmbition = ambition;
                    candidateLeaderId = c.Id();
                }
            }
            for (int enemyId : rels.GetEnemies(c.Id())) {
                if (GetClanId(enemyId) == rulingClanId) {
                    rivalriesWithRulingClan++;
                }
            }
        }

        if (adultCount < 2 || candidateLeaderId < 0) continue;

        // Spaltungsbedingung: Niedrige Clan-Kohäsion ODER starker Machtkampf mit dem regierenden Clan
        if (clan.cohesion < 42.0f || (rivalriesWithRulingClan >= 2 && bestAmbition > 0.35f)) {
            outRebelClanId = clan.id;
            outRebelLeaderId = candidateLeaderId;
            return true;
        }
    }

    return false;
}

void SocialHierarchy::RegisterSplinterSettlement(const SplinterSettlement& settlement) {
    splinterSettlements_.push_back(settlement);
    if (settlement.leaderId >= 0) {
        ranks_[settlement.leaderId] = SocialRank::Chief;
    }
    if (Clan* c = GetClanMutable(settlement.clanId)) {
        c->cohesion = 85.0f;
        c->chiefId = settlement.leaderId;
    }
}

Vector2 SocialHierarchy::GetFactionCampForMember(int characterId, Vector2 defaultCampPos) const {
    int clanId = GetClanId(characterId);
    for (const auto& s : splinterSettlements_) {
        if (s.clanId == clanId) {
            return s.centerPos;
        }
    }
    return defaultCampPos;
}


