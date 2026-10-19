#include "skill_tree.h"
#include "event_log.h"
#include <algorithm>

SkillTree::SkillTree() {
    Init();
}

void SkillTree::Init() {
    nodes_.clear();
    focusNodeId_ = -1;

    // --- 1. Steinzeit --------------------------------------------------------
    {
        TechNode n;
        n.id = 1;
        n.key = "fire";
        n.name = "Feuerbeherrschung";
        n.description = "Bändigung der Flammen. Spendet lebenswichtige Wärme, schützt vor Kälte und kocht Speisen.";
        n.benefit = "Lagerfeuer spendet Wärme; Basisschutz";
        n.era = Era::StoneAge;
        n.isMilestone = false;
        n.state = TechState::Mastered; // Vom Start weg bekannt
        n.uiCol = 0; n.uiRow = 1;
        nodes_.push_back(n);
    }
    {
        TechNode n;
        n.id = 2;
        n.key = "flint";
        n.name = "Steinklingen & Keile";
        n.description = "Abschlagen scharfer Feuersteinkanten. Ermöglicht die Herstellung primitiver Steinspitzhacken.";
        n.benefit = "Schaltet Werkzeug- & Spitzhacken-Herstellung frei";
        n.era = Era::StoneAge;
        n.isMilestone = false;
        n.state = TechState::Ready;
        n.prereqIds = { 1 };
        n.uiCol = 1; n.uiRow = 1;
        nodes_.push_back(n);
    }
    {
        TechNode n;
        n.id = 3;
        n.key = "torches";
        n.name = "Fackeln & Pech";
        n.description = "Harzgetränkte Holzscheite. Erhellen tiefe Höhlen und vertreiben nachtaktive Raubtiere.";
        n.benefit = "Ermöglicht das Betreten dunkler Höhlen";
        n.era = Era::StoneAge;
        n.isMilestone = false;
        n.state = TechState::Locked;
        n.prereqIds = { 2 };
        n.uiCol = 2; n.uiRow = 0;
        nodes_.push_back(n);
    }
    {
        TechNode n;
        n.id = 4;
        n.key = "cave";
        n.name = "Höhlenkunde";
        n.description = "Erforschung des Höhlensystems. Funde uralter Zeichnungen und geologischer Erzspuren.";
        n.benefit = "Epochen-Meilenstein! Schaltet Erzsuche frei";
        n.era = Era::StoneAge;
        n.isMilestone = true;
        n.state = TechState::Locked;
        n.prereqIds = { 3 };
        n.requiresPOI = true;
        n.targetPOIType = POIType::LimestoneCave;
        n.gear = { true, true, true }; // Fackel + Hacke + Proviant
        n.uiCol = 3; n.uiRow = 0;
        nodes_.push_back(n);
    }
    {
        TechNode n;
        n.id = 5;
        n.key = "megalith";
        n.name = "Megalith-Kult";
        n.description = "Errichtung und Verehrung gewaltiger Steinblöcke. Stärkt den Stammeszusammenhalt und die Kultur.";
        n.benefit = "Epochen-Meilenstein! +20% Stammes-Morale";
        n.era = Era::StoneAge;
        n.isMilestone = true;
        n.state = TechState::Locked;
        n.prereqIds = { 2 };
        n.requiresPOI = true;
        n.targetPOIType = POIType::AncientMegalith;
        n.gear = { false, false, true }; // Proviant
        n.uiCol = 3; n.uiRow = 2;
        nodes_.push_back(n);
    }

    // --- 2. Bronzezeit -------------------------------------------------------
    {
        TechNode n;
        n.id = 6;
        n.key = "carpentry";
        n.name = "Zimmererkunst";
        n.description = "Fügen und Spalten von Holz. Erlaubt Holzbalken, feste Blockhütten (Stufe 2) und Werkbänke.";
        n.benefit = "Epochen-Meilenstein! Schaltet Werkbank & Balken frei";
        n.era = Era::BronzeAge;
        n.isMilestone = true;
        n.state = TechState::Locked;
        n.prereqIds = { 2 };
        n.uiCol = 4; n.uiRow = 1;
        nodes_.push_back(n);
    }
    {
        TechNode n;
        n.id = 7;
        n.key = "copper";
        n.name = "Kupfergewinnung";
        n.description = "Ausbeutung oberflächennaher Kupfervorkommen aus freiliegenden Gesteinsadern.";
        n.benefit = "Ermöglicht Rohkupfer-Erze für Legierungen";
        n.era = Era::BronzeAge;
        n.isMilestone = false;
        n.state = TechState::Locked;
        n.prereqIds = { 4 };
        n.requiresPOI = true;
        n.targetPOIType = POIType::CopperOutcrop;
        n.gear = { false, true, true }; // Hacke + Proviant
        n.uiCol = 5; n.uiRow = 0;
        nodes_.push_back(n);
    }
    {
        TechNode n;
        n.id = 8;
        n.key = "tin";
        n.name = "Zinnwäsche";
        n.description = "Gewinnung von Kassiterit-Zinnkörnern aus Flussbetten und sandigen Ufern.";
        n.benefit = "Ermöglicht Zinnwäsche für Bronzelegierung";
        n.era = Era::BronzeAge;
        n.isMilestone = false;
        n.state = TechState::Locked;
        n.prereqIds = { 4 };
        n.requiresPOI = true;
        n.targetPOIType = POIType::TinRiverbed;
        n.gear = { false, true, true }; // Hacke + Proviant
        n.uiCol = 5; n.uiRow = 2;
        nodes_.push_back(n);
    }
    {
        TechNode n;
        n.id = 9;
        n.key = "bronze";
        n.name = "Bronzelegierung & Guss";
        n.description = "Schmelzen von Kupfer und Zinn zu harter Bronze. Revolutioniert alle Werkzeuge und Bauten.";
        n.benefit = "Epochen-Meilenstein! +35% Arbeitsgeschwindigkeit";
        n.era = Era::BronzeAge;
        n.isMilestone = true;
        n.state = TechState::Locked;
        n.prereqIds = { 7, 8 };
        n.uiCol = 6; n.uiRow = 1;
        nodes_.push_back(n);
    }

    // --- 3. Eisenzeit --------------------------------------------------------
    {
        TechNode n;
        n.id = 10;
        n.key = "bloomery";
        n.name = "Rennofen & Eisen";
        n.description = "Hohe Schmelztemperaturen im Rennfeuer. Verhüttet Eisenerz zu formbarem Schmiedeeisen.";
        n.benefit = "Epochen-Meilenstein! Eisenwerkzeuge & Waffen";
        n.era = Era::IronAge;
        n.isMilestone = true;
        n.state = TechState::Locked;
        n.prereqIds = { 9 };
        n.requiresPOI = true;
        n.targetPOIType = POIType::IronRift;
        n.gear = { true, true, true }; // Fackel + Hacke + Proviant
        n.uiCol = 7; n.uiRow = 0;
        nodes_.push_back(n);
    }
    {
        TechNode n;
        n.id = 11;
        n.key = "masonry";
        n.name = "Steinmetzkunst";
        n.description = "Präzises Behauen von Natursteinen und Ziegelherstellung. Erlaubt feste Steinhäuser (Stufe 3).";
        n.benefit = "Epochen-Meilenstein! Steinhäuser & Ziegel";
        n.era = Era::IronAge;
        n.isMilestone = true;
        n.state = TechState::Locked;
        n.prereqIds = { 9 };
        n.uiCol = 7; n.uiRow = 2;
        nodes_.push_back(n);
    }
    {
        TechNode n;
        n.id = 12;
        n.key = "defense";
        n.name = "Wehrbau & Wachturm";
        n.description = "Befestigte Wachtürme mit Rundumsicht. Frühwarnung vor Raubtieren und fremden Überfällen.";
        n.benefit = "Schaltet Wachtürme & erweiterte Sicht frei";
        n.era = Era::IronAge;
        n.isMilestone = false;
        n.state = TechState::Locked;
        n.prereqIds = { 6, 11 };
        n.uiCol = 8; n.uiRow = 1;
        nodes_.push_back(n);
    }
}

