#ifndef PERSONALITY_H
#define PERSONALITY_H

#include "common.h"
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// 1. Persönlichkeitsmodell (Werte von -1.0 bis +1.0)
// -----------------------------------------------------------------------------
struct Personality {
    float dominance   = 0.0f; // -1.0 (unterwürfig) .. +1.0 (autoritär, Führungsdrang)
    float altruism    = 0.0f; // -1.0 (egoistisch)  .. +1.0 (selbstlos, teilt gern)
    float sociability = 0.0f; // -1.0 (Einzelgänger).. +1.0 (gesellig, extravertiert)
    float aggression  = 0.0f; // -1.0 (konfliktscheu) .. +1.0 (reizbar, angriffslustig)
    float loyalty     = 0.0f; // -1.0 (opportunistisch) .. +1.0 (treu zu Clan/Reich)

    static Personality GenerateRandom(int roleIndex);
    std::string GetPrimaryTraitDescription() const;
};

// -----------------------------------------------------------------------------
// 2. Episodisches Gedächtnis
// -----------------------------------------------------------------------------
enum class MemoryType {
    SharedWork,       // Gemeinsam geschuftet (+Sympathie, +Respekt)
    SharedFood,       // Nahrung geschenkt (+Sympathie, +Vertrauen)
    SavedInCombat,    // Im Kampf gerettet (++Sympathie, ++Respekt, ++Vertrauen)
    Flirted,          // Zuneigung / Kompliment (+Sympathie, Anziehung)
    Insulted,         // Gestichelt / beleidigt (-Sympathie, -Respekt)
    RefusedResource,  // Nahrung verwehrt (-Sympathie, -Vertrauen)
    DisturbedSleep,   // Schlafplatz streitig gemacht (-Sympathie)
    Attacked,         // Körperlicher Übergriff / Streit (--Sympathie, +Furcht/Wut)
    Betrayed          // Verrat / Diebstahl (---Vertrauen, ---Sympathie)
};

const char* MemoryTypeName(MemoryType type);

struct MemoryEntry {
    MemoryType type = MemoryType::SharedWork;
    int otherId = -1;
    float timestamp = 0.0f;
    float baseImpact = 0.0f;    // Initialer Einfluss auf die Meinung
    float duration = 120.0f;    // Dauer bis zum Verblassen in Sekunden
};

class EpisodicMemory {
public:
    void Add(MemoryType type, int otherId, float simTime);
    void Update(float simTime);

    // Summierter Meinungseinfluss über eine andere Figur
    float GetOpinionOffset(int otherId) const;
    const std::vector<MemoryEntry>& Entries() const { return entries_; }

private:
    std::vector<MemoryEntry> entries_;
};

#endif // PERSONALITY_H

