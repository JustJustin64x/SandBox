# Analyse-Bericht: Ursachen für das rasche Massensterben der Humans

## 1. Executive Summary & Problem-Diagnose
Das Aussterben der Clans nach wenigen Spielminuten basiert **nicht auf einem einzelnen Programmabsturz oder Engine-Fehler**, sondern auf einer **systemischen Imbalance** mehrerer überlappender Gameplay- und Simulationssysteme. 

Todesursachen treten exponentiell schnell ein, während gleichzeitig die Überlebensmechanismen (Nahrungssuche, Feuerholz-Logistik), die Lebenspunkte-Regeneration und vor allem die Fortpflanzung durch extrem harte Schwellenwerte blockiert sind. Ein Stamm stirbt im Regelfall innerhalb von **60 bis 180 Sekunden** (bei 1x Geschwindigkeit) vollständig aus.

---

## 2. Detaillierte Ursachenanalyse

### Ursache 1: Extrem beschleunigte Alterung & hohes Startalter (Altersschwäche)
* **Code-Fundstelle:** `src/entities/caveman.cpp` (Zeile 240) & `include/core/config.h` (Zeile 109)
* **Formel:**
  ```cpp
  ageYears_ += dt * 0.20f;
  ```
* **Mathematischer Effekt:**
  - Jede reale Sekunde altern Menschen um **0,20 Jahre**.
  - In 1 Minute vergehen **12 Lebensjahre**.
  - In 5 Minuten vergehen **60 Lebensjahre**.
* **Start-Bedingungen:**
  - Beim Spawnen einer Welt werden Figuren nach Alterskategorien verteilt (`World::GenerateSpawnWorld` / `Tribe::Initialize`). Erwachsene spawnen mit 18–35 Jahren, Älteste (`Elder`) mit 56–66 Jahren.
  - Die maximale Lebensspanne wird bei der Geburt/beim Spawnen festgelegt:
    ```cpp
    maxLifespan_ = static_cast<float>(GetRandomValue(74, 86));
    ```
* **Konsequenz:**
  - Ein mit 66 Jahren gespawnter Stammesältester stirbt bereits nach **40 bis 50 Sekunden** an Altersschwäche (`"Altersschwäche"`).
  - Ein 35-jähriger Erwachsener erreicht das Lebensende nach **3,25 Minuten** (195 Sekunden). Bei 2x oder 4x Simulationsgeschwindigkeit stirbt die gesamte Startpopulation innerhalb von **45 bis 90 Sekunden** ohne Chance auf Nachwuchs.

---

### Ursache 2: Rasanter Hungertod & fehlende Startnahrung
* **Code-Fundstelle:** `include/core/config.h` (Zeile 92ff.), `src/entities/caveman.cpp` (Zeile 270ff.)
* **Metriken:**
  - `kHungerPerSecond = 0.9f`: Ein Mensch verbraucht pro Sekunde fast 1 Hungerpunkt. Von 0 auf 100 Hunger vergehen lediglich **111 Sekunden** (unter 2 Minuten).
  - Bei Hunger = 100 greift der Schadens-Timer:
    ```cpp
    if (hunger_ >= cfg::kMaxStat) {
        starvationTimer_ += dt;
        if (starvationTimer_ >= 2.5f) {
            starvationTimer_ = 0.0f;
            TakeDamage(4.0f, "Hunger");
        }
    }
    ```
  - Hunger fügt **alle 2,5 Sekunden 4 HP Schaden** zu. Nach Erreichen der 100%-Marke stirbt ein Mensch nach genau **62,5 Sekunden** an Verhungerung (`"Hunger"`).
* **Vorrats- und Sättigungs-Diskrepanz:**
  - Jede verzehrte Portion Nahrung regeneriert nur **22 Hungerpunkte** (`kHungerPerFood = 22.0f`). Ein Mensch benötigt somit ca. 5 Einheiten Nahrung, um von 100 auf 0 zu kommen.
  - Stämme starten im `TownCenter` mit **0 roher Nahrung und nur 2 gekochten Mahlzeiten** für 3 bis 5 Stammesmitglieder.
  - Bis Sammler Büsche gefunden, abgeerntet und Nahrung zurückgetragen haben, sind bereits alle Vorräte aufgebraucht und die ersten Schadenszyklen aktiv.

---

### Ursache 3: Tödliche Unterkühlung (Erfrieren bei Nacht & Unwetter)
* **Code-Fundstelle:** `include/core/config.h` (Zeile 96ff.), `src/world/environmental_system.cpp`, `src/entities/caveman.cpp` (Zeile 300ff.)
* **Temperaturabfall:**
  - Basis-Wärmeverlust bei Nacht: `kWarmthLossNight = 2.2f / s`.
  - Bei Regen oder Gewitter sinkt die Umgebungswärme zusätzlich um bis zu `-2.2f / s`.
  - Ein Mensch verliert nachts seine kompletten 100 Wärmepunkte in **22 bis 45 Sekunden**.
