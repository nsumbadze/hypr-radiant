#pragma once

#include <hypr-radiant/Config.hpp>
#include <hypr-radiant/RadiantState.hpp>

#include <hyprland/src/helpers/Color.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Region.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

#include <string>
#include <unordered_map>

namespace Render {
class ITexture;
}

namespace hypr_radiant {

// Owns the overview's text cache and all label drawing. Pulled out of OverlayRenderer so the
// texture map and the ITexture dependency stop leaking into the renderer's public header, and so the
// caching rationale lives next to the one thing that relies on it.
class LabelRenderer {
  public:
    explicit LabelRenderer(const RadiantConfig& config) : m_config(config) {}

    // Drop every cached texture. Called on each overview open/close so labels never accumulate
    // across sessions.
    void clear() { m_textures.clear(); }

    [[nodiscard]] RadiantSize measure(const std::string& text, double maxWidth, int pointSize, CHyprColor color);
    // Draws in the theme foreground colour.
    void render(const std::string& text, double x, double y, double maxWidth, int pointSize, double alpha, const CRegion& damage);
    void renderColored(const std::string& text, double x, double y, double maxWidth, int pointSize, CHyprColor color, double alpha,
        const CRegion& damage);
    void renderCentered(const std::string& text, const CBox& within, int pointSize, CHyprColor color, double alpha, const CRegion& damage);

  private:
    [[nodiscard]] SP<Render::ITexture> texture(const std::string& text, double maxWidth, int pointSize, CHyprColor color);

    const RadiantConfig&                                    m_config;
    std::unordered_map<std::string, SP<Render::ITexture>> m_textures;
};

} // namespace hypr_radiant
