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
        n.progress = 100.0f;
        n.uiCol = 0; n.uiRow = 1;
        nodes_.push_back(n);
    }
    {
        TechNode n;
        n.id = 2;
        n.key = "flint";
        n.name = "Steinklingen & Keile";
        n.description = "Abschlagen scharfer Feuersteinkanten. Ermöglicht Faustkeile, Speere und Steinspitzhacken.";
        n.benefit = "Schaltet Faustkeil, Speer & Spitzhacken-Herstellung frei";
        n.era = Era::StoneAge;
        n.isMilestone = false;
        n.state = TechState::Ready;
        n.prereqIds = { 1 };
        n.uiCol = 1; n.uiRow = 1;
        nodes_.push_back(n);
    }
    {
        TechNode n;
        n.id = 13;
        n.key = "tailoring";
        n.name = "Fellkleidung & Körbe";
        n.description = "Gerben von Tierhäuten mit Knochennadeln und Flechten von Weidenkörben.";
        n.benefit = "Schaltet Pelzkleidung (+Kälteschutz) & Tragekörbe (2x Traglast) frei";
        n.era = Era::StoneAge;
        n.isMilestone = false;
        n.state = TechState::Locked;
        n.prereqIds = { 2 };
        n.uiCol = 0; n.uiRow = 0;
        nodes_.push_back(n);
    }
    {
        TechNode n;
        n.id = 3;
        n.key = "torches";
        n.name = "Fackeln & Pech";
        n.description = "Harzgetränkte Holzscheite. Erhellen tiefe Höhlen und vertreiben nachtaktive Raubtiere.";
        n.benefit = "Ermöglicht das Betreten dunkler Höhlen & Kompositbögen";
        n.era = Era::StoneAge;
        n.isMilestone = false;
        n.state = TechState::Locked;
        n.prereqIds = { 2 };
        n.uiCol = 2; n.uiRow = 0;
        nodes_.push_back(n);
    }
    {
        TechNode n;
        n.id = 14;
        n.key = "agriculture";
        n.name = "Ackerbau & Saatgut";
        n.description = "Beobachtung wilder Pflanzen und Samen beim Sammeln. Ermöglicht gezielte Aussaat und Domestizierung.";
        n.benefit = "Neolithikum-Meilenstein! Schaltet Ackerland, Viehpferch & Vorratskammer frei";
        n.era = Era::StoneAge;
        n.isMilestone = true;
        n.state = TechState::Locked;
        n.prereqIds = { 2, 3 }; // Kausale Progression: Steinklingen & Feuer/Fackeln nötig
        n.uiCol = 2; n.uiRow = 2;
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
        n.prereqIds = { 2, 3 }; // Fackel + Steinaxt/Hacke zur Erkundung
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
        n.prereqIds = { 2, 3 };
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
        n.description = "Fügen und Spalten von Holz. Erlaubt Holzbalken, geschäftete Äxte, feste Blockhütten (Stufe 2) und Werkbänke.";
        n.benefit = "Epochen-Meilenstein! Schaltet Werkbank, Steinaxt & Balken frei";
        n.era = Era::BronzeAge;
        n.isMilestone = true;
        n.state = TechState::Locked;
        n.prereqIds = { 2, 3 };
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
        n.description = "Schmelzen von Kupfer und Zinn zu harter Bronze. Revolutioniert alle Werkzeuge, Tuniken und Bauten.";
        n.benefit = "Epochen-Meilenstein! Bronzewerkzeuge & +35% Arbeitstempo";
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
        n.benefit = "Epochen-Meilenstein! Eisenwerkzeuge, Waffen & Rüstung";
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
        n.description = "Präzises Behauen von Natursteinen und Straßenpflaster. Erlaubt feste Steinhäuser (Stufe 3).";
        n.benefit = "Epochen-Meilenstein! Steinhäuser & Gepflasterte Straßen";
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
    RefreshNodeStates(pois, log);
}

void SkillTree::RefreshNodeStates(const POIManager& pois, EventLog& log) {
    for (auto& node : nodes_) {
        if (node.state == TechState::Mastered) {
            node.progress = node.requiredPractice;
            continue;
        }

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
                node.progress = node.requiredPractice;
                CompleteNode(node.id, log);
            } else if (poi->state == POIState::Exploring) {
                node.state = TechState::InExpedition;
                node.progress = std::max(node.progress, poi->explorationProgress * 100.0f);
            } else {
                node.state = TechState::Ready; // Bereit für Expedition!
            }
        } else {
            // Technologie ohne POI (Learning by Doing im Alltag & an der Werkbank)
            if (node.state != TechState::Mastered) {
                node.state = TechState::Ready;
                if (node.progress >= node.requiredPractice) {
                    CompleteNode(node.id, log);
                }
            }
        }
    }
}

