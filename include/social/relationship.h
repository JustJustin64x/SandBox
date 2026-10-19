#ifndef RELATIONSHIP_H
#define RELATIONSHIP_H

#include "common.h"
#include "personality.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

enum class RelationshipTag {
    Stranger,       // Neu begegnet
    Neutral,        // Bekannt, keine tiefe Bindung
    Friend,         // Positive Sympathie & Vertrauen
    BestFriend,     // Sehr hohe Sympathie & Vertrauen
    Rival,          // Konkurrierend um Status/Ressourcen
    Enemy,          // Feindselig / Streit
    Partner         // Lebenspartner / Ehegatte
};

const char* RelationshipTagName(RelationshipTag tag);

struct RelationshipEdge {
    int fromId = -1;
    int toId   = -1;

    float affinity = 0.0f;    // -100 (Hass) bis +100 (Liebe/tiefe Zuneigung)
    float respect  = 0.0f;    // -100 (Verachtung) bis +100 (hohe Ehrfurcht)
    float trust    = 50.0f;   // 0 (misstrauisch) bis 100 (blindes Vertrauen)

    RelationshipTag tag = RelationshipTag::Neutral;
    float interactionTimer = 0.0f;

    void RecalculateTag(bool areKin, bool isSpouse);
};

class RelationshipManager {
public:
    RelationshipEdge& GetOrCreate(int fromId, int toId);
    const RelationshipEdge* Get(int fromId, int toId) const;

    void ApplyInteraction(int fromId, int toId, MemoryType type, float simTime);
    void UpdateTick(float dt);

    // Partnerschaften (Liebespaare)
    bool FormPartnership(int idA, int idB, class EventLog& log, const std::string& nameA, const std::string& nameB);
    void BreakPartnership(int idA, int idB);
    int GetPartner(int id) const;

    // Abfragen für Freunde & Feinde
    std::vector<int> GetFriends(int id) const;
    std::vector<int> GetEnemies(int id) const;
    float GetAffinity(int fromId, int toId) const;
    float GetRespect(int fromId, int toId) const;
    float GetTrust(int fromId, int toId) const;

    // Formatierte Übersicht
    std::string GetRelationshipSummary(int characterId, const std::string& characterName) const;

private:
    static inline uint64_t MakeKey(int a, int b) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) | static_cast<uint32_t>(b);
    }

    std::unordered_map<uint64_t, RelationshipEdge> edges_;
    std::unordered_map<int, int> partners_; // ID -> Partner ID
    float tickTimer_ = 0.0f;
};

#endif // RELATIONSHIP_H

