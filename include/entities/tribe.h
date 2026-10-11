#ifndef TRIBE_H
#define TRIBE_H

#include "common.h"

#include <vector>

class World;
class EventLog;

// Globales Stammes-Lager (Inventar).
struct Stockpile {
    int food  = 0;
    int wood  = 0;
    int stone = 0;
    int huts  = 0;   // Anzahl FERTIGER Hütten
};

// Eine Hütte – entweder Baustelle (finished == false) oder fertiger Schlafplatz.
struct Hut {
    Vector2 position = { 0.0f, 0.0f };
    float progress = 0.0f;   // 0..100 %
    bool finished = false;
    int sleepers = 0;        // belegte Schlafplätze
    int builders = 0;        // aktuell zugewiesene Bauarbeiter
};

// Der Stamm: Lagerfeuer/Lager, Inventar und Hütten.
class Tribe {
public:
    explicit Tribe(Vector2 campPosition);

    Stockpile& Stock() { return stock_; }
    const Stockpile& Stock() const { return stock_; }

    Vector2 CampPosition() const { return campPos_; }
    Vector2 StockpilePosition() const { return { campPos_.x + 20.0f, campPos_.y + 2.0f }; }

    // --- Hüttenbau ------------------------------------------------------------
    bool CanAffordHut() const;
    bool NeedsMoreHuts(int population) const;   // fertig + im Bau reicht nicht für alle
    bool HasConstructionSite() const { return activeSite_ >= 0; }
    int ConstructionSiteIndex() const { return activeSite_; }
    // Gibt die aktive Baustelle zurück oder platziert eine neue (zieht Kosten ab). -1 = nicht möglich.
    int EnsureConstructionSite(const World& world, EventLog& log);
    // Fügt Baufortschritt hinzu; true, wenn die Hütte dadurch fertig wurde.
    bool AddBuildProgress(int hutIndex, float amount, EventLog& log);

    // --- Schlafplätze ------------------------------------------------------------
    int FindSleepingHut(Vector2 from) const;    // nächste fertige Hütte mit freiem Platz
    int SleepingCapacity() const;

    Hut* GetHut(int index);
    const Hut* GetHut(int index) const;
    const std::vector<Hut>& Huts() const { return huts_; }

    void Draw(float time) const;

private:
    bool FindHutSpot(const World& world, Vector2& out) const;
    void DrawCampfire(float time) const;
    void DrawStockpile() const;
    void DrawHut(const Hut& hut, int index) const;

    Vector2 campPos_;
    Stockpile stock_;
    std::vector<Hut> huts_;
    int activeSite_ = -1;
};

#endif // TRIBE_H

