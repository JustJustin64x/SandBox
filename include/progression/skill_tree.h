#ifndef SKILL_TREE_H
#define SKILL_TREE_H

#include "common.h"
#include "age_manager.h"
#include "poi.h"

#include <string>
#include <vector>

enum class TechState {
    Locked,        // Voraussetzungen noch nicht erfüllt
    Ready,         // Voraussetzungen erfüllt, wartet auf Expedition / Erforschung
    InExpedition,  // Eine Expedition ist unterwegs oder erforscht vor Ort
    Mastered       // Vollständig gemeistert & freigeschaltet
};

struct TechNode {
    int id = 0;
    std::string key;
    std::string name;
    std::string description;
    std::string benefit;
    Era era = Era::StoneAge;
    int epochId = 1;           // AUFTRAG: Epochen-ID für Tab-System (1 = Steinzeit, 2 = Bronzezeit, 3 = Eisenzeit, 1..14)
    bool isMilestone = false;  // Zwingend für Zeitalter-Aufstieg!

    TechState state = TechState::Locked;
    std::vector<int> prereqIds;
    bool requireAnyPrereq = false;

    // Koppelung an POI & Learning-by-Doing Praxis
    bool requiresPOI = false;
    POIType targetPOIType = POIType::LimestoneCave;
    RequiredGear gear;
    float progress = 0.0f;          // 0.0 .. 100.0 (Learning by Doing / Forschungspraxis)
    float requiredPractice = 100.0f;

    // UI-Layout (Spalte, Zeile auf dem Skilltree)
    int uiCol = 0;
    int uiRow = 0;
};

class SkillTree {
public:
    SkillTree();

    void Init();
    void Update(POIManager& pois, EventLog& log);

    // AUFTRAG PHASE 5: Kausales Hard-Gating für Epochen-Fortschritt
    bool isNeolithic = false;
    bool isBronzeAge = false;
    bool isIronAge = false;

    bool IsResearched(const std::string& nameOrKey) const;
    bool CanResearch(const TechNode& node) const { return CanResearchNode(node); }
    bool CanResearch(int id) const;
    bool CanResearch(const std::string& key) const;

    void TriggerNeolithic(AgeManager* age = nullptr, EventLog* log = nullptr);
    void TriggerBronzeAge(AgeManager* age = nullptr, EventLog* log = nullptr);
    void TriggerIronAge(AgeManager* age = nullptr, EventLog* log = nullptr);

    void CheckEpochAdvancement(AgeManager* age = nullptr, EventLog* log = nullptr);

    bool IsUnlocked(const std::string& key) const;
    bool IsUnlocked(int id) const;

    // Learning-by-Doing: Praxis-Fortschritt durch reale Aktionen hinzufügen.
    // Gibt true zurück, wenn dadurch ein Heureka-Durchbruch ausgelöst wurde.
    bool AddPractice(const std::string& key, float amount, EventLog& log, const std::string& actorName = "");

    // Autonome Fokus-Wahl durch den Stamm anhand akuter Engpässe
    void AutoSelectFocus(Era currentEra, bool coldUrgent, bool toolsUrgent);

    // Meilenstein-Prüfung für Epochenwechsel
    bool AreEraMilestonesCompleted(Era era) const;
    int GetMilestonesTotal(Era era) const;
    int GetMilestonesCompleted(Era era) const;

    // Strikte Epochen-Schranke (Hard Gating): Prüft, ob eine Technologie nach Epochenregeln erforschbar ist
    bool CanResearchNode(const TechNode& node) const;

    TechNode* GetNode(int id);
    const TechNode* GetNode(int id) const;
    TechNode* GetNodeByKey(const std::string& key);
    const TechNode* GetNodeByKey(const std::string& key) const;

    const std::vector<TechNode>& AllNodes() const { return nodes_; }

    // Spieler- oder KI-Fokus / Priorisierung
    void SetFocusNode(int id) { focusNodeId_ = id; }
    int GetFocusNode() const { return focusNodeId_; }

    // Abschluss einer Technologie
    bool CompleteNode(int id, EventLog& log, const std::string& actorName = "");

    // Findet die fokussierte oder nächste erforschbare Technologie
    const TechNode* GetActiveOrNextDiscoverable(Era currentEra) const;

private:
    void RefreshNodeStates(const POIManager& pois, EventLog& log);

    std::vector<TechNode> nodes_;
    int focusNodeId_ = -1;
};

#endif // SKILL_TREE_H

