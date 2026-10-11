#pragma once

#include <string>
#include <vector>

// Forward Declaration zur Vermeidung zirkulärer Abhängigkeiten
class Settlement;

// =============================================================================
// ZIVILISATIONS-EPOCHEN (AGES)
// =============================================================================
enum class Age {
    STONE_AGE = 0,   // Steinzeit: Primitive Hütten, Sammeln, Holz & Stein
    BRONZE_AGE = 1,  // Bronzezeit: Schmelzöfen, Kupfer/Bronze, robustere Häuser
    IRON_AGE = 2     // Eisenzeit: Schmieden, Eisenverhüttung, hochentwickelte Werkzeuge
};

// =============================================================================
// ZIVILISATIONS-WAREN & RESSOURCEN (CIV RESOURCE TYPES)
// =============================================================================
enum class CivResourceType {
    FOOD = 0,        // Grundnahrungsmittel für das Überleben
    WOOD = 1,        // Baumaterial für Hütten & Werkzeuge
    STONE = 2,       // Fundamente, Werkzeuge & Öfen
    COPPER = 3,      // Erst ab Bronzezeit abbaubar / nutzbar
    IRON = 4         // Erst ab Eisenzeit abbaubar / nutzbar
};

// =============================================================================
// GEBÄUDE-TYPEN
// =============================================================================
enum class BuildingType {
    HUT = 0,         // Wohnraum & Schlafplatz für Creatures
    STORAGE = 1,     // Erhöht maximale Lagerkapazität
    SMELTER = 2,     // Schmelzofen (freigeschaltet in Bronzezeit)
    FORGE = 3        // Schmiede (freigeschaltet in Eisenzeit)
};

// =============================================================================
// EPOCHEN-BALANCING & SCHWELLENWERTE
// Hier können die Voraussetzungen für Epochen-Aufstiege angepasst werden!
// =============================================================================
struct AgeRequirements {
    int requiredFood       = 0;
    int requiredWood       = 0;
    int requiredStone      = 0;
    int requiredCopper     = 0;
    int requiredHuts       = 0;
    int requiredBuildings  = 0;
};

// Modifikatoren und Boni für das jeweilige Zeitalter
struct AgeModifiers {
    float gatherEfficiency = 1.0f; // Faktor auf Sammelertrag (z. B. 1.0x -> 1.5x -> 2.2x)
    float buildEfficiency  = 1.0f; // Faktor auf Baufortschritt pro Arbeitsschritt
    int maxPopulationCap   = 20;   // Maximales Bevölkerungslimit
};

// =============================================================================
// TECHNOLOGIE- & EPOCHEN-VERWALTUNG
// =============================================================================
class CivilizationTech {
public:
    CivilizationTech();

    // --- Epochen-Status ------------------------------------------------------
    Age CurrentAge() const { return currentAge_; }
    const char* CurrentAgeName() const { return AgeToString(currentAge_); }
    static const char* AgeToString(Age age);
    static const char* ResourceToString(CivResourceType type);
    static const char* BuildingToString(BuildingType type);

    // --- Modifikatoren & Boni ------------------------------------------------
    const AgeModifiers& CurrentModifiers() const;
    float GetGatherEfficiency() const { return CurrentModifiers().gatherEfficiency; }
    float GetBuildEfficiency() const  { return CurrentModifiers().buildEfficiency; }

    // --- Aufstiegslogik ------------------------------------------------------
    bool HasNextAge() const;
    const AgeRequirements& NextAgeRequirements() const;
    bool CanAdvanceAge(const Settlement& settlement) const;
    bool TryAdvanceAge(Settlement& settlement, std::string& outMessage);

    // --- Freischaltungen prüfen ----------------------------------------------
    bool IsResourceUnlocked(CivResourceType type) const;
    bool IsBuildingUnlocked(BuildingType type) const;

    // --- Baukosten-Abfragen --------------------------------------------------
    static void GetBuildingCosts(BuildingType type, int& outWood, int& outStone, int& outCopper);

private:
    Age currentAge_ = Age::STONE_AGE;

    // Vordefinierte Schwellenwerte für Epochen-Aufstiege
    AgeRequirements reqStoneToBronze_;
    AgeRequirements reqBronzeToIron_;

    // Vordefinierte Boni pro Epoche
    AgeModifiers modStoneAge_;
    AgeModifiers modBronzeAge_;
    AgeModifiers modIronAge_;
};

