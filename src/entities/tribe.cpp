#include "tribe.h"
#include "config.h"
#include "event_log.h"
#include "world.h"

#include <algorithm>

Tribe::Tribe(Vector2 campPosition) : campPos_(campPosition) {
    stock_.food  = cfg::kStartFood;
    stock_.wood  = cfg::kStartWood;
    stock_.stone = cfg::kStartStone;
}

// ============================================================================
//  Hüttenbau
// ============================================================================
bool Tribe::CanAffordHut() const {
    return stock_.wood >= cfg::kHutWoodCost && stock_.stone >= cfg::kHutStoneCost;
}

bool Tribe::NeedsMoreHuts(int population) const {
    // Jede Hütte (fertig oder im Bau) zählt mit ihrer Kapazität.
    return (int)huts_.size() * cfg::kHutCapacity < population;
}

bool Tribe::FindHutSpot(const World& world, Vector2& out) const {
    for (int attempt = 0; attempt < 80; ++attempt) {
        const float angle = (float)GetRandomValue(0, 3600) / 3600.0f * 2.0f * PI;
        const float radius = (float)GetRandomValue(3 * TILE_SIZE, 9 * TILE_SIZE);
        const Vector2 p = { campPos_.x + std::cos(angle) * radius, campPos_.y + std::sin(angle) * radius };

        if (!world.IsWalkable(p)) continue;
        if (Vector2Distance(p, StockpilePosition()) < 30.0f) continue;

        bool blocked = false;
        for (const Hut& h : huts_) {
            if (Vector2Distance(h.position, p) < 34.0f) { blocked = true; break; }
        }
        if (blocked) continue;

        out = p;
        return true;
    }
    return false;
}

int Tribe::EnsureConstructionSite(const World& world, EventLog& log) {
    if (activeSite_ >= 0) return activeSite_;
    if (!CanAffordHut()) return -1;

    Vector2 spot;
    if (!FindHutSpot(world, spot)) return -1;

    stock_.wood  -= cfg::kHutWoodCost;
    stock_.stone -= cfg::kHutStoneCost;

    Hut hut;
    hut.position = spot;
    huts_.push_back(hut);
    activeSite_ = (int)huts_.size() - 1;

    log.Add(TextFormat("Neue Baustelle für Hütte #%d platziert (-%d Holz, -%d Stein)",
                       activeSite_ + 1, cfg::kHutWoodCost, cfg::kHutStoneCost),
            { 240, 200, 90, 255 });
    return activeSite_;
}

bool Tribe::AddBuildProgress(int hutIndex, float amount, EventLog& log) {
    Hut* hut = GetHut(hutIndex);
    if (!hut || hut->finished) return false;

    hut->progress = std::min(100.0f, hut->progress + amount);
    if (hut->progress < 100.0f) return false;

    hut->finished = true;
    stock_.huts++;
    if (activeSite_ == hutIndex) activeSite_ = -1;
    log.Add(TextFormat("Hütte #%d fertiggestellt! (%d Hütten, %d Schlafplätze)",
                       hutIndex + 1, stock_.huts, SleepingCapacity()),
            { 120, 230, 120, 255 });
    return true;
}

// ============================================================================
//  Schlafplätze
// ============================================================================
int Tribe::FindSleepingHut(Vector2 from) const {
    int best = -1;
    float bestDist = 0.0f;
    for (size_t i = 0; i < huts_.size(); ++i) {
        const Hut& h = huts_[i];
        if (!h.finished || h.sleepers >= cfg::kHutCapacity) continue;
        const float d = Vector2Distance(from, h.position);
        if (best < 0 || d < bestDist) {
            best = (int)i;
            bestDist = d;
        }
    }
    return best;
}

int Tribe::SleepingCapacity() const {
    return stock_.huts * cfg::kHutCapacity;
}

Hut* Tribe::GetHut(int index) {
    if (index < 0 || index >= (int)huts_.size()) return nullptr;
    return &huts_[(size_t)index];
}

const Hut* Tribe::GetHut(int index) const {
    if (index < 0 || index >= (int)huts_.size()) return nullptr;
    return &huts_[(size_t)index];
}

// ============================================================================
//  Zeichnen
// ============================================================================
void Tribe::Draw(float time) const {
    for (size_t i = 0; i < huts_.size(); ++i) DrawHut(huts_[i], (int)i);
    DrawStockpile();
    DrawCampfire(time);
}

