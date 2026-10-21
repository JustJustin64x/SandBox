# CHANGELOG & ARCHITEKTUR-DOKUMENTATION: BUGFIXES & SYSTEM-STABILISIERUNG

**Projekt:** Primitive Sandbox (Worldbox C++)  
**Repository:** `https://github.com/JustJustin64x/SandBox.git`  
**Datum:** 20. Oktober 2026  
**Rolle:** Lead C++ Gameplay- & Engine-Architekt  

---

## 1. Executive Summary & Build-Status

In diesem umfassenden Refactoring- und Stabilisierungszyklus wurden alle im Live-Gameplay, in Videoaufzeichnungen und im 3100-Sekunden-Echtzeit-Log (`chat_log.txt`) identifizierten Architekturfehler, KI-Freezes, Endlosschleifen, biologischen Logikfehler und visuellen Lesbarkeitsprobleme behoben.

- **Compiler-Verifikation:** `make clean && make -j$(sysctl -n hw.ncpu)` -> **0 Fehler, 0 Warnungen**.
- **Build-Target:** `worldbox` (C++17, Raylib).

---

## 2. Master-Überarbeitung: Die 5 System-Blöcke im Detail

### BLOCK 1: Epochen-System, Hard-Gating im Skilltree & Gebäude-Evolution

| Bug / Anforderung | Root Cause (Technische Ursache) | Betroffene Dateien & Funktionen | Implementierte Lösung |
|---|---|---|---|
| **Fehlende Epochen-Synchronisation & Eisenzeit-Blockade** | Das HUD besaß 5 Epochen, das UI 3 Spalten. Spalte 3 (`SIEDLUNGS- & EISENZEIT`) wurde selbst nach 100% Forschung aller 12 Techs nie als `[AKTIV]` markiert; Figuren erforschten `Rennofen & Eisen` bereits in der Steinzeit vor Spalte 2. | `include/progression/skill_tree.h`<br>`src/progression/skill_tree.cpp`<br>`CanResearchNode()`<br>`src/ui/skill_tree_ui.cpp`<br>`src/world/age_manager.cpp` | **Strikte Epochen-Schranke (Hard Gating):**<br>1. Spalte 2 (`BRONZE- & WERKZEUGZEIT`) schaltet erst frei, wenn alle 3 Steinzeit-Meilensteine (`agriculture`, `cave`, `megalith`) gemeistert sind.<br>2. Spalte 3 (`SIEDLUNGS- & EISENZEIT`) schaltet erst frei, wenn `copper`, `tin` UND `bronze` aus Spalte 2 vollständig erforscht sind.<br>3. `SkillTreeUI` markiert Spalte 3 dynamisch als `[AKTIV]`, sobald die Bronze-Meilensteine gemeistert sind (Spalte 1 & 2 werden als `[GEMEISTERT]` deklariert). |
| **POIs vor der Zeit nutzbar** | Kupfer- und Zinn-Vorkommen sowie Eisen-Spalten konnten in der Steinzeit erkundet werden. | `include/progression/poi.h`<br>`src/progression/poi.cpp`<br>`CanExplorePOI()` | Era-Beschränkung in `CanExplorePOI()`: `CopperOutcrop` und `TinRiverbed` erfordern mindestens die Bronzezeit (`EraIndex >= 1`); `IronRift` erfordert strikt die Eisenzeit (`EraIndex >= 2`). |
| **Automatische Gebäude-Evolution über Epochen** | Stämme verblieben auch in der Eisenzeit in einfachen Fellzelten (`Notunterkunft Stufe 1`) und am Feuerkreis. | `include/buildings/building.h`<br>`src/buildings/building.cpp`<br>`src/entities/tribe.cpp`<br>`UpdateSettlementPlanner()` | **Epochen-Upgrades:**<br>1. **Stammeszentrum (`TownCenter`):** Stufe 1 (Feuerkreis) $\rightarrow$ Stufe 2 (Langhaus mit Clan-Bannern) in der Bronzezeit $\rightarrow$ Stufe 3 (Befestigte Halle / Rathaus) in der Eisenzeit.<br>2. **Wohnhütten (`Hut`):** Stufe 1 (Fellzelt) $\rightarrow$ Stufe 2 (Holzhütte mit Strohdach) $\rightarrow$ Stufe 3 (Steinhaussiedlung mit Ziegeldach) nach Erforschung von `Steinmetzkunst`.<br>3. `EnsureConstructionSite()` initiiert automatische Umbau-Baustellen (`underUpgrade = true`). |

