#include "age_manager.h"
#include "tribe.h"
#include "building.h"
#include "skill_tree.h"
#include "event_log.h"
#include "config.h"

#include <algorithm>

const char* EraName(Era era) {
    switch (era) {
        case Era::StoneAge:  return "Steinzeit";
        case Era::BronzeAge: return "Bronze- / Werkzeugzeit";
        case Era::IronAge:   return "Siedlungs- / Eisenzeit";
    }
    return "Unbekannt";
}

Color EraColor(Era era) {
    switch (era) {
        case Era::StoneAge:  return Color{ 210, 195, 170, 255 };
        case Era::BronzeAge: return Color{ 230, 160, 50, 255 };
        case Era::IronAge:   return Color{ 120, 200, 240, 255 };
    }
    return WHITE;
}

const char* HistoricalEpochName(HistoricalEpoch epoch) {
    switch (epoch) {
        case HistoricalEpoch::HunterGatherer:       return "1. Jäger & Sammler (Frühe Steinzeit)";
        case HistoricalEpoch::Neolithic:            return "2. Späte Steinzeit / Neolithikum";
        case HistoricalEpoch::CopperAge:            return "3. Kupferzeit (Chalkolithikum)";
        case HistoricalEpoch::BronzeAge:            return "4. Bronzezeit";
        case HistoricalEpoch::IronAge:              return "5. Eisenzeit";
        case HistoricalEpoch::Antiquity:            return "6. Antike Hochkulturen";
        case HistoricalEpoch::MiddleAges:           return "7. Mittelalter";
        case HistoricalEpoch::Renaissance:          return "8. Renaissance";
        case HistoricalEpoch::Enlightenment:        return "9. Aufklärung & Frühindustrie";
        case HistoricalEpoch::IndustrialRevolution: return "10. Industrielle Revolution";
        case HistoricalEpoch::Electrification:      return "11. Elektrifizierung";
        case HistoricalEpoch::ModernEra:            return "12. Moderne";
        case HistoricalEpoch::InformationAge:       return "13. Informationszeitalter";
        case HistoricalEpoch::NearFuture:           return "14. Nahe Zukunft";
    }
    return "Unbekannte Epoche";
}

int HistoricalEpochNumber(HistoricalEpoch epoch) {
    return static_cast<int>(epoch) + 1;
}

AgeManager::AgeManager() {}

float AgeManager::RequiredEraDuration(Era era) const {
    switch (era) {
        case Era::StoneAge:  return cfg::kStoneAgeDuration;
        case Era::BronzeAge: return cfg::kBronzeAgeDuration;
        case Era::IronAge:   return cfg::kIronAgeDuration;
    }
    return 9999.0f;
}

