#ifndef AGE_MANAGER_H
#define AGE_MANAGER_H

#include "common.h"
#include <string>

class EventLog;
class BuildingManager;
class SkillTree;
struct Stockpile;

enum class Era {
    StoneAge = 0,     // Steinzeit / Urzeit
    BronzeAge = 1,    // Bronze- / Werkzeugzeit
    IronAge = 2       // Siedlungs- / Eisenzeit
};

// Langfristiges 14-Stufen-Epochenmodell der Menschheitsgeschichte
enum class HistoricalEpoch {
    HunterGatherer = 0,       // 1. Jäger und Sammler (Frühe Steinzeit)
    Neolithic = 1,            // 2. Späte Steinzeit / Neolithikum (Sesshaftigkeit & Ackerbau)
    CopperAge = 2,            // 3. Kupferzeit (Chalkolithikum / Frühe Metallurgie)
    BronzeAge = 3,            // 4. Bronzezeit (Legierungen, Arbeitsteilung & Handel)
    IronAge = 4,              // 5. Eisenzeit (Rennöfen, Stahlwaffen & Befestigungen)
    Antiquity = 5,            // 6. Antike Hochkulturen (Steinarchitektur, Monumente & Gesetze)
    MiddleAges = 6,           // 7. Mittelalter (Feudalwesen, Mühlen & Zünfte)
    Renaissance = 7,          // 8. Renaissance (Wissenschaft, Buchdruck & Fernhandel)
    Enlightenment = 8,        // 9. Aufklärung & frühe Industrialisierung (Manufakturen)
    IndustrialRevolution = 9, // 10. Industrielle Revolution (Dampfkraft & Eisenbahn)
    Electrification = 10,     // 11. Elektrifizierung (Stromnetz & moderne Städte)
    ModernEra = 11,           // 12. Moderne (Automobil, Luftfahrt & Massenproduktion)
    InformationAge = 12,      // 13. Informationszeitalter (Computer & Vernetzung)
    NearFuture = 13           // 14. Nahe Zukunft (Fusionsenergie, KI & Planetare Zivilisation)
};

const char* EraName(Era era);
Color EraColor(Era era);
const char* HistoricalEpochName(HistoricalEpoch epoch);
int HistoricalEpochNumber(HistoricalEpoch epoch);

enum class BuildingType;

class AgeManager {
public:
    AgeManager();

    void Update(const Stockpile& stock, const BuildingManager& buildings, const SkillTree& skillTree,
                int population, float dt, EventLog& log);

    // Kausale Aufstiegsprüfung (keine künstliche Stoppuhr: Wissen + Nahrungssicherheit + Siedlungsbau)
    bool CanAdvance(const Stockpile& stock, const BuildingManager& buildings, const SkillTree& skillTree,
                    int population, std::string* outRequirement = nullptr) const;
    bool AdvanceEra(Stockpile& stock, EventLog& log);

    Era CurrentEra() const { return currentEra_; }
    int EraIndex() const { return static_cast<int>(currentEra_); }
    HistoricalEpoch CurrentEpoch() const { return currentEpoch_; }
    float ResearchPoints() const { return researchPoints_; }
    float EraElapsedTime() const { return eraElapsedTime_; }
    float RequiredEraDuration(Era era) const;

    // Freischaltungen
    bool CanBuild(BuildingType type) const;
    bool CanCraftRefined() const;

private:
    void UpdateHistoricalEpoch(const Stockpile& stock, const BuildingManager& buildings,
                               const SkillTree& skillTree, int population, EventLog& log);

    Era currentEra_ = Era::StoneAge;
    HistoricalEpoch currentEpoch_ = HistoricalEpoch::HunterGatherer;
    float researchPoints_ = 0.0f;
    float eraElapsedTime_ = 0.0f;
    float autoCheckTimer_ = 0.0f;
};

#endif // AGE_MANAGER_H
