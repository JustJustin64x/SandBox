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

// Abgespaltene Splittergruppe / unabhängige Clan-Siedlung (Stufe 4: Stammesspaltung)
struct SplinterSettlement {
    int clanId = -1;
    std::string factionName;
    Color bannerColor = WHITE;
    Vector2 centerPos = { 0.0f, 0.0f };
    int leaderId = -1;
    int townCenterBuildingId = -1;
    float territoryRadius = 115.0f;
};

class SocialHierarchy {
public:
    SocialHierarchy();

    void InitDefaultClans();
    void AssignToClan(int characterId, int roleIndex, int preferredClanId = -1);
    void SetRank(int characterId, SocialRank rank);
    SocialRank GetRank(int characterId) const;

    // Clan-Abfragen
    int GetClanId(int characterId) const;
    const Clan* GetClan(int clanId) const;
    Clan* GetClanMutable(int clanId);
    const std::vector<Clan>& GetClans() const { return clans_; }
    bool AreSameClan(int idA, int idB) const;

    // Familien
    Family* GetOrCreateFamily(int parent1Id, int parent2Id);
    Family* GetFamily(int familyId);
    const Family* GetFamily(int familyId) const;
    const std::vector<Family>& GetFamilies() const { return families_; }
    bool AreKin(int idA, int idB) const;

    // Führung, Meuterei & Stammesspaltung (Schisma)
    int GetChiefId() const { return kingdom_.monarchId; }
    void UpdateLeadership(const class RelationshipManager& rels, const std::vector<class Caveman>& cavemen, class EventLog& log);
    bool CheckForSchism(const class RelationshipManager& rels, const std::vector<class Caveman>& cavemen,
                        int& outRebelClanId, int& outRebelLeaderId);
    void RegisterSplinterSettlement(const SplinterSettlement& settlement);
    const std::vector<SplinterSettlement>& GetSplinterSettlements() const { return splinterSettlements_; }
    Vector2 GetFactionCampForMember(int characterId, Vector2 defaultCampPos) const;

    const Kingdom& GetKingdom() const { return kingdom_; }

private:
    std::vector<Clan> clans_;
    std::vector<Family> families_;
    std::vector<SplinterSettlement> splinterSettlements_;
    Kingdom kingdom_;
    std::unordered_map<int, SocialRank> ranks_;
    std::unordered_map<int, int> memberToClan_;
    int nextFamilyId_ = 1;
    float leadershipCheckTimer_ = 0.0f;
};

#endif // SOCIAL_STRUCTURE_H

