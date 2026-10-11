#include "CivilizationTech.h"
#include "Settlement.h"

CivilizationTech::CivilizationTech() {
    // =========================================================================
    // PARAMETER-KONFIGURATION & BALANCING
    // Hier kannst du die Voraussetzungen für Epochen-Aufstiege und Boni anpassen!
    // =========================================================================

    // 1. Voraussetzungen: Steinzeit -> Bronzezeit
    reqStoneToBronze_.requiredFood       = 35; // Benötigte Nahrung im Lager
    reqStoneToBronze_.requiredWood       = 25; // Benötigtes Holz
    reqStoneToBronze_.requiredStone      = 20; // Benötigter Stein
    reqStoneToBronze_.requiredCopper     = 0;
    reqStoneToBronze_.requiredHuts       = 2;  // Mindestens 2 fertige Hütten
    reqStoneToBronze_.requiredBuildings  = 2;  // Gesamtgebäude

    // 2. Voraussetzungen: Bronzezeit -> Eisenzeit
    reqBronzeToIron_.requiredFood        = 70;
    reqBronzeToIron_.requiredWood        = 50;
    reqBronzeToIron_.requiredStone       = 40;
    reqBronzeToIron_.requiredCopper      = 20; // Erst in Bronzezeit abbaubar
    reqBronzeToIron_.requiredHuts        = 4;
    reqBronzeToIron_.requiredBuildings   = 4;  // Inklusive Schmelzofen

    // 3. Epochen-Boni: Steinzeit (Basis-Werte)
    modStoneAge_.gatherEfficiency = 1.0f;
    modStoneAge_.buildEfficiency  = 1.0f;
    modStoneAge_.maxPopulationCap = 15;

    // 4. Epochen-Boni: Bronzezeit (+50% Sammeln & Bauen)
    modBronzeAge_.gatherEfficiency = 1.5f;
    modBronzeAge_.buildEfficiency  = 1.4f;
    modBronzeAge_.maxPopulationCap = 30;

    // 5. Epochen-Boni: Eisenzeit (+120% Effizienz, robuste Werkzeuge)
    modIronAge_.gatherEfficiency   = 2.2f;
    modIronAge_.buildEfficiency    = 2.0f;
    modIronAge_.maxPopulationCap   = 60;
}

const char* CivilizationTech::AgeToString(Age age) {
    switch (age) {
        case Age::STONE_AGE:  return "Steinzeit";
        case Age::BRONZE_AGE: return "Bronzezeit";
        case Age::IRON_AGE:   return "Eisenzeit";
    }
    return "Unbekannt";
}

const char* CivilizationTech::ResourceToString(CivResourceType type) {
    switch (type) {
        case CivResourceType::FOOD:   return "Nahrung";
        case CivResourceType::WOOD:   return "Holz";
        case CivResourceType::STONE:  return "Stein";
        case CivResourceType::COPPER: return "Kupfer";
        case CivResourceType::IRON:   return "Eisen";
    }
    return "Ressource";
}

const char* CivilizationTech::BuildingToString(BuildingType type) {
    switch (type) {
        case BuildingType::HUT:     return "Wohnhaushütte";
        case BuildingType::STORAGE: return "Vorratslager";
        case BuildingType::SMELTER: return "Schmelzofen";
        case BuildingType::FORGE:   return "Dorfschmiede";
    }
    return "Gebäude";
}

const AgeModifiers& CivilizationTech::CurrentModifiers() const {
    switch (currentAge_) {
        case Age::STONE_AGE:  return modStoneAge_;
        case Age::BRONZE_AGE: return modBronzeAge_;
        case Age::IRON_AGE:   return modIronAge_;
    }
    return modStoneAge_;
}

bool CivilizationTech::HasNextAge() const {
    return currentAge_ != Age::IRON_AGE;
}

const AgeRequirements& CivilizationTech::NextAgeRequirements() const {
    if (currentAge_ == Age::STONE_AGE) {
        return reqStoneToBronze_;
    }
    return reqBronzeToIron_;
}

bool CivilizationTech::CanAdvanceAge(const Settlement& settlement) const {
    if (!HasNextAge()) return false;

    const auto& req = NextAgeRequirements();
    const auto& inv = settlement.Inventory();

    // 1. Ressourcen-Prüfung
    if (inv.food < req.requiredFood) return false;
    if (inv.wood < req.requiredWood) return false;
    if (inv.stone < req.requiredStone) return false;
    if (inv.copper < req.requiredCopper) return false;

    // 2. Infrastruktur-Prüfung (Gebäude)
    if (settlement.FinishedBuildingCount(BuildingType::HUT) < req.requiredHuts) return false;
    if (settlement.FinishedBuildingCount() < req.requiredBuildings) return false;

    return true;
}

bool CivilizationTech::TryAdvanceAge(Settlement& settlement, std::string& outMessage) {
    if (!CanAdvanceAge(settlement)) {
        return false;
    }

    const auto& req = NextAgeRequirements();
    auto& inv = settlement.Inventory();

    // Ressourcen für den Epochenaufstieg investieren
    inv.Consume(CivResourceType::FOOD, req.requiredFood / 2); // Ein Teil wird als Feier/Entwicklung verbraucht
    inv.Consume(CivResourceType::WOOD, req.requiredWood / 2);
    inv.Consume(CivResourceType::STONE, req.requiredStone / 2);
    if (req.requiredCopper > 0) {
        inv.Consume(CivResourceType::COPPER, req.requiredCopper / 2);
    }

    // Epochenwechsel vollziehen
    if (currentAge_ == Age::STONE_AGE) {
        currentAge_ = Age::BRONZE_AGE;
        outMessage = "EPOCHALER FORTSCHRITT: Die Zivilisation erreicht die Bronzezeit! Schmelzöfen & Kupferabbau freigeschaltet.";
    } else if (currentAge_ == Age::BRONZE_AGE) {
        currentAge_ = Age::IRON_AGE;
        outMessage = "EPOCHALER FORTSCHRITT: Die Zivilisation erreicht die Eisenzeit! Schmieden & Eisenverhüttung freigeschaltet.";
    }

    return true;
}

bool CivilizationTech::IsResourceUnlocked(CivResourceType type) const {
    switch (type) {
        case CivResourceType::FOOD:
        case CivResourceType::WOOD:
        case CivResourceType::STONE:
            return true; // Von Anfang an verfügbar
        case CivResourceType::COPPER:
            return currentAge_ >= Age::BRONZE_AGE;
        case CivResourceType::IRON:
            return currentAge_ >= Age::IRON_AGE;
    }
    return false;
}

bool CivilizationTech::IsBuildingUnlocked(BuildingType type) const {
    switch (type) {
        case BuildingType::HUT:
        case BuildingType::STORAGE:
            return true; // Immer baubar
        case BuildingType::SMELTER:
            return currentAge_ >= Age::BRONZE_AGE;
        case BuildingType::FORGE:
            return currentAge_ >= Age::IRON_AGE;
    }
    return false;
}

void CivilizationTech::GetBuildingCosts(BuildingType type, int& outWood, int& outStone, int& outCopper) {
    outWood = 0;
    outStone = 0;
    outCopper = 0;
    switch (type) {
        case BuildingType::HUT:
            outWood = 15;
            outStone = 5;
            break;
        case BuildingType::STORAGE:
            outWood = 25;
            outStone = 20;
            break;
        case BuildingType::SMELTER:
            outWood = 10;
            outStone = 30;
            break;
        case BuildingType::FORGE:
            outWood = 15;
            outStone = 35;
            outCopper = 15;
            break;
    }
}

