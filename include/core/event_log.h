#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include "common.h"

#include <cstddef>
#include <deque>
#include <string>

// Ein Eintrag im Ereignisprotokoll (wird im UI unten links angezeigt).
struct LogEntry {
    std::string text;
    Color color;
    float timestamp;   // Simulationszeit in Sekunden
};

// Kleines Ereignisprotokoll: hält die letzten N Ereignisse und spiegelt sie
// zusätzlich auf die Konsole (hilfreich zum Debuggen der KI).
class EventLog {
public:
    void SetTime(float simTime) { time_ = simTime; }

    void Add(const std::string& text, Color color = RAYWHITE) {
        entries_.push_back({ text, color, time_ });
        while (entries_.size() > kMaxEntries) entries_.pop_front();
        std::printf("[%7.1fs] %s\n", time_, text.c_str());
        std::fflush(stdout);
    }

    const std::deque<LogEntry>& Entries() const { return entries_; }

private:
    static constexpr std::size_t kMaxEntries = 8;
    std::deque<LogEntry> entries_;
    float time_ = 0.0f;
};

#endif // EVENT_LOG_H

