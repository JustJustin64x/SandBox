#ifndef SOCIAL_STRUCTURE_H
#define SOCIAL_STRUCTURE_H

#include "common.h"
#include <string>
#include <vector>
#include <unordered_map>

enum class SocialRank {
    Chief = 0,       // Stammesführer / Monarch
    Elder = 1,       // Ältestenrat / Berater
    Specialist = 2,  // Krieger, Meisterhandwerker
    Worker = 3,      // Arbeiter, Sammler, Bauern
    Outcast = 4      // Ausgestoßene / Parias
};

const char* SocialRankName(SocialRank rank);

struct Family {
    int id = -1;
    int parent1Id = -1;
    int parent2Id = -1;
    std::vector<int> childrenIds;
    int assignedHutId = -1;
};

struct Clan {
    int id = -1;
    std::string name;
    Color bannerColor = WHITE;
    int chiefId = -1;
    std::vector<int> memberIds;
    float cohesion = 100.0f; // Zusammenhalt
};

struct Kingdom {
    int id = 1;
    std::string name = "Vereinte Stämme";
    int monarchId = -1;
    float stability = 80.0f; // 0..100%
};

class SocialHierarchy {
public:
    SocialHierarchy();

    void InitDefaultClans();
    void AssignToClan(int characterId, int roleIndex);
    void SetRank(int characterId, SocialRank rank);
    SocialRank GetRank(int characterId) const;

    // Clan-Abfragen
    int GetClanId(int characterId) const;
    const Clan* GetClan(int clanId) const;
    bool AreSameClan(int idA, int idB) const;

    // Familien
    Family* GetOrCreateFamily(int parent1Id, int parent2Id);
    Family* GetFamily(int familyId);
    const Family* GetFamily(int familyId) const;
    bool AreKin(int idA, int idB) const;

    // Führung & Meuterei
    int GetChiefId() const { return kingdom_.monarchId; }
    void UpdateLeadership(const class RelationshipManager& rels, const std::vector<class Caveman>& cavemen, class EventLog& log);

    const Kingdom& GetKingdom() const { return kingdom_; }

private:
    std::vector<Clan> clans_;
    std::vector<Family> families_;
    Kingdom kingdom_;
    std::unordered_map<int, SocialRank> ranks_;
    std::unordered_map<int, int> memberToClan_;
    int nextFamilyId_ = 1;
    float leadershipCheckTimer_ = 0.0f;
};

#endif // SOCIAL_STRUCTURE_H

