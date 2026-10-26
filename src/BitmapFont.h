#pragma once
#include <SFML/Graphics.hpp>
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace forge {
// SpriteForge bitmap fonts use an editable JSON format, NOT TrueType (.ttf).
struct BitmapFont {
    std::string name = "New Pixel Font";
    int width = 8;
    int height = 8;
    int advance = 9;
    int baseline = 7;
    // Printable ASCII 32..126. Every glyph is row-major (height x width).
    std::array<std::vector<std::uint8_t>, 95> glyphs{};
    void reset(int w, int h);
    std::vector<std::uint8_t>& glyph(int ascii);
    const std::vector<std::uint8_t>& glyph(int ascii) const;
};

bool loadBitmapFont(BitmapFont& font, const std::filesystem::path& file, std::string& error);
bool saveBitmapFont(const BitmapFont& font, const std::filesystem::path& file, std::string& error);
bool exportBitmapAtlas(const BitmapFont& font, const std::filesystem::path& file, std::string& error);
void drawBitmapText(sf::RenderTarget& target, const BitmapFont& font, const std::string& text,
                    sf::Vector2f pos, float pixelScale, sf::Color color);
} // namespace forge
