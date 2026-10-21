#pragma once

#include "core/common.h"

#include <cstddef>
#include <cstdio>
#include <deque>
#include <string>
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>

// Ein Eintrag im Ereignisprotokoll (wird im UI unten links angezeigt).
struct LogEntry {
    std::string text;
    Color color;
    float timestamp;   // Simulationszeit in Sekunden
};

// Ereignisprotokoll & Echtzeit-Chat-Logger:
// Hält die letzten N Ereignisse für das HUD-Overlay, spiegelt sie auf die Konsole
// und streamt jeden Eintrag in Echtzeit in eine dedizierte Log-Datei ("chat_log.txt").
// Die Datei wird bei jedem Neustart der Simulation frisch angelegt ("trunc"),
// während die vorherige Sitzung als "chat_log_prev.txt" gesichert wird.
// Das Log bleibt dauerhaft bis zum nächsten Ausführen auf der Festplatte gespeichert.
class EventLog {
public:
    inline static EventLog* sInstance_ = nullptr;
    static EventLog* Instance() { return sInstance_; }

    static void AddMessage(const std::string& text, Color color = RAYWHITE) {
        if (sInstance_) {
            sInstance_->Add(text, color);
        } else {
            std::printf("%s\n", text.c_str());
            std::fflush(stdout);
        }
    }

    explicit EventLog(const std::string& logFilePath = "chat_log.txt")
        : logFilePath_(logFilePath) {
        sInstance_ = this;
        OpenLogFile(logFilePath_);
    }

    ~EventLog() {
        if (sInstance_ == this) sInstance_ = nullptr;
        CloseLogFile();
    }

    EventLog(const EventLog&) = delete;
    EventLog& operator=(const EventLog&) = delete;

    EventLog(EventLog&& other) noexcept {
        MoveFrom(std::move(other));
    }

    EventLog& operator=(EventLog&& other) noexcept {
        if (this != &other) {
            CloseLogFile();
            MoveFrom(std::move(other));
        }
        return *this;
    }

    void SetTime(float simTime) { time_ = simTime; }

    void OpenLogFile(const std::string& filePath) {
        if (fileStream_.is_open()) {
            CloseLogFile();
        }
        logFilePath_ = filePath;

        // Vorherige Logdatei als Backup sichern, bevor die neue Sitzung beginnt
        std::remove("chat_log_prev.txt");
        std::rename(logFilePath_.c_str(), "chat_log_prev.txt");

        // Neue Datei im Schreibmodus (trunc) öffnen
        fileStream_.open(logFilePath_, std::ios::out | std::ios::trunc);
        if (fileStream_.is_open()) {
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            char timeBuf[64];
            #if defined(_WIN32)
            struct tm t;
            localtime_s(&t, &now);
            std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &t);
            #else
            struct tm* t = std::localtime(&now);
            if (t) std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", t);
            else std::snprintf(timeBuf, sizeof(timeBuf), "Unbekannte Zeit");
            #endif

            fileStream_ << "================================================================================\n";
            fileStream_ << "Stone Age 2D Sandbox - Simulations-Chat & Ereignisprotokoll\n";
            fileStream_ << "Sitzungsstart: " << timeBuf << "\n";
            fileStream_ << "Logdatei:      " << logFilePath_ << " (Echtzeit-Stream)\n";
            fileStream_ << "================================================================================\n\n";
            fileStream_.flush();

            std::printf("[EventLog] Chat-Log wird in '%s' gestreamt.\n", logFilePath_.c_str());
            std::fflush(stdout);
        }
    }

    void CloseLogFile() {
        if (fileStream_.is_open()) {
            char timeBuf[64];
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            #if defined(_WIN32)
            struct tm t;
            localtime_s(&t, &now);
            std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &t);
            #else
            struct tm* t = std::localtime(&now);
            if (t) std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", t);
            else std::snprintf(timeBuf, sizeof(timeBuf), "Unbekannte Zeit");
            #endif

            fileStream_ << "\n================================================================================\n";
            fileStream_ << "Sitzungsende: " << timeBuf << " (Simulationszeit: " << std::fixed << std::setprecision(1) << time_ << "s)\n";
            fileStream_ << "================================================================================\n";
            fileStream_.flush();
            fileStream_.close();
        }
    }

    void Add(const std::string& text, Color color = RAYWHITE) {
        entries_.push_back({ text, color, time_ });
        while (entries_.size() > kMaxEntries) entries_.pop_front();

        // 1. Terminal-Ausgabe
        std::printf("[%7.1fs] %s\n", time_, text.c_str());
        std::fflush(stdout);

        // 2. Echtzeit-Dateiausgabe mit sofortigem Flush
        if (fileStream_.is_open()) {
            char timeBuf[32];
            std::snprintf(timeBuf, sizeof(timeBuf), "[%7.1fs] ", time_);
            fileStream_ << timeBuf << text << "\n";
            fileStream_.flush(); // Garantiert sofortiges Schreiben auf Festplatte in Echtzeit
        }
    }

    const std::deque<LogEntry>& Entries() const { return entries_; }
    const std::string& GetLogFilePath() const { return logFilePath_; }
    bool IsFileOpen() const { return fileStream_.is_open(); }

private:
    void MoveFrom(EventLog&& other) {
        entries_ = std::move(other.entries_);
        time_ = other.time_;
        logFilePath_ = std::move(other.logFilePath_);
        fileStream_ = std::move(other.fileStream_);
    }

    static constexpr std::size_t kMaxEntries = 8;
    std::deque<LogEntry> entries_;
    float time_ = 0.0f;
    std::string logFilePath_ = "chat_log.txt";
    std::ofstream fileStream_;
};

