#include "gfx/asset_manager.h"
#include <cstdio>

AssetManager& AssetManager::Instance() {
    static AssetManager sInstance;
    return sInstance;
}

AssetManager::AssetManager() {
}

AssetManager::~AssetManager() {
    UnloadAll();
}

void AssetManager::Init() {
    if (initialized_) return;
    CreateFallbackTexture();
    initialized_ = true;
    LoadCoreAssets();
}

void AssetManager::CreateFallbackTexture() {
    // 16x16 Magenta / Lila Schacheffekt als unverkennbare Platzhalter-Textur
    Image img = GenImageColor(16, 16, MAGENTA);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            if ((x < 8 && y < 8) || (x >= 8 && y >= 8)) {
                ImageDrawPixel(&img, x, y, Color{ 30, 30, 30, 255 }); // Schwarz-Magenta Checkerboard
            }
        }
    }
    fallbackTexture_ = LoadTextureFromImage(img);
    UnloadImage(img);
}

bool AssetManager::LoadTexture(const std::string& name, const std::string& filepath) {
    if (!initialized_) {
        Init();
    }

    // Wenn bereits geladen und kein Fallback, nicht erneut laden (Caching)
    auto it = textures_.find(name);
    if (it != textures_.end() && !isFallbackMap_[name]) {
        return true;
    }

    if (FileExists(filepath.c_str())) {
        Texture2D tex = ::LoadTexture(filepath.c_str());
        if (tex.id > 0) {
            SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
            textures_[name] = tex;
            isFallbackMap_[name] = false;
            std::printf("[AssetManager] Erfolgreich geladen: %s -> %s (%dx%d)\n",
                        name.c_str(), filepath.c_str(), tex.width, tex.height);
            return true;
        }
    }

    // Datei nicht vorhanden oder Laden fehlgeschlagen -> Sichere Fallback-Zuweisung
    textures_[name] = fallbackTexture_;
    isFallbackMap_[name] = true;
    return false;
}

const Texture2D& AssetManager::GetTexture(const std::string& name) {
    if (!initialized_) {
        Init();
    }

    auto it = textures_.find(name);
    if (it != textures_.end()) {
        return it->second;
    }

    // Falls Name noch gar nicht registriert wurde, liefere Fallback
    return fallbackTexture_;
}

bool AssetManager::HasTexture(const std::string& name) const {
    auto it = textures_.find(name);
    if (it == textures_.end()) return false;
    auto fit = isFallbackMap_.find(name);
    return (fit != isFallbackMap_.end() && !fit->second);
}

void AssetManager::UnloadAll() {
    for (auto& pair : textures_) {
        if (!isFallbackMap_[pair.first] && pair.second.id > 0 && pair.second.id != fallbackTexture_.id) {
            ::UnloadTexture(pair.second);
        }
    }
    textures_.clear();
    isFallbackMap_.clear();

    if (fallbackTexture_.id > 0) {
        ::UnloadTexture(fallbackTexture_);
        fallbackTexture_ = { 0, 0, 0, 0, 0 };
    }
    initialized_ = false;
}

void AssetManager::DrawTile(const std::string& name, Rectangle destRect, Color tint) {
    const Texture2D& tex = GetTexture(name);
    Rectangle srcRect = { 0.0f, 0.0f, (float)tex.width, (float)tex.height };
    DrawTexturePro(tex, srcRect, destRect, { 0.0f, 0.0f }, 0.0f, tint);
}

void AssetManager::DrawSpriteCentered(const std::string& name, Vector2 pos, float width, float height, Color tint) {
    const Texture2D& tex = GetTexture(name);
    Rectangle srcRect = { 0.0f, 0.0f, (float)tex.width, (float)tex.height };
    Rectangle destRect = { pos.x - width * 0.5f, pos.y - height * 0.5f, width, height };
    DrawTexturePro(tex, srcRect, destRect, { 0.0f, 0.0f }, 0.0f, tint);
}

void AssetManager::DrawRegion(const std::string& name, Rectangle srcRect, Rectangle destRect, Color tint, bool flipX) {
    const Texture2D& tex = GetTexture(name);
    if (flipX) srcRect.width = -srcRect.width;
    DrawTexturePro(tex, srcRect, destRect, { 0.0f, 0.0f }, 0.0f, tint);
}

