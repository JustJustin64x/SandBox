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

struct EpochTabInfo {
    int id;
    const char* name;
    Era era;
};

static const EpochTabInfo kEpochTabs[14] = {
    { 1, "1. STEINZEIT", Era::StoneAge },
    { 2, "2. BRONZEZEIT", Era::BronzeAge },
    { 3, "3. EISENZEIT", Era::IronAge },
    { 4, "4. ANTIKE HOCHKULTUR", Era::IronAge },
    { 5, "5. MITTELALTER", Era::IronAge },
    { 6, "6. RENAISSANCE", Era::IronAge },
    { 7, "7. AUFKLÄRUNG", Era::IronAge },
    { 8, "8. INDUSTRIELLE REV.", Era::IronAge },
    { 9, "9. ELEKTRIFIZIERUNG", Era::IronAge },
    { 10, "10. MODERNE", Era::IronAge },
    { 11, "11. INFORMATIONSZEIT", Era::IronAge },
    { 12, "12. NAHE ZUKUNFT", Era::IronAge },
    { 13, "13. PLANETARE ÄRA", Era::IronAge },
    { 14, "14. ZUKUNFTS-FUSION", Era::IronAge }
};
static constexpr int kTotalTabs = 14;
static constexpr int kVisibleTabs = 6;
static constexpr int kMaxTabScroll = kTotalTabs - kVisibleTabs; // 8

Rectangle GetNodeRect(int nodeId, float boxX, float boxY) {
    float canvasX = boxX;
    float canvasY = boxY + 100.0f; // Exakt unterhalb von Box 1 (60px) und Box 2 (40px)
    float nodeW = 215.0f;
    float nodeH = 82.0f;

    switch (nodeId) {
        // =====================================================================
        // TAB 1: STEINZEIT & NEOLITHIKUM (epochId == 1)
        // =====================================================================
        // Spalte 0 (x + 110): Urzeit-Wurzeln (Root)
        case 2:  return { canvasX + 110.0f, canvasY + 180.0f, nodeW, nodeH }; // Steinklingen & Keile
        case 1:  return { canvasX + 110.0f, canvasY + 400.0f, nodeW, nodeH }; // Feuerbeherrschung

        // Spalte 1 (x + 450): Alltagspraxis & Domestizierung (Tier 1)
        case 13: return { canvasX + 450.0f, canvasY + 130.0f, nodeW, nodeH }; // Fellkleidung & Körbe
        case 3:  return { canvasX + 450.0f, canvasY + 295.0f, nodeW, nodeH }; // Fackeln & Pech
        case 15: return { canvasX + 450.0f, canvasY + 460.0f, nodeW, nodeH }; // Tierzucht & Haltung

        // Spalte 2 (x + 790): Neolithische Revolution (Tier 2)
        case 14: return { canvasX + 790.0f, canvasY + 460.0f, nodeW, nodeH }; // Ackerbau & Saatgut

        // Spalte 3 (x + 1130): Höhlen & Kult-Meilensteine (Tier 3)
        case 4:  return { canvasX + 1130.0f, canvasY + 160.0f, nodeW, nodeH }; // Höhlenkunde
        case 5:  return { canvasX + 1130.0f, canvasY + 400.0f, nodeW, nodeH }; // Megalith-Kult

        // =====================================================================
        // TAB 2: BRONZEZEIT (epochId == 2)
        // =====================================================================
        // Spalte 0 (x + 260): Frühe Metallurgie & Holzbau
        case 7:  return { canvasX + 260.0f, canvasY + 160.0f, nodeW, nodeH }; // Kupfergewinnung
        case 6:  return { canvasX + 260.0f, canvasY + 310.0f, nodeW, nodeH }; // Zimmererkunst
        case 8:  return { canvasX + 260.0f, canvasY + 460.0f, nodeW, nodeH }; // Zinnwäsche

        // Spalte 1 (x + 800): Bronze-Schmelzmeister (Meilenstein)
        case 9:  return { canvasX + 800.0f, canvasY + 310.0f, nodeW, nodeH }; // Bronzelegierung & Guss

        // =====================================================================
        // TAB 3: EISENZEIT (epochId == 3)
        // =====================================================================
        // Spalte 0 (x + 280): Rennofen & Steinmetz
        case 10: return { canvasX + 280.0f, canvasY + 190.0f, nodeW, nodeH }; // Rennofen & Eisen
        case 11: return { canvasX + 280.0f, canvasY + 430.0f, nodeW, nodeH }; // Steinmetzkunst

        // Spalte 1 (x + 820): Befestigung & Wehrbau
        case 12: return { canvasX + 820.0f, canvasY + 310.0f, nodeW, nodeH }; // Wehrbau & Wachturm

        default: return { canvasX + 100.0f, canvasY + 100.0f, nodeW, nodeH };
    }
}

} // namespace