void AgeManager::UpdateHistoricalEpoch(const Stockpile& stock, const BuildingManager& buildings,
                                       const SkillTree& skillTree, int population, EventLog& log) {
    int totalFood = stock.food + stock.cookedFood;
    int huts = buildings.GetBuildingCount(BuildingType::Hut, true);
    int farms = buildings.GetBuildingCount(BuildingType::FarmPlot, true);
    int pantries = buildings.GetBuildingCount(BuildingType::Pantry, true);
    int benches = buildings.GetBuildingCount(BuildingType::CraftBench, true);
    int kilns = buildings.GetBuildingCount(BuildingType::Kiln, true);
    int tcTier = buildings.GetMaxTier(BuildingType::TownCenter);
    int hutTier = buildings.GetMaxTier(BuildingType::Hut);
    int kilnTier = buildings.GetMaxTier(BuildingType::Kiln);
    int monTier = buildings.GetMaxTier(BuildingType::Monument);

    // Kausaler Übergang 1 -> 2: Jäger & Sammler -> Späte Steinzeit / Neolithikum
    // Voraussetzung: Beherrschung des Feuers & Faustkeils, erste feste Hütte, stabile Nahrungsreserve
    if (currentEpoch_ == HistoricalEpoch::HunterGatherer) {
        bool hasBasicTech = skillTree.IsUnlocked("fire") && skillTree.IsUnlocked("flint") &&
                            (skillTree.IsUnlocked("agriculture") || skillTree.IsUnlocked("tailoring"));
        if (hasBasicTech && huts >= 1 && totalFood >= 4 && population >= 3) {
            currentEpoch_ = HistoricalEpoch::Neolithic;
            log.Add("====================================================", { 180, 235, 130, 255 });
            log.Add("EPOCHE II ERREICHT: Späte Steinzeit / Neolithikum!", { 180, 235, 130, 255 });
            log.Add("Sesshaftigkeit, Vorratshaltung und früher Ackerbau entstehen kausal!", { 210, 245, 175, 255 });
            log.Add("====================================================", { 180, 235, 130, 255 });
        }
    }
    // Kausaler Übergang 2 -> 3: Neolithikum -> Kupferzeit
    // Voraussetzung: Ackerbau oder Vorratskammer etabliert + Höhlenkunde oder Kupferabbau entdeckt
    else if (currentEpoch_ == HistoricalEpoch::Neolithic) {
        bool hasMetallurgySeed = skillTree.IsUnlocked("cave") || skillTree.IsUnlocked("copper");
        bool hasSurplusInfra = (farms >= 1 || pantries >= 1 || huts >= 2);
        if (hasMetallurgySeed && hasSurplusInfra && totalFood >= 6 && population >= 3) {
            currentEpoch_ = HistoricalEpoch::CopperAge;
            log.Add("====================================================", { 235, 155, 90, 255 });
            log.Add("EPOCHE III ERREICHT: Die Kupferzeit (Chalkolithikum)!", { 235, 155, 90, 255 });
            log.Add("Erste Erzadern und Brennöfen leiten das Metallzeitalter ein!", { 245, 190, 130, 255 });
            log.Add("====================================================", { 235, 155, 90, 255 });
        }
    }
    // Kausaler Übergang 3 -> 4: Kupferzeit -> Bronzezeit
    else if (currentEpoch_ == HistoricalEpoch::CopperAge) {
        if (currentEra_ >= Era::BronzeAge && skillTree.IsUnlocked("bronze") && benches >= 1) {
            currentEpoch_ = HistoricalEpoch::BronzeAge;
            log.Add("====================================================", GOLD);
            log.Add("EPOCHE IV ERREICHT: Hochblüte der Bronzezeit!", GOLD);
            log.Add("Bronzelegierungen, spezialisierte Werkstätten und Handelswege entstehen!", { 255, 230, 120, 255 });
            log.Add("====================================================", GOLD);
        }
    }
    // Kausaler Übergang 4 -> 5: Bronzezeit -> Eisenzeit
    else if (currentEpoch_ == HistoricalEpoch::BronzeAge) {
        if (currentEra_ >= Era::IronAge || (skillTree.IsUnlocked("bloomery") && kilns >= 1)) {
            currentEpoch_ = HistoricalEpoch::IronAge;
            log.Add("====================================================", SKYBLUE);
            log.Add("EPOCHE V ERREICHT: Die Eisenzeit!", SKYBLUE);
            log.Add("Rennöfen, Eisenwerkzeuge und befestigte Siedlungen prägen die Welt!", { 170, 230, 255, 255 });
            log.Add("====================================================", SKYBLUE);
        }
    }
    // Kausaler Übergang 5 -> 6: Eisenzeit -> Antike Hochkultur
    else if (currentEpoch_ == HistoricalEpoch::IronAge) {
        if (skillTree.IsUnlocked("masonry") && skillTree.IsUnlocked("defense") &&
            tcTier >= 3 && hutTier >= 3 && (kilnTier >= 2 || monTier >= 2) && population >= 5) {
            currentEpoch_ = HistoricalEpoch::Antiquity;
            log.Add("====================================================", { 220, 175, 255, 255 });
            log.Add("EPOCHE VI ERREICHT: Antike Hochkultur!", { 220, 175, 255, 255 });
            log.Add("Steinerne Hallen, Tempel-Altäre, gepflasterte Straßen und Stadtmauern!", { 235, 205, 255, 255 });
            log.Add("====================================================", { 220, 175, 255, 255 });
        }
    }
}

