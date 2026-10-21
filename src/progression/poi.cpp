#include "poi.h"
#include "world.h"
#include "caveman.h"
#include "tribe.h"
#include "event_log.h"
#include "config.h"

#include <cmath>
#include <algorithm>

const char* POITypeName(POIType type) {
    switch (type) {
        case POIType::LimestoneCave:   return "Verborgene Kalksteinhöhle";
        case POIType::AncientMegalith: return "Uralter Megalith / Hünengrab";
        case POIType::CopperOutcrop:   return "Kupfer-Aufschluss";
        case POIType::TinRiverbed:     return "Zinn-Flussbett / Seife";
        case POIType::IronRift:        return "Eisenerz-Spalte";
    }
    return "Unbekannter Ort";
}

Color POIColor(POIType type) {
    switch (type) {
        case POIType::LimestoneCave:   return Color{ 140, 160, 190, 255 };
        case POIType::AncientMegalith: return Color{ 180, 210, 130, 255 };
        case POIType::CopperOutcrop:   return Color{ 230, 120, 60, 255 };
        case POIType::TinRiverbed:     return Color{ 180, 220, 240, 255 };
        case POIType::IronRift:        return Color{ 160, 175, 195, 255 };
    }
    return WHITE;
}

POIManager::POIManager() {}

void POIManager::InitWorldPOIs(const World& world, Vector2 campPos) {
    pois_.clear();
    nextId_ = 1;

    auto addPOI = [&](POIType type, const char* name, const char* desc,
                      RequiredGear gear, Era era, float delay) {
        PointOfInterest poi;
        poi.id = nextId_++;
        poi.type = type;
        poi.name = name;
        poi.description = desc;
        poi.gear = gear;
        poi.requiredEra = era;
        poi.spawnDelay = delay;
        poi.state = POIState::Hidden;
        poi.spawned = true;
        poi.state = POIState::Hidden;

        Vector2 spot;
        if (PlacePOISpot(world, campPos, type, spot)) {
            poi.position = spot;
        } else {
            // Gezielte globale Fallback-Suche nach passendem Terrain
            bool found = false;
            for (int y = 6; y < world.Height() - 6 && !found; y += 2) {
                for (int x = 6; x < world.Width() - 6 && !found; x += 2) {
                    Vector2 cand = World::TileCenter(x, y);
                    if (!world.IsWalkable(cand)) continue;
                    const TileData& td = world.GetTileData(x, y);
                    if (td.waterType != WaterType::None || td.type == TILE_MOUNTAIN || td.isCliff) continue;

                    if (type == POIType::LimestoneCave || type == POIType::CopperOutcrop || type == POIType::IronRift) {
                        if (world.HasNeighbor(x, y, TILE_MOUNTAIN)) {
                            spot = cand;
                            found = true;
                        }
                    } else if (type == POIType::TinRiverbed) {
                        if ((td.isRiver || world.HasNeighbor(x, y, TILE_SHALLOW_WATER)) && td.waterType != WaterType::Saltwater) {
                            spot = cand;
                            found = true;
                        }
                    } else {
                        if (td.type == TILE_GRASS) {
                            spot = cand;
                            found = true;
                        }
                    }
                }
            }
            if (found) {
                poi.position = spot;
            } else {
                world.FindRandomWalkablePosition(campPos, 120.0f, poi.position, 60);
            }
        }
        pois_.push_back(poi);
    };

    // 1. Steinzeit: Kalksteinhöhle (benötigt Fackel + Spitzhacke + Proviant)
    addPOI(POIType::LimestoneCave, "Verborgene Kalksteinhöhle",
           "Eine dunkle Höhle mit Höhlenmalereien und tiefen Gängen. Erfordert Fackel und Hacke.",
           { true, true, true }, Era::StoneAge, 0.0f);

    // 2. Steinzeit: Megalith (benötigt Proviant)
    addPOI(POIType::AncientMegalith, "Uralter Megalith",
           "Kolossale, aufgerichtete Steinblöcke unserer Ahnen. Zentrum für Glauben und Weisheit.",
           { false, false, true }, Era::StoneAge, 0.0f);

    // 3. Bronzezeit: Kupfer-Aufschluss (benötigt Spitzhacke + Proviant)
    addPOI(POIType::CopperOutcrop, "Kupfer-Aufschluss",
           "Gesteinsadern mit leuchtend grünem Malachit und rötlichem Rohkupfer.",
           { false, true, true }, Era::BronzeAge, 0.0f);

    // 4. Bronzezeit: Zinn-Flussbett (benötigt Spitzhacke/Grabwerkzeug + Proviant)
    addPOI(POIType::TinRiverbed, "Zinn-Flussbett",
           "Schwemmsand mit schwerem Kassiterit-Zinnkies. Unverzichtbar für Bronzelegierungen.",
           { false, true, true }, Era::BronzeAge, 0.0f);

    // 5. Eisenzeit: Eisenerz-Spalte (benötigt Fackel + Spitzhacke + Proviant)
    addPOI(POIType::IronRift, "Eisenerz-Spalte",
           "Eine tiefe Felsspalte mit rötlich-schwarzen Hämatit-Adern für Eisenverhüttung.",
           { true, true, true }, Era::IronAge, 0.0f);
}

