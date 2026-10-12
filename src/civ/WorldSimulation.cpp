#include "civ/WorldSimulation.h"
#include "world/world.h"
#include "core/event_log.h"
#include "gfx/sprite_sheet.h"

#include <cstdio>
#include <algorithm>

WorldSimulation::WorldSimulation(Vector2 settlementCenter, int initialCreatureCount)
    : settlement_(settlementCenter) {

    // Start-Bevölkerung der Zivilisation spawnen
    for (int i = 0; i < initialCreatureCount; ++i) {
        Vector2 offset = {
            settlementCenter.x + (float)((rand() % 40) - 20),
            settlementCenter.y + (float)((rand() % 40) - 20)
        };
        SpawnCreature(offset);
    }
}

int WorldSimulation::LivingCreaturesCount() const {
    int alive = 0;
    for (const auto& c : creatures_) {
        if (!c.IsDead()) ++alive;
    }
    return alive;
}

void WorldSimulation::SpawnCreature(Vector2 worldPos, const std::string& customName) {
    int id = nextCreatureId_++;
    std::string name = customName.empty() ? TextFormat("Siedler #%d", id) : customName;
    creatures_.emplace_back(id, name, worldPos);
}

const Creature* WorldSimulation::GetCreatureById(int id) const {
    for (const auto& c : creatures_) {
        if (c.Id() == id) return &c;
    }
    return nullptr;
}

void WorldSimulation::RunSettlementDecisionTick(const World& world, EventLog& log) {
    // =========================================================================
    // PERIODISCHER DORF-ENTSCHEIDUNGS-TICK (PERFORMANCE-OPTIMIERT)
    // Läuft in festen Intervallen (z.B. jede 1 Sekunde), anstatt jeden Frame!
    // =========================================================================

    // 1. EPOCHEN-PRÜFUNG: Kann die Zivilisation in das nächste Zeitalter aufsteigen?
    std::string advanceMsg;
    if (tech_.TryAdvanceAge(settlement_, advanceMsg)) {
        log.Add(advanceMsg.c_str(), GOLD);
    }

    // 2. BEDARFSERMITTLUNG & JOB BOARD: Erzeugt neue sinnvolle Aufgaben
    int livingCount = LivingCreaturesCount();
    settlement_.AnalyzeAndGenerateTasks(tech_, world, livingCount, &log);

    // 3. AUFGABEN-VERGABE: Weist freien, gesunden Creatures offene Aufgaben zu
    for (auto& c : creatures_) {
        if (c.IsDead()) continue;

        // Nur Kreaturen im Leerlauf (ohne akuten Hunger oder Erschöpfung) anstellen
        if (c.State() == CreatureState::IDLE && !c.HasTask()) {
            auto task = settlement_.RequestTaskForCreature(c.Id());
            if (task) {
                c.AssignTask(task);
            }
        }
    }
}

void WorldSimulation::Update(float dt, const World& world, EventLog& log) {
    // -------------------------------------------------------------------------
    // A) FESTER TAKT FÜR RECHENINTENSIVE ENTSCHEIDUNGEN
    // -------------------------------------------------------------------------
    decisionTimer_ += dt;
    while (decisionTimer_ >= DECISION_INTERVAL) {
        RunSettlementDecisionTick(world, log);
        decisionTimer_ -= DECISION_INTERVAL;
    }

    // -------------------------------------------------------------------------
    // B) KONTINUIERLICHES FRAME-UPDATE FÜR KREATUREN (BEWEGUNG & VERHALTEN)
    // -------------------------------------------------------------------------
    for (auto& c : creatures_) {
        c.Update(dt, settlement_, world, &log);
    }
}

void WorldSimulation::DrawWorld(const SpriteSheet* spriteSheet, int selectedCreatureId) const {
    // 1. Siedlungs-Gebäude, Lager und Baustellen zeichnen
    settlement_.Draw((float)GetTime(), tech_);

    // 2. Kreaturen Y-sortiert zeichnen
    std::vector<const Creature*> sortedCreatures;
    sortedCreatures.reserve(creatures_.size());
    for (const auto& c : creatures_) {
        if (!c.IsDead()) sortedCreatures.push_back(&c);
    }
    std::sort(sortedCreatures.begin(), sortedCreatures.end(), [](const Creature* a, const Creature* b) {
        return a->Position().y < b->Position().y;
    });

    for (const auto* c : sortedCreatures) {
        bool selected = (c->Id() == selectedCreatureId);
        c->Draw(spriteSheet, selected);
    }
}

void WorldSimulation::DrawUI(int screenW, int screenH) const {
    (void)screenH;
    // -------------------------------------------------------------------------
    // EPOCHEN- & ZIVILISATIONS-STATUSLEISTE
    // -------------------------------------------------------------------------
    int barW = 340;
    int barH = 58;
    int barX = (screenW - barW) / 2;
    int barY = 56;

    // Epochen-Farbton
    Color ageColor = LIGHTGRAY;
    if (tech_.CurrentAge() == Age::BRONZE_AGE) ageColor = Color{ 205, 127, 50, 255 }; // Bronze
    if (tech_.CurrentAge() == Age::IRON_AGE)   ageColor = Color{ 170, 190, 210, 255 }; // Eisen

    DrawRectangle(barX, barY, barW, barH, Fade(BLACK, 0.85f));
    DrawRectangleLines(barX, barY, barW, barH, ageColor);

    // Epochentitel
    const char* eraTitle = TextFormat("EPOCHE: %s", tech_.CurrentAgeName());
    int titleW = MeasureText(eraTitle, 16);
    DrawText(eraTitle, barX + (barW - titleW) / 2, barY + 8, 16, ageColor);

    // Nächste Epochen-Anforderung oder Maximalstufe
    if (tech_.HasNextAge()) {
        const auto& req = tech_.NextAgeRequirements();
        const auto& inv = settlement_.Inventory();
        const char* nextEra = (tech_.CurrentAge() == Age::STONE_AGE) ? "Bronzezeit" : "Eisenzeit";

        std::string progressStr = TextFormat("Ziel: %s (Nahr: %d/%d | Holz: %d/%d | Stein: %d/%d)",
            nextEra, inv.food, req.requiredFood, inv.wood, req.requiredWood, inv.stone, req.requiredStone);
        int progW = MeasureText(progressStr.c_str(), 11);
        DrawText(progressStr.c_str(), barX + (barW - progW) / 2, barY + 32, 11, RAYWHITE);
    } else {
        const char* maxStr = "Maximale Epochenstufe erreicht (Eisenzeit)!";
        int maxW = MeasureText(maxStr, 11);
        DrawText(maxStr, barX + (barW - maxW) / 2, barY + 32, 11, GOLD);
    }
}

