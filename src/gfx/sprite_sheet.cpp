#include "sprite_sheet.h"

#include <algorithm>
#include <cstdint>

namespace {

// Zusammenhängender Bereich in einem Projektionsprofil: [start, end)
struct Span {
    int start = 0;
    int end = 0;
    long mass = 0;
    float Center() const { return 0.5f * (float)(start + end); }
};

// Findet Bereiche, in denen das Profil >= minValue ist. Lücken kleiner als
// minGap werden überbrückt (z. B. Speer neben dem Körper), Bereiche mit zu
// wenig Masse (Rauschen) werden verworfen.
std::vector<Span> FindSpans(const std::vector<int>& profile, int minValue, int minGap, long minMass) {
    std::vector<Span> spans;
    const int n = (int)profile.size();
    int i = 0;
    while (i < n) {
        while (i < n && profile[(size_t)i] < minValue) ++i;
        if (i >= n) break;

        Span span;
        span.start = i;
        int lastFilled = i;
        int j = i;
        for (; j < n; ++j) {
            if (profile[(size_t)j] >= minValue) {
                lastFilled = j;
                span.mass += profile[(size_t)j];
            } else if (j - lastFilled >= minGap) {
                break;
            }
        }
        span.end = lastFilled + 1;
        if (span.mass >= minMass) spans.push_back(span);
        i = std::max(j, span.end);
    }
    return spans;
}

inline bool IsOpaque(const Color* px, int width, int x, int y) {
    return px[(size_t)y * (size_t)width + (size_t)x].a > 0;
}

} // namespace

// ============================================================================
//  Laden / Entladen
// ============================================================================
SpriteSheet::~SpriteSheet() {
    Unload();
}

void SpriteSheet::Unload() {
    if (loaded_) {
        UnloadTexture(texture_);
        loaded_ = false;
    }
    frames_.clear();
    walkColumns_.clear();
}

bool SpriteSheet::Load(const SpriteSheetConfig& config) {
    Unload();
    config_ = config;

    // Pfad relativ zum Arbeitsverzeichnis, sonst relativ zur .exe versuchen
    std::string path = config_.path;
    if (!FileExists(path.c_str())) {
        const std::string alt = std::string(GetApplicationDirectory()) + config_.path;
        if (FileExists(alt.c_str())) path = alt;
    }

    Image image = LoadImage(path.c_str());
    if (image.data == nullptr) {
        TraceLog(LOG_WARNING, "SPRITES: '%s' konnte nicht geladen werden - Fallback-Grafik aktiv", config_.path.c_str());
        return false;
    }

    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    if (config_.useColorKey) ApplyColorKey(image, config_);

    std::string report;
    frames_ = BuildFrames(image, config_, &report);
    TraceLog(LOG_INFO, "SPRITES: %s", report.c_str());

    texture_ = LoadTextureFromImage(image);
    UnloadImage(image);
    if (texture_.id == 0) return false;

    if (config_.smoothFiltering) {
        GenTextureMipmaps(&texture_);
        SetTextureFilter(texture_, TEXTURE_FILTER_TRILINEAR);
    } else {
        SetTextureFilter(texture_, TEXTURE_FILTER_POINT);
    }
    loaded_ = true;

    // Gültige Lauf-Frames je Zeile (z. B. fehlt beim Kind "Walk 3")
    walkColumns_.assign((size_t)config_.rows, {});
    for (int r = 0; r < config_.rows; ++r) {
        for (int i = 0; i < config_.walkFrameCount; ++i) {
            const int c = config_.walkFirstColumn + i;
            if (HasFrame(r, c)) walkColumns_[(size_t)r].push_back(c);
        }
        if (walkColumns_[(size_t)r].empty()) walkColumns_[(size_t)r].push_back(ResolveColumn(r, config_.idleColumn));
    }

    // Einheitlicher Maßstab: erwachsenes Idle-Sprite (Zeile 0) = drawHeight
    float referenceHeight = 0.0f;
    if (const SpriteFrame* ref = Frame(0, ResolveColumn(0, config_.idleColumn))) referenceHeight = ref->source.height;
    scale_ = (referenceHeight > 0.0f) ? config_.drawHeight / referenceHeight : 1.0f;
    return true;
}

