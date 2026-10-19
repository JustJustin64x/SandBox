#include "skill_tree_ui.h"
#include "tribe.h"
#include "skill_tree.h"
#include "poi.h"
#include "age_manager.h"
#include "config.h"
#include "font_manager.h"

#include <cmath>
#include <algorithm>

namespace {

Rectangle GetNodeRect(int nodeId, float boxX, float boxY) {
    float nodeW = 195.0f;
    float nodeH = 76.0f;

    // Epochen-Spalten
    float c0 = boxX + 40.0f;   // Steinzeit
    float c1 = boxX + 535.0f;  // Bronzezeit
    float c2 = boxX + 1030.0f; // Eisenzeit

    switch (nodeId) {
        // --- Steinzeit & Neolithikum ---
        case 13: return { c0 + 20.0f,  boxY + 150.0f, nodeW, nodeH }; // Fellkleidung & Körbe
        case 5:  return { c0 + 240.0f, boxY + 150.0f, nodeW, nodeH }; // Megalith
        case 1:  return { c0 + 20.0f,  boxY + 265.0f, nodeW, nodeH }; // Feuer
        case 2:  return { c0 + 240.0f, boxY + 265.0f, nodeW, nodeH }; // Flint
        case 14: return { c0 + 240.0f, boxY + 380.0f, nodeW, nodeH }; // Ackerbau & Saatgut (Neolithikum)
        case 3:  return { c0 + 20.0f,  boxY + 495.0f, nodeW, nodeH }; // Fackeln
        case 4:  return { c0 + 240.0f, boxY + 495.0f, nodeW, nodeH }; // Höhlenkunde

        // --- Bronzezeit ---
        case 6:  return { c1 + 20.0f,  boxY + 160.0f, nodeW, nodeH }; // Zimmerei
        case 7:  return { c1 + 20.0f,  boxY + 330.0f, nodeW, nodeH }; // Kupfer
        case 8:  return { c1 + 20.0f,  boxY + 490.0f, nodeW, nodeH }; // Zinn
        case 9:  return { c1 + 240.0f, boxY + 410.0f, nodeW, nodeH }; // Bronzelegierung

        // --- Eisenzeit ---
        case 10: return { c2 + 20.0f,  boxY + 230.0f, nodeW, nodeH }; // Rennofen
        case 11: return { c2 + 20.0f,  boxY + 430.0f, nodeW, nodeH }; // Steinmetzkunst
        case 12: return { c2 + 240.0f, boxY + 330.0f, nodeW, nodeH }; // Wehrbau

        default: return { boxX, boxY, nodeW, nodeH };
    }
}

} // namespace

SkillTreeUI::SkillTreeUI() {}

void SkillTreeUI::Update(Tribe& tribe, Vector2 mousePos, bool mouseClicked) {
    if (!isOpen_) return;

    animTimer_ += GetFrameTime();
    hoveredNodeId_ = -1;

    float boxW = 1520.0f;
    float boxH = 840.0f;
    float boxX = ((float)SCREEN_WIDTH - boxW) / 2.0f;
    float boxY = ((float)SCREEN_HEIGHT - boxH) / 2.0f;

    // Schließen-Button (oben rechts)
    Rectangle closeBtn = { boxX + boxW - 50.0f, boxY + 18.0f, 32.0f, 32.0f };
    if (mouseClicked && CheckCollisionPointRec(mousePos, closeBtn)) {
        isOpen_ = false;
        return;
    }

    // Nodes auf Klicks und Hover prüfen
    for (const auto& node : tribe.Skills().AllNodes()) {
        Rectangle r = GetNodeRect(node.id, boxX, boxY);
        if (CheckCollisionPointRec(mousePos, r)) {
            hoveredNodeId_ = node.id;
            if (mouseClicked) {
                if (node.state == TechState::Ready || node.state == TechState::InExpedition) {
                    if (tribe.Skills().GetFocusNode() == node.id) {
                        tribe.Skills().SetFocusNode(-1);
                    } else {
                        tribe.Skills().SetFocusNode(node.id);
                    }
                }
            }
            break;
        }
    }
}