bool POIManager::PlacePOISpot(const World& world, Vector2 campPos, POIType type, Vector2& outPos) const {
    for (int attempt = 0; attempt < 300; ++attempt) {
        float angle = (float)GetRandomValue(0, 3600) / 3600.0f * 2.0f * PI;
        float radius = (float)GetRandomValue(70, 200);
        Vector2 candidate = { campPos.x + std::cos(angle) * radius, campPos.y + std::sin(angle) * radius };

        if (!world.IsWalkable(candidate)) continue;
        if (Vector2Distance(candidate, campPos) < 55.0f) continue;

        int tx = World::ToTile(candidate.x);
        int ty = World::ToTile(candidate.y);

        const TileData& td = world.GetTileData(tx, ty);
        if (td.waterType != WaterType::None || td.type == TILE_MOUNTAIN || td.isCliff) continue;

        // Spezifische Terrain-Vorlieben
        if (type == POIType::LimestoneCave || type == POIType::CopperOutcrop || type == POIType::IronRift) {
            // Höhlen, Kupfer und Eisenerz strikt auf Landkacheln angrenzend an Gebirge (TILE_MOUNTAIN)
            bool nearMountain = world.HasNeighbor(tx, ty, TILE_MOUNTAIN);
            if (!nearMountain) continue;
        } else if (type == POIType::TinRiverbed) {
            // Zinn-Flussbett strikt an Binnengewässern / Süßwasser-Flüssen (niemals Salzwasser/Ozean!)
            bool nearFreshRiver = false;
            bool nearSaltwater = false;
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    int nx = tx + dx, ny = ty + dy;
                    if (!world.IsInBounds(nx, ny)) continue;
                    const TileData& ntd = world.GetTileData(nx, ny);
                    if (ntd.waterType == WaterType::Saltwater) nearSaltwater = true;
                    if (ntd.waterType == WaterType::Freshwater || ntd.isRiver) nearFreshRiver = true;
                }
            }
            if (nearSaltwater || !nearFreshRiver) continue;
        } else if (type == POIType::AncientMegalith) {
            // Schöne Lichtung auf Grasland
            if (td.type != TILE_GRASS) continue;
        }

        // Abstand zu anderen POIs sicherstellen
        bool tooClose = false;
        for (const auto& other : pois_) {
            if (other.id > 0 && Vector2Distance(other.position, candidate) < 45.0f) {
                tooClose = true;
                break;
            }
        }
        if (tooClose) continue;

        outPos = candidate;
        return true;
    }
    return false;
}

void POIManager::Update(const World& world, Vector2 campPos, Era era, float eraTime,
                        const std::vector<Caveman>& cavemen, EventLog& log) {
    (void)world;
    (void)campPos;
    (void)eraTime;

    for (auto& poi : pois_) {
        // Kausales Aufdecken:
        // 1. Organisch durch wandernde Stammesmitglieder in der Wildnis
        if (poi.state == POIState::Hidden) {
            for (const auto& c : cavemen) {
                if (!c.IsDead() && Vector2Distance(c.Position(), poi.position) < 95.0f) {
                    poi.state = POIState::Discovered;
                    log.Add(TextFormat("%s hat %s in der Wildnis entdeckt!", c.Name().c_str(), poi.name.c_str()),
                            POIColor(poi.type));
                    break;
                }
            }
        }

        // 2. Kausal durch Erreichen der entsprechenden Epoche oder durch Stammeskunde
        if (poi.state == POIState::Hidden && era >= poi.requiredEra) {
            // Höhle & Megalith in der Steinzeit immer als Gerücht bekannt
            if (poi.type == POIType::LimestoneCave || poi.type == POIType::AncientMegalith ||
                poi.type == POIType::CopperOutcrop) {
                poi.state = POIState::Discovered;
                log.Add(TextFormat("KUNDE: Der Stamm erfährt von einem geheimnisvollen Ort: %s!", poi.name.c_str()),
                        POIColor(poi.type));
            } else if (era > poi.requiredEra) {
                poi.state = POIState::Discovered;
            }
        }
    }
}