void AssetManager::LoadCoreAssets() {
    // -------------------------------------------------------------------------
    // 1. TERRAIN (Tiles)
    // -------------------------------------------------------------------------
    LoadTexture("terrain_deep_water",    "assets/terrain/water_deep.png");
    LoadTexture("terrain_shallow_water", "assets/terrain/water_shallow.png");
    LoadTexture("terrain_sand",          "assets/terrain/sand.png");
    LoadTexture("terrain_grass_0",       "assets/terrain/grass_1.png");
    LoadTexture("terrain_grass_1",       "assets/terrain/grass_2.png");
    LoadTexture("terrain_grass_2",       "assets/terrain/grass_3.png");
    LoadTexture("terrain_forest",        "assets/terrain/forest.png");
    LoadTexture("terrain_mountain",      "assets/terrain/mountain_base.png");
    LoadTexture("terrain_cliff",         "assets/terrain/mountain_cliff.png");
    LoadTexture("terrain_snow",          "assets/terrain/snow.png");

    // -------------------------------------------------------------------------
    // 2. GEBÄUDE & POIs
    // -------------------------------------------------------------------------
    LoadTexture("building_scaffold",     "assets/buildings/construction_scaffold.png");
    LoadTexture("building_ruined",       "assets/buildings/building_ruined.png");
    LoadTexture("building_towncenter_1", "assets/buildings/towncenter_tier1.png");
    LoadTexture("building_towncenter_2", "assets/buildings/towncenter_tier2.png");
    LoadTexture("building_towncenter_3", "assets/buildings/towncenter_tier3.png");
    LoadTexture("building_hut_1",        "assets/buildings/hut_tier1.png");
    LoadTexture("building_hut_2",        "assets/buildings/hut_tier2.png");
    LoadTexture("building_hut_3",        "assets/buildings/hut_tier3.png");
    LoadTexture("building_pantry",       "assets/buildings/pantry.png");
    LoadTexture("building_craftbench",   "assets/buildings/craftbench.png");
    LoadTexture("building_watchtower",   "assets/buildings/watchtower.png");
    LoadTexture("building_kiln",         "assets/buildings/kiln.png");
    LoadTexture("building_farmplot",     "assets/buildings/farmplot.png");
    LoadTexture("building_animalpen",    "assets/buildings/animalpen.png");
    LoadTexture("building_monument",     "assets/buildings/monument.png");
    LoadTexture("building_grave",        "assets/buildings/grave.png");
    LoadTexture("building_bordertotem",  "assets/buildings/bordertotem.png");
    LoadTexture("building_outpost",      "assets/buildings/outpost.png");

    // -------------------------------------------------------------------------
    // 3. ENTITÄTEN & SPRITE-SHEETS
    // -------------------------------------------------------------------------
    LoadTexture("entity_human_male",     "assets/entities/human_male_spritesheet.png");
    LoadTexture("entity_human_female",   "assets/entities/human_female_spritesheet.png");
    LoadTexture("entity_human_child",    "assets/entities/human_child_spritesheet.png");
    LoadTexture("entity_animal_wild",    "assets/entities/animal_spritesheet.png");

    // Kleidung & Rüstung (Layering)
    LoadTexture("clothing_raw_hide",     "assets/entities/clothing_raw_hide.png");
    LoadTexture("clothing_sewn_furs",    "assets/entities/clothing_sewn_furs.png");
    LoadTexture("clothing_woven_tunic",  "assets/entities/clothing_woven_tunic.png");
    LoadTexture("clothing_armor",        "assets/entities/clothing_armor.png");

    // -------------------------------------------------------------------------
    // 4. ITEMS, WAFFEN & TRAGEGERÄTE
    // -------------------------------------------------------------------------
    LoadTexture("item_club",             "assets/items/weapon_club.png");
    LoadTexture("item_spear",            "assets/items/weapon_spear.png");
    LoadTexture("item_bow",              "assets/items/weapon_bow.png");
    LoadTexture("item_sword",            "assets/items/weapon_sword.png");
    LoadTexture("item_axe",              "assets/items/tool_axe.png");
    LoadTexture("item_pickaxe",          "assets/items/tool_pickaxe.png");
    LoadTexture("item_torch",            "assets/items/tool_torch.png");
    LoadTexture("item_basket",           "assets/items/container_basket.png");
    LoadTexture("item_handcart",         "assets/items/container_handcart.png");

    // -------------------------------------------------------------------------
    // 5. UI-ELEMENTE
    // -------------------------------------------------------------------------
    LoadTexture("ui_panel_bg",           "assets/ui/panel_background.png");
    LoadTexture("ui_button_normal",      "assets/ui/button_normal.png");
    LoadTexture("ui_button_hover",       "assets/ui/button_hover.png");
    LoadTexture("ui_icon_food",          "assets/ui/icon_food.png");
    LoadTexture("ui_icon_wood",          "assets/ui/icon_wood.png");
    LoadTexture("ui_icon_stone",         "assets/ui/icon_stone.png");
}