SkillTreeUI::SkillTreeUI() {}

void SkillTreeUI::Update(Tribe& tribe, Vector2 mousePos, bool mouseClicked) {
    if (!isOpen_) return;

    animTimer_ += GetFrameTime();
    hoveredNodeId_ = -1;

    float boxW = 1560.0f;
    float boxH = 860.0f;
    float boxX = ((float)SCREEN_WIDTH - boxW) * 0.5f;
    float boxY = ((float)SCREEN_HEIGHT - boxH) * 0.5f;

    // 1. Schließen-Button (Box 1 oben rechts)
    Rectangle closeBtn = { boxX + boxW - 46.0f, boxY + 14.0f, 32.0f, 32.0f };
    if (mouseClicked && CheckCollisionPointRec(mousePos, closeBtn)) {
        isOpen_ = false;
        return;
    }

    // 2. Tab-Bar Interaktion (Box 2)
    Rectangle tabBarRect = { boxX, boxY + 60.0f, boxW, 40.0f };

    // Mausrad-Scrolling über der Tab-Bar
    if (CheckCollisionPointRec(mousePos, tabBarRect)) {
        float wheel = GetMouseWheelMove();
        if (wheel > 0.0f && tabScrollOffset_ > 0) {
            tabScrollOffset_--;
        } else if (wheel < 0.0f && tabScrollOffset_ < kMaxTabScroll) {
            tabScrollOffset_++;
        }
    }

    // Scroll-Button Links (< Scroll)
    Rectangle scrollLeftBtn = { boxX + 12.0f, boxY + 64.0f, 90.0f, 32.0f };
    if (mouseClicked && CheckCollisionPointRec(mousePos, scrollLeftBtn)) {
        if (tabScrollOffset_ > 0) tabScrollOffset_--;
    }

    // Scroll-Button Rechts (Scroll >)
    Rectangle scrollRightBtn = { boxX + boxW - 102.0f, boxY + 64.0f, 90.0f, 32.0f };
    if (mouseClicked && CheckCollisionPointRec(mousePos, scrollRightBtn)) {
        if (tabScrollOffset_ < kMaxTabScroll) tabScrollOffset_++;
    }

    // Tab-Buttons prüfen
    float tabAreaX = boxX + 110.0f;
    float tabW = 210.0f;
    float tabGap = 8.0f;
    for (int i = 0; i < kVisibleTabs; ++i) {
        int tabIdx = tabScrollOffset_ + i;
        if (tabIdx >= kTotalTabs) break;

        Rectangle tabRect = { tabAreaX + (float)i * (tabW + tabGap), boxY + 64.0f, tabW, 32.0f };
        if (mouseClicked && CheckCollisionPointRec(mousePos, tabRect)) {
            activeEpochTab = kEpochTabs[tabIdx].id;
            break;
        }
    }

    // 3. Node-Canvas Interaktion (Box 3): NUR Nodes des aktiven Tabs prüfen!
    for (const auto& node : tribe.Skills().AllNodes()) {
        if (node.epochId != activeEpochTab) continue;

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

    float boxW = 1560.0f;
    float boxH = 860.0f;
    float boxX = ((float)SCREEN_WIDTH - boxW) * 0.5f;
    float boxY = ((float)SCREEN_HEIGHT - boxH) * 0.5f;

    // 1. Verdunkelnder Hintergrund
    DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.80f));

    // 2. Haupt-Fenster Rahmen
    Rectangle mainBox = { boxX, boxY, boxW, boxH };
    DrawRectangleRec(mainBox, Color{ 16, 19, 25, 253 });
    DrawRectangleLinesEx(mainBox, 2.5f, Color{ 60, 75, 100, 255 });

    // =========================================================================
    // BOX 1: HEADER (Höhe: 60px) - y: boxY bis boxY + 60
    // =========================================================================
    Rectangle headerRect = { boxX, boxY, boxW, 60.0f };
    DrawRectangleRec(headerRect, Color{ 22, 26, 35, 255 });
    DrawLine((int)boxX, (int)(boxY + 60.0f), (int)(boxX + boxW), (int)(boxY + 60.0f), Color{ 48, 58, 75, 255 });

    // Links: Titel & Subtitel
    FontManager::DrawTextShadow("FORSCHUNGSBAUM (KAUSALE ZIVILISATIONSENTWICKLUNG)", boxX + 24.0f, boxY + 12.0f, 21.0f, GOLD);
    FontManager::DrawTextShadow("Wissen entsteht organisch durch Alltagspraxis, Expeditionen und Heureka-Momente",
                                boxX + 24.0f, boxY + 36.0f, 13.0f, LIGHTGRAY);

    // Rechts: Schließen-Button [X]
    Rectangle closeBtn = { boxX + boxW - 46.0f, boxY + 14.0f, 32.0f, 32.0f };
    bool hovClose = CheckCollisionPointRec(mousePos, closeBtn);
    DrawRectangleRec(closeBtn, hovClose ? RED : Color{ 45, 52, 64, 255 });
    DrawRectangleLinesEx(closeBtn, 1.5f, RAYWHITE);
    FontManager::DrawTextCenteredShadow("X", closeBtn.x + closeBtn.width * 0.5f, closeBtn.y + 4.0f, 20.0f, RAYWHITE);

    // Rechts: Epochen- & Meilenstein-Status
    HistoricalEpoch curEpoch = tribe.Age().CurrentEpoch();
    Era curEra = tribe.Age().CurrentEra();
    if (curEra == Era::BronzeAge && tribe.Skills().IsResearched("Rennofen & Eisen")) {
        curEra = Era::IronAge;
    } else if (curEra == Era::StoneAge && tribe.Skills().IsResearched("Bronzelegierung & Guss")) {
        curEra = Era::BronzeAge;
    }
    int doneMilestones = tribe.Skills().GetMilestonesCompleted(curEra);
    int totalMilestones = std::max(1, tribe.Skills().GetMilestonesTotal(curEra));
    float milestonePct = std::clamp((float)doneMilestones / (float)totalMilestones, 0.0f, 1.0f);
    bool canAdvance = tribe.Age().CanAdvance(tribe.Stock(), tribe.Buildings(), tribe.Skills(), 5);

    float statusX = boxX + boxW - 560.0f;
    FontManager::DrawTextShadow(TextFormat("Epoche: %d/14 (%s)  |  Meilensteine: %d/%d",
                                           HistoricalEpochNumber(curEpoch), HistoricalEpochName(curEpoch),
                                           doneMilestones, totalMilestones),
                                statusX, boxY + 13.0f, 15.0f, (doneMilestones >= totalMilestones) ? LIME : GOLD);

    float barW = 220.0f;
    float barH = 12.0f;
    DrawRectangle((int)statusX, (int)(boxY + 36.0f), (int)barW, (int)barH, Fade(BLACK, 0.5f));
    DrawRectangle((int)statusX, (int)(boxY + 36.0f), (int)(barW * milestonePct), (int)barH, (milestonePct >= 1.0f) ? LIME : SKYBLUE);
    DrawRectangleLines((int)statusX, (int)(boxY + 36.0f), (int)barW, (int)barH, Fade(WHITE, 0.45f));

    if (canAdvance) {
        FontManager::DrawTextShadow("AUFSTIEG BEREIT!", statusX + barW + 12.0f, boxY + 34.0f, 13.0f, LIME);
    } else {
        FontManager::DrawTextShadow(TextFormat("%.0f%%", milestonePct * 100.0f), statusX + barW + 12.0f, boxY + 34.0f, 13.0f,
                                    (milestonePct >= 1.0f) ? LIME : SKYBLUE);
    }

    // =========================================================================
    // BOX 2: TAB-BAR (Höhe: 40px) - y: boxY + 60 bis boxY + 100
    // =========================================================================
    Rectangle tabBarRect = { boxX, boxY + 60.0f, boxW, 40.0f };
    DrawRectangleRec(tabBarRect, Color{ 18, 22, 28, 255 });
    DrawLine((int)boxX, (int)(boxY + 100.0f), (int)(boxX + boxW), (int)(boxY + 100.0f), Color{ 48, 58, 75, 255 });

    // Scroll-Button Links (< Scroll)
    Rectangle scrollLeftBtn = { boxX + 12.0f, boxY + 64.0f, 90.0f, 32.0f };
    bool hovLeft = CheckCollisionPointRec(mousePos, scrollLeftBtn);
    bool canScrollLeft = (tabScrollOffset_ > 0);
    DrawRectangleRec(scrollLeftBtn, canScrollLeft ? (hovLeft ? Color{ 40, 52, 68, 255 } : Color{ 26, 32, 42, 255 })
                                                 : Color{ 20, 23, 28, 255 });
    DrawRectangleLinesEx(scrollLeftBtn, 1.0f, canScrollLeft ? (hovLeft ? SKYBLUE : Color{ 60, 75, 95, 255 })
                                                           : Color{ 40, 45, 55, 255 });
    FontManager::DrawTextCenteredShadow("< Scroll", scrollLeftBtn.x + scrollLeftBtn.width * 0.5f, scrollLeftBtn.y + 7.0f, 13.0f,
                                        canScrollLeft ? (hovLeft ? RAYWHITE : LIGHTGRAY) : DARKGRAY);

    // Scroll-Button Rechts (Scroll >)
    Rectangle scrollRightBtn = { boxX + boxW - 102.0f, boxY + 64.0f, 90.0f, 32.0f };
    bool hovRight = CheckCollisionPointRec(mousePos, scrollRightBtn);
    bool canScrollRight = (tabScrollOffset_ < kMaxTabScroll);
    DrawRectangleRec(scrollRightBtn, canScrollRight ? (hovRight ? Color{ 40, 52, 68, 255 } : Color{ 26, 32, 42, 255 })
                                                   : Color{ 20, 23, 28, 255 });
    DrawRectangleLinesEx(scrollRightBtn, 1.0f, canScrollRight ? (hovRight ? SKYBLUE : Color{ 60, 75, 95, 255 })
                                                             : Color{ 40, 45, 55, 255 });
    FontManager::DrawTextCenteredShadow("Scroll >", scrollRightBtn.x + scrollRightBtn.width * 0.5f, scrollRightBtn.y + 7.0f, 13.0f,
                                        canScrollRight ? (hovRight ? RAYWHITE : LIGHTGRAY) : DARKGRAY);

    // Klickbare Epochen-Reiter
    float tabAreaX = boxX + 110.0f;
    float tabW = 210.0f;
    float tabGap = 8.0f;
    for (int i = 0; i < kVisibleTabs; ++i) {
        int tabIdx = tabScrollOffset_ + i;
        if (tabIdx >= kTotalTabs) break;

        Rectangle tabRect = { tabAreaX + (float)i * (tabW + tabGap), boxY + 64.0f, tabW, 32.0f };
        bool isActive = (kEpochTabs[tabIdx].id == activeEpochTab);
        bool isHovered = CheckCollisionPointRec(mousePos, tabRect);

        Color tabBg = isActive ? Color{ 36, 48, 66, 255 } :
                      isHovered ? Color{ 28, 36, 48, 255 } : Color{ 20, 24, 32, 255 };
        Color tabBorder = isActive ? GOLD :
                          isHovered ? RAYWHITE : Color{ 50, 60, 75, 255 };

        DrawRectangleRec(tabRect, tabBg);
        DrawRectangleLinesEx(tabRect, isActive ? 2.0f : 1.0f, tabBorder);

        if (isActive) {
            DrawRectangle((int)tabRect.x + 2, (int)(tabRect.y + tabRect.height - 3), (int)tabRect.width - 4, 3, GOLD);
        }

        std::string tabLabel = kEpochTabs[tabIdx].name;
        if (isActive) {
            tabLabel += " (Aktiv)";
        }
        FontManager::DrawTextCenteredShadow(tabLabel.c_str(),
                                            tabRect.x + tabRect.width * 0.5f, tabRect.y + 7.0f, 13.0f,
                                            isActive ? GOLD : (isHovered ? RAYWHITE : Color{ 160, 175, 195, 255 }));
    }

    // =========================================================================
    // BOX 3: NODE-CANVAS (Höhe: 728px) - y: boxY + 100 bis boxY + boxH - 32
    // =========================================================================
    Rectangle canvasRect = { boxX, boxY + 100.0f, boxW, boxH - 100.0f - 32.0f };
    DrawRectangleRec(canvasRect, Color{ 14, 16, 21, 255 });

    // Verbindungslinien zwischen Nodes im aktiven Tab zeichnen
    DrawConnectionLines(tribe.Skills(), boxX, boxY);

    // Nodes im aktiven Tab zeichnen
    int focusId = tribe.Skills().GetFocusNode();
    int nodesInCurrentTab = 0;
    for (const auto& node : tribe.Skills().AllNodes()) {
        if (node.epochId != activeEpochTab) continue;

        nodesInCurrentTab++;
        Rectangle r = GetNodeRect(node.id, boxX, boxY);
        bool hovered = (node.id == hoveredNodeId_);
        bool focused = (node.id == focusId);
        DrawNodeCard(node, r, hovered, focused, animTimer_);
    }

    // Wenn im gewählten Tab (z.B. Zukunftsepochen 4..14) noch keine Nodes existieren:
    if (nodesInCurrentTab == 0) {
        float cx = boxX + boxW * 0.5f;
        float cy = canvasRect.y + canvasRect.height * 0.5f;
        const char* eName = (activeEpochTab >= 1 && activeEpochTab <= kTotalTabs)
                                ? kEpochTabs[activeEpochTab - 1].name
                                : "Unbekannte Epoche";
        FontManager::DrawTextCenteredShadow(TextFormat("EPOCHE %d: %s", activeEpochTab, eName),
                                            cx, cy - 36.0f, 22.0f, Fade(GOLD, 0.75f));
        FontManager::DrawTextCenteredShadow("In dieser Zivilisations-Epoche sind noch keine Technologien verzeichnet.",
                                            cx, cy, 16.0f, LIGHTGRAY);
        FontManager::DrawTextCenteredShadow("Meistere die Meilensteine der vorherigen Zeitalter, um die Zivilisation organisch voranzubringen.",
                                            cx, cy + 28.0f, 14.0f, GRAY);
    }

    // =========================================================================
    // BOX 4: FOOTER (Höhe: 32px) - y: boxY + boxH - 32 bis boxY + boxH
    // =========================================================================
    Rectangle footerRect = { boxX, boxY + boxH - 32.0f, boxW, 32.0f };
    DrawRectangleRec(footerRect, Color{ 20, 24, 30, 255 });
    DrawLine((int)boxX, (int)(boxY + boxH - 32.0f), (int)(boxX + boxW), (int)(boxY + boxH - 32.0f), Color{ 48, 58, 75, 255 });
    FontManager::DrawTextShadow("Tipp: Klicke auf eine erforschbare Technologie, um sie als aktives Stammesziel zu priorisieren! | [K] Schließen",
                                boxX + 24.0f, boxY + boxH - 24.0f, 14.0f, LIGHTGRAY);

    // =========================================================================
    // TOOLTIP: Hover-Informationen dynamisch an die Maus gebunden & Screen-Clamped
    // =========================================================================
    if (hoveredNodeId_ >= 0) {
        const auto* hNode = tribe.Skills().GetNode(hoveredNodeId_);
        if (hNode && hNode->epochId == activeEpochTab) {
            DrawTooltip(*hNode, tribe.POIs(), mousePos);
        }
    }
}