bool SkillTree::AddPractice(const std::string& key, float amount, EventLog& log, const std::string& actorName) {
    if (amount <= 0.0f) return false;
    auto* node = GetNodeByKey(key);
    if (!node || node->state == TechState::Mastered) return false;

    // Wenn der Stamm diese Technologie gerade aktiv fokussiert, lernt er schneller (+35%)
    float mult = (focusNodeId_ == node->id) ? 1.35f : 1.0f;
    float delta = amount * mult;

    if (node->state == TechState::Locked) {
        // Frühe Intuition durch Beobachtung im Alltag (bis max. 30% vorab ansammelbar)
        node->progress = std::min(30.0f, node->progress + delta * 0.45f);
        return false;
    }

    if (node->requiresPOI) {
        // Bei POI-Knoten steigert Alltagspraxis die Expeditionsvorbereitung bis 85%
        node->progress = std::min(85.0f, node->progress + delta * 0.5f);
        return false;
    }

    // Regulärer Learning-by-Doing Knoten im Status Ready
    node->progress = std::min(node->requiredPractice, node->progress + delta);
    if (node->progress >= node->requiredPractice) {
        return CompleteNode(node->id, log, actorName);
    }
    return false;
}

void SkillTree::AutoSelectFocus(Era currentEra, bool coldUrgent, bool toolsUrgent) {
    // Falls bereits ein gültiger Ready/InExpedition Fokus gesetzt ist, behalten
    if (focusNodeId_ >= 0) {
        const auto* cur = GetNode(focusNodeId_);
        if (cur && (cur->state == TechState::Ready || cur->state == TechState::InExpedition)) {
            return;
        }
        focusNodeId_ = -1;
    }

    // 1. Akuter Werkzeugbedarf -> Steinklingen ("flint") oder Zimmererkunst ("carpentry")
    if (toolsUrgent) {
        if (const auto* n = GetNodeByKey("flint"); n && n->state == TechState::Ready) {
            focusNodeId_ = n->id;
            return;
        }
        if (const auto* n = GetNodeByKey("carpentry"); n && n->state == TechState::Ready) {
            focusNodeId_ = n->id;
            return;
        }
    }

    // 2. Akute Kälte -> Fellkleidung ("tailoring") oder Fackeln ("torches")
    if (coldUrgent) {
        if (const auto* n = GetNodeByKey("tailoring"); n && n->state == TechState::Ready) {
            focusNodeId_ = n->id;
            return;
        }
        if (const auto* n = GetNodeByKey("torches"); n && n->state == TechState::Ready) {
            focusNodeId_ = n->id;
            return;
        }
    }

    // 3. Nicht-POI Basistechnologien der aktuellen oder früheren Epoche bevorzugen (damit Meilensteine frei werden)
    for (const auto& n : nodes_) {
        if (!n.requiresPOI && n.state == TechState::Ready && static_cast<int>(n.era) <= static_cast<int>(currentEra)) {
            focusNodeId_ = n.id;
            return;
        }
    }

    // 4. Erster bereiter Meilenstein der aktuellen Epoche
    for (const auto& n : nodes_) {
        if (n.era == currentEra && n.isMilestone && n.state == TechState::Ready) {
            focusNodeId_ = n.id;
            return;
        }
    }

    // 5. Beliebige bereite Technologie
    for (const auto& n : nodes_) {
        if (n.state == TechState::Ready) {
            focusNodeId_ = n.id;
            return;
        }
    }
}

bool SkillTree::CompleteNode(int id, EventLog& log, const std::string& actorName) {
    auto* node = GetNode(id);
    if (!node || node->state == TechState::Mastered) return false;

    node->state = TechState::Mastered;
    node->progress = node->requiredPractice;
    if (!actorName.empty()) {
        log.Add(TextFormat("HEUREKA! %s meistert: %s!", actorName.c_str(), node->name.c_str()), GOLD);
    } else {
        log.Add(TextFormat("FORSCHUNG GEMEISTERT: %s!", node->name.c_str()), GOLD);
    }
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