---

### BLOCK 2: Behebung des „`BUILDING` / `zZ`“-Freeze-Bugs & Kollisions-Separation

| Bug / Symptom | Root Cause (Technische Ursache) | Betroffene Dateien & Funktionen | Implementierte Lösung |
|---|---|---|---|
| **„86% / 91% Gebäude-Zustands-Freeze“** | Fertige Gebäude verloren durch Wetter/Verschleiß HP (z. B. auf 91% oder 86%). Die Utility-AI interpretierte `hp < maxHp` fälschlicherweise als unfertige Baustelle (`constructionProgress < 100%`) und wies allen Figuren `Status: BUILDING` zu. Da das Gebäude jedoch `finished == true` war, arbeiteten die Figuren nicht weiter. Gleichzeitig renderten Hütten mit Bewohnern dauerhaft `zZ` über den Köpfen der Arbeiter. | `include/buildings/building.h`<br>`src/buildings/building.cpp`<br>`NeedsWork()`, `NeedsRepair()`<br>`src/ai/utility_ai.cpp`<br>`ScoreBuild()`<br>`src/entities/caveman.cpp`<br>`DrawFloatingEmote()`, `UpdateSleeping()` | 1. **Strikte Trennung von `constructionProgress` und `condition/HP`:** Sobald ein Bauprojekt 100% erreicht, wird `finished = true` unumkehrbar gesetzt.<br>2. **Reparatur-Gate (< 50% HP):** Ein fertiges Gebäude generiert erst dann eine Reparaturaufgabe, wenn `hp < maxHp * 0.50f` und `builders == 0` (maximal **1 einziger Handwerker** zugelassen). Die Reparatur stellt 100% HP innerhalb von 2–3s wieder her.<br>3. **`zZ`-Icon strikt an Schlaf gebunden:** `zZ` wird ausschließlich gerendert, wenn `state == Sleeping` UND `tiredness > 0`. Sobald `tiredness <= 5.0f` erreicht ist, wacht die Figur sofort auf. Das permanente `zZ` über Hütten wurde komplett entfernt. |
| **Figuren-Stacking am Lagerfeuer (10 Figuren auf 1 Kachel)** | Wartende und wärmende Figuren liefen exakt auf die Mittelpunkt-Koordinate des Lagerfeuers und überlappten sich vollständig. | `src/entities/caveman.cpp`<br>`Update()`<br>`StartAction(WarmingUp)` | 1. **Sanfte Repulsion:** Wenn zwei Figuren näher als 8.0 Pixel zueinander stehen, wirkt eine weiche Abstoßungskraft.<br>2. **Kreisförmige Feuer-Verteilung:** Figuren verteilen sich beim Aufwärmen deterministisch auf die 8 umliegenden Nachbarkacheln (`offset = angle(id) * 1.5 TILE_SIZE`). |

---

### BLOCK 3: Biologische Logik & Behebung aller `chat_log.txt`-Bugs

