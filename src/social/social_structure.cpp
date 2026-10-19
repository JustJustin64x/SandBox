#include "social_structure.h"
#include "relationship.h"
#include "caveman.h"
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
    InitDefaultClans();
}

void SocialHierarchy::InitDefaultClans() {
    clans_.clear();

    Clan clan1;
    clan1.id = 1;
    clan1.name = "Clan des Bären";
    clan1.bannerColor = Color{ 190, 80, 50, 255 };
    clans_.push_back(clan1);

    Clan clan2;
    clan2.id = 2;
    clan2.name = "Clan des Wolfs";
    clan2.bannerColor = Color{ 90, 150, 210, 255 };
    clans_.push_back(clan2);

    Clan clan3;
    clan3.id = 3;
    clan3.name = "Clan des Raben";
    clan3.bannerColor = Color{ 140, 90, 190, 255 };
    clans_.push_back(clan3);
}

void SocialHierarchy::AssignToClan(int characterId, int roleIndex, int preferredClanId) {
    if (clans_.empty()) return;
    int clanIdx = (characterId % (int)clans_.size());
    if (preferredClanId >= 0) {
        for (size_t i = 0; i < clans_.size(); ++i) {
            if (clans_[i].id == preferredClanId) {
                clanIdx = (int)i;
                break;
            }
        }
    }

    Clan& clan = clans_[clanIdx];
    clan.memberIds.push_back(characterId);
    memberToClan_[characterId] = clan.id;

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
        clan.chiefId = characterId;
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

void SocialHierarchy::UpdateLeadership(const RelationshipManager& rels, const std::vector<Caveman>& cavemen, EventLog& log) {
    leadershipCheckTimer_ += 1.0f;
    if (leadershipCheckTimer_ < 15.0f) return;
    leadershipCheckTimer_ = 0.0f;

    int currentLeader = kingdom_.monarchId;

    // 1. Clan-Kohäsion (Zufriedenheit mit dem Hauptstamm) aktualisieren
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

            // Nicht-regierende Clans verlieren bei schlechter Moral oder Konflikten Kohäsion
            float delta = 0.0f;
            if (clan.id == rulingClanId) {
                delta = +1.5f;
            } else {
                delta = (avgMorale - 62.0f) * 0.14f + (avgRespectToChief - 18.0f) * 0.08f - 1.2f;
            }
            clan.cohesion = std::clamp(clan.cohesion + delta, 0.0f, 100.0f);
        }
    }

    if (currentLeader < 0) return;

    // 2. Prüfe, ob der Anführer noch lebt
    bool leaderAlive = false;
    for (const auto& c : cavemen) {
        if (c.Id() == currentLeader && !c.IsDead()) {
            leaderAlive = true;
            break;
        }
    }

    if (!leaderAlive) {
        // Neuer Häuptling: Suche ältesten / angesehensten Erwachsenen
        int bestCandidate = -1;
        float bestScore = -1.0f;
        for (const auto& c : cavemen) {
            if (c.IsDead() || c.GetRole() == Role::Child) continue;
            float score = (c.GetRole() == Role::Elder ? 50.0f : 25.0f)
                        + c.GetPersonality().dominance * 25.0f + c.Ap() * 0.3f;
            if (score > bestScore) {
                bestScore = score;
                bestCandidate = c.Id();
            }
        }
        if (bestCandidate >= 0) {
            SetRank(bestCandidate, SocialRank::Chief);
            log.Add(TextFormat("Der alte Häuptling ist verstorben. Figur #%d übernimmt die Krone!", bestCandidate), GOLD);
        }
        return;
    }

    // 3. Putsch / Meuterei-Logik:
    float totalRespect = 0.0f;
    int count = 0;
    for (const auto& c : cavemen) {
        if (c.IsDead() || c.Id() == currentLeader) continue;
        totalRespect += rels.GetRespect(c.Id(), currentLeader);
        count++;
    }

    float avgRespect = (count > 0) ? (totalRespect / (float)count) : 50.0f;
    if (avgRespect < 6.0f) {
        for (const auto& c : cavemen) {
            if (c.IsDead() || c.Id() == currentLeader) continue;
            if ((c.GetRole() == Role::Hunter || c.GetRole() == Role::Elder) && c.GetPersonality().dominance > 0.25f) {
                auto friends = rels.GetFriends(c.Id());
                if (friends.size() >= 2) {
                    SetRank(currentLeader, SocialRank::Worker);
                    SetRank(c.Id(), SocialRank::Chief);
                    log.Add(TextFormat("Machtwechsel! %s stürzt den unpopulären Häuptling und beansprucht die Krone!", c.Name().c_str()),
                            { 255, 100, 100, 255 });
                    break;
                }
            }
        }
    }
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


