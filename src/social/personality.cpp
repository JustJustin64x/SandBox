#include "personality.h"
#include <algorithm>
#include <cmath>

Personality Personality::GenerateRandom(int roleIndex) {
    Personality p;
    // Zufallsbasis -1.0 bis +1.0
    auto randFloat = []() {
        return (float)GetRandomValue(-100, 100) / 100.0f;
    };

    p.dominance   = randFloat();
    p.altruism    = randFloat();
    p.sociability = randFloat();
    p.aggression  = randFloat();
    p.loyalty     = randFloat();

    // Rollen-Biases:
    // Jäger (roleIndex 0): mehr Dominanz & Aggression
    // Sammlerin (roleIndex 1): mehr Geselligkeit & Altruismus
    // Älteste (roleIndex 2): hohe Loyalität & Dominanz/Weisheit
    // Kind (roleIndex 3): hohe Soziabilität, geringe Dominanz
    if (roleIndex == 0) {
        p.dominance  = std::clamp(p.dominance + 0.35f, -1.0f, 1.0f);
        p.aggression = std::clamp(p.aggression + 0.30f, -1.0f, 1.0f);
    } else if (roleIndex == 1) {
        p.altruism    = std::clamp(p.altruism + 0.25f, -1.0f, 1.0f);
        p.sociability = std::clamp(p.sociability + 0.20f, -1.0f, 1.0f);
    } else if (roleIndex == 2) {
        p.loyalty   = std::clamp(p.loyalty + 0.40f, -1.0f, 1.0f);
        p.dominance = std::clamp(p.dominance + 0.20f, -1.0f, 1.0f);
    } else if (roleIndex == 3) {
        p.dominance   = std::clamp(p.dominance - 0.40f, -1.0f, 1.0f);
        p.sociability = std::clamp(p.sociability + 0.35f, -1.0f, 1.0f);
    }

    return p;
}

std::string Personality::GetPrimaryTraitDescription() const {
    // Finde die stärkste Ausprägung
    float maxVal = 0.0f;
    std::string desc = "Ausgeglichen";

    auto check = [&](float val, const char* high, const char* low) {
        if (std::abs(val) > maxVal && std::abs(val) > 0.35f) {
            maxVal = std::abs(val);
            desc = (val > 0.0f) ? high : low;
        }
    };

    check(dominance, "Führungsstark", "Unterwürfig");
    check(altruism, "Großzügig", "Egoistisch");
    check(sociability, "Gesellig", "Einzelgängerisch");
    check(aggression, "Hitzköpfig", "Friedfertig");
    check(loyalty, "Eingeschworen", "Eigenwillig");

    return desc;
}

const char* MemoryTypeName(MemoryType type) {
    switch (type) {
        case MemoryType::SharedWork:      return "Gemeinsame Arbeit";
        case MemoryType::SharedFood:      return "Nahrung geteilt";
        case MemoryType::SavedInCombat:   return "Im Kampf beigestanden";
        case MemoryType::Flirted:         return "Kompliment / Zuneigung";
        case MemoryType::Insulted:        return "Beleidigung / Spott";
        case MemoryType::RefusedResource: return "Hilfe verwehrt";
        case MemoryType::DisturbedSleep:  return "Ruhe gestört";
        case MemoryType::Attacked:        return "Körperlicher Streit";
        case MemoryType::Betrayed:        return "Verrat";
    }
    return "Ereignis";
}

void EpisodicMemory::Add(MemoryType type, int otherId, float simTime) {
    if (otherId < 0) return;

    MemoryEntry e;
    e.type = type;
    e.otherId = otherId;
    e.timestamp = simTime;

    switch (type) {
        case MemoryType::SharedWork:      e.baseImpact =  8.0f; e.duration = 100.0f; break;
        case MemoryType::SharedFood:      e.baseImpact = 18.0f; e.duration = 150.0f; break;
        case MemoryType::SavedInCombat:   e.baseImpact = 35.0f; e.duration = 240.0f; break;
        case MemoryType::Flirted:         e.baseImpact = 12.0f; e.duration = 120.0f; break;
        case MemoryType::Insulted:        e.baseImpact = -12.0f; e.duration = 90.0f; break;
        case MemoryType::RefusedResource: e.baseImpact = -16.0f; e.duration = 120.0f; break;
        case MemoryType::DisturbedSleep:  e.baseImpact = -8.0f; e.duration = 60.0f; break;
        case MemoryType::Attacked:        e.baseImpact = -30.0f; e.duration = 200.0f; break;
        case MemoryType::Betrayed:        e.baseImpact = -50.0f; e.duration = 300.0f; break;
    }

    entries_.push_back(e);

    // Maximal 25 prägende Erinnerungen pro Figur behalten
    if (entries_.size() > 25) {
        entries_.erase(entries_.begin());
    }
}

void EpisodicMemory::Update(float simTime) {
    // Entferne vollständig verblasste Erinnerungen
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
            [simTime](const MemoryEntry& e) {
                return (simTime - e.timestamp) >= e.duration;
            }),
        entries_.end()
    );
}

float EpisodicMemory::GetOpinionOffset(int otherId) const {
    float offset = 0.0f;
    for (const auto& e : entries_) {
        if (e.otherId == otherId) {
            offset += e.baseImpact;
        }
    }
    return std::clamp(offset, -60.0f, 60.0f);
}

