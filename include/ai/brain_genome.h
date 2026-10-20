#ifndef BRAIN_GENOME_H
#define BRAIN_GENOME_H

#include <string>
#include <vector>
#include <algorithm>

// -----------------------------------------------------------------------------
// 14 Gene / kognitive Gewichte des Gehirn-Genoms (BrainGenome)
// -----------------------------------------------------------------------------
enum class BrainGene {
    FoodCaution = 0,     // Vorsicht / Reaktionsschwelle bei Hunger & Nahrungssuche
    WarmthPriority,      // Priorität für Aufwärmen, Feuerunterhalt & Kälteschutz
    SleepRest,           // Priorität für Erholung, Regeneration & Schlaf
    GatherFood,          // Affinität für Beerensammeln, Feldbau & Herdenpflege
    Hunt,                // Jagdlust & Fleischbeschaffung
    GatherWood,          // Holzbeschaffung & Forstarbeit
    GatherStone,         // Steinabbau & Bergbau
    Build,               // Bautrieb, Konstruktion & Gebäude-Upgrades
    Craft,               // Handwerk, Veredelung & Werkzeugfertigung
    Social,              // Soziales, Familie, Teilen & Plaudern
    Expedition,          // Neugier, Mut & Erforschung von POIs
    BorderRaid,          // Wachsamkeit, Grenzverteidigung & Gebietsanspruch
    PredatorCaution,     // Furcht vor Raubtieren, Fluchtinstinkt & Vorsicht
    Neuroplasticity,     // Evolutionäre Lernfähigkeit (Bestimmt RL-Lernrate alpha)
    Count
};

const char* BrainGeneName(BrainGene gene);
const char* BrainGeneDescription(BrainGene gene);

// -----------------------------------------------------------------------------
// BrainGenome: Das vererbbare genetische Gehirnprofil (Evolution)
// -----------------------------------------------------------------------------
struct BrainGenome {
    float genes[static_cast<size_t>(BrainGene::Count)];

    BrainGenome();

    static BrainGenome GenerateRandom();
    static BrainGenome Crossover(const BrainGenome& parentA, const BrainGenome& parentB,
                                 float mutationRate = 0.06f, float mutationStrength = 0.12f);

    float GetGene(BrainGene g) const { return genes[static_cast<size_t>(g)]; }
    void SetGene(BrainGene g, float val) { genes[static_cast<size_t>(g)] = std::clamp(val, 0.20f, 3.0f); }
};

// -----------------------------------------------------------------------------
// BrainWeights: Das lernende Arbeitsgedächtnis zu Lebzeiten (Reinforcement Learning)
// -----------------------------------------------------------------------------
struct BrainWeights {
    float weights[static_cast<size_t>(BrainGene::Count)];
    float lastReward = 0.0f;
    BrainGene lastModifiedGene = BrainGene::FoodCaution;
    float lastAdjustment = 0.0f;
    std::string lastLessonDescription = "Keine prägenden Erfahrungen";
    int totalExperiences = 0;

    BrainWeights();

    void InitFromGenome(const BrainGenome& genome);

    // Reinforcement Learning Update: W_neu = clamp(W_alt + alpha_eff * reward, 0.15f, 3.2f)
    void ApplyReward(BrainGene gene, float reward, float alphaEff, const std::string& lesson);

    // Kulturelles Lernen / Wissensweitergabe (z. B. TeachChild durch Ältesten)
    void AssimilateCulture(const BrainWeights& elder, float ratio = 0.50f);

    float Get(BrainGene gene) const { return weights[static_cast<size_t>(gene)]; }
    void Set(BrainGene gene, float val) { weights[static_cast<size_t>(gene)] = std::clamp(val, 0.15f, 3.2f); }
};

#endif // BRAIN_GENOME_H