void AgeManager::Update(const Stockpile& stock, const BuildingManager& buildings, const SkillTree& skillTree,
                        int population, float dt, EventLog& log) {
    // 1. Epochen-Laufzeit dokumentieren (reine Statistik, kein künstlicher Blocker mehr!)
    eraElapsedTime_ += dt;

    // 2. Kausale Prüfung der 14 historischen Epochen
    UpdateHistoricalEpoch(stock, buildings, skillTree, population, log);

    // 3. Forschungspunkte durch Kultur/Gebäude
    int tcTier = buildings.GetMaxTier(BuildingType::TownCenter);
    int craftBenches = buildings.GetBuildingCount(BuildingType::CraftBench, true);
    float gainRate = 0.05f + (float)tcTier * 0.1f + (float)craftBenches * 0.15f;
    researchPoints_ += gainRate * dt;

    // 4. Regelmäßige Status-Prüfung
    autoCheckTimer_ += dt;
    if (autoCheckTimer_ >= 12.0f) {
        autoCheckTimer_ = 0.0f;
        std::string req;
        if (CanAdvance(stock, buildings, skillTree, population, &req)) {
            log.Add(TextFormat("Aufstieg in die %s bereit! (%s)",
                               EraName(static_cast<Era>(EraIndex() + 1)), req.c_str()),
                    GOLD);
        }
    }
}

bool AgeManager::CanAdvance(const Stockpile& stock, const BuildingManager& buildings, const SkillTree& skillTree,
                            int population, std::string* outRequirement) const {
    int totalFood = stock.food + stock.cookedFood;
    if (currentEra_ == Era::StoneAge) {
        // Kausale Kriterien für den Übergang in die Bronze-/Werkzeugzeit (KEINE künstliche Stoppuhr!):
        // 1. Alle Steinzeit-Meilensteine erforscht (Ackerbau & Saatgut, Höhlenkunde, Megalith)
        // 2. Sesshaftigkeit: Stammesplatz + mindestens 2 feste Hütten errichtet
        // 3. Nahrungssicherheit: Mindestens 6 Nahrungseinheiten auf Vorrat
        // 4. Demografie: Mindestens 3 lebende Stammesmitglieder
        bool milestonesMet = skillTree.AreEraMilestonesCompleted(Era::StoneAge);
        bool hasTC = buildings.GetBuildingCount(BuildingType::TownCenter, true) > 0;
        int huts = buildings.GetBuildingCount(BuildingType::Hut, true);
        bool hasShelters = hasTC && (huts >= 2);
        bool hasFood = (totalFood >= 6);
        bool hasPop = (population >= 3);

        int doneMilestones = skillTree.GetMilestonesCompleted(Era::StoneAge);
        int totalMilestones = skillTree.GetMilestonesTotal(Era::StoneAge);

        if (outRequirement) {
            *outRequirement = TextFormat("Meilensteine: %d/%d | Hütten: %d/2 | Vorrat: %d/6 | Pop: %d/3",
                                         doneMilestones, totalMilestones,
                                         huts, totalFood, population);
        }
        return milestonesMet && hasShelters && hasFood && hasPop;
    }
    else if (currentEra_ == Era::BronzeAge) {
        // Kausale Kriterien für den Übergang in die Eisenzeit (KEINE künstliche Stoppuhr!):
        // 1. Alle Bronzezeit-Meilensteine erforscht (Bronzelegierung & Zimmererkunst)
        // 2. Infrastruktur & Arbeitsteilung: Langhaus (TC Stufe 2), Werkbank & Acker/Vorratskammer/Ofen
        // 3. Nahrungsüberschuss: Mindestens 8 Nahrungseinheiten
        // 4. Demografie: Mindestens 4 Einwohner
        bool milestonesMet = skillTree.AreEraMilestonesCompleted(Era::BronzeAge);
        bool tcTier2 = buildings.GetMaxTier(BuildingType::TownCenter) >= 2;
        bool hasBench = buildings.GetBuildingCount(BuildingType::CraftBench, true) > 0;
        bool hasProduction = (buildings.GetBuildingCount(BuildingType::FarmPlot, true) > 0 ||
                              buildings.GetBuildingCount(BuildingType::Pantry, true) > 0 ||
                              buildings.GetBuildingCount(BuildingType::Kiln, true) > 0);
        bool hasFood = (totalFood >= 8);
        bool hasPop = (population >= 4);

        int doneMilestones = skillTree.GetMilestonesCompleted(Era::BronzeAge);
        int totalMilestones = skillTree.GetMilestonesTotal(Era::BronzeAge);

        if (outRequirement) {
            *outRequirement = TextFormat("Meilensteine: %d/%d | Langhaus & Werkbank: %s | Vorrat: %d/8 | Pop: %d/4",
                                         doneMilestones, totalMilestones,
                                         (tcTier2 && hasBench && hasProduction) ? "Bereit" : "Im Aufbau",
                                         totalFood, population);
        }
        return milestonesMet && tcTier2 && hasBench && hasProduction && hasFood && hasPop;
    }

    if (outRequirement) *outRequirement = TextFormat("Aktuelle Epoche: %s", HistoricalEpochName(currentEpoch_));
    return false;
}