* **Kälteschaden & Lagerfeuer-Versagen:**
  - Sobald `warmth_ <= 0.0f` fällt:
    ```cpp
    coldDamageTimer_ += dt;
    if (coldDamageTimer_ >= 2.5f) {
        coldDamageTimer_ = 0.0f;
        TakeDamage(4.0f, "Unterkuehlung");
    }
    ```
    (Führt nach weiteren 62,5 Sekunden unweigerlich zum Tod).
  - Unterkühlte Menschen versuchen, sich am TownCenter-Lagerfeuer zu wärmen (`ActionType::WARMING_UP`).
  - **Der Teufelskreis:** Das Lagerfeuer verbraucht kontinuierlich Holz (`woodBurnRate = 0.72f/s`). Das Start-Holzlager (100 Fuel) erlischt nach ca. **138 Sekunden**. Besitzt der Stamm kein Holz mehr im Lager, bricht `Caveman::UpdateWarmingUp` sofort mit `FinishTask()` ab:
    ```cpp
    if (stock.wood <= 0 && tc->GetCampfireFuel() <= 0.0f) {
        FinishTask(); // Feuer aus, keine Rettung
    }
    ```
  - Da Figuren ohne Hütte im Freien schlafen und das Feuer ausgeht, erfrieren oft ganze Stämme in der ersten oder zweiten Nacht.

---

### Ursache 4: Nahezu unmögliche HP-Regeneration (Keine passive Heilung)
* **Code-Fundstelle:** `src/entities/caveman.cpp` (Zeile 650ff.)
* **Funktionsweise der Heilung:**
  - Im Wachzustand existiert **keinerlei Lebenspunkte-Regeneration**.
  - Heilung findet ausschließlich während des Schlafs statt:
    ```cpp
    hp_ = std::min(cfg::kMaxStat, hp_ + cfg::kSleepHpRegen * dt); // kSleepHpRegen = 1.0f
    ```
* **Das Schlaf-Dilemma:**
  - Menschen wachen auf, sobald ihre Müdigkeit wieder auf 0 gesunken ist.
  - Die Müdigkeits-Erholung ist extrem schnell: `kSleepTirednessRecovery = 6.0f / s` (im Freien) bzw. `9.6f / s` (in einer Hütte).
  - Ein vollkommen erschöpfter Mensch (Müdigkeit 75) schläft daher im Schnitt nur **7,8 bis 12,5 Sekunden**.
  - In diesen 8 bis 12 Sekunden regeneriert er lediglich **8 bis 12 HP**!
  - Jeder Schaden aus Tierangriffen, Kälte oder Hunger bleibt nahezu permanent bestehen und summiert sich rasant auf 100 Schadenspunkte.

---

### Ursache 5: Demografische Aussterbe-Falle (Geburtsblockade)
* **Code-Fundstelle:** `src/entities/tribe.cpp` (Funktion `TryReproduce`)
* **Harte Schwellenwerte für Geburten:**
  Eine Fortpflanzung wird nur initiiert, wenn **alle** folgenden Bedingungen gleichzeitig erfüllt sind:
  1. `Buildings().GetBuildingCount(BuildingType::Hut, true) > 0`: Es **muss** mindestens eine fertige Hütte existieren.
  2. `Stock().food >= std::max(6, tribePop + 2)`: Es müssen mindestens 6 bis 8 Einheiten Nahrung im Lager liegen.
  3. `hunger_ <= 65.0f`: Beide Elternteile dürfen nicht hungrig sein.
  4. Ein aktiver Paarungspartner (`GetPartner(id) >= 0`) muss vorhanden sein.
* **Die Blockade:**
  - Stämme starten mit **0 Hütten**, **0 Holz** und **0 Stein**!
  - Um eine Hütte zu bauen, müssen erst Baumeister Holz fällen, Stein abbauen, das Material herantragen und die Bauzeit abschließen.
  - Da die Startfiguren bereits nach 40 bis 180 Sekunden an Alter oder Kälte sterben, wird in den allermeisten Runden vor dem Aussterben **nicht eine einzige Hütte fertiggestellt**.
  - Die Geburtenrate sinkt somit auf exakt **0,0 Kinder**, wodurch das Aussterben der Dynastie mathematisch garantiert ist.

---

