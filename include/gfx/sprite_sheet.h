#pragma once

#include "core/common.h"

#include <string>
#include <vector>

// Wie die Einzelbilder im Sheet gefunden werden.
enum class SliceMode {
    UniformGrid,   // klassisches Raster: frameWidth x frameHeight (+ Padding)
    AutoDetect     // Sprites anhand transparenter Lücken automatisch finden
};

// Konfiguration des Sprite-Sheets. Zeilen = Rollen, Spalten = Posen.
struct SpriteSheetConfig {
    std::string path = "assets/cavemen.png";
    int rows    = 4;   // 0 Jäger, 1 Sammlerin, 2 Ältester, 3 Kind
    int columns = 7;   // Idle, Walk1-3, Action, Crafting, Sleeping

    // --- Slicing ----------------------------------------------------------------
    // Die gelieferte cavemen.png ist 2752x1536 groß und die Posen haben
    // unregelmäßige Abstände -> AutoDetect. Für ein echtes Raster-Sheet
    // (z. B. 16x16 / 24x24 Pixel pro Frame) UniformGrid wählen und
    // frameWidth/frameHeight setzen.
    SliceMode sliceMode = SliceMode::AutoDetect;
    int frameWidth  = 0;   // UniformGrid: Breite pro Frame (0 = Texturbreite / columns)
    int frameHeight = 0;   // UniformGrid: Höhe pro Frame   (0 = Texturhöhe / rows)
    int paddingX = 0;      // UniformGrid: Abstand zwischen Frames
    int paddingY = 0;
    int offsetX  = 0;      // UniformGrid: Rand oben/links
    int offsetY  = 0;

    // --- Transparenz ----------------------------------------------------------
    // Das Sheet hat einen grauen Hintergrund statt Alpha -> Color-Key.
    bool  useColorKey        = true;
    bool  autoDetectKeyColor = true;   // Farbe des Pixels (0,0) verwenden
    Color keyColor           = { 71, 74, 79, 255 };
    int   keyTolerance       = 20;     // max. Abweichung pro Farbkanal

    // --- Darstellung ----------------------------------------------------------
    float drawHeight       = 22.0f;    // Höhe eines erwachsenen Idle-Sprites in Welt-Pixeln
    bool  spritesFaceRight = true;     // Blickrichtung im Sheet (für das Spiegeln)
    bool  smoothFiltering  = true;     // Mipmaps + trilinear (starkes Verkleinern); false = Point

    // --- Posen-Spalten ----------------------------------------------------------
    int idleColumn      = 0;
    int walkFirstColumn = 1;
    int walkFrameCount  = 3;
    int actionColumn    = 4;   // Jagen / Sammeln
    int craftColumn     = 5;   // Bauen / Handwerk
    int sleepColumn     = -1;  // -1 = letzte Spalte
};

struct SpriteFrame {
    Rectangle source = { 0, 0, 0, 0 };
    bool valid = false;
};

// Lädt das Sprite-Sheet, ermittelt die Quell-Rechtecke und zeichnet Frames.
class SpriteSheet {
public:
    SpriteSheet() = default;
    ~SpriteSheet();
    SpriteSheet(const SpriteSheet&) = delete;
    SpriteSheet& operator=(const SpriteSheet&) = delete;

    bool Load(const SpriteSheetConfig& config);   // benötigt ein offenes Fenster (GPU)
    void Unload();
    bool IsLoaded() const { return loaded_; }
    const SpriteSheetConfig& Config() const { return config_; }

    // Quell-Rechteck für Zeile (Rolle) und Spalte (Pose). flipX -> negative Breite.
    Rectangle GetSourceRect(int row, int column, bool flipX) const;
    bool HasFrame(int row, int column) const;
    int ResolveColumn(int row, int desiredColumn) const;   // Fallback bei fehlenden Frames

    int IdleColumn() const   { return config_.idleColumn; }
    int ActionColumn() const { return config_.actionColumn; }
    int CraftColumn() const  { return config_.craftColumn; }
    int SleepColumn() const  { return config_.sleepColumn < 0 ? config_.columns - 1 : config_.sleepColumn; }
    int WalkFrameCount(int row) const;
    int WalkColumn(int row, int index) const;

    // Zeichnet einen Frame mit den Füßen auf feetPos.
    void Draw(int row, int column, Vector2 feetPos, bool facingLeft, Color tint) const;

    // --- Hilfsfunktionen ohne GPU (auch für Tests nutzbar) ---------------------
    static void ApplyColorKey(Image& image, const SpriteSheetConfig& config);
    static std::vector<SpriteFrame> BuildFrames(const Image& image, const SpriteSheetConfig& config,
                                                std::string* report = nullptr);

private:
    const SpriteFrame* Frame(int row, int column) const;

    Texture2D texture_{};
    bool loaded_ = false;
    SpriteSheetConfig config_;
    std::vector<SpriteFrame> frames_;              // rows * columns
    std::vector<std::vector<int>> walkColumns_;    // gültige Lauf-Spalten je Zeile
    float scale_ = 1.0f;                           // Quellpixel -> Weltpixel
};


