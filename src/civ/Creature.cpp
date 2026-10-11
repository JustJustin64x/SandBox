#include "Creature.h"
#include "Settlement.h"
#include "world/world.h"
#include "core/event_log.h"
#include "gfx/sprite_sheet.h"

#include <cmath>
#include <algorithm>

Creature::Creature(int id, const std::string& name, Vector2 startPosition)
    : id_(id), name_(name), position_(startPosition), targetPosition_(startPosition) {
}

const char* Creature::StateToString(CreatureState state) {
    switch (state) {
        case CreatureState::IDLE:         return "Untätig (Wartet auf Aufgabe)";
        case CreatureState::SEEKING_FOOD: return "Nahrungssuche (Überlebensdrang)";
        case CreatureState::WORKING:      return "Arbeitet an Dorfaufgabe";
        case CreatureState::RESTING:      return "Ruht / Schläft (Erschöpfung)";
        case CreatureState::DEAD:         return "Verstorben";
    }
    return "Unbekannt";
}

std::string Creature::ActivityDescription() const {
    if (state_ == CreatureState::DEAD) return "Tot";
    if (state_ == CreatureState::SEEKING_FOOD) return "Hungrig! Sucht Nahrung im Dorflager.";
    if (state_ == CreatureState::RESTING) return "Erschöpft. Regeneriert Energie im Schlaf.";
    if (state_ == CreatureState::WORKING && currentTask_) {
        if (currentTask_->type == TaskType::GATHER_RESOURCE) {
            if (carriedAmount_ > 0) {
                return TextFormat("Bringt %d x %s zum Dorflager", carriedAmount_,
                                  CivilizationTech::ResourceToString(carriedResource_));
            }
            return TextFormat("Sammelt %s", CivilizationTech::ResourceToString(currentTask_->resource));
        }
        if (currentTask_->type == TaskType::CONSTRUCT_BUILDING) {
            return "Arbeitet an Baustelle";
        }
    }
    return "Wartet im Dorf auf neue Aufgaben";
}

void Creature::AssignTask(std::shared_ptr<Task> task) {
    if (state_ == CreatureState::DEAD || state_ == CreatureState::SEEKING_FOOD || state_ == CreatureState::RESTING) {
        return; // Überleben hat Vorrang: hungrige/müde Creatures nehmen keine Arbeit an
    }
    currentTask_ = task;
    state_ = CreatureState::WORKING;
    targetPosition_ = task->targetPosition;
}

void Creature::ReleaseTask(Settlement& settlement) {
    if (currentTask_) {
        settlement.ReleaseTask(currentTask_->id);
        currentTask_ = nullptr;
    }
}

void Creature::TakeDamage(float amount, const char* reason, EventLog* log) {
    if (IsDead()) return;

    health_ -= amount;
    if (health_ <= 0.0f) {
        health_ = 0.0f;
        state_ = CreatureState::DEAD;
        if (log) {
            const char* cause = reason ? reason : "unbekannte Ursache";
            log->Add(TextFormat("%s ist gestorben (%s)!", name_.c_str(), cause), RED);
        }
    }
}

void Creature::Heal(float amount) {
    if (IsDead()) return;
    health_ = std::min(CreatureConfig::MAX_HEALTH, health_ + amount);
}

void Creature::MoveTowards(Vector2 target, float speed, float dt) {
    float dist = Vector2Distance(position_, target);
    if (dist < 2.0f) {
        position_ = target;
        return;
    }

    Vector2 dir = Vector2Scale(Vector2Subtract(target, position_), 1.0f / dist);
    position_ = Vector2Add(position_, Vector2Scale(dir, speed * dt));
}