| Bug / Symptom | Root Cause (Technische Ursache) | Betroffene Dateien & Funktionen | Implementierte Lösung |
|---|---|---|---|
| **Unbiologische Geburten, Inzest & 16s-Babyboom** | Zwei männliche Jäger (`#32` & `#33`) zeugten alle 16 Sekunden ein Kind (22 Kinder); Nachkommen gingen Partnerschaften mit direkten Geschwistern ein. | `include/entities/caveman.h`<br>`src/entities/caveman.cpp`<br>`src/social/social_actions.cpp`<br>`ScoreFlirt()`<br>`src/simulation.cpp`<br>`UpdateDemographics()` | 1. **Biologische Paarung:** Fortpflanzung erfordert strikt 1x Mann und 1x Frau (`partnerA.gender != partnerB.gender`).<br>2. **Inzest-Schutz:** Speicherung von `motherId_` und `fatherId_`. Figuren mit gleichen Eltern oder Eltern-Kind-Beziehung können keine Partner werden.<br>3. **Schwangerschafts-Cooldown & Limit:** Nach einer Geburt erhält die Mutter einen Cooldown von 210 Sekunden (`pregnancyCooldown_`). Maximal 3–4 Kinder pro Paar. |
| **`Kulturelle Lehre`-Dauerspam (alle 1,4s)** | `ScoreTeachChild` besaß keinen Cooldown und gab bei jedem Frame hohe Utility. | `include/entities/caveman.h`<br>`src/social/social_actions.cpp`<br>`ScoreTeachChild()`, `ExecuteTeachChild()` | Sowohl Lehrer als auch Kind erhalten 120s Cooldown (`teachCooldown_`). Nach maximal 2 Lektionen (`lessonsReceived_ >= 2`) ist das Kind voll geprägt. |
| **„Kind #36“ bleibt als Erwachsener ein „Kind“** | Bei Erreichen von 16 Jahren wurde zwar die Rolle auf Jäger/Sammler gesetzt, aber der Name im String unverändert gelassen. | `src/entities/caveman.cpp`<br>`UpdateAging()` | Mit Vollendung des 16. Lebensjahres wird der Name automatisch umbenannt (z. B. aus `"Kind #36"` wird `"Jäger #36"` bzw. `"Sammlerin #36"`). |
| **Vorratskammer ohne Verderb-Schutz & Log-Spam** | Der Verderb-Timer ignorierte das Vorhandensein einer Vorratskammer; Warnmeldungen spamten alle 16s. | `src/entities/tribe.cpp`<br>`UpdateFoodSpoilage()` | Eine fertige `Pantry` senkt die Verderbrate um **88,5%** (Intervall von 16s auf 140s angehoben). Warnungen werden bei existierender Pantry stummgeschaltet und sonst auf alle 120s gedrosselt. |
| **Suizidales Schisma bei Erfrierung** | Figuren mit -4 HP im Kältetod spalteten sich ab und erfroren wenige Sekunden später. | `src/simulation.cpp`<br>`CheckAndExecuteSecession()` | Schisma erfordert strikt: Rebell und Begleiter müssen gesund und versorgt sein (`Hp >= 70`, `Warmth >= 50`, `Hunger <= 50`). |
| **Geister-Stämme im UI & Karte nach Auslöschung** | Ausgestorbene Stämme behielten Top-HUD-Buttons und Welt-Banner. | `src/entities/tribe.cpp`<br>`Draw()`<br>`src/simulation.cpp`<br>`DrawUI()` | Sobald ein Clan 0 erwachsene Mitglieder hat, wird er auf `isDisbanded_ = true` gesetzt, das Welt-Banner ausgeblendet und der Button aus dem Top-HUD entfernt. |
| **Ertrinken in Endlosschleife** | Schwimmer mit erschöpften Aktionspunkten (AP) scheiterten an der A*-Pfadsuche im tiefen Wasser. | `src/entities/caveman.cpp`<br>`UpdateNeeds()` | **Wasser-Notfall-Rettung:** Sinkt die Ausdauer beim Schwimmen auf `AP < 25%`, bewegt sich die Figur per direktem Vektor ohne A*-Blocking zur nächstgelegenen Landkachel. |

---

### BLOCK 4: Radikale UI-Entrümpelung, Dorf-Layout & Detailreiches 2.5D-Rendering

