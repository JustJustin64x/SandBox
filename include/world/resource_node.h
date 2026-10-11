#ifndef RESOURCE_NODE_H
#define RESOURCE_NODE_H

#include "common.h"

#include <vector>

class World;

// Arten von Ressourcen-Objekten in der Welt.
enum class ResourceType {
    Tree,     // liefert Holz
    Bush,     // liefert Beeren (Nahrung)
    Rock,     // liefert Stein
    Animal,   // Wildtier: liefert Fleisch (Nahrung) für Jäger
    Count
};

const char* ResourceTypeName(ResourceType type);

// Ein einzelnes Ressourcen-Objekt (Baum, Busch, Fels oder Wildtier).
struct ResourceNode {
    ResourceType type = ResourceType::Tree;
    Vector2 position = { 0.0f, 0.0f };
    int amount = 0;            // verbleibende Einheiten (Holz/Beeren/Stein/Fleisch)
    int maxAmount = 0;
    float regrowTimer = 0.0f;

    // Nur für Wildtiere
    bool alive = true;
    float hp = 0.0f;
    bool engaged = false;      // steht still, weil es gerade bekämpft wird
    Vector2 wanderTarget = { 0.0f, 0.0f };
    float wanderTimer = 0.0f;
    float respawnTimer = 0.0f;
    bool facingLeft = false;
    float animTime = 0.0f;

    int claims = 0;            // Anzahl Steinzeitmenschen, die dieses Objekt ansteuern
    unsigned int generation = 0;  // ändert sich bei Tod/Respawn (ungültige Ziele erkennen)

    bool IsAvailable() const { return alive && amount > 0; }
};

// Verwaltet alle Ressourcen-Objekte: Platzierung, Nachwachsen, Tier-KI, Zeichnen.
class ResourceManager {
public:
    void Populate(const World& world, Vector2 campPos);
    void Update(const World& world, Vector2 campPos, float dt);
    void Draw(const Camera2D& camera) const;

    // Bestes (nahes, wenig umkämpftes) verfügbares Objekt eines Typs; -1 wenn keins.
    int FindBest(ResourceType type, Vector2 from) const;

    ResourceNode* Get(int index);
    const ResourceNode* Get(int index) const;
    bool IsValidTarget(int index, unsigned int generation) const;

    void Claim(int index);
    void Release(int index);

    int Harvest(int index, int wanted);   // gibt tatsächlich geerntete Menge zurück
    int KillAnimal(int index);            // gibt Fleischmenge zurück
    int SpawnAnimal(const World& world, Vector2 position);

    int CountAvailable(ResourceType type) const;
    const std::vector<ResourceNode>& Nodes() const { return nodes_; }

private:
    void SpawnNode(ResourceType type, Vector2 position);
    void UpdateAnimal(ResourceNode& node, const World& world, Vector2 campPos, float dt);
    void RespawnAnimal(ResourceNode& node, const World& world, Vector2 campPos);

    std::vector<ResourceNode> nodes_;
};

#endif // RESOURCE_NODE_H