void Creature::EvaluateNeedsAndState(Settlement& settlement) {
    if (IsDead()) return;

    // =========================================================================
    // FSM-ENTSCHEIDUNGSLOGIK MIT KLAREN PRIORITÄTEN
    // =========================================================================

    // 1. PRIORITÄT: VERHUNGERN / DRINGENDER HUNGER (> 70%)
    if (hunger_ >= CreatureConfig::HUNGER_URGENT_LEVEL) {
        if (state_ != CreatureState::SEEKING_FOOD) {
            ReleaseTask(settlement); // Arbeit sofort abbrechen!
            state_ = CreatureState::SEEKING_FOOD;
            targetPosition_ = settlement.StockpilePosition();
        }
        return;
    }

    // 2. PRIORITÄT: ERSCHÖPFUNG / DRINGENDE ENERGIE (< 20%)
    if (energy_ <= CreatureConfig::ENERGY_LOW_LEVEL) {
        if (state_ != CreatureState::RESTING) {
            ReleaseTask(settlement); // Arbeit sofort abbrechen!
            state_ = CreatureState::RESTING;
            targetPosition_ = settlement.FindRestingSpot(position_);
        }
        return;
    }

    // 3. BEDÜRFNIS-AUFLÖSUNG (Wiederaufnahme der normalen Aktivität)
    if (state_ == CreatureState::SEEKING_FOOD) {
        // Creature ist wieder ausreichend gesättigt
        if (hunger_ <= CreatureConfig::HUNGER_SATISFIED_LEVEL) {
            state_ = CreatureState::IDLE;
        }
        return;
    }

    if (state_ == CreatureState::RESTING) {
        // Creature hat wieder genügend Energie getankt
        if (energy_ >= CreatureConfig::ENERGY_RESTED_LEVEL) {
            state_ = CreatureState::IDLE;
        }
        return;
    }

    // 4. ARBEITS-ABSCHLUSS: Wenn Task ungültig/abgeschlossen ist -> IDLE
    if (state_ == CreatureState::WORKING) {
        if (!currentTask_ || currentTask_->isCompleted) {
            currentTask_ = nullptr;
            state_ = CreatureState::IDLE;
        }
    }
}

void Creature::ProcessStateBehavior(float dt, Settlement& settlement, const World& world, EventLog* log) {
    (void)world;
    if (IsDead()) return;

    // Grundbedürfnis-Verfall über Zeit
    hunger_ = std::min(CreatureConfig::MAX_HUNGER, hunger_ + CreatureConfig::HUNGER_INCREASE_RATE * dt);

    // Verhungern: Wenn Hunger das Maximum (100) erreicht, erleidet die Creature Schaden
    if (hunger_ >= CreatureConfig::MAX_HUNGER) {
        TakeDamage(CreatureConfig::STARVATION_DAMAGE_RATE * dt, "Verhungert", log);
        if (IsDead()) return;
    }

    // -------------------------------------------------------------------------
    // ZUSTANDS-AUSFÜHRUNG
    // -------------------------------------------------------------------------
    switch (state_) {
        case CreatureState::SEEKING_FOOD: {
            // Geht zum Dorflager, um Nahrung zu essen
            MoveTowards(targetPosition_, moveSpeed_, dt);
            float distToStock = Vector2Distance(position_, settlement.StockpilePosition());
            if (distToStock < 10.0f) {
                // Versuche, 1 Nahrung aus dem Dorflager zu konsumieren
                if (settlement.Inventory().Consume(CivResourceType::FOOD, 1)) {
                    hunger_ = std::max(0.0f, hunger_ - CreatureConfig::FOOD_HUNGER_REDUCTION);
                    Heal(15.0f); // Gut genährt regeneriert Lebenspunkte
                } else {
                    // Kein Essen im Lager vorhanden! Notfalls im Wald Beeren suchen
                    hunger_ = std::max(0.0f, hunger_ - (CreatureConfig::FOOD_HUNGER_REDUCTION * 0.5f));
                }
            }
            break;
        }

        case CreatureState::RESTING: {
            // Geht zur Hütte / Schlafplatz
            MoveTowards(targetPosition_, moveSpeed_ * 0.8f, dt);
            float distToBed = Vector2Distance(position_, targetPosition_);
            if (distToBed < 12.0f) {
                // Regeneriert Ausdauer und heilt sich langsam
                energy_ = std::min(CreatureConfig::MAX_ENERGY, energy_ + CreatureConfig::ENERGY_RECOVER_RATE * dt);
                Heal(3.0f * dt);
            }
            break;
        }

        case CreatureState::WORKING: {
            // Arbeit verbraucht Energie
            energy_ = std::max(0.0f, energy_ - CreatureConfig::ENERGY_DRAIN_WORKING * dt);

            if (!currentTask_) {
                state_ = CreatureState::IDLE;
                break;
            }

            if (currentTask_->type == TaskType::GATHER_RESOURCE) {
                if (carriedAmount_ == 0) {
                    // Phase 1: Zur Ressource laufen
                    MoveTowards(currentTask_->targetPosition, moveSpeed_, dt);
                    if (Vector2Distance(position_, currentTask_->targetPosition) < 8.0f) {
                        // Phase 2: Abbau-Arbeit leisten
                        currentTask_->currentProgress += 35.0f * dt;
                        if (currentTask_->currentProgress >= currentTask_->requiredProgress) {
                            // Rohstoff aufgenommen!
                            carriedResource_ = currentTask_->resource;
                            carriedAmount_ = CreatureConfig::CARRY_CAPACITY;
                            targetPosition_ = settlement.StockpilePosition();
                        }
                    }
                } else {
                    // Phase 3: Mit Ware zurück zum Dorflager laufen
                    MoveTowards(settlement.StockpilePosition(), moveSpeed_ * 0.9f, dt);
                    if (Vector2Distance(position_, settlement.StockpilePosition()) < 10.0f) {
                        // Ressource im Lager abliefern
                        settlement.CompleteTask(currentTask_->id, carriedResource_, carriedAmount_, log);
                        carriedAmount_ = 0;
                        currentTask_ = nullptr;
                        state_ = CreatureState::IDLE;
                    }
                }
            } else if (currentTask_->type == TaskType::CONSTRUCT_BUILDING) {
                // Zur Baustelle laufen und bauen
                MoveTowards(currentTask_->targetPosition, moveSpeed_, dt);
                if (Vector2Distance(position_, currentTask_->targetPosition) < 12.0f) {
                    bool finished = settlement.AddBuildProgress(currentTask_->buildingId, 16.0f * dt, log);
                    if (finished) {
                        settlement.CompleteTask(currentTask_->id, CivResourceType::WOOD, 0, log);
                        currentTask_ = nullptr;
                        state_ = CreatureState::IDLE;
                    }
                }
            }
            break;
        }

        case CreatureState::IDLE: {
            // Sanftes Umherwandern im Dorfzentrum
            energy_ = std::min(CreatureConfig::MAX_ENERGY, energy_ + 1.0f * dt);
            idleWanderTimer_ -= dt;
            if (idleWanderTimer_ <= 0.0f) {
                idleWanderTimer_ = 3.0f + (float)(rand() % 4);
                Vector2 center = settlement.CenterPosition();
                targetPosition_ = { center.x + (float)((rand() % 60) - 30), center.y + (float)((rand() % 60) - 30) };
            }
            MoveTowards(targetPosition_, moveSpeed_ * 0.5f, dt);
            break;
        }

        case CreatureState::DEAD:
            break;
    }
}