void SkillTree::Update(POIManager& pois, EventLog& log) {
    (void)log;
    RefreshNodeStates(pois);
}

void SkillTree::RefreshNodeStates(const POIManager& pois) {
    for (auto& node : nodes_) {
        if (node.state == TechState::Mastered) continue;

        // Prüfen, ob alle Vorbedingungen gemeistert sind
        bool prereqsMet = true;
        for (int pid : node.prereqIds) {
            const auto* pnode = GetNode(pid);
            if (!pnode || pnode->state != TechState::Mastered) {
                prereqsMet = false;
                break;
            }
        }

        if (!prereqsMet) {
            node.state = TechState::Locked;
            continue;
        }

        // Wenn die Technologie an einen POI gebunden ist
        if (node.requiresPOI) {
            const auto* poi = pois.GetByType(node.targetPOIType);
            if (!poi || !poi->spawned) {
                node.state = TechState::Locked; // POI noch nicht in der Welt gespawnt
            } else if (poi->state == POIState::Explored) {
                node.state = TechState::Mastered;
            } else if (poi->state == POIState::Exploring) {
                node.state = TechState::InExpedition;
            } else {
                node.state = TechState::Ready; // Bereit für Expedition!
            }
        } else {
            // Technologie ohne POI (z.B. Flint oder Bronzelegierung)
            if (node.state != TechState::Mastered) {
                node.state = TechState::Ready;
            }
        }
    }
}