| Bereich | Vorgabe | Betroffene Dateien | Umgesetzte Lösung |
|---|---|---|---|
| **Clean Map View (Kein Text-Müll)** | Alle permanenten Textbanner (`Notunterkunft (91%)`, `Feuerkreis`, `Vorratskammer`, `Werkbank`, `Ahnen-Steingrab`, `Ackerfeld`, `ERNTE`, `KALT`, `WOLLE`) entfernen. | `src/buildings/building.cpp`<br>`src/entities/tribe.cpp`<br>`src/progression/poi.cpp` | **Hover-Only-Prinzip:**<br>1. Gebäude-, POI- und Grabstein-Namen und Zustände erscheinen **ausschließlich** bei direktem Mouse-Hover ($\le 1.2$ Kacheln).<br>2. Auf der Weltkarte verbleibt lediglich das dezente Clan-Namensschild über dem TownCenter lebender Stämme.<br>3. Bei aktiven Baustellen wird ein winziger, textloser grüner Fortschrittsbalken ($16 \times 3$ Pixel) gezeichnet.<br>4. Reife Felder und kalte Feuer werden rein visuell dargestellt. |
| **Dorf-Architektur & Friedhofs-Sektor** | Mindestabstand von 3 Kacheln; Gräber an den Dorfrand. | `include/entities/tribe.h`<br>`src/entities/tribe.cpp`<br>`FindBuildingSpotAround()`, `FindGraveyardSpot()` | 1. `FindBuildingSpotAround`: Mindestabstand von $3.0$ Kacheln zu allen bestehenden Gebäuden erzwungen.<br>2. `FindGraveyardSpot`: Ahnengräber und Megalithen werden geordnet in einem peripheren Friedhofssektor (8–14 Kacheln vom Dorfzentrum) platziert. |
| **2.5D-Gebäudegrafiken nach Typ & Epoche** | Eindeutige, detailreiche Pixel-Sprites für jede Ausbaustufe. | `src/buildings/building.cpp` | 1. **Wohnhütten:** Stufe 1 Fellzelt mit Holzstangen $\rightarrow$ Stufe 2 Blockhaus mit Stroh-Giebeldach $\rightarrow$ Stufe 3 hellgraues Steinhaus mit Ziegeldach, Fenster und rauchendem Kamin.<br>2. **Stammeszentrum:** Stufe 1 Steinring mit Lagerfeuer $\rightarrow$ Stufe 2 Großes Langhaus mit Clan-Bannern $\rightarrow$ Stufe 3 Stein-/Fachwerk-Rathaus mit gepflastertem Vorplatz und Flaggen.<br>3. **Spezialgebäude:** Werkbank mit Amboss, Sägebock und Holzstapeln; Vorratskammer als Pfahlbau mit Töpfen; Lehm- und Rennofen mit Glüheffekt; Ackerfeld mit Furchen und reifem Getreide; Tiergehege mit Holzzaun und weidenden Tieren. |
| **Differenzierte Menschen, Kleidung & Waffen** | Klare visuelle Unterscheidung ohne Text nach Geschlecht, Alter, Ausrüstung. | `include/gfx/sprite_sheet.h`<br>`src/gfx/sprite_sheet.cpp`<br>`include/entities/caveman.h`<br>`src/entities/caveman.cpp`<br>`Draw()`, `DrawClothingAndGear()` | 1. **Phänotypen:** Frauen haben schmalere Silhouette, langes Haar mit Clan-Haarband und **niemals einen Bart**; Männer mit Bartoption; Älteste mit silbergrauem Haar, Bart und Holzstab; Kinder auf $65\%$ skaliert ohne Waffen.<br>2. **Fackeln:** Werden strikt nur nachts (`isDark`) in der Hand gerendert.<br>3. **Kleidungs-Tiers:** Unbekleidet (Lendenschurz), Rohfell (zottelig über Schulter), Leder (helle Tunika mit Gürtel), Clan-Tunika (Gewebe + Umhang in Clan-Farbe), Metallrüstung (Silberpanzer, Schulterplatten, Helm).<br>4. **Werkzeuge & Waffen:** Faustkeil (Stein), Steinaxt mit Stiel, Kompositbogen mit Rückenköcher, Bronzewerkzeug (goldenes Beil), Eisenschwert mit Parierstange und Clan-Rundschild; geflochtener Tragekorb auf dem Rücken bei Ressourcentransport. |

---

### BLOCK 5: Verifikations- und Build-Ergebnisse

1. **Compiler-Test:**
   ```bash
   make clean && make -j$(sysctl -n hw.ncpu)
   ```
   Ergebnis: **0 Fehler, 0 Warnungen**.
2. **UI-Inspektion:**
   - Keine permanenten Textboxen auf der Karte vorhanden (alles auf Mouse-Hover $\le 1.2$ Kacheln umgestellt).
   - Saubere Clan-Banner über den Dorfzentren lebender Stämme.
   - Baustellen werden mit minimalen $16 \times 3$ Pixel grünen Fortschrittsbalken gerendert.
3. **Gameplay-Simulation:**
   - Technologiebaum sperrt Spalte 2 & 3 strikt hinter Meilensteinen.
   - Stämme bauen ihre Gebäude über die Zeitalter hinweg auf Stufe 2 und 3 aus.
   - Kein Freeze mehr bei beschädigten Gebäuden; Handwerker reparieren Gebäude unter 50% HP in wenigen Sekunden.
   - Biologische Fortpflanzung mit Inzestsperre, Cooldown und korrekten Rollennamen ab 16 Jahren.
   - Gestrandete Schwimmer retten sich bei sinkenden Aktionspunkten ans Ufer.
