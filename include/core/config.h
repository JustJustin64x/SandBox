#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
//  Zentrale Spielbalance. Alle Werte sind bewusst hier gebündelt, damit man
//  die Simulation ohne Eingriff in die Logik feinjustieren kann.
//  Zeitangaben in Sekunden, Raten pro Sekunde, Distanzen in Welt-Pixeln.
// ============================================================================
namespace cfg {

// --- Allgemein --------------------------------------------------------------
constexpr float kMaxStat       = 100.0f;
constexpr float kThinkInterval = 0.5f;    // Abstand zwischen KI-Entscheidungen
constexpr float kInteractRange = 12.0f;   // Reichweite für Nahkampf / Interaktion
constexpr float kMaxSimStep    = 0.05f;   // größter Simulationsschritt (Sub-Stepping)

// --- Bedürfnisse ------------------------------------------------------------
// Hunger ist ein BEDÜRFNIS: 0 = satt, 100 = verhungernd (steigt über Zeit).
// "Hunger > 70" bedeutet also: nur noch 30 % Sättigung übrig.
constexpr float kHungerPerSecond           = 0.9f;
constexpr float kTirednessPerSecond        = 0.65f;
constexpr float kWorkTirednessPerSecond    = 0.5f;   // zusätzlich während Arbeit
constexpr float kStarvationDamagePerSecond = 2.5f;   // HP-Verlust bei Hunger = 100
constexpr float kHungerThreshold           = 70.0f;  // ab hier: Nahrung suchen
constexpr float kHungerCritical            = 90.0f;  // weckt Schlafende auf
constexpr float kSatedHunger               = 10.0f;  // hört auf zu essen
constexpr float kTirednessThreshold        = 80.0f;  // ab hier: schlafen gehen
constexpr float kApMinForWork              = 10.0f;  // darunter: erst ausruhen

// --- Schlaf -----------------------------------------------------------------
constexpr float kSleepTirednessRecovery = 6.0f;   // pro Sekunde (unter freiem Himmel)
constexpr float kSleepApRegen           = 8.0f;   // pro Sekunde
constexpr float kHutSleepBonus          = 1.6f;   // Multiplikator in einer Hütte
constexpr float kSleepHungerFactor      = 0.35f;  // Hunger steigt im Schlaf langsamer
constexpr float kSleepHpRegen           = 1.0f;   // HP-Heilung im Schlaf (wenn nicht hungrig)
constexpr float kWakeTiredness          = 5.0f;
constexpr float kWakeAp                 = 90.0f;

// --- Essen ------------------------------------------------------------------
constexpr float kEatInterval          = 0.8f;   // Sekunden pro verzehrter Nahrungseinheit
constexpr float kHungerPerFood        = 22.0f;  // so viel Hunger stillt 1 Nahrung
constexpr int   kFoodReservePerMember = 2;      // Vorratsziel: Jäger/Sammler füllen auf

// --- Arbeit: AP-Kosten pro Sekunde ------------------------------------------
constexpr float kApCostHunt       = 6.0f;
constexpr float kApCostGather     = 3.0f;
constexpr float kApCostMaterials  = 4.0f;
constexpr float kApCostBuild      = 5.0f;
constexpr float kLowApWorkFactor  = 0.4f;   // Effizienz, wenn AP = 0 (nur Notfall-Arbeit)

// --- Ernte ------------------------------------------------------------------
constexpr float kGatherFoodDuration = 3.0f;
constexpr float kChopWoodDuration   = 4.0f;
constexpr float kMineStoneDuration  = 5.0f;
constexpr int   kBerryYield         = 3;
constexpr int   kWoodYield          = 3;
constexpr int   kStoneYield         = 2;

// --- Jagd -------------------------------------------------------------------
constexpr float kAnimalHp            = 30.0f;
constexpr int   kAnimalMeat          = 6;
constexpr int   kAnimalCounterChance = 20;    // % Chance pro Sekunde auf Gegenangriff
constexpr int   kAnimalDamageMin     = 4;
constexpr int   kAnimalDamageMax     = 10;
constexpr float kAnimalSpeed         = 16.0f;
constexpr float kAnimalRespawnTime   = 25.0f;

// --- Nachwachsen von Ressourcen (Sekunden pro Einheit) ----------------------
constexpr float kTreeRegrowTime = 30.0f;
constexpr float kBushRegrowTime = 9.0f;
constexpr float kRockRegrowTime = 60.0f;

// --- Hütten -----------------------------------------------------------------
constexpr int   kHutWoodCost        = 6;
constexpr int   kHutStoneCost       = 3;
constexpr int   kHutCapacity        = 3;     // Schlafplätze pro Hütte
constexpr float kBuildRatePerSecond = 5.0f;  // Fortschritt in % pro Sekunde & Bauarbeiter

// --- Startbedingungen -------------------------------------------------------
constexpr int kStartFood  = 12;
constexpr int kStartWood  = 6;
constexpr int kStartStone = 3;

constexpr int kStartHunters   = 3;
constexpr int kStartGatherers = 3;
constexpr int kStartElders    = 2;
constexpr int kStartChildren  = 2;

// --- Weltbevölkerung (Ressourcen-Objekte) -----------------------------------
constexpr int kTreeCount   = 80;
constexpr int kBushCount   = 40;
constexpr int kRockCount   = 28;
constexpr int kAnimalCount = 14;

// --- Wärme & Kälte ---------------------------------------------------------
constexpr float kWarmthLossDay            = 0.3f;   // pro Sekunde tagsüber
constexpr float kWarmthLossNight          = 2.2f;   // pro Sekunde nachts
constexpr float kWarmthRecoverNearFire    = 8.0f;   // am Lagerfeuer
constexpr float kWarmthRecoverInShelter   = 12.0f;  // in einer Hütte
constexpr float kFreezingThreshold        = 25.0f;  // ab hier: dringend Schutz suchen
constexpr float kColdDamagePerSecond      = 2.0f;   // Schaden wenn Wärme = 0

// --- Veredelung an der Werkbank ---------------------------------------------
constexpr float kCraftDuration            = 3.5f;   // Sekunden pro Balken / Ziegel
constexpr float kApCostCraft              = 3.5f;

// --- Tag-Nacht-Zyklus -------------------------------------------------------
constexpr float kDayCycleDuration         = 240.0f; // Sekunden für vollen Tag-Nacht-Zyklus

// --- Historisch skalierte Zeitalter-Dauer (Time-Gating) ----------------------
// Reale Epochen skaliert: Steinzeit am längsten (3 Min), Bronzezeit (2.5 Min), Eisenzeit (2 Min).
constexpr float kStoneAgeDuration         = 360.0f; // Sekunden Mindestdauer für Steinzeit
constexpr float kBronzeAgeDuration        = 300.0f; // Sekunden Mindestdauer für Bronzezeit
constexpr float kIronAgeDuration          = 240.0f; // Sekunden Mindestdauer für Eisenzeit

// --- Ausrüstung & Expedition ------------------------------------------------
constexpr float kExpeditionDuration       = 12.0f;   // Sekunden Erkundungsdauer vor Ort am POI
constexpr float kExpeditionTorchLight     = 55.0f;  // Lichtradius einer Fackel bei Nacht
constexpr float kExpeditionMoraleBoost    = 30.0f;  // Morale-Bonus nach erfolgreicher Expedition
constexpr int   kTorchWoodCost            = 1;      // Holzbedarf für eine Fackel
constexpr int   kPickaxeWoodCost          = 1;      // Holzbedarf für eine Steinspitzhacke
constexpr int   kPickaxeStoneCost         = 1;      // Steinbedarf für eine Steinspitzhacke

// --- Animation --------------------------------------------------------------
constexpr float kWalkAnimFps      = 8.0f;
constexpr float kWorkAnimInterval = 0.35f;

} // namespace cfg

#endif // CONFIG_H

