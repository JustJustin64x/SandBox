#ifndef POI_H
#define POI_H

#include "common.h"
#include "age_manager.h"
#include <string>
#include <vector>

class World;
class Caveman;
class EventLog;
struct Stockpile;

// Arten von dynamischen Entdeckungsorten (Points of Interest)
enum class POIType {
    LimestoneCave,    // Verborgene Höhle (Schaltet Höhlenkunde frei)
    AncientMegalith,  // Megalithen / Hünengrab (Schaltet Megalith-Kult frei)
    CopperOutcrop,    // Kupfer-Aufschluss (Schaltet Kupfergewinnung frei)
    TinRiverbed,      // Zinn-Flussbett / Seife (Schaltet Zinnwäsche frei)
    IronRift          // Eisenerz-Spalte / Meteorit (Schaltet Rennofen frei)
};

enum class POIState {
    Hidden,      // Unentdeckt im Nebel / der Wildnis
    Discovered,  // Sichtbar auf der Karte, wartet auf Expedition
    Exploring,   // Eine Figur erforscht den Ort aktiv vor Ort
    Explored     // Vollständig erkundet, Wissen in die Siedlung gebracht
};

struct RequiredGear {
    bool torch = false;       // Fackel gegen Dunkelheit
    bool pickaxe = false;     // Steinspitzhacke / Grabwerkzeug
    bool provisions = false;  // Proviant / Rationen
};

const char* POITypeName(POIType type);
Color POIColor(POIType type);

// Ein einzelner Entdeckungsort in der Welt
struct PointOfInterest {
    int id = -1;
    POIType type = POIType::LimestoneCave;
    POIState state = POIState::Hidden;
    Vector2 position = { 0.0f, 0.0f };
    std::string name;
    std::string description;
    float explorationProgress = 0.0f; // 0.0f bis 1.0f
    int assignedExplorerId = -1;
    RequiredGear gear;
    Era requiredEra = Era::StoneAge;
    float spawnDelay = 0.0f;          // Zeitfenster ab Epochenstart
    bool spawned = false;

    bool CanBeExplored() const {
        return spawned && (state == POIState::Discovered || state == POIState::Exploring);
    }
};

// Verwaltet alle Entdeckungsorte, dynamisches Spawning und Rendering
class POIManager {
public:
    POIManager();

    void InitWorldPOIs(const World& world, Vector2 campPos);
    void Update(const World& world, Vector2 campPos, Era era, float eraTime,
                const std::vector<Caveman>& cavemen, EventLog& log);
    void Draw(const Camera2D& camera, float time) const;

    PointOfInterest* Get(int id);
    const PointOfInterest* Get(int id) const;
    PointOfInterest* GetByType(POIType type);
    const PointOfInterest* GetByType(POIType type) const;

    const std::vector<PointOfInterest>& All() const { return pois_; }

    // Sucht einen geeigneten POI für eine Expedition (unter Beachtung der Epochen-Schranke)
    int FindBestForExpedition(Vector2 from, const Stockpile& stock, int targetPoiId = -1, Era currentEra = Era::IronAge) const;

    // Prüft, ob benötigte Ausrüstung vorhanden ist
    bool HasRequiredGear(const PointOfInterest& poi, const Stockpile& stock) const;

private:
    bool PlacePOISpot(const World& world, Vector2 campPos, POIType type, Vector2& outPos) const;
    void DrawCave(Vector2 pos, POIState state, float progress, float time) const;
    void DrawMegalith(Vector2 pos, POIState state, float progress, float time) const;
    void DrawCopper(Vector2 pos, POIState state, float progress, float time) const;
    void DrawTin(Vector2 pos, POIState state, float progress, float time) const;
    void DrawIron(Vector2 pos, POIState state, float progress, float time) const;

    std::vector<PointOfInterest> pois_;
    int nextId_ = 1;
};

#endif // POI_H

