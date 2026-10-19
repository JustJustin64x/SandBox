#include "relationship.h"
#include "event_log.h"
#include <algorithm>
#include <cmath>

const char* RelationshipTagName(RelationshipTag tag) {
    switch (tag) {
        case RelationshipTag::Stranger:   return "Fremder";
        case RelationshipTag::Neutral:    return "Bekannter";
        case RelationshipTag::Friend:     return "Freund";
        case RelationshipTag::BestFriend: return "Bester Freund";
        case RelationshipTag::Rival:      return "Rivale";
        case RelationshipTag::Enemy:      return "Feind";
        case RelationshipTag::Partner:    return "Lebenspartner";
    }
    return "?";
}

void RelationshipEdge::RecalculateTag(bool areKin, bool isSpouse) {
    (void)areKin;
    if (isSpouse) {
        tag = RelationshipTag::Partner;
        return;
    }

    if (affinity >= 65.0f && trust >= 70.0f) {
        tag = RelationshipTag::BestFriend;
    } else if (affinity >= 28.0f && trust >= 40.0f) {
        tag = RelationshipTag::Friend;
    } else if (affinity <= -45.0f) {
        tag = RelationshipTag::Enemy;
    } else if (affinity <= -15.0f || (respect <= -25.0f && affinity < 0.0f)) {
        tag = RelationshipTag::Rival;
    } else {
        tag = RelationshipTag::Neutral;
    }
}

RelationshipEdge& RelationshipManager::GetOrCreate(int fromId, int toId) {
    uint64_t key = MakeKey(fromId, toId);
    auto it = edges_.find(key);
    if (it != edges_.end()) {
        return it->second;
    }

    RelationshipEdge edge;
    edge.fromId = fromId;
    edge.toId   = toId;
    edge.affinity = (float)GetRandomValue(-5, 15);
    edge.respect  = (float)GetRandomValue(0, 15);
    edge.trust    = 50.0f;
    edge.tag      = RelationshipTag::Neutral;

    edges_[key] = edge;
    return edges_[key];
}

const RelationshipEdge* RelationshipManager::Get(int fromId, int toId) const {
    uint64_t key = MakeKey(fromId, toId);
    auto it = edges_.find(key);
    if (it != edges_.end()) return &it->second;
    return nullptr;
}

void RelationshipManager::ApplyInteraction(int fromId, int toId, MemoryType type, float simTime) {
    (void)simTime;
    if (fromId == toId || fromId < 0 || toId < 0) return;

    RelationshipEdge& edge = GetOrCreate(fromId, toId);
    edge.interactionTimer = 0.0f;

    switch (type) {
        case MemoryType::SharedWork:
            edge.affinity = std::clamp(edge.affinity + 6.0f, -100.0f, 100.0f);
            edge.respect  = std::clamp(edge.respect + 4.0f, -100.0f, 100.0f);
            edge.trust    = std::clamp(edge.trust + 3.0f, 0.0f, 100.0f);
            break;
        case MemoryType::SharedFood:
            edge.affinity = std::clamp(edge.affinity + 16.0f, -100.0f, 100.0f);
            edge.trust    = std::clamp(edge.trust + 10.0f, 0.0f, 100.0f);
            break;
        case MemoryType::SavedInCombat:
            edge.affinity = std::clamp(edge.affinity + 30.0f, -100.0f, 100.0f);
            edge.respect  = std::clamp(edge.respect + 25.0f, -100.0f, 100.0f);
            edge.trust    = std::clamp(edge.trust + 20.0f, 0.0f, 100.0f);
            break;
        case MemoryType::Flirted:
            edge.affinity = std::clamp(edge.affinity + 12.0f, -100.0f, 100.0f);
            edge.trust    = std::clamp(edge.trust + 5.0f, 0.0f, 100.0f);
            break;
        case MemoryType::Insulted:
            edge.affinity = std::clamp(edge.affinity - 14.0f, -100.0f, 100.0f);
            edge.respect  = std::clamp(edge.respect - 8.0f, -100.0f, 100.0f);
            break;
        case MemoryType::RefusedResource:
            edge.affinity = std::clamp(edge.affinity - 12.0f, -100.0f, 100.0f);
            edge.trust    = std::clamp(edge.trust - 12.0f, 0.0f, 100.0f);
            break;
        case MemoryType::DisturbedSleep:
            edge.affinity = std::clamp(edge.affinity - 8.0f, -100.0f, 100.0f);
            break;
        case MemoryType::Attacked:
            edge.affinity = std::clamp(edge.affinity - 32.0f, -100.0f, 100.0f);
            edge.trust    = std::clamp(edge.trust - 25.0f, 0.0f, 100.0f);
            edge.respect  = std::clamp(edge.respect + 6.0f, -100.0f, 100.0f); // Respekt aus Furcht
            break;
        case MemoryType::Betrayed:
            edge.affinity = std::clamp(edge.affinity - 45.0f, -100.0f, 100.0f);
            edge.trust    = std::clamp(edge.trust - 40.0f, 0.0f, 100.0f);
            break;
    }

    bool isSpouse = (GetPartner(fromId) == toId);
    edge.RecalculateTag(false, isSpouse);
}

