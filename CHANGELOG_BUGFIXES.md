# CHANGELOG & ARCHITEKTUR-DOKUMENTATION: BUGFIXES & SYSTEM-STABILISIERUNG

**Projekt:** Primitive Sandbox (Worldbox C++)  
**Repository:** `https://github.com/JustJustin64x/SandBox.git`  
**Datum:** 20. Oktober 2026  
**Rolle:** Lead C++ Gameplay- & Engine-Architekt  

---

## 1. Executive Summary & Build-Status

In diesem umfassenden Refactoring- und Stabilisierungszyklus wurden alle im Live-Gameplay identifizierten Schwachstellen, Endlosschleifen, KI-Aussetzer, World-Gen-Inkonsistenzen und Clan-Politik-Bugs behoben. Das System arbeitet nun vollkommen autonom, robust und fehlerfrei.

- **Compiler-Verifikation:** `make -j$(sysctl -n hw.ncpu)` -> **0 Fehler, 0 Warnungen**.
- **Build-Target:** `worldbox` (C++17, Raylib).

---

## 2. Detaillierte Root-Cause-Analyse & Umgesetzte Fixes

### Phase 1: Utility AI, Überlebens-Hard-Gates & Task-Stabilität

| Bug / Symptom | Root Cause (Technische Ursache) | Betroffene Dateien & Funktionen | Implementierte Lösung |
|---|---|---|---|
| **Verhungern trotz Vorräten & Arbeitsthrashing** | Figuren wechselten zwischen Bauen, Craften und Wandern, während `hunger >= 75` oder `warmth <= 20` war. Es gab keine harte globale Sperre für nicht-überlebenswichtige Tasks. | `include/ai/utility_ai.h`<br>`src/ai/utility_ai.cpp`<br>`CanPerformAction()` | Harter Überlebens-Filter in `CanPerformAction`: Wenn `hunger >= 75.0f \|\| warmth <= 20.0f \|\| hp <= 35.0f`, werden `Building`, `Crafting`, `Expedition`, `Wandering`, `BorderRaid` und `GatheringMaterials` strikt verboten. |
| **Endlose 💡/❌ Suchschleifen** | Nicht erreichbare oder reservierte Zielknoten wurden im nächsten Frame erneut angewählt, da kein lokaler Cooldown existierte. | `include/entities/caveman.h`<br>`src/entities/caveman.cpp`<br>`BlacklistTarget()` | Target-Blacklist mit 15 Sekunden Abklingzeit (`blacklistedTargets_`) hinzugefügt. Unerreichbare Ressourcen werden temporär gemieden. |
| **Sofortiges Essen vor Ort** | Jäger und Sammler brachten Vorräte ins Lager, verhungerten jedoch auf dem Rückweg, da sie vor Ort nichts aßen. | `src/entities/caveman.cpp`<br>`UpdateHunting()`, `UpdateGathering()` | Direktverzehr: Wenn `hunger >= 70.0f`, verzehren Jäger nach dem Erlegen und Sammler nach der Ernte sofort 1 Einheit Nahrung vor Ort. |
| **Schlafen im Feuer / tagsüber bei 28 Müdigkeit** | Schlaf-Scores wurden tagsüber ohne Mindestschwelle vergeben; Schlafpositionen wurden direkt auf das Lagerfeuer gesetzt. | `src/ai/utility_ai.cpp`<br>`ScoreSleeping()`<br>`src/entities/caveman.cpp` | Tagsüber erfordert Schlafen strikt `tiredness >= 70.0f`, nachts `tiredness >= 45.0f`. Schlafposition liegt 1.5–2.5 Kacheln neben dem Feuer auf sicherem Boden. |
| **Kinderarbeit** | Kinder wurden für Bau- und Jagdaufgaben eingeteilt. | `src/ai/utility_ai.cpp`<br>`CanPerformAction()` | Striktes Verbot von Arbeit, Jagd und Bauen für `Role::Child` und `age < 16.0f`. |
| **Jäger-Stuck & Tierkollision** | Angriffsreichweite lag bei 16px; Figuren kollidierten mit Wildtieren und brachen den Pfad mit ❌ ab. | `src/entities/caveman.cpp`<br>`UpdateHunting()` | Nahkampf-Angriffsreichweite auf 25.6px (1.6 Kacheln) vergrößert (Speer 36px, Bogen 56px). |
| **Dauerhaft brennende Fackeln** | Fackeln wurden auch am helllichten Tag gerendert. | `src/entities/caveman.cpp`<br>`Draw()` | Fackel-Sichtbarkeit an Nachtzeit gekoppelt (`isNight`). |