void SkillTreeUI::DrawConnectionLines(const SkillTree& skills, float boxX, float boxY) const {
    for (const auto& node : skills.AllNodes()) {
        // AUFTRAG: Nur Verbindungen für Nodes zeichnen, die im aktuellen Tab sichtbar sind!
        if (node.epochId != activeEpochTab) continue;

        Rectangle toRect = GetNodeRect(node.id, boxX, boxY);
        // Child-Anchor: Immer linke Kante, exakt vertikal mittig
        Vector2 endPos = { toRect.x, toRect.y + toRect.height * 0.5f };

        for (int pId : node.prereqIds) {
            const auto* pNode = skills.GetNode(pId);
            if (!pNode) continue;

            Vector2 startPos;
            bool isExternalPrereq = false;

            if (pNode->epochId == activeEpochTab) {
                // Parent ist im selben Tab -> Rechter Rand der Parent-Node, exakt vertikal mittig
                Rectangle fromRect = GetNodeRect(pId, boxX, boxY);
                startPos.x = fromRect.x + fromRect.width;
                startPos.y = fromRect.y + fromRect.height * 0.5f;
            } else if (pNode->epochId < activeEpochTab) {
                // Parent ist in einem alten Tab -> Ziehe saubere Linie vom linken Rand
                startPos.x = boxX + 50.0f; // Rand-Offset
                startPos.y = endPos.y;     // Gerade Linie von links
                isExternalPrereq = true;
            } else {
                continue;
            }

            // Linienfarbe & Dicke nach Fortschritt
            Color lineCol = Fade(GRAY, 0.40f);
            float thick = 1.8f;

            if (pNode->state == TechState::Mastered && node.state == TechState::Mastered) {
                lineCol = Fade(GOLD, 0.85f);
                thick = 2.6f;
            } else if (pNode->state == TechState::Mastered &&
                       (node.state == TechState::Ready || node.state == TechState::InExpedition)) {
                lineCol = Fade(SKYBLUE, 0.90f);
                thick = 2.2f;
            }

            // Gebogene Bezier-Linie von startPos zu endPos
            DrawLineBezier(startPos, endPos, thick, lineCol);

            // Anchor-Punkte zeichnen
            DrawCircleV(startPos, isExternalPrereq ? 4.5f : 3.0f, lineCol);
            DrawCircleV(endPos, 3.0f, lineCol);

            // Wenn Voraussetzung aus vorheriger Epoche stammt: Hinweistext
            if (isExternalPrereq) {
                FontManager::DrawTextShadow(TextFormat("< [Aus Epoche %d: %s]", pNode->epochId, pNode->name.c_str()),
                                            startPos.x + 8.0f, startPos.y - 16.0f, 11.0f, Fade(lineCol, 0.95f));
            }
        }
    }
}