// ============================================================================
//  Color-Key: Hintergrundfarbe -> transparent
// ============================================================================
void SpriteSheet::ApplyColorKey(Image& image, const SpriteSheetConfig& config) {
    if (image.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

    Color* px = (Color*)image.data;
    const Color key = config.autoDetectKeyColor ? px[0] : config.keyColor;
    const int tol = config.keyTolerance;
    const size_t count = (size_t)image.width * (size_t)image.height;

    for (size_t i = 0; i < count; ++i) {
        if (std::abs((int)px[i].r - (int)key.r) <= tol &&
            std::abs((int)px[i].g - (int)key.g) <= tol &&
            std::abs((int)px[i].b - (int)key.b) <= tol) {
            px[i] = { 0, 0, 0, 0 };
        }
    }
}

// ============================================================================
//  Frames ermitteln
// ============================================================================
std::vector<SpriteFrame> SpriteSheet::BuildFrames(const Image& source, const SpriteSheetConfig& config,
                                                  std::string* report) {
    const int rows = std::max(1, config.rows);
    const int cols = std::max(1, config.columns);
    std::vector<SpriteFrame> frames((size_t)(rows * cols));

    // ---------------------------------------------------------------------------
    // 1) Klassisches Raster: source = (col * w, row * h, w, h)
    // ---------------------------------------------------------------------------
    if (config.sliceMode == SliceMode::UniformGrid) {
        const int fw = config.frameWidth  > 0 ? config.frameWidth  : source.width / cols;
        const int fh = config.frameHeight > 0 ? config.frameHeight : source.height / rows;
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                SpriteFrame& f = frames[(size_t)(r * cols + c)];
                f.source = { (float)(config.offsetX + c * (fw + config.paddingX)),
                             (float)(config.offsetY + r * (fh + config.paddingY)),
                             (float)fw, (float)fh };
                f.valid = f.source.x + f.source.width <= (float)source.width &&
                          f.source.y + f.source.height <= (float)source.height;
            }
        }
        if (report) *report = TextFormat("Raster %dx%d, Frame %dx%d px", cols, rows, fw, fh);
        return frames;
    }

    // ---------------------------------------------------------------------------
    // 2) Automatische Erkennung über Alpha-Projektionen
    // ---------------------------------------------------------------------------
    Image img = source;
    bool ownsCopy = false;
    if (img.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
        img = ImageCopy(source);
        ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        ownsCopy = true;
    }
    const Color* px = (const Color*)img.data;
    const int W = img.width, H = img.height;

    // a) Zeilenbänder (Rollen) über die vertikale Projektion
    std::vector<int> rowProfile((size_t)H, 0);
    for (int y = 0; y < H; ++y) {
        int count = 0;
        for (int x = 0; x < W; ++x) count += IsOpaque(px, W, x, y) ? 1 : 0;
        rowProfile[(size_t)y] = count;
    }
    std::vector<Span> bands = FindSpans(rowProfile, 2, std::max(4, H / 60), (long)W);
    if ((int)bands.size() > rows) {   // nur die "schwersten" Bänder behalten
        std::sort(bands.begin(), bands.end(), [](const Span& a, const Span& b) { return a.mass > b.mass; });
        bands.resize((size_t)rows);
        std::sort(bands.begin(), bands.end(), [](const Span& a, const Span& b) { return a.start < b.start; });
    }
    bool uniformRows = false;
    if ((int)bands.size() != rows) {  // Fallback: gleichmäßige Zeilen
        uniformRows = true;
        bands.clear();
        for (int r = 0; r < rows; ++r) bands.push_back({ r * H / rows, (r + 1) * H / rows, 0 });
    }

    // b) Pro Band: Sprites über die horizontale Projektion
    std::vector<std::vector<Span>> rowSpans((size_t)rows);
    for (int r = 0; r < rows; ++r) {
        const Span& band = bands[(size_t)r];
        std::vector<int> colProfile((size_t)W, 0);
        for (int y = band.start; y < band.end; ++y)
            for (int x = 0; x < W; ++x)
                colProfile[(size_t)x] += IsOpaque(px, W, x, y) ? 1 : 0;
        rowSpans[(size_t)r] = FindSpans(colProfile, 2, std::max(3, W / 90), 300);
        if ((int)rowSpans[(size_t)r].size() > cols) {
            auto& spans = rowSpans[(size_t)r];
            std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.mass > b.mass; });
            spans.resize((size_t)cols);
            std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.start < b.start; });
        }
    }

    // c) Spaltenmitten aus vollständigen Zeilen lernen (sonst gleichmäßig)
    std::vector<float> centers((size_t)cols, 0.0f);
    int fullRows = 0;
    for (const auto& spans : rowSpans) {
        if ((int)spans.size() != cols) continue;
        for (int c = 0; c < cols; ++c) centers[(size_t)c] += spans[(size_t)c].Center();
        ++fullRows;
    }
    for (int c = 0; c < cols; ++c)
        centers[(size_t)c] = fullRows > 0 ? centers[(size_t)c] / (float)fullRows
                                          : ((float)c + 0.5f) * (float)W / (float)cols;

    // d) Sprites den Posen-Spalten zuordnen + vertikal zuschneiden
    int validCount = 0;
    std::string missing;
    for (int r = 0; r < rows; ++r) {
        const Span& band = bands[(size_t)r];
        std::vector<bool> taken((size_t)cols, false);

        for (const Span& s : rowSpans[(size_t)r]) {
            int bestCol = -1;
            float bestDist = 0.0f;
            for (int c = 0; c < cols; ++c) {
                if (taken[(size_t)c]) continue;
                const float d = std::fabs(centers[(size_t)c] - s.Center());
                if (bestCol < 0 || d < bestDist) { bestCol = c; bestDist = d; }
            }
            if (bestCol < 0) continue;
            taken[(size_t)bestCol] = true;

            int top = band.end, bottom = band.start;
            for (int y = band.start; y < band.end; ++y) {
                for (int x = s.start; x < s.end; ++x) {
                    if (IsOpaque(px, W, x, y)) {
                        top = std::min(top, y);
                        bottom = std::max(bottom, y);
                        break;
                    }
                }
            }
            if (bottom < top) continue;

            SpriteFrame& f = frames[(size_t)(r * cols + bestCol)];
            f.source = { (float)s.start, (float)top, (float)(s.end - s.start), (float)(bottom - top + 1) };
            f.valid = true;
            ++validCount;
        }
        for (int c = 0; c < cols; ++c)
            if (!frames[(size_t)(r * cols + c)].valid) missing += TextFormat(" [r%d c%d]", r, c);
    }

    if (report) {
        *report = TextFormat("Auto-Erkennung %dx%d px: %d/%d Frames gefunden%s%s%s", W, H, validCount, rows * cols,
                             uniformRows ? " (gleichmäßige Zeilen)" : "",
                             missing.empty() ? "" : ", fehlend:", missing.c_str());
    }
    if (ownsCopy) UnloadImage(img);
    return frames;
}