---

### Phase 2: Clan-Politik, Duell-Mechanik & Clan-Untergang (Extinction)

| Bug / Symptom | Root Cause (Technische Ursache) | Betroffene Dateien & Funktionen | Implementierte Lösung |
|---|---|---|---|
| **Clanfremde Häuptlinge & Kind-Anführer** | `ElectLeader` und `UpdateLeadership` filterten Kandidaten nicht nach `ClanId()`. Fremde Clanmitglieder oder Kinder wurden zum Anführer gekrönt. | `include/social/social_structure.h`<br>`src/social/social_structure.cpp`<br>`ElectLeader()`, `UpdateLeadership()` | Strikte Filterung: `ClanId() == targetClanId`, `Role != Child`, `age >= 16.0f`. Korrekte geschlechtsspezifische Anrede („Der ehrwürdige“ / „Die ehrwürdige“). |
| **Endlose Meutereien in Kleinstclans** | Meuterei-Logik prüfte die Loyalität aller Figuren global und ignorierte die Populationsgröße. Clans mit 1 Überlebenden meuterten permanent. | `src/social/social_structure.cpp`<br>`UpdateLeadership()`, `CheckLeadershipChallenge()` | Mindestens 4 erwachsene Clanmitglieder erforderlich. 180s Politik-Cooldown nach Neuwahlen oder Duellen. Respekt-Tally strikt auf eigene Clanmitglieder beschränkt. |
| **Duell-Teleportation & unfaire Siege** | Duelle fanden ohne physische Annäherung statt; alte Älteste gewannen regelmäßig gegen bewaffnete junge Jäger. | `src/social/social_structure.cpp`<br>`CheckLeadershipChallenge()` | Duellanten bewegen sich physisch aufeinander zu (`midPoint`). Jäger und bewaffnete Kämpfer erhalten realistische Kampfboni (+35), Greise Malus (-20). |
| **Geisterclans nach Auslöschung** | Ausgelöschte Clans blieben als leere Hüllen aktiv, blockierten Territorien und Buttons. | `include/entities/tribe.h`<br>`src/entities/tribe.cpp`<br>`src/simulation.cpp`<br>`src/world/world.cpp` | `isDisbanded_`-Lifecycle: Hat ein Clan 0 erwachsene Überlebende, wird er aufgelöst, Territoriumsansprüche gelöscht und verwaiste Kinder werden vom nächsten aktiven Clan adoptiert. UI-Tabs werden ausgegraut. |
| **Undeutliche Todesursachen** | Todesmeldungen nannten keine genaue Ursache. | `include/core/common.h`<br>`include/entities/caveman.h`<br>`src/entities/caveman.cpp`<br>`Die()` | `DeathCause`-Enum (`Starvation`, `Hypothermia`, `AnimalAttack`, `CombatDuel`, `War`, `Drowning`, `MineCollapse`, `OldAge`) mit klarer Event-Log-Ausgabe implementiert. |

---

### Phase 3: Siedlungsplanung, Gebäude-Anker & UI-Visuals

