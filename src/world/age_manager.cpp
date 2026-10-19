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

AgeManager::AgeManager() {}

float AgeManager::RequiredEraDuration(Era era) const {
    switch (era) {
        case Era::StoneAge:  return cfg::kStoneAgeDuration;
        case Era::BronzeAge: return cfg::kBronzeAgeDuration;
        case Era::IronAge:   return cfg::kIronAgeDuration;
    }
    return 9999.0f;
}

void AgeManager::Update(const Stockpile& stock, const BuildingManager& buildings, const SkillTree& skillTree,
                        int population, float dt, EventLog& log) {
    // 1. Epochen-Laufzeit erhöhen
    eraElapsedTime_ += dt;

    // 2. Forschungspunkte durch Kultur/Gebäude
    int tcTier = buildings.GetMaxTier(BuildingType::TownCenter);
    int craftBenches = buildings.GetBuildingCount(BuildingType::CraftBench, true);
    float gainRate = 0.05f + (float)tcTier * 0.1f + (float)craftBenches * 0.15f;
    researchPoints_ += gainRate * dt;

    // 3. Regelmäßige Status-Prüfung
    autoCheckTimer_ += dt;
    if (autoCheckTimer_ >= 12.0f) {
        autoCheckTimer_ = 0.0f;
        std::string req;
        if (CanAdvance(stock, buildings, skillTree, population, &req)) {
            log.Add(TextFormat("Aufstieg in die %s möglich! (%s)",
                               EraName(static_cast<Era>(EraIndex() + 1)), req.c_str()),
                    GOLD);
        }
    }
}

bool AgeManager::CanAdvance(const Stockpile& stock, const BuildingManager& buildings, const SkillTree& skillTree,
                            int population, std::string* outRequirement) const {
    (void)stock;
    if (currentEra_ == Era::StoneAge) {
        // Kriterien für Bronzezeit:
        // 1. Mindest-Epochenzeit (Time-Gating)
        // 2. Alle Steinzeit-Meilensteine erforscht (Höhlenkunde & Megalith)
        // 3. Stammesplatz vorhanden
        // 4. Mindestens 3 lebende Bürger
        bool timeMet = (eraElapsedTime_ >= RequiredEraDuration(Era::StoneAge));
        bool milestonesMet = skillTree.AreEraMilestonesCompleted(Era::StoneAge);
        bool hasTC = buildings.GetBuildingCount(BuildingType::TownCenter, true) > 0;
        bool hasPop = population >= 3;

        int doneMilestones = skillTree.GetMilestonesCompleted(Era::StoneAge);
        int totalMilestones = skillTree.GetMilestonesTotal(Era::StoneAge);

        if (outRequirement) {
            *outRequirement = TextFormat("Zeit: %.0f/%.0fs | Meilensteine: %d/%d (%s) | Bürger: %d/3",
                                         eraElapsedTime_, RequiredEraDuration(Era::StoneAge),
                                         doneMilestones, totalMilestones,
                                         milestonesMet ? "Bereit" : "Höhle/Megalith fehlt",
                                         population);
        }
        return timeMet && milestonesMet && hasTC && hasPop;
    }
    else if (currentEra_ == Era::BronzeAge) {
        // Kriterien für Eisenzeit:
        // 1. Mindest-Epochenzeit
        // 2. Alle Bronzezeit-Meilensteine erforscht (Bronzelegierung & Zimmererkunst)
        // 3. Langhaus (Town Center Stufe 2) & Werkbank
        // 4. Mindestens 4 Einwohner
        bool timeMet = (eraElapsedTime_ >= RequiredEraDuration(Era::BronzeAge));
        bool milestonesMet = skillTree.AreEraMilestonesCompleted(Era::BronzeAge);
        bool tcTier2 = buildings.GetMaxTier(BuildingType::TownCenter) >= 2;
        bool hasBench = buildings.GetBuildingCount(BuildingType::CraftBench, true) > 0;
        bool hasPop = population >= 4;

        int doneMilestones = skillTree.GetMilestonesCompleted(Era::BronzeAge);
        int totalMilestones = skillTree.GetMilestonesTotal(Era::BronzeAge);

        if (outRequirement) {
            *outRequirement = TextFormat("Zeit: %.0f/%.0fs | Meilensteine: %d/%d (%s) | Langhaus & Werkbank: %s",
                                         eraElapsedTime_, RequiredEraDuration(Era::BronzeAge),
                                         doneMilestones, totalMilestones,
                                         milestonesMet ? "Bereit" : "Bronze/Zimmerei fehlt",
                                         (tcTier2 && hasBench) ? "Bereit" : "Fehlt");
        }
        return timeMet && milestonesMet && tcTier2 && hasBench && hasPop;
    }

    if (outRequirement) *outRequirement = "Höchstes Zeitalter erreicht.";
    return false;
}

bool AgeManager::AdvanceEra(Stockpile& stock, EventLog& log) {
    (void)stock;
    if (currentEra_ == Era::StoneAge) {
        currentEra_ = Era::BronzeAge;
        eraElapsedTime_ = 0.0f; // Reset Epochenzeit für die neue Epoche!
        log.Add("====================================================", GOLD);
        log.Add("HISTORISCHER EPOCHENWECHSEL: Die Bronze- & Werkzeugzeit bricht an!", GOLD);
        log.Add("Freigeschaltet: Werkbänke, Holzhütten (Stufe 2), Bronze-Schmelze!", { 255, 230, 100, 255 });
        log.Add("====================================================", GOLD);
        return true;
    } else if (currentEra_ == Era::BronzeAge) {
        currentEra_ = Era::IronAge;
        eraElapsedTime_ = 0.0f;
        log.Add("====================================================", SKYBLUE);
        log.Add("HISTORISCHER EPOCHENWECHSEL: Die Siedlungs- & Eisenzeit ist erreicht!", SKYBLUE);
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
            return true;
        case BuildingType::CraftBench:
        case BuildingType::Pantry:
            return currentEra_ >= Era::BronzeAge;
        case BuildingType::Watchtower:
            return currentEra_ >= Era::IronAge;
    }
    return true;
}

bool AgeManager::CanCraftRefined() const {
    return currentEra_ >= Era::BronzeAge;
}
