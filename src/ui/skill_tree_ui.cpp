#include "skill_tree_ui.h"
#include "tribe.h"
#include "skill_tree.h"
#include "poi.h"
#include "age_manager.h"
#include "config.h"

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
        // --- Steinzeit ---
        case 1:  return { c0 + 20.0f,  boxY + 300.0f, nodeW, nodeH }; // Feuer
        case 2:  return { c0 + 240.0f, boxY + 300.0f, nodeW, nodeH }; // Flint
        case 3:  return { c0 + 20.0f,  boxY + 460.0f, nodeW, nodeH }; // Fackeln
        case 4:  return { c0 + 240.0f, boxY + 460.0f, nodeW, nodeH }; // Höhlenkunde
        case 5:  return { c0 + 240.0f, boxY + 160.0f, nodeW, nodeH }; // Megalith

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
    DrawText("EVOLUTIONS- & FORSCHUNGSBAUM (SKILLTREE)", (int)boxX + 36, (int)boxY + 22, 26, GOLD);
    DrawText("Entdecke die Geheimnisse der Erde durch gezielte Expeditionen und historisches Wissen",
             (int)boxX + 36, (int)boxY + 54, 14, LIGHTGRAY);

    // Schließen-Button
    Rectangle closeBtn = { boxX + boxW - 50.0f, boxY + 18.0f, 32.0f, 32.0f };
    bool hovClose = CheckCollisionPointRec(mousePos, closeBtn);
    DrawRectangleRec(closeBtn, hovClose ? RED : Color{ 50, 55, 65, 255 });
    DrawRectangleLinesEx(closeBtn, 1.5f, RAYWHITE);
    DrawText("X", (int)closeBtn.x + 10, (int)closeBtn.y + 6, 20, RAYWHITE);

    // 4. Epochen-Statusleiste (Time-Gating & Meilensteine)
    Era curEra = tribe.Age().CurrentEra();
    float eraElapsed = tribe.Age().EraElapsedTime();
    float reqTime = tribe.Age().RequiredEraDuration(curEra);
    float eraPct = std::clamp(eraElapsed / reqTime, 0.0f, 1.0f);

    int doneMilestones = tribe.Skills().GetMilestonesCompleted(curEra);
    int totalMilestones = tribe.Skills().GetMilestonesTotal(curEra);
    bool canAdvance = tribe.Age().CanAdvance(tribe.Stock(), tribe.Buildings(), tribe.Skills(), 5);

    // Fortschrittsbalken oben
    int barX = (int)boxX + 680;
    int barY = (int)boxY + 26;
    int barW = 340;
    int barH = 14;

    DrawText(TextFormat("Epochenzeit: %.0f / %.0fs", eraElapsed, reqTime), barX, barY - 18, 13, RAYWHITE);
    DrawRectangle(barX, barY, barW, barH, Fade(GRAY, 0.3f));
    DrawRectangle(barX, barY, (int)(barW * eraPct), barH, (eraPct >= 1.0f) ? LIME : SKYBLUE);
    DrawRectangleLines(barX, barY, barW, barH, Fade(WHITE, 0.5f));

    DrawText(TextFormat("Meilensteine: %d/%d", doneMilestones, totalMilestones), barX + barW + 18, barY - 2, 14,
             (doneMilestones >= totalMilestones) ? LIME : GOLD);

    if (canAdvance) {
        DrawText("EPOCHENWECHSEL BEREIT!", barX, barY + 22, 14, LIME);
    } else {
        std::string req;
        tribe.Age().CanAdvance(tribe.Stock(), tribe.Buildings(), tribe.Skills(), 5, &req);
        DrawText(req.c_str(), barX, barY + 22, 12, Color{ 220, 180, 120, 255 });
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
        DrawText(eName, (int)cx + 16, (int)headerRect.y + 11, 16, EraColor(colEra));

        if (isCurrent) {
            DrawText("[AKTIV]", (int)cx + (int)colWidth - 75, (int)headerRect.y + 12, 13, GOLD);
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
    DrawText("Tipp: Klicke auf eine erforschbare Technologie, um sie als aktives Expeditionsziel zu priorisieren! | [K] Schließen",
             (int)boxX + 36, (int)boxY + boxH - 32, 13, LIGHTGRAY);
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
    DrawText(node.name.c_str(), (int)rect.x + 10, (int)rect.y + 10, 14, nameCol);

    // Status-Badge
    const char* statusTxt = (node.state == TechState::Locked) ? "Gesperrt" :
                            (node.state == TechState::Ready) ? "Bereit" :
                            (node.state == TechState::InExpedition) ? "Expedition..." : "Gemeistert";
    Color statusCol = (node.state == TechState::Locked) ? DARKGRAY :
                      (node.state == TechState::Ready) ? SKYBLUE :
                      (node.state == TechState::InExpedition) ? GOLD : LIME;

    DrawText(statusTxt, (int)rect.x + 10, (int)rect.y + 32, 12, statusCol);

    // Meilenstein-Kennzeichnung
    if (node.isMilestone) {
        DrawText("* MEILENSTEIN", (int)rect.x + 10, (int)rect.y + 52, 11, GOLD);
    } else {
        DrawText(EraName(node.era), (int)rect.x + 10, (int)rect.y + 52, 11, Fade(GRAY, 0.7f));
    }

    // Fokus-Ring
    if (isFocused) {
        DrawRectangleLinesEx(rect, 2.5f, GOLD);
        DrawText("FOKUS", (int)rect.x + (int)rect.width - 50, (int)rect.y + 10, 11, GOLD);
    }
}

void SkillTreeUI::DrawTooltip(const TechNode& node, const POIManager& pois, Vector2 mousePos) const {
    float tipW = 340.0f;
    float tipH = 210.0f;
    float tipX = mousePos.x + 16.0f;
    float tipY = mousePos.y + 16.0f;

    // Sicherstellen, dass Tooltip im Bild bleibt
    if (tipX + tipW > SCREEN_WIDTH - 20) tipX = mousePos.x - tipW - 16.0f;
    if (tipY + tipH > SCREEN_HEIGHT - 20) tipY = mousePos.y - tipH - 16.0f;

    Rectangle r = { tipX, tipY, tipW, tipH };
    DrawRectangleRec(r, Color{ 16, 18, 24, 250 });
    DrawRectangleLinesEx(r, 1.8f, GOLD);

    DrawText(node.name.c_str(), (int)tipX + 14, (int)tipY + 12, 17, RAYWHITE);
    DrawText(TextFormat("Epoche: %s", EraName(node.era)), (int)tipX + 14, (int)tipY + 34, 12, EraColor(node.era));

    // Beschreibung
    DrawText(node.description.c_str(), (int)tipX + 14, (int)tipY + 54, 11, LIGHTGRAY);

    // Nutzen
    DrawLine((int)tipX + 10, (int)tipY + 104, (int)tipX + (int)tipW - 10, (int)tipY + 104, Fade(GRAY, 0.4f));
    DrawText(TextFormat("Effekt: %s", node.benefit.c_str()), (int)tipX + 14, (int)tipY + 112, 12, LIME);

    // Koppelung an Ort & Ausrüstung
    if (node.requiresPOI) {
        const auto* poi = pois.GetByType(node.targetPOIType);
        std::string poiName = poi ? poi->name : "Unbekannt";
        DrawText(TextFormat("Erkundungsort: %s", poiName.c_str()), (int)tipX + 14, (int)tipY + 138, 12, SKYBLUE);

        std::string gearStr = "Ausrüstung: ";
        if (node.gear.torch) gearStr += "[Fackel] ";
        if (node.gear.pickaxe) gearStr += "[Spitzhacke] ";
        if (node.gear.provisions) gearStr += "[Proviant] ";
        DrawText(gearStr.c_str(), (int)tipX + 14, (int)tipY + 158, 11, ORANGE);
    } else {
        DrawText("Erforschung: Durch Stammeswerkbank & Erfahrung", (int)tipX + 14, (int)tipY + 138, 11, LIGHTGRAY);
    }

    if (node.state == TechState::Ready) {
        DrawText("Klick: Als Expeditions-Fokus setzen", (int)tipX + 14, (int)tipY + 184, 11, GOLD);
    }
}