void SkillTreeUI::Draw(const Tribe& tribe, Vector2 mousePos) const {
    if (!isOpen_) return;

    float boxW = 1520.0f;
    float boxH = 840.0f;
    float boxX = ((float)SCREEN_WIDTH - boxW) / 2.0f;
    float boxY = ((float)SCREEN_HEIGHT - boxH) / 2.0f;

    // 1. Verdunkelnder Hintergrund
    DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.78f));

    // 2. Haupt-Fenster
    Rectangle mainBox = { boxX, boxY, boxW, boxH };
    DrawRectangleRec(mainBox, Color{ 20, 23, 29, 252 });
    DrawRectangleLinesEx(mainBox, 2.5f, Color{ 65, 80, 105, 255 });

    // 3. Titelzeile
    FontManager::DrawTextShadow("EVOLUTIONS- & FORSCHUNGSBAUM (KAUSALE ZIVILISATIONSENTWICKLUNG)", boxX + 36.0f, boxY + 20.0f, 26.0f, GOLD);
    FontManager::DrawTextShadow("Wissen entsteht organisch durch Alltagspraxis (Learning by Doing), Expeditionen und Heureka-Momente",
                                boxX + 36.0f, boxY + 54.0f, 15.0f, LIGHTGRAY);

    // Schließen-Button
    Rectangle closeBtn = { boxX + boxW - 50.0f, boxY + 18.0f, 32.0f, 32.0f };
    bool hovClose = CheckCollisionPointRec(mousePos, closeBtn);
    DrawRectangleRec(closeBtn, hovClose ? RED : Color{ 50, 55, 65, 255 });
    DrawRectangleLinesEx(closeBtn, 1.5f, RAYWHITE);
    FontManager::DrawTextCenteredShadow("X", closeBtn.x + closeBtn.width * 0.5f, closeBtn.y + 4.0f, 22.0f, RAYWHITE);

    // 4. Kausale Epochen-Statusleiste (14-Stufen-Modell & Meilensteine)
    Era curEra = tribe.Age().CurrentEra();
    HistoricalEpoch curEpoch = tribe.Age().CurrentEpoch();
    int doneMilestones = tribe.Skills().GetMilestonesCompleted(curEra);
    int totalMilestones = std::max(1, tribe.Skills().GetMilestonesTotal(curEra));
    float milestonePct = std::clamp((float)doneMilestones / (float)totalMilestones, 0.0f, 1.0f);
    bool canAdvance = tribe.Age().CanAdvance(tribe.Stock(), tribe.Buildings(), tribe.Skills(), 5);

    int barX = (int)boxX + 760;
    int barY = (int)boxY + 26;
    int barW = 340;
    int barH = 14;

    FontManager::DrawTextShadow(TextFormat("Epoche (%d/14): %s", HistoricalEpochNumber(curEpoch), HistoricalEpochName(curEpoch)),
                                (float)barX, (float)(barY - 20), 15.0f, GOLD);
    DrawRectangle(barX, barY, barW, barH, Fade(GRAY, 0.3f));
    DrawRectangle(barX, barY, (int)(barW * milestonePct), barH, (milestonePct >= 1.0f) ? LIME : SKYBLUE);
    DrawRectangleLines(barX, barY, barW, barH, Fade(WHITE, 0.5f));

    FontManager::DrawTextShadow(TextFormat("Meilensteine: %d/%d", doneMilestones, totalMilestones),
                                (float)(barX + barW + 18), (float)(barY - 3), 16.0f,
                                (doneMilestones >= totalMilestones) ? LIME : GOLD);

    if (canAdvance) {
        FontManager::DrawTextShadow("KAUSALER EPOCHENWECHSEL BEREIT!", (float)barX, (float)(barY + 22), 15.0f, LIME);
    } else {
        std::string req;
        tribe.Age().CanAdvance(tribe.Stock(), tribe.Buildings(), tribe.Skills(), 5, &req);
        FontManager::DrawTextShadow(req.c_str(), (float)barX, (float)(barY + 22), 14.0f, Color{ 220, 180, 120, 255 });
    }

    // 5. Trennlinien und Spalten-Header für Epochen
    float colWidth = 470.0f;
    for (int i = 0; i < 3; ++i) {
        float cx = boxX + 30.0f + (float)i * (colWidth + 20.0f);
        Era colEra = static_cast<Era>(i);

        // Epochen-Header
        Rectangle headerRect = { cx, boxY + 88.0f, colWidth, 40.0f };
        bool isCurrent = (colEra == curEra);
        Color hCol = isCurrent ? Fade(EraColor(colEra), 0.25f) : Fade(BLACK, 0.4f);
        DrawRectangleRec(headerRect, hCol);
        DrawRectangleLinesEx(headerRect, isCurrent ? 2.0f : 1.0f, isCurrent ? EraColor(colEra) : Fade(GRAY, 0.4f));

        const char* eName = (i == 0) ? "1. STEINZEIT / URZEIT" :
                            (i == 1) ? "2. BRONZE- & WERKZEUGZEIT" : "3. SIEDLUNGS- & EISENZEIT";
        FontManager::DrawTextShadow(eName, cx + 16.0f, headerRect.y + 10.0f, 18.0f, EraColor(colEra));

        if (isCurrent) {
            FontManager::DrawTextShadow("[AKTIV]", cx + colWidth - 85.0f, headerRect.y + 11.0f, 15.0f, GOLD);
        }
    }

    // 6. Verbindungslinien zwischen Technologien
    Rectangle dummyBounds[3] = {};
    DrawConnectionLines(tribe.Skills(), dummyBounds);

    // 7. Technologie-Karten zeichnen
    int focusId = tribe.Skills().GetFocusNode();
    for (const auto& node : tribe.Skills().AllNodes()) {
        Rectangle r = GetNodeRect(node.id, boxX, boxY);
        bool hovered = (node.id == hoveredNodeId_);
        bool focused = (node.id == focusId);
        DrawNodeCard(node, r, hovered, focused, animTimer_);
    }

    // 8. Tooltip für gehoverten Knoten zeichnen (im Vordergrund)
    if (hoveredNodeId_ >= 0) {
        const auto* hNode = tribe.Skills().GetNode(hoveredNodeId_);
        if (hNode) {
            DrawTooltip(*hNode, tribe.POIs(), mousePos);
        }
    }

    // Fußzeile
    FontManager::DrawTextShadow("Tipp: Klicke auf eine erforschbare Technologie, um sie als aktives Stammesziel zu priorisieren! | [K] Schließen",
                                boxX + 36.0f, boxY + boxH - 32.0f, 15.0f, LIGHTGRAY);
}