bool SkillTree::CompleteNode(int id, EventLog& log) {
    auto* node = GetNode(id);
    if (!node || node->state == TechState::Mastered) return false;

    node->state = TechState::Mastered;
    log.Add(TextFormat("FORSCHUNG GEMEISTERT: %s!", node->name.c_str()), GOLD);
    log.Add(TextFormat("Nutzen: %s", node->benefit.c_str()), { 255, 230, 120, 255 });

    if (focusNodeId_ == id) {
        focusNodeId_ = -1;
    }
    return true;
}

bool SkillTree::IsUnlocked(const std::string& key) const {
    const auto* node = GetNodeByKey(key);
    return node && node->state == TechState::Mastered;
}

bool SkillTree::IsUnlocked(int id) const {
    const auto* node = GetNode(id);
    return node && node->state == TechState::Mastered;
}

bool SkillTree::AreEraMilestonesCompleted(Era era) const {
    for (const auto& node : nodes_) {
        if (node.era == era && node.isMilestone) {
            if (node.state != TechState::Mastered) return false;
        }
    }
    return true;
}

int SkillTree::GetMilestonesTotal(Era era) const {
    int count = 0;
    for (const auto& node : nodes_) {
        if (node.era == era && node.isMilestone) count++;
    }
    return count;
}

int SkillTree::GetMilestonesCompleted(Era era) const {
    int count = 0;
    for (const auto& node : nodes_) {
        if (node.era == era && node.isMilestone && node.state == TechState::Mastered) count++;
    }
    return count;
}

TechNode* SkillTree::GetNode(int id) {
    for (auto& n : nodes_) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

const TechNode* SkillTree::GetNode(int id) const {
    for (const auto& n : nodes_) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

TechNode* SkillTree::GetNodeByKey(const std::string& key) {
    for (auto& n : nodes_) {
        if (n.key == key) return &n;
    }
    return nullptr;
}

const TechNode* SkillTree::GetNodeByKey(const std::string& key) const {
    for (const auto& n : nodes_) {
        if (n.key == key) return &n;
    }
    return nullptr;
}

const TechNode* SkillTree::GetActiveOrNextDiscoverable(Era currentEra) const {
    // 1. Wenn der Spieler ein Ziel fokussiert hat
    if (focusNodeId_ >= 0) {
        const auto* fn = GetNode(focusNodeId_);
        if (fn && (fn->state == TechState::Ready || fn->state == TechState::InExpedition)) {
            return fn;
        }
    }

    // 2. Erster erforschbarer Meilenstein der aktuellen Epoche
    for (const auto& n : nodes_) {
        if (n.era == currentEra && n.isMilestone && n.state == TechState::Ready) {
            return &n;
        }
    }

    // 3. Beliebige bereite Technologie
    for (const auto& n : nodes_) {
        if (n.era == currentEra && n.state == TechState::Ready) {
            return &n;
        }
    }
    return nullptr;
}

