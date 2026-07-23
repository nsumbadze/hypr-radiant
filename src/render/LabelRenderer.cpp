#include <hypr-radiant/render/LabelRenderer.hpp>

#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace hypr_radiant {
namespace {

double centered(double available, double size) {
    return std::max(0.0, (available - size) / 2.0);
}

} // namespace

SP<Render::ITexture> LabelRenderer::texture(const std::string& text, double maxWidth, int pointSize, CHyprColor color) {
    if (!g_pHyprRenderer || text.empty() || maxWidth <= 0.0)
        return {};

    // Cache key: (pointSize, ceil(maxWidth), color, text). clear() runs on every open/close, so a
    // session only holds the labels actually drawn — workspace/window names plus a fixed set of
    // helpers. The one unbounded-per-open key is the live search string, which grows one entry per
    // distinct typed prefix; that is fine for realistic queries, so no eviction is warranted here.
    const auto channel = [](float value) {
        return static_cast<int>(std::round(std::clamp(value, 0.0F, 1.0F) * 255.0F));
    };
    const auto key = std::format("{}:{}:{:02x}{:02x}{:02x}{:02x}:{}", pointSize, static_cast<int>(std::ceil(maxWidth)),
        channel(color.r), channel(color.g), channel(color.b), channel(color.a), text);
    auto       it  = m_textures.find(key);
    if (it == m_textures.end())
        it = m_textures.emplace(key, g_pHyprRenderer->renderText(text, color, pointSize, false, m_config.fontFamily(), static_cast<int>(maxWidth)))
               .first;

    const auto& tex = it->second;
    if (!tex || !tex->ok() || tex->m_size.x <= 0.0 || tex->m_size.y <= 0.0)
        return {};

    return tex;
}

RadiantSize LabelRenderer::measure(const std::string& text, double maxWidth, int pointSize, CHyprColor color) {
    const auto tex = texture(text, maxWidth, pointSize, color);
    if (!tex)
        return {.width = 0.0, .height = 0.0};

    return {.width = std::min(tex->m_size.x, maxWidth), .height = tex->m_size.y};
}

void LabelRenderer::render(const std::string& text, double x, double y, double maxWidth, int pointSize, double alpha, const CRegion& damage) {
    renderColored(text, x, y, maxWidth, pointSize, m_config.foregroundColor(), alpha, damage);
}

void LabelRenderer::renderColored(
    const std::string& text, double x, double y, double maxWidth, int pointSize, CHyprColor color, double alpha, const CRegion& damage) {
    if (alpha <= 0.001)
        return;

    const auto tex = texture(text, maxWidth, pointSize, color);
    if (!tex)
        return;

    CTexPassElement::SRenderData data;
    data.tex      = tex;
    data.box      = CBox{std::round(x), std::round(y), std::min(tex->m_size.x, maxWidth), tex->m_size.y};
    data.overallA = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
    data.damage   = damage;

    g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(std::move(data)));
}

// Places text using its measured size instead of a hand-tuned offset, so glyphs stay optically
// centered when the interface font or point size changes.
void LabelRenderer::renderCentered(const std::string& text, const CBox& within, int pointSize, CHyprColor color, double alpha, const CRegion& damage) {
    if (alpha <= 0.001 || within.w <= 0.0)
        return;

    const auto size = measure(text, within.w, pointSize, color);
    if (size.width <= 0.0)
        return;

    renderColored(text, within.x + centered(within.w, size.width), within.y + centered(within.h, size.height), within.w, pointSize, color, alpha,
        damage);
}

} // namespace hypr_radiant