void Tribe::DrawCampfire(float time) const {
    const float x = campPos_.x, y = campPos_.y;
    // Steinring
    for (int i = 0; i < 8; ++i) {
        const float a = (float)i / 8.0f * 2.0f * PI;
        DrawCircleV({ x + std::cos(a) * 6.0f, y + std::sin(a) * 3.5f }, 1.6f, { 120, 115, 110, 255 });
    }
    // Flackernde Flamme
    const float flicker = std::sin(time * 13.0f) * 0.8f + std::sin(time * 7.3f) * 0.6f;
    DrawCircleV({ x, y - 2.0f }, 3.8f + flicker * 0.3f, Fade(ORANGE, 0.9f));
    DrawCircleV({ x, y - 3.5f - flicker * 0.4f }, 2.4f, { 255, 220, 80, 255 });
    DrawCircleV({ x, y - 4.0f }, 14.0f + flicker, Fade(ORANGE, 0.08f));   // Lichtschein
}

void Tribe::DrawStockpile() const {
    const Vector2 p = StockpilePosition();
    DrawRectangleRec({ p.x - 9.0f, p.y - 1.0f, 18.0f, 4.0f }, Fade(BLACK, 0.2f));

    // Holzstapel
    for (int i = 0; i < std::min(stock_.wood, 8); ++i)
        DrawRectangleRec({ p.x - 9.0f + (float)(i % 4) * 2.2f, p.y - 2.0f - (float)(i / 4) * 2.0f, 2.0f, 2.0f },
                         { 130, 85, 40, 255 });
    // Steinhaufen
    for (int i = 0; i < std::min(stock_.stone, 8); ++i)
        DrawCircleV({ p.x + 1.0f + (float)(i % 4) * 2.0f, p.y - 1.0f - (float)(i / 4) * 2.0f }, 1.0f,
                    { 150, 148, 145, 255 });
    // Nahrung (Fleisch/Beeren)
    for (int i = 0; i < std::min(stock_.food, 10); ++i)
        DrawCircleV({ p.x - 8.0f + (float)(i % 5) * 2.2f, p.y + 4.0f + (float)(i / 5) * 2.0f }, 1.0f,
                    (i % 2) ? Color{ 200, 40, 60, 255 } : Color{ 210, 120, 110, 255 });
}

void Tribe::DrawHut(const Hut& hut, int index) const {
    const float x = hut.position.x, y = hut.position.y;
    DrawEllipse((int)x, (int)(y + 1), 12, 3, Fade(BLACK, 0.25f));

    if (hut.finished) {
        // Fellzelt: Dreieck (gegen den Uhrzeigersinn für raylib) + Eingang + Stangen
        DrawTriangle({ x, y - 18.0f }, { x - 11.0f, y }, { x + 11.0f, y }, { 150, 100, 55, 255 });
        DrawTriangle({ x, y - 18.0f }, { x - 11.0f, y }, { x - 4.0f, y }, { 125, 82, 45, 255 });
        DrawTriangle({ x, y - 8.0f }, { x - 3.5f, y }, { x + 3.5f, y }, { 40, 25, 15, 255 });
        DrawLineEx({ x - 2.0f, y - 21.0f }, { x + 1.0f, y - 16.0f }, 1.0f, { 90, 60, 30, 255 });
        DrawLineEx({ x + 2.0f, y - 21.0f }, { x - 1.0f, y - 16.0f }, 1.0f, { 90, 60, 30, 255 });
        if (hut.sleepers > 0) DrawText("z", (int)(x + 7), (int)(y - 22), 8, RAYWHITE);
    } else {
        // Baustelle: Pfähle + teilweise Fellbedeckung entsprechend Fortschritt
        const float t = hut.progress / 100.0f;
        DrawLineEx({ x - 10.0f, y }, { x, y - 18.0f }, 1.2f, { 110, 75, 40, 255 });
        DrawLineEx({ x + 10.0f, y }, { x, y - 18.0f }, 1.2f, { 110, 75, 40, 255 });
        if (t > 0.0f) {
            const float h = 18.0f * t;
            const float w = 11.0f * t;
            DrawTriangle({ x, y - h }, { x - w, y }, { x + w, y }, Fade({ 150, 100, 55, 255 }, 0.85f));
        }
        // Fortschrittsbalken
        DrawRectangleRec({ x - 10.0f, y - 25.0f, 20.0f, 2.5f }, Fade(BLACK, 0.6f));
        DrawRectangleRec({ x - 10.0f, y - 25.0f, 20.0f * t, 2.5f }, { 240, 200, 90, 255 });
        DrawText(TextFormat("#%d", index + 1), (int)(x - 5), (int)(y - 33), 8, RAYWHITE);
    }
}