void SkillTreeUI::DrawNodeCard(const TechNode& node, Rectangle rect, bool isHovered, bool isFocused, float time) const {
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
        int pBarX = (int)rect.x + 115;
        int pBarY = (int)rect.y + 32;
        int pBarW = (int)rect.width - 125;
        DrawRectangle(pBarX, pBarY, pBarW, 6, Fade(BLACK, 0.5f));
        DrawRectangle(pBarX, pBarY, (int)(pBarW * pct), 6, (node.state == TechState::Ready) ? SKYBLUE : GOLD);
    }

    // Meilenstein-Kennzeichnung
    if (node.isMilestone) {
        FontManager::DrawTextShadow("* MEILENSTEIN", rect.x + 10.0f, rect.y + 54.0f, 12.0f, GOLD);
    } else {
        FontManager::DrawTextShadow(EraName(node.era), rect.x + 10.0f, rect.y + 54.0f, 12.0f, Fade(GRAY, 0.8f));
    }

    // Fokus-Ring
    if (isFocused) {
        DrawRectangleLinesEx(rect, 2.5f, GOLD);
        FontManager::DrawTextShadow("FOKUS", rect.x + rect.width - 55.0f, rect.y + 8.0f, 12.0f, GOLD);
    }
}

void SkillTreeUI::DrawTooltip(const TechNode& node, const POIManager& pois, Vector2 mousePos) const {
    float tipW = 400.0f;
    float tipH = 225.0f;
    float tipX = mousePos.x + 18.0f;
    float tipY = mousePos.y + 18.0f;

    // Dynamischer Bounds-Check: Tooltip darf NIEMALS aus dem Bildschirm ragen!
    if (tipX + tipW > (float)SCREEN_WIDTH - 20.0f) {
        tipX = mousePos.x - tipW - 18.0f;
    }
    if (tipY + tipH > (float)SCREEN_HEIGHT - 20.0f) {
        tipY = mousePos.y - tipH - 18.0f;
    }
    if (tipX < 20.0f) tipX = 20.0f;
    if (tipY < 20.0f) tipY = 20.0f;

    Rectangle r = { tipX, tipY, tipW, tipH };
    DrawRectangleRec(r, Color{ 16, 19, 26, 252 });
    DrawRectangleLinesEx(r, 1.8f, GOLD);

    FontManager::DrawTextShadow(node.name.c_str(), tipX + 14.0f, tipY + 10.0f, 19.0f, RAYWHITE);
    FontManager::DrawTextShadow(TextFormat("Epoche: %s (Epoche %d)", EraName(node.era), node.epochId),
                                tipX + 14.0f, tipY + 34.0f, 14.0f, EraColor(node.era));

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