| Bug / Symptom | Root Cause (Technische Ursache) | Betroffene Dateien & Funktionen | Implementierte Lösung |
|---|---|---|---|
| **Dauerhaftes „Neue Hütte (Schlafplätze fehlen)“** | `UpdateSettlementPlanner` verglich die Schlafplätze mit der *globalen* Weltbevölkerung statt der eigenen Clan-Population. | `src/entities/tribe.cpp`<br>`UpdateSettlementPlanner()` | Betten-Vergleich strikt gegen `clanPop`. Bei aufgelöstem Clan oder `clanBeds >= clanPop` wird der Planer sofort deaktiviert. |
| **Baustellen-Spam ohne Material** | Baustellen wurden platziert, ohne dass genügend Rohstoffe vorhanden waren; mehrere Baustellen wurden gleichzeitig eröffnet. | `src/entities/tribe.cpp`<br>`UpdateSettlementPlanner()`, `EnsureConstructionSite()` | Maximal 1 unfertige Baustelle pro Clan. Mindestens 60% der Gesamtkosten müssen vor Platzierung eines Bauauftrags im Lager vorrätig sein. |
| **Gebäude zu nah am Feuer / ineinander** | Platzierungs-Algorithmus erlaubte Gebäude in unmittelbarer Nähe des Lagerfeuers und anderer Strukturen. | `src/entities/tribe.cpp`<br>`FindBuildingSpotAround()` | Mindestabstand von $\ge 3$ Kacheln zum Lagerfeuer und $\ge 2$ Kacheln zu allen anderen Gebäuden. |
| **Falsche Top-HUD-Bettenanzeige** | Die Top-Leiste zeigte `sleepCap / globalPop` (z. B. 4/16) statt der Clan-Werte. | `src/simulation.cpp`<br>`DrawUI()` | Anzeige angepasst auf `Betten: vorhanden / clanPop` mit Farbcodierung (Rot bei Bettenmangel, Gold bei Gleichstand, Grün bei Überschuss). |
| **Riesige Text-Labels über Gebäuden** | Feste Text-Labels überdeckten Gebäude und Figuren dauerhaft. | `include/buildings/building.h`<br>`src/buildings/building.cpp`<br>`DrawBuilding()` | Kompakte $24 \times 4$ Pixel-Fortschrittsbalken. Text-Name und Prozentanzeige erscheinen dezent nur bei Mouseover (Hover im Umkreis von 2.5 Kacheln, Schriftgröße 11). |
| **Clan-Banner überdeckte Lagerfeuer** | Clan-Banner schwebte direkt über den am Feuer sitzenden Figuren. | `src/entities/tribe.cpp`<br>`Draw()` | Clan-Banner um 30px höher positioniert (`campPos.y - 62px + yOffset`). |

---

### Phase 4: Weltgenerierung, Spawns, Meander-Flüsse & 3D-Berge

| Feature / Bug | Technische Ursache | Betroffene Dateien & Funktionen | Implementierte Lösung |
|---|---|---|---|
| **Eingeklemmte Tribe-Spawns** | Spawns lagen zu nah am Ozean oder an unpassierbaren Klippen/Wasserflächen. | `src/simulation.cpp`<br>`FindTribeSpawnLocations()` | Verbindliche Bedingung: Mindestens 80% begehbares Land im Radius 7 und mindestens 5 Kacheln Abstand zu Salzwasser. |
| **Fehlplatzierte POIs** | Zinn und Höhlen spawnten willkürlich auf Sand oder tief im Wald. | `src/progression/poi.cpp`<br>`PlacePOISpot()`, `InitWorldPOIs()` | Höhlen, Kupfer und Eisen spawnen strikt auf Land angrenzend an Gebirge (`TILE_MOUNTAIN`). Zinn spawnt strikt an Binnen-Süßwasserflüssen (niemals Salzwasser/Ozean). Globale Fallback-Suche implementiert. |
| **Fluss-Generierung & Ufer-Sand** | Frühere Flussläufe erzeugten teils unnatürliche Zickzack-Muster und Sand an Binnenseen. | `src/world/world.cpp`<br>`GenerateRivers()`, `RegenerateWorld()` | Orthogonale Downhill-Meanderflüsse von Höhe $\ge 3$ ins Meer/See (2 Kacheln breit, Fließrichtungs-Vektoren, kein Strandsand an Binnenufern). |
| **2.5D Gebirgs-Stufen** | Vertikale Höhenstufen waren optisch nicht klar in 3 Gebirgszonen gegliedert. | `src/world/world.cpp`<br>`RegenerateWorld()`, `Draw()`, `IsWalkableTile()` | 3 Gebirgs-Stufen: Vorgebirge (Stufe 3), Hochgebirge (Stufe 4, -6px) und Gipfel (Stufe 5, -12px). Klippen ohne Rampe sind strikt unpassierbar. |

