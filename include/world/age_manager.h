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

const char* EraName(Era era);
Color EraColor(Era era);

enum class BuildingType;

class AgeManager {
public:
    AgeManager();

    void Update(const Stockpile& stock, const BuildingManager& buildings, const SkillTree& skillTree,
                int population, float dt, EventLog& log);

    // Aufstiegsprüfung: Time-Gating + Meilenstein-Entdeckungen + Siedlungsbasis
    bool CanAdvance(const Stockpile& stock, const BuildingManager& buildings, const SkillTree& skillTree,
                    int population, std::string* outRequirement = nullptr) const;
    bool AdvanceEra(Stockpile& stock, EventLog& log);

    Era CurrentEra() const { return currentEra_; }
    int EraIndex() const { return static_cast<int>(currentEra_); }
    float ResearchPoints() const { return researchPoints_; }
    float EraElapsedTime() const { return eraElapsedTime_; }
    float RequiredEraDuration(Era era) const;

    // Freischaltungen
    bool CanBuild(BuildingType type) const;
    bool CanCraftRefined() const;

private:
    Era currentEra_ = Era::StoneAge;
    float researchPoints_ = 0.0f;
    float eraElapsedTime_ = 0.0f;
    float autoCheckTimer_ = 0.0f;
};

#endif // AGE_MANAGER_H