bool AgeManager::AdvanceEra(Stockpile& stock, EventLog& log) {
    (void)stock;
    if (currentEra_ == Era::StoneAge) {
        currentEra_ = Era::BronzeAge;
        if (currentEpoch_ < HistoricalEpoch::CopperAge) {
            currentEpoch_ = HistoricalEpoch::CopperAge;
        }
        eraElapsedTime_ = 0.0f;
        log.Add("====================================================", GOLD);
        log.Add("KAUSALER EPOCHENWECHSEL: Die Bronze- & Werkzeugzeit bricht an!", GOLD);
        log.Add("Freigeschaltet: Werkbänke, Holzhütten (Stufe 2), Bronze-Schmelze!", { 255, 230, 100, 255 });
        log.Add("====================================================", GOLD);
        return true;
    } else if (currentEra_ == Era::BronzeAge) {
        currentEra_ = Era::IronAge;
        if (currentEpoch_ < HistoricalEpoch::IronAge) {
            currentEpoch_ = HistoricalEpoch::IronAge;
        }
        eraElapsedTime_ = 0.0f;
        log.Add("====================================================", SKYBLUE);
        log.Add("KAUSALER EPOCHENWECHSEL: Die Siedlungs- & Eisenzeit ist erreicht!", SKYBLUE);
        log.Add("Freigeschaltet: Steinhäuser (Stufe 3), Wachtürme, Rennofen & Eisen!", { 160, 230, 255, 255 });
        log.Add("====================================================", SKYBLUE);
        return true;
    }
    return false;
}

bool AgeManager::CanBuild(BuildingType type) const {
    switch (type) {
        case BuildingType::TownCenter:
        case BuildingType::Hut:
        case BuildingType::Grave:
        case BuildingType::Monument:
        case BuildingType::FarmPlot:
        case BuildingType::Pantry:
        case BuildingType::Kiln:
            return true;
        case BuildingType::CraftBench:
        case BuildingType::AnimalPen:
            return currentEra_ >= Era::BronzeAge || currentEpoch_ >= HistoricalEpoch::Neolithic;
        case BuildingType::Watchtower:
            return currentEra_ >= Era::IronAge;
    }
    return true;
}

bool AgeManager::CanCraftRefined() const {
    return currentEra_ >= Era::BronzeAge || currentEpoch_ >= HistoricalEpoch::CopperAge;
}