PointOfInterest* POIManager::Get(int id) {
    for (auto& p : pois_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

const PointOfInterest* POIManager::Get(int id) const {
    for (const auto& p : pois_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

PointOfInterest* POIManager::GetByType(POIType type) {
    for (auto& p : pois_) {
        if (p.type == type) return &p;
    }
    return nullptr;
}

const PointOfInterest* POIManager::GetByType(POIType type) const {
    for (const auto& p : pois_) {
        if (p.type == type) return &p;
    }
    return nullptr;
}

bool POIManager::HasRequiredGear(const PointOfInterest& poi, const Stockpile& stock) const {
    // Prüft, ob Stamm genug Ressourcen oder gefertigte Ausrüstung für die Expedition hat
    if (poi.gear.torch && (stock.torches == 0 && stock.wood < cfg::kTorchWoodCost)) return false;
    if (poi.gear.pickaxe && (stock.pickaxes == 0 && (stock.wood < cfg::kPickaxeWoodCost || stock.stone < cfg::kPickaxeStoneCost))) return false;
    if (poi.gear.provisions && (stock.food + stock.cookedFood < 2)) return false;
    return true;
}

int POIManager::FindBestForExpedition(Vector2 from, const Stockpile& stock, int targetPoiId, Era currentEra) const {
    // Wenn ein bestimmter POI gezielt fokussiert wird
    if (targetPoiId >= 0) {
        const auto* target = Get(targetPoiId);
        if (target && target->CanBeExplored() && target->state != POIState::Explored && target->requiredEra <= currentEra) {
            if (HasRequiredGear(*target, stock)) {
                return target->id;
            }
        }
        return -1;
    }

    // Ansonsten den nächstgelegenen erforschbaren POI der aktuellen oder früheren Epoche suchen
    int bestId = -1;
    float bestDist = 1e9f;

    for (const auto& poi : pois_) {
        if (!poi.CanBeExplored()) continue;
        if (poi.state == POIState::Explored) continue;
        if (poi.requiredEra > currentEra) continue; // Epochen-Schranke: Keine Eisen-Spalte in der Steinzeit!
        if (!HasRequiredGear(poi, stock)) continue;

        float dist = Vector2Distance(from, poi.position);
        if (dist < bestDist) {
            bestDist = dist;
            bestId = poi.id;
        }
    }
    return bestId;
}

// ============================================================================
//  Zeichnen der POIs (Procedural Vector Art)
// ============================================================================
void POIManager::Draw(const Camera2D& camera, float time) const {
    Vector2 mouseScreen = GetMousePosition();
    Vector2 mouseWorld = GetScreenToWorld2D(mouseScreen, camera);

    for (const auto& poi : pois_) {
        if (!poi.spawned) continue;

        Vector2 p = poi.position;

        // Typ-spezifisches Rendering
        switch (poi.type) {
            case POIType::LimestoneCave:   DrawCave(p, poi.state, poi.explorationProgress, time); break;
            case POIType::AncientMegalith: DrawMegalith(p, poi.state, poi.explorationProgress, time); break;
            case POIType::CopperOutcrop:   DrawCopper(p, poi.state, poi.explorationProgress, time); break;
            case POIType::TinRiverbed:     DrawTin(p, poi.state, poi.explorationProgress, time); break;
            case POIType::IronRift:        DrawIron(p, poi.state, poi.explorationProgress, time); break;
        }

        // Fortschrittsbalken während aktiver Erkundung
        if (poi.state == POIState::Exploring && poi.explorationProgress > 0.0f) {
            int barW = 28;
            int barH = 4;
            int barX = (int)p.x - barW / 2;
            int barY = (int)p.y - 20;

            DrawRectangle(barX - 1, barY - 1, barW + 2, barH + 2, Fade(BLACK, 0.7f));
            DrawRectangle(barX, barY, (int)(barW * poi.explorationProgress), barH, GOLD);
            DrawRectangleLines(barX - 1, barY - 1, barW + 2, barH + 2, Fade(RAYWHITE, 0.6f));
        }

        // Meisterungs-Abzeichen oder dezentes [?] für unerkundete POIs
        if (poi.state == POIState::Explored) {
            DrawCircle((int)p.x + 8, (int)p.y - 12, 4.0f, GREEN);
            DrawCircleLines((int)p.x + 8, (int)p.y - 12, 4.0f, RAYWHITE);
        } else if (poi.state != POIState::Exploring) {
            // Dezenter, schwebender [?]-Indikator
            float bob = std::sin(time * 3.5f + p.x * 0.15f) * 1.5f;
            int qx = (int)p.x;
            int qy = (int)(p.y - 18.0f + bob);
            DrawCircle(qx, qy, 5.0f, Fade(BLACK, 0.70f));
            DrawCircleLines(qx, qy, 5.0f, Fade(GOLD, 0.85f));
            DrawText("?", qx - 2, qy - 4, 10, GOLD);
        }

        // Hover-Tooltip
        if (Vector2Distance(mouseWorld, p) < 18.0f) {
            const char* statusStr = (poi.state == POIState::Explored) ? "Erforscht" :
                                    (poi.state == POIState::Exploring) ? "Expedition aktiv..." : "Bereit zur Erkundung";
            Color statusCol = (poi.state == POIState::Explored) ? LIME :
                              (poi.state == POIState::Exploring) ? GOLD : SKYBLUE;

            const char* label = TextFormat("%s (%s)", poi.name.c_str(), statusStr);
            int textW = MeasureText(label, 11);
            DrawRectangle((int)p.x - textW / 2 - 4, (int)p.y - 32, textW + 8, 16, Fade(BLACK, 0.85f));
            DrawRectangleLines((int)p.x - textW / 2 - 4, (int)p.y - 32, textW + 8, 16, statusCol);
            DrawText(label, (int)p.x - textW / 2, (int)p.y - 30, 11, statusCol);
        }
    }
}

void POIManager::DrawCave(Vector2 pos, POIState state, float progress, float time) const {
    (void)state;
    (void)progress;
    // Schatten
    DrawEllipse((int)pos.x, (int)pos.y + 4, 14.0f, 6.0f, Fade(BLACK, 0.35f));

    // Felsformation um Höhle
    DrawCircle((int)pos.x - 7, (int)pos.y - 4, 8.0f, Color{ 110, 115, 125, 255 });
    DrawCircle((int)pos.x + 7, (int)pos.y - 5, 9.0f, Color{ 130, 135, 145, 255 });
    DrawCircle((int)pos.x, (int)pos.y - 10, 10.0f, Color{ 95, 100, 110, 255 });

    // Dunkler Höhleneingang
    DrawEllipse((int)pos.x, (int)pos.y - 2, 7.0f, 9.0f, Color{ 20, 20, 25, 255 });

    // Fackelglühen am Höhleneingang
    float flicker = std::sin(time * 8.0f) * 1.5f;
    DrawCircleGradient(Vector2{ pos.x + 6.0f, pos.y - 4.0f }, 6.0f + flicker, Fade(ORANGE, 0.7f), Fade(RED, 0.0f));
    DrawPixel((int)pos.x + 6, (int)pos.y - 4, YELLOW);
}

void POIManager::DrawMegalith(Vector2 pos, POIState state, float progress, float time) const {
    (void)state;
    (void)progress;
    DrawEllipse((int)pos.x, (int)pos.y + 5, 16.0f, 7.0f, Fade(BLACK, 0.3f));

    // Steinkreis: 3 aufrechte Monolithen und ein Querriegel
    Color stoneCol = Color{ 160, 165, 170, 255 };
    Color stoneDark = Color{ 120, 125, 130, 255 };

    // Linker Pfeiler
    DrawRectangle((int)pos.x - 10, (int)pos.y - 14, 5, 16, stoneDark);
    DrawRectangle((int)pos.x - 9, (int)pos.y - 14, 3, 16, stoneCol);

    // Rechter Pfeiler
    DrawRectangle((int)pos.x + 5, (int)pos.y - 14, 5, 16, stoneDark);
    DrawRectangle((int)pos.x + 6, (int)pos.y - 14, 3, 16, stoneCol);

    // Deckstein (Dolmen)
    DrawRectangle((int)pos.x - 12, (int)pos.y - 18, 24, 5, stoneDark);
    DrawRectangle((int)pos.x - 11, (int)pos.y - 18, 22, 4, Color{ 175, 180, 185, 255 });

    // Mystisches Glimmen im Zentrum (Ahnenkult)
    float pulse = (std::sin(time * 3.0f) + 1.0f) * 0.5f;
    DrawCircleGradient(Vector2{ pos.x, pos.y - 6.0f }, 8.0f, Fade(Color{ 160, 240, 180, 255 }, 0.15f + 0.25f * pulse), Fade(GREEN, 0.0f));
}

void POIManager::DrawCopper(Vector2 pos, POIState state, float progress, float time) const {
    (void)state;
    (void)progress;
    (void)time;
    DrawEllipse((int)pos.x, (int)pos.y + 3, 13.0f, 5.0f, Fade(BLACK, 0.3f));

    // Felsbrocken
    DrawCircle((int)pos.x, (int)pos.y - 4, 9.0f, Color{ 135, 120, 110, 255 });
    DrawCircle((int)pos.x - 5, (int)pos.y - 2, 6.0f, Color{ 110, 100, 90, 255 });

    // Malachit-Grün & Kupfer-Glanz
    DrawPixel((int)pos.x - 2, (int)pos.y - 6, Color{ 60, 180, 120, 255 });
    DrawPixel((int)pos.x - 1, (int)pos.y - 5, Color{ 60, 180, 120, 255 });
    DrawPixel((int)pos.x + 3, (int)pos.y - 3, Color{ 235, 140, 60, 255 });
    DrawPixel((int)pos.x + 4, (int)pos.y - 4, Color{ 255, 180, 90, 255 });
    DrawPixel((int)pos.x - 4, (int)pos.y - 2, Color{ 235, 140, 60, 255 });
}

void POIManager::DrawTin(Vector2 pos, POIState state, float progress, float time) const {
    (void)state;
    (void)progress;
    // Kiesbett am Ufer mit Schimmern
    DrawEllipse((int)pos.x, (int)pos.y, 12.0f, 6.0f, Color{ 190, 195, 205, 200 });

    // Wasserwellen
    float wave = std::sin(time * 4.0f) * 1.5f;
    DrawLine((int)pos.x - 8, (int)pos.y + 2, (int)pos.x + 8, (int)pos.y + 2 + (int)wave, Fade(SKYBLUE, 0.5f));

    // Silberne Kassiterit-Körner
    DrawPixel((int)pos.x - 3, (int)pos.y - 1, RAYWHITE);
    DrawPixel((int)pos.x + 2, (int)pos.y + 1, Color{ 220, 240, 255, 255 });
    DrawPixel((int)pos.x, (int)pos.y - 2, Color{ 160, 180, 210, 255 });
}

void POIManager::DrawIron(Vector2 pos, POIState state, float progress, float time) const {
    (void)state;
    (void)progress;
    (void)time;
    DrawEllipse((int)pos.x, (int)pos.y + 4, 15.0f, 6.0f, Fade(BLACK, 0.4f));

    // Dunkler Basaltfels mit Spalte
    DrawRectangle((int)pos.x - 9, (int)pos.y - 10, 18, 12, Color{ 70, 75, 85, 255 });
    DrawTriangle({ pos.x - 10, pos.y - 10 }, { pos.x - 2, pos.y - 16 }, { pos.x + 2, pos.y - 10 }, Color{ 85, 90, 100, 255 });

    // Zickzack-Spalte
    DrawLine((int)pos.x - 1, (int)pos.y - 12, (int)pos.x + 2, (int)pos.y - 6, BLACK);
    DrawLine((int)pos.x + 2, (int)pos.y - 6, (int)pos.x - 1, (int)pos.y, BLACK);

    // Rostroter Hämatit
    DrawPixel((int)pos.x - 3, (int)pos.y - 7, Color{ 190, 70, 50, 255 });
    DrawPixel((int)pos.x + 4, (int)pos.y - 5, Color{ 200, 85, 60, 255 });
    DrawPixel((int)pos.x, (int)pos.y - 3, Color{ 140, 50, 40, 255 });
}