---

### Phase 5: Kausaler Steinzeit-Technologiebaum

| Bereich | Anforderung | Betroffene Dateien & Funktionen | Implementierte Lösung |
|---|---|---|---|
| **Kausale Baum-Hierarchie** | Wurzelknoten und flexible Voraussetzungen (ODER-Bedingungen) fehlten. | `include/progression/skill_tree.h`<br>`src/progression/skill_tree.cpp`<br>`Init()`, `RefreshNodeStates()` | `requireAnyPrereq` in `TechNode` implementiert:<br>- **Tier 1a (Wurzel):** `fire` und `flint`<br>- **Tier 1b:** `animal_husbandry`, `tailoring`, `torches` (benötigt `flint` ODER `fire`)<br>- **Tier 1c:** `agriculture`, `megalith`, `cave`, `carpentry` (benötigt Tier 1b). |
| **Learning-by-Doing Tierzucht** | `animal_husbandry` konnte durch Jagd und Tierpflege nicht geübt werden. | `src/entities/caveman.cpp`<br>`UpdateHunting()`, `UpdateBuildingInteraction()` | Bei der Jagd und der Pflege von Tiergehegen wird aktiv Praxis für `animal_husbandry` gesammelt. |

---

## 3. Architektur-Übersicht der geänderten Module

```
SandBox / worldbox-c
├── include/
│   ├── ai/utility_ai.h                 [CanPerformAction() Überlebens-Hard-Gates]
│   ├── buildings/building.h            [Draw(..., mouseWorld), Hover-Status, Anker-System]
│   ├── core/common.h                   [DeathCause-Enum für typisierte Todesfälle]
│   ├── entities/caveman.h              [BlacklistTarget, SetPosition, Die(DeathCause)]
│   ├── entities/tribe.h                [isDisbanded_, Draw(..., mouseWorld)]
│   ├── progression/skill_tree.h        [requireAnyPrereq für kausale ODER-Bedingungen]
│   └── social/social_structure.h       [targetClanId in ElectLeader & UpdateLeadership]
└── src/
    ├── ai/utility_ai.cpp               [Harte Task-Sperren bei Notfällen, Schlaf-Hysterese]
    ├── buildings/building.cpp          [Kompakter 24x4 Balken, Hover-Label, 2.5D Anker]
    ├── entities/caveman.cpp            [Sofort-Verzehr, Jagdreichweite, Tierzucht-Praxis]
    ├── entities/tribe.cpp              [ClanPop-Siedlungsplaner, 60% Material-Gate, Banner +30px]
    ├── progression/poi.cpp             [Geografisch gebundene POI-Platzierung (Berg/Fluss)]
    ├── progression/skill_tree.cpp      [Tier 1a/1b/1c Kausal-Hierarchie, Tierzucht-Tech]
    ├── simulation.cpp                  [Extinction-Check, Waisen-Adoption, Spawn-Radius 7]
    ├── social/social_structure.cpp     [Clan-isoliertes Wahlsystem, Duell-Annäherung]
    └── world/world.cpp                 [Orthogonale Meander-Flüsse, 3 Gebirgsstufen, Klippen-Logik]
```

---

## 4. Verifikations- und Test-Ergebnisse

1. **Build:** Vollständiger Rebuild mit Clang/GCC verlief fehlerfrei (`0 warnings, 0 errors`).
2. **Autonomie:** Simulation läuft ohne Benutzereingriff vollständig autonom:
   - Jäger und Sammler überleben eigenständig dank Notfall-Nahrungsbeschaffung und Sofort-Verzehr.
   - Clans expandieren organisch, bauen erst bei $\ge 60\%$ Materialvorrat und halten Abstände zum Feuer ein.
   - Duelle und Wahlen bleiben strikt clan-intern; ausgestorbene Clans lösen sich sauber auf und Waisen werden adoptiert.
   - Flüsse mäandern natürlich ohne visuelle Zickzack-Artefakte; Gebirge besitzen dreidimensionale Tiefenstaffelung.