void SkillTreeUI::DrawConnectionLines(const SkillTree& skills, Rectangle eraBounds[3]) const {
    (void)eraBounds;
    float boxW = 1520.0f;
    float boxH = 840.0f;
    float boxX = ((float)SCREEN_WIDTH - boxW) / 2.0f;
    float boxY = ((float)SCREEN_HEIGHT - boxH) / 2.0f;

    for (const auto& node : skills.AllNodes()) {
        Rectangle toRect = GetNodeRect(node.id, boxX, boxY);
        Vector2 toPoint = { toRect.x, toRect.y + toRect.height * 0.5f };

        for (int pId : node.prereqIds) {
            const auto* pNode = skills.GetNode(pId);
            if (!pNode) continue;

            Rectangle fromRect = GetNodeRect(pId, boxX, boxY);
            Vector2 fromPoint = { fromRect.x + fromRect.width, fromRect.y + fromRect.height * 0.5f };

            // Linienfarbe nach Fortschritt
            Color lineCol = Fade(GRAY, 0.35f);
            float thick = 1.6f;

            if (pNode->state == TechState::Mastered && node.state == TechState::Mastered) {
                lineCol = Fade(GOLD, 0.75f);
                thick = 2.4f;
            } else if (pNode->state == TechState::Mastered && (node.state == TechState::Ready || node.state == TechState::InExpedition)) {
                lineCol = Fade(YELLOW, 0.85f);
                thick = 2.2f;
            }

            // Gebogene Bezier-Linie
            DrawLineBezier(fromPoint, toPoint, thick, lineCol);
        }
    }
}

void SkillTreeUI::DrawNodeCard(const TechNode& node, Rectangle rect, bool isHovered, bool isFocused, float time) const {
    // Hintergrundfarbe je nach Status
    Color fillCol = Color{ 28, 32, 40, 255 };
    Color borderCol = Color{ 70, 75, 90, 255 };

    switch (node.state) {
        case TechState::Locked:
            fillCol = Color{ 25, 27, 34, 255 };
            borderCol = Color{ 55, 60, 72, 255 };
            break;
        case TechState::Ready:
            fillCol = Color{ 32, 45, 60, 255 };
            borderCol = Color{ 100, 180, 240, 255 };
            break;
        case TechState::InExpedition: {
            float pulse = (std::sin(time * 5.0f) + 1.0f) * 0.5f;
            fillCol = Color{ 45, 42, 28, 255 };
            borderCol = Fade(GOLD, 0.6f + 0.4f * pulse);
            break;
        }
        case TechState::Mastered:
            fillCol = Color{ 30, 48, 38, 255 };
            borderCol = Color{ 80, 200, 120, 255 };
            break;
    }

    if (isHovered) {
        borderCol = RAYWHITE;
        fillCol = Color{ (unsigned char)std::min(255, fillCol.r + 20),
                         (unsigned char)std::min(255, fillCol.g + 20),
                         (unsigned char)std::min(255, fillCol.b + 20), 255 };
    }

    // Karte zeichnen
    DrawRectangleRec(rect, fillCol);
    DrawRectangleLinesEx(rect, isFocused ? 3.0f : 1.5f, isFocused ? GOLD : borderCol);

    // Name
    Color nameCol = (node.state == TechState::Locked) ? GRAY : RAYWHITE;
    FontManager::DrawTextShadow(node.name.c_str(), rect.x + 10.0f, rect.y + 8.0f, 15.0f, nameCol);

    // Status-Badge + Praxis-Prozent
    const char* statusTxt = (node.state == TechState::Locked) ? "Gesperrt" :
                            (node.state == TechState::Ready) ? TextFormat("Praxis: %.0f%%", node.progress) :
                            (node.state == TechState::InExpedition) ? "Expedition..." : "Gemeistert";
    Color statusCol = (node.state == TechState::Locked) ? DARKGRAY :
                      (node.state == TechState::Ready) ? SKYBLUE :
                      (node.state == TechState::InExpedition) ? GOLD : LIME;

    FontManager::DrawTextShadow(statusTxt, rect.x + 10.0f, rect.y + 28.0f, 13.0f, statusCol);

    // Kleiner Fortschrittsbalken für Learning-by-Doing
    if (node.state != TechState::Mastered && node.progress > 0.5f) {
        float pct = std::clamp(node.progress / std::max(1.0f, node.requiredPractice), 0.0f, 1.0f);
        int pBarX = (int)rect.x + 110;
        int pBarY = (int)rect.y + 32;
        int pBarW = (int)rect.width - 120;
        DrawRectangle(pBarX, pBarY, pBarW, 6, Fade(BLACK, 0.5f));
        DrawRectangle(pBarX, pBarY, (int)(pBarW * pct), 6, (node.state == TechState::Ready) ? SKYBLUE : GOLD);
    }

    // Meilenstein-Kennzeichnung
    if (node.isMilestone) {
        FontManager::DrawTextShadow("* MEILENSTEIN", rect.x + 10.0f, rect.y + 50.0f, 12.0f, GOLD);
    } else {
        FontManager::DrawTextShadow(EraName(node.era), rect.x + 10.0f, rect.y + 50.0f, 12.0f, Fade(GRAY, 0.8f));
    }

    // Fokus-Ring
    if (isFocused) {
        DrawRectangleLinesEx(rect, 2.5f, GOLD);
        FontManager::DrawTextShadow("FOKUS", rect.x + rect.width - 55.0f, rect.y + 8.0f, 12.0f, GOLD);
    }
}