void RelationshipManager::UpdateTick(float dt) {
    tickTimer_ += dt;
    if (tickTimer_ < 2.5f) return;
    tickTimer_ = 0.0f;

    // Sanfter Zerfall von extremen Emotionen über Zeit in Richtung 0
    for (auto& pair : edges_) {
        RelationshipEdge& edge = pair.second;
        edge.interactionTimer += 2.5f;

        if (edge.interactionTimer > 30.0f) {
            if (edge.affinity > 5.0f)  edge.affinity = std::max(5.0f, edge.affinity - 0.5f);
            if (edge.affinity < -5.0f) edge.affinity = std::min(-5.0f, edge.affinity + 0.5f);
            if (edge.respect > 10.0f)  edge.respect  = std::max(10.0f, edge.respect - 0.4f);
        }

        bool isSpouse = (GetPartner(edge.fromId) == edge.toId);
        edge.RecalculateTag(false, isSpouse);
    }
}

bool RelationshipManager::FormPartnership(int idA, int idB, EventLog& log, const std::string& nameA, const std::string& nameB) {
    if (idA == idB || idA < 0 || idB < 0) return false;
    if (GetPartner(idA) >= 0 || GetPartner(idB) >= 0) return false; // Bereits vergeben

    partners_[idA] = idB;
    partners_[idB] = idA;

    RelationshipEdge& eAB = GetOrCreate(idA, idB);
    RelationshipEdge& eBA = GetOrCreate(idB, idA);

    eAB.affinity = std::max(eAB.affinity, 85.0f);
    eBA.affinity = std::max(eBA.affinity, 85.0f);
    eAB.trust = std::max(eAB.trust, 90.0f);
    eBA.trust = std::max(eBA.trust, 90.0f);

    eAB.tag = RelationshipTag::Partner;
    eBA.tag = RelationshipTag::Partner;

    log.Add(TextFormat("%s und %s sind jetzt Lebenspartner! ❤️", nameA.c_str(), nameB.c_str()), { 255, 120, 180, 255 });
    return true;
}

void RelationshipManager::BreakPartnership(int idA, int idB) {
    partners_.erase(idA);
    partners_.erase(idB);

    RelationshipEdge& eAB = GetOrCreate(idA, idB);
    RelationshipEdge& eBA = GetOrCreate(idB, idA);
    eAB.RecalculateTag(false, false);
    eBA.RecalculateTag(false, false);
}

int RelationshipManager::GetPartner(int id) const {
    auto it = partners_.find(id);
    if (it != partners_.end()) return it->second;
    return -1;
}

std::vector<int> RelationshipManager::GetFriends(int id) const {
    std::vector<int> res;
    for (const auto& pair : edges_) {
        const auto& edge = pair.second;
        if (edge.fromId == id && (edge.tag == RelationshipTag::Friend || edge.tag == RelationshipTag::BestFriend || edge.tag == RelationshipTag::Partner)) {
            res.push_back(edge.toId);
        }
    }
    return res;
}

std::vector<int> RelationshipManager::GetEnemies(int id) const {
    std::vector<int> res;
    for (const auto& pair : edges_) {
        const auto& edge = pair.second;
        if (edge.fromId == id && (edge.tag == RelationshipTag::Enemy || edge.tag == RelationshipTag::Rival)) {
            res.push_back(edge.toId);
        }
    }
    return res;
}

float RelationshipManager::GetAffinity(int fromId, int toId) const {
    const auto* e = Get(fromId, toId);
    return e ? e->affinity : 0.0f;
}

float RelationshipManager::GetRespect(int fromId, int toId) const {
    const auto* e = Get(fromId, toId);
    return e ? e->respect : 0.0f;
}

float RelationshipManager::GetTrust(int fromId, int toId) const {
    const auto* e = Get(fromId, toId);
    return e ? e->trust : 50.0f;
}

std::string RelationshipManager::GetRelationshipSummary(int characterId, const std::string& characterName) const {
    (void)characterName;
    int partner = GetPartner(characterId);
    auto friends = GetFriends(characterId);
    auto enemies = GetEnemies(characterId);

    char buf[256];
    std::snprintf(buf, sizeof(buf), "Partner: %s | Freunde: %d | Rivalen: %d",
                  (partner >= 0 ? TextFormat("#%d", partner) : "Keiner"),
                  (int)friends.size(), (int)enemies.size());
    return std::string(buf);
}