// ============================================================================
//  Abfragen
// ============================================================================
const SpriteFrame* SpriteSheet::Frame(int row, int column) const {
    if (row < 0 || row >= config_.rows || column < 0 || column >= config_.columns) return nullptr;
    const size_t idx = (size_t)(row * config_.columns + column);
    if (idx >= frames_.size()) return nullptr;
    return &frames_[idx];
}

bool SpriteSheet::HasFrame(int row, int column) const {
    const SpriteFrame* f = Frame(row, column);
    return f && f->valid;
}

int SpriteSheet::ResolveColumn(int row, int desired) const {
    if (HasFrame(row, desired)) return desired;
    // Fehlender Lauf-Frame -> anderer Lauf-Frame
    if (desired >= config_.walkFirstColumn && desired < config_.walkFirstColumn + config_.walkFrameCount) {
        for (int i = 0; i < config_.walkFrameCount; ++i)
            if (HasFrame(row, config_.walkFirstColumn + i)) return config_.walkFirstColumn + i;
    }
    if (HasFrame(row, config_.idleColumn)) return config_.idleColumn;
    for (int c = 0; c < config_.columns; ++c)
        if (HasFrame(row, c)) return c;
    return -1;
}

int SpriteSheet::WalkFrameCount(int row) const {
    if (row < 0 || row >= (int)walkColumns_.size()) return 1;
    return (int)walkColumns_[(size_t)row].size();
}

int SpriteSheet::WalkColumn(int row, int index) const {
    if (row < 0 || row >= (int)walkColumns_.size() || walkColumns_[(size_t)row].empty()) return config_.idleColumn;
    const auto& list = walkColumns_[(size_t)row];
    return list[(size_t)(index % (int)list.size())];
}

Rectangle SpriteSheet::GetSourceRect(int row, int column, bool flipX) const {
    const SpriteFrame* f = Frame(row, ResolveColumn(row, column));
    if (!f || !f->valid) return { 0, 0, 0, 0 };
    Rectangle src = f->source;
    // raylib spiegelt horizontal, wenn die Quellbreite negativ ist.
    if (flipX) src.width = -src.width;
    return src;
}

void SpriteSheet::Draw(int row, int column, Vector2 feetPos, bool facingLeft, Color tint) const {
    if (!loaded_) return;
    const bool flip = config_.spritesFaceRight ? facingLeft : !facingLeft;
    const Rectangle src = GetSourceRect(row, column, flip);
    if (src.height <= 0.0f) return;

    const float w = std::fabs(src.width) * scale_;
    const float h = src.height * scale_;
    const Rectangle dest = { feetPos.x, feetPos.y, w, h };
    const Vector2 origin = { w * 0.5f, h };   // Ankerpunkt: unten mittig (Füße)
    DrawTexturePro(texture_, src, dest, origin, 0.0f, tint);
}