void Creature::Update(float dt, Settlement& settlement, const World& world, EventLog* log) {
    if (IsDead()) return;

    // 1. Zuerst Bedürfnisse und Zustandswechsel evaluieren (Überleben vor Arbeit)
    EvaluateNeedsAndState(settlement);

    // 2. Danach das Verhalten des aktuellen Zustands ausführen
    ProcessStateBehavior(dt, settlement, world, log);
}

void Creature::Draw(const SpriteSheet* spriteSheet, bool isSelected) const {
    if (IsDead()) return;

    (void)spriteSheet;

    // Basis-Kreis für die Kreatur
    Color bodyColor = SKYBLUE;
    switch (state_) {
        case CreatureState::SEEKING_FOOD: bodyColor = ORANGE; break;
        case CreatureState::WORKING:      bodyColor = LIME; break;
        case CreatureState::RESTING:      bodyColor = PURPLE; break;
        case CreatureState::IDLE:         bodyColor = SKYBLUE; break;
        case CreatureState::DEAD:         bodyColor = DARKGRAY; break;
    }

    DrawCircle((int)position_.x, (int)position_.y, 5.0f, bodyColor);
    DrawCircleLines((int)position_.x, (int)position_.y, 5.0f, RAYWHITE);

    // Wenn selektiert: gelber Auswahlring und Name
    if (isSelected) {
        DrawCircleLines((int)position_.x, (int)position_.y, 9.0f, GOLD);
        DrawText(name_.c_str(), (int)position_.x - 15, (int)position_.y - 18, 10, GOLD);
    }

    // Wenn Ware getragen wird: kleiner Indikatorpunkt
    if (carriedAmount_ > 0) {
        Color resColor = YELLOW;
        if (carriedResource_ == CivResourceType::WOOD)   resColor = BROWN;
        if (carriedResource_ == CivResourceType::STONE)  resColor = GRAY;
        if (carriedResource_ == CivResourceType::COPPER) resColor = ORANGE;
        if (carriedResource_ == CivResourceType::IRON)   resColor = DARKBLUE;
        DrawCircle((int)position_.x + 5, (int)position_.y - 4, 3.0f, resColor);
    }
}