### Ursache 6: Ertrinken in Deep Water
* **Code-Fundstelle:** `include/world/world.h` (Definition von `kTileProperties`) & `src/entities/caveman.cpp`
* **Problem:**
  - `TILE_DEEP_WATER` ist mit `isWalkable = true` konfiguriert.
  - Das Pathfinding erlaubt Menschen daher das Durchqueren von Tiefwasser- und Meeres-Kacheln.
  - Im tiefen Wasser sinkt die Ausdauer (`AP`) rapide um 14–21 AP pro Sekunde. Fällt die AP auf 0, zieht `Caveman::Update` alle 1,5 Sekunden **8 HP Ertrinkungs-Schaden** ab (`TakeDamage(8.0f, "Ertrinken")`).
  - Wanderer oder Entdecker, die Flüsse oder Buchten durchqueren, ertrinken oft nach wenigen Kacheln.

---

### Ursache 7: Asymmetrisches Jagd- & Kampfrisiko
* **Code-Fundstelle:** `src/entities/caveman.cpp` (Zeile 780ff.)
* **Ablauf:**
  - Wenn ein Jäger Tiere jagt, besitzt das Tier eine 20%-Konterchance pro Sekunde und verursacht 4 bis 10 Direktschaden.
  - Ein Jäger, der 2–3 Tiere erlegt, hat danach meist nur noch 30–50 HP übrig.
  - Wegen der unter Punkt 4 genannten quasi nicht vorhandenen HP-Regeneration stirbt fast jeder Jäger bei seinem 3. oder 4. Jagdausflug.

---

## 3. Zusammenfassung der Sterberate nach Spielzeit (Timeline)

| Spielzeit (1x Speed) | Ereignis & Todesursachen |
| :--- | :--- |
| **00:00 - 00:30** | Start der Simulation. Nahrung im Lager wird sofort von ersten 2 Menschen verzehrt. Lagerfeuer brennt. |
| **00:40 - 00:55** | **Erste Todeswelle:** Gespawnte Stammesälteste (Elder, Alter 56–66) sterben an Altersschwäche (`ageYears_ >= maxLifespan_`). |
| **01:00 - 01:30** | Lagerfeuer-Holz geht zur Neige. Erste Jäger verunglücken oder nehmen schweren Gegenschaden. Hunger steigt flächendeckend über 80. |
| **01:45 - 02:15** | Erste Nacht bricht an. Lagerfeuer erlischt wegen Holzmangel. Wärme fällt auf 0. **Zweite Todeswelle:** Unterkühlung (`"Unterkuehlung"`). |
| **02:15 - 03:00** | Hunger erreicht 100. Starvation-Ticks setzen ein. **Dritte Todeswelle:** Verhungern (`"Hunger"`). |
| **03:00 - 03:30** | Verbliebene Erwachsene erreichen Alter 75+ und sterben an Altersschwäche. Da keine Hütte gebaut wurde: **0 Neugeborene, Clan vollständig ausgelöscht.** |

---

## 4. Empfohlene Stellschrauben für das Balancing (Referenzwerte)

1. **Alterung drastisch verlangsamen:**
   - `ageYears_ += dt * 0.20f` $\rightarrow$ `ageYears_ += dt * 0.02f` (Faktor 10 verlangsamen: 1 Jahr pro 50 Sekunden statt alle 5 Sekunden).
   - Startalter für gespawnte Stammesmitglieder absenken (Erwachsene: 18–24 Jahre, keine greisen Ältesten beim Spielstart).
2. **Hunger & Vorräte anpassen:**
   - `kHungerPerSecond` von `0.9f` auf `0.25f` senken.
   - Startressourcen erhöhen: TownCenter sollte mit mindestens **15 Nahrung** und **50 Holz** starten.
   - Sättigungswert von Nahrung (`kHungerPerFood`) von `22.0f` auf `45.0f` erhöhen.
3. **Wärme & Nacht mildern:**
   - `kWarmthLossNight` von `2.2f` auf `0.8f` senken.
   - Campfire-Fuel-Verbrauch von `0.72f/s` auf `0.20f/s` reduzieren.
4. **Lebenspunkte-Regeneration ermöglichen:**
   - Passive Lebensregeneration im Wachzustand einführen, wenn Hunger `< 50` und Wärme `> 50` (z. B. `+0.5 HP/s`).
   - `kSleepHpRegen` von `1.0f` auf `6.0f / s` anheben, damit eine 10-sekündige Schlafphase mindestens 60 HP heilt.
5. **Geburtsanforderungen für die erste Generation lockern:**
   - Für die ersten 2 Kinder keine Hütte zwingend voraussetzen, sondern ein aktives Lagerfeuer und Nahrungsvorrat genügen lassen.
6. **Pathfinding & Deep Water:**
   - `TILE_DEEP_WATER` in `kTileProperties` auf `isWalkable = false` setzen, um Massenertrinken zu verhindern.

