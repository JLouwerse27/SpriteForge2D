#include "BitmapFont.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace forge {
namespace fs = std::filesystem;
void BitmapFont::reset(int w, int h) {
    width = std::clamp(w, 1, 32);
    height = std::clamp(h, 1, 32);
    advance = width + 1;
    baseline = height - 1;
    for (auto& g : glyphs) g.assign(static_cast<size_t>(width * height), 0);
}
std::vector<std::uint8_t>& BitmapFont::glyph(int ascii) {
    return glyphs[static_cast<size_t>(std::clamp(ascii,32,126)-32)];
}
const std::vector<std::uint8_t>& BitmapFont::glyph(int ascii) const {
    return glyphs[static_cast<size_t>(std::clamp(ascii,32,126)-32)];
}

bool loadBitmapFont(BitmapFont& font, const fs::path& file, std::string& error) {
    try {
        std::ifstream in(file, std::ios::binary);
        if (!in) throw std::runtime_error("Cannot read font: " + file.string());
        const auto j = nlohmann::json::parse(in);
        if (j.value("format",std::string{}) != "SpriteForgeBitmapFont1") throw std::runtime_error("Invalid SpriteForge font format");
        BitmapFont tmp;
        tmp.reset(j.value("width",8),j.value("height",8));
        tmp.name=j.value("name",std::string{"Pixel Font"});
        tmp.advance=std::clamp(j.value("advance",tmp.advance),1,64);
        tmp.baseline=std::clamp(j.value("baseline",tmp.baseline),0,tmp.height);
        if (j.contains("glyphs")) {
            for (auto it=j["glyphs"].begin(); it!=j["glyphs"].end(); ++it) {
                const int code=std::stoi(it.key());
                if (code<32||code>126) continue;
                std::vector<std::uint8_t> pixels=it.value().get<std::vector<std::uint8_t>>();
                if (pixels.size()!=static_cast<size_t>(tmp.width*tmp.height)) throw std::runtime_error("Incorrect glyph size");
                for (auto& p:pixels) p = p?1:0;
                tmp.glyph(code)=std::move(pixels);
            }
        }
        font=std::move(tmp); error.clear(); return true;
    } catch (const std::exception& e) { error=e.what(); return false; }
}
bool saveBitmapFont(const BitmapFont& font, const fs::path& file, std::string& error) {
    try {
        nlohmann::json j;
        j["format"]="SpriteForgeBitmapFont1";
        j["name"]=font.name; j["width"]=font.width; j["height"]=font.height;
        j["advance"]=font.advance; j["baseline"]=font.baseline;
        j["glyphs"]=nlohmann::json::object();
        for (int code=32;code<=126;++code) j["glyphs"][std::to_string(code)]=font.glyph(code);
        fs::create_directories(file.parent_path());
        std::ofstream out(file,std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Cannot write font: " + file.string());
        out << j.dump(2) << '\n';
        if (!out) throw std::runtime_error("Write font failed");
        error.clear(); return true;
    } catch (const std::exception& e) { error=e.what(); return false; }
}
void drawBitmapText(sf::RenderTarget& target, const BitmapFont& font, const std::string& text,
                    sf::Vector2f pos, float pixelScale, sf::Color color) {
    if (pixelScale<=0.f) return;
    float x=pos.x; float y=pos.y;
    for (unsigned char c:text) {
        if (c=='\n') { x=pos.x; y+=(font.height+2)*pixelScale; continue; }
        if (c<32||c>126) c='?';
        const auto& bits=font.glyph(c);
        if (bits.size()!=static_cast<size_t>(font.width*font.height)) continue;
        for (int gy=0;gy<font.height;++gy) for (int gx=0;gx<font.width;++gx) {
            if (!bits[static_cast<size_t>(gy*font.width+gx)]) continue;
            sf::RectangleShape px({pixelScale,pixelScale});
            px.setPosition({x+gx*pixelScale,y+gy*pixelScale});
            px.setFillColor(color);
            target.draw(px);
        }
        x+=font.advance*pixelScale;
    }
}
bool exportBitmapAtlas(const BitmapFont& font, const fs::path& file, std::string& error) {
    try {
        constexpr int cols=16;
        constexpr int rows=6;
        sf::Image image({static_cast<unsigned>(cols*font.width),static_cast<unsigned>(rows*font.height)},sf::Color::Transparent);
        for (int code=32;code<=126;++code) {
            const int slot=code-32, tx=slot%cols*font.width, ty=slot/cols*font.height;
            const auto& bits=font.glyph(code);
            if (bits.size()!=static_cast<size_t>(font.width*font.height)) continue;
            for (int y=0;y<font.height;++y) for(int x=0;x<font.width;++x)
                if(bits[static_cast<size_t>(y*font.width+x)])
                    image.setPixel({static_cast<unsigned>(tx+x),static_cast<unsigned>(ty+y)},sf::Color::White);
        }
        fs::create_directories(file.parent_path());
        if(!image.saveToFile(file)) throw std::runtime_error("Cannot save PNG font atlas");
        error.clear(); return true;
    } catch (const std::exception& e) { error=e.what(); return false; }
}
} // namespace forge
