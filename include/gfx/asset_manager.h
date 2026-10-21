#pragma once

#include "core/common.h"
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================================
//  AssetManager
//  Zentraler Manager für alle externen Texturen, Sprites und Grafiken.
//  - Textur-Caching gegen doppeltes Laden in den VRAM
//  - Sichere Fallbacks (Magenta-Dummy), falls Bilddateien noch nicht existieren
//  - Bequeme Render-Funktionen für Kacheln, Sprites und Regionen
// ============================================================================
class AssetManager {
public:
    static AssetManager& Instance();
    static AssetManager& Get() { return Instance(); }

    AssetManager();
    ~AssetManager();

    // Nicht kopierbar
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // Initialisierung des internen Magenta-Fallbacks
    void Init();

    // Lädt alle Standard-Assets des Spiels (Terrain, Gebäude, Entitäten, Items, UI)
    void LoadCoreAssets();

    // Lädt eine Texturdatei unter einem logischen Schlüssel in den Cache
    bool LoadTexture(const std::string& name, const std::string& filepath);

    // Liefert die gecachte Textur oder den Magenta-Fallback, falls nicht vorhanden
    const Texture2D& GetTexture(const std::string& name);

    // Prüft, ob eine Textur geladen und kein Fallback ist
    bool HasTexture(const std::string& name) const;

    // Gibt alle GPU-Ressourcen frei
    void UnloadAll();

    // Hilfsfunktionen zum Zeichnen
    void DrawTile(const std::string& name, Rectangle destRect, Color tint = WHITE);
    void DrawSpriteCentered(const std::string& name, Vector2 pos, float width, float height, Color tint = WHITE);
    void DrawRegion(const std::string& name, Rectangle srcRect, Rectangle destRect, Color tint = WHITE, bool flipX = false);

    // Fallback-Informationen
    const Texture2D& GetFallbackTexture() const { return fallbackTexture_; }

private:
    void CreateFallbackTexture();

    std::unordered_map<std::string, Texture2D> textures_;
    std::unordered_map<std::string, bool> isFallbackMap_;
    Texture2D fallbackTexture_{ 0, 0, 0, 0, 0 };
    bool initialized_ = false;
};

