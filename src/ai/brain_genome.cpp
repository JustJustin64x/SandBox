#include "ai/brain_genome.h"
#include <random>
#include <cmath>

static std::mt19937& BrainRng() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    return gen;
}

static float RandomFloat(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(BrainRng());
}

const char* BrainGeneName(BrainGene gene) {
    switch (gene) {
        case BrainGene::FoodCaution:     return "Nahrungs-Vorsicht";
        case BrainGene::WarmthPriority:  return "Wärme-Priorität";
        case BrainGene::SleepRest:       return "Ruhe-Erholung";
        case BrainGene::GatherFood:      return "Sammeln & Ernten";
        case BrainGene::Hunt:            return "Jagdlust";
        case BrainGene::GatherWood:      return "Holzbeschaffung";
        case BrainGene::GatherStone:     return "Steinabbau";
        case BrainGene::Build:           return "Bautrieb";
        case BrainGene::Craft:           return "Handwerk";
        case BrainGene::Social:          return "Gemeinschaftssinn";
        case BrainGene::Expedition:      return "Entdeckerdrang";
        case BrainGene::BorderRaid:      return "Wachsamkeit & Grenzschutz";
        case BrainGene::PredatorCaution: return "Raubtier-Furcht";
        case BrainGene::Neuroplasticity: return "Neuroplastizität";
        case BrainGene::Count:           return "Unbekannt";
    }
    return "Unbekannt";
}

const char* BrainGeneDescription(BrainGene gene) {
    switch (gene) {
        case BrainGene::FoodCaution:     return "Reaktionsschnelligkeit bei einsetzendem Hunger.";
        case BrainGene::WarmthPriority:  return "Dringlichkeit von Schutz bei Frost und Sturm.";
        case BrainGene::SleepRest:       return "Bereitschaft zum Ausruhen bei Erschöpfung.";
        case BrainGene::GatherFood:      return "Motivation zur pflanzlichen Nahrungssuche.";
        case BrainGene::Hunt:            return "Bereitschaft zum Aufspüren und Erlegen von Tieren.";
        case BrainGene::GatherWood:      return "Priorität für das Fällen und Sammeln von Holz.";
        case BrainGene::GatherStone:     return "Priorität für das Brechen von Fels und Stein.";
        case BrainGene::Build:           return "Antrieb zur Errichtung und Reparatur von Bauten.";
        case BrainGene::Craft:           return "Interesse an Werkzeugen, Kleidung und Veredelung.";
        case BrainGene::Social:          return "Bereitschaft zu Gespräch, Fürsorge und Teilen.";
        case BrainGene::Expedition:      return "Drang zur Erforschung entlegener Wahrzeichen.";
        case BrainGene::BorderRaid:      return "Einsatz bei Grenzkonflikten und Schutz des Landes.";
        case BrainGene::PredatorCaution: return "Furcht und Fluchtbereitschaft vor Raubtieren.";
        case BrainGene::Neuroplasticity: return "Evolutionäre Fähigkeit, zu Lebzeiten aus Erfahrung zu lernen.";
        case BrainGene::Count:           return "";
    }
    return "";
}

// -----------------------------------------------------------------------------
// BrainGenome
// -----------------------------------------------------------------------------

BrainGenome::BrainGenome() {
    for (size_t i = 0; i < static_cast<size_t>(BrainGene::Count); ++i) {
        genes[i] = 1.0f;
    }
    genes[static_cast<size_t>(BrainGene::Neuroplasticity)] = 1.0f;
}

BrainGenome BrainGenome::GenerateRandom() {
    BrainGenome bg;
    // Natürliche Start-Variation in der Ur-Population:
    // Ein Teil der Gene liegt zwischen 0.5f und 1.6f
    for (size_t i = 0; i < static_cast<size_t>(BrainGene::Count); ++i) {
        bg.genes[i] = RandomFloat(0.65f, 1.45f);
    }

    // Spezifische Schwellen für Schlüsselgene
    bg.genes[static_cast<size_t>(BrainGene::FoodCaution)]     = RandomFloat(0.70f, 1.40f);
    bg.genes[static_cast<size_t>(BrainGene::WarmthPriority)]  = RandomFloat(0.70f, 1.40f);
    bg.genes[static_cast<size_t>(BrainGene::PredatorCaution)] = RandomFloat(0.70f, 1.35f);
    // Neuroplastizität startet bei primitiven Frühmenschen typisch um 0.70 bis 1.15
    bg.genes[static_cast<size_t>(BrainGene::Neuroplasticity)] = RandomFloat(0.75f, 1.20f);

    return bg;
}