void SkillTreeUI::DrawTooltip(const TechNode& node, const POIManager& pois, Vector2 mousePos) const {
    float tipW = 380.0f;
    float tipH = 225.0f;
    float tipX = mousePos.x + 16.0f;
    float tipY = mousePos.y + 16.0f;

    // Sicherstellen, dass Tooltip im Bild bleibt
    if (tipX + tipW > SCREEN_WIDTH - 20) tipX = mousePos.x - tipW - 16.0f;
    if (tipY + tipH > SCREEN_HEIGHT - 20) tipY = mousePos.y - tipH - 16.0f;

    Rectangle r = { tipX, tipY, tipW, tipH };
    DrawRectangleRec(r, Color{ 16, 18, 24, 252 });
    DrawRectangleLinesEx(r, 1.8f, GOLD);

    FontManager::DrawTextShadow(node.name.c_str(), tipX + 14.0f, tipY + 10.0f, 19.0f, RAYWHITE);
    FontManager::DrawTextShadow(TextFormat("Epoche: %s", EraName(node.era)), tipX + 14.0f, tipY + 34.0f, 14.0f, EraColor(node.era));

    // Beschreibung
    FontManager::DrawTextShadow(node.description.c_str(), tipX + 14.0f, tipY + 54.0f, 13.0f, LIGHTGRAY);

    // Nutzen
    DrawLine((int)tipX + 10, (int)tipY + 108, (int)tipX + (int)tipW - 10, (int)tipY + 108, Fade(GRAY, 0.4f));
    FontManager::DrawTextShadow(TextFormat("Effekt: %s", node.benefit.c_str()), tipX + 14.0f, tipY + 116.0f, 14.0f, LIME);

    // Koppelung an Ort & Ausrüstung
    if (node.requiresPOI) {
        const auto* poi = pois.GetByType(node.targetPOIType);
        std::string poiName = poi ? poi->name : "Unbekannt";
        FontManager::DrawTextShadow(TextFormat("Erkundungsort: %s", poiName.c_str()), tipX + 14.0f, tipY + 142.0f, 13.0f, SKYBLUE);

        std::string gearStr = "Ausrüstung: ";
        if (node.gear.torch) gearStr += "[Fackel] ";
        if (node.gear.pickaxe) gearStr += "[Spitzhacke] ";
        if (node.gear.provisions) gearStr += "[Proviant] ";
        FontManager::DrawTextShadow(gearStr.c_str(), tipX + 14.0f, tipY + 164.0f, 13.0f, ORANGE);
    } else {
        FontManager::DrawTextShadow("Erforschung: Durch Stammeswerkbank & Alltagspraxis", tipX + 14.0f, tipY + 142.0f, 13.0f, LIGHTGRAY);
    }

    if (node.state == TechState::Ready) {
        FontManager::DrawTextShadow("Klick: Als Stammes-Fokus priorisieren", tipX + 14.0f, tipY + 192.0f, 13.0f, GOLD);
    }
}