BrainGenome BrainGenome::Crossover(const BrainGenome& parentA, const BrainGenome& parentB,
                                   float mutationRate, float mutationStrength) {
    BrainGenome child;

    for (size_t i = 0; i < static_cast<size_t>(BrainGene::Count); ++i) {
        // Crossover: Zufällige Vererbung von Elternteil A oder B mit weicher Rekombination
        float parentBias = RandomFloat(0.0f, 1.0f);
        float baseGene = (parentBias < 0.45f) ? parentA.genes[i] :
                         ((parentBias < 0.90f) ? parentB.genes[i] : (parentA.genes[i] * 0.5f + parentB.genes[i] * 0.5f));

        // Mutation (Zufallsrauschen)
        if (RandomFloat(0.0f, 1.0f) < mutationRate) {
            float delta = RandomFloat(-mutationStrength, mutationStrength);
            baseGene += delta;
        }

        // Grenzen: 0.25f (stark gedämpft) bis 2.60f (hoch priorisiert)
        child.genes[i] = std::clamp(baseGene, 0.25f, 2.60f);
    }

    // Neuroplastizität kann sich im Laufe der Generationen erhöhen
    child.genes[static_cast<size_t>(BrainGene::Neuroplasticity)] =
        std::clamp(child.genes[static_cast<size_t>(BrainGene::Neuroplasticity)], 0.40f, 2.50f);

    return child;
}

// -----------------------------------------------------------------------------
// BrainWeights (Arbeitsgedächtnis zu Lebzeiten)
// -----------------------------------------------------------------------------

BrainWeights::BrainWeights() {
    for (size_t i = 0; i < static_cast<size_t>(BrainGene::Count); ++i) {
        weights[i] = 1.0f;
    }
}

void BrainWeights::InitFromGenome(const BrainGenome& genome) {
    for (size_t i = 0; i < static_cast<size_t>(BrainGene::Count); ++i) {
        weights[i] = genome.genes[i];
    }
    lastReward = 0.0f;
    lastModifiedGene = BrainGene::FoodCaution;
    lastAdjustment = 0.0f;
    lastLessonDescription = "Angeborene Instinkte";
    totalExperiences = 0;
}

void BrainWeights::ApplyReward(BrainGene gene, float reward, float alphaEff, const std::string& lesson) {
    size_t idx = static_cast<size_t>(gene);
    if (idx >= static_cast<size_t>(BrainGene::Count)) return;

    // Reinforcement Learning Update:
    // W_neu = W_alt + alpha_eff * reward
    float adjustment = alphaEff * reward;

    // Dämpfung bei extremen Werten: Annäherung an Grenzen verlangsamt
    if (adjustment > 0.0f && weights[idx] > 2.2f) {
        adjustment *= 0.5f;
    } else if (adjustment < 0.0f && weights[idx] < 0.4f) {
        adjustment *= 0.5f;
    }

    weights[idx] = std::clamp(weights[idx] + adjustment, 0.15f, 3.20f);

    lastReward = reward;
    lastModifiedGene = gene;
    lastAdjustment = adjustment;
    lastLessonDescription = lesson;
    totalExperiences++;
}

void BrainWeights::AssimilateCulture(const BrainWeights& elder, float ratio) {
    float clampedRatio = std::clamp(ratio, 0.10f, 0.80f);

    for (size_t i = 0; i < static_cast<size_t>(BrainGene::Count); ++i) {
        // Neuroplastizität wird nicht direkt kulturell überschrieben, sondern erlerntes Verhalten
        if (i == static_cast<size_t>(BrainGene::Neuroplasticity)) continue;

        // Angleichung an die erlernten Gewichte des Ältesten
        weights[i] = weights[i] * (1.0f - clampedRatio) + elder.weights[i] * clampedRatio;
    }

    lastLessonDescription = "Weisheit von Ältestem übernommen";
    totalExperiences += 3;
}
