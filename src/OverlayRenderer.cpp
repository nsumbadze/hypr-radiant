#include <hypr-radiant/OverlayRenderer.hpp>
#include <hypr-radiant/Log.hpp>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include <algorithm>
#include <stdexcept>

namespace hypr_radiant {

OverlayRenderer::OverlayRenderer(const RadiantConfig& config) : m_config(config) {}

void OverlayRenderer::install() {
    if (!Event::bus())
        throw std::runtime_error{"hypr-radiant: Hyprland event bus is not available"};

    m_renderStageListener = Event::bus()->m_events.render.stage.listen([this](eRenderStage stage) { onRenderStage(stage); });
}

void OverlayRenderer::uninstall() {
    hideImmediate();
    m_renderStageListener.reset();
}

void OverlayRenderer::toggle() {
    m_animation.animateTo(!m_animation.targetVisible(), m_config.animationDurationMs());
    damageAllMonitors();
}

void OverlayRenderer::hideImmediate() {
    const auto wasRenderable = m_animation.renderable();
    m_animation.hideImmediate();

    if (wasRenderable)
        damageAllMonitors();
}

bool OverlayRenderer::active() const noexcept {
    return m_animation.targetVisible();
}

void OverlayRenderer::onRenderStage(eRenderStage stage) {
    if (stage != RENDER_LAST_MOMENT || !m_animation.renderable())
        return;

    const auto alpha = std::clamp(static_cast<float>(m_animation.value()) * m_config.opacity(), 0.0F, 1.0F);

    if (alpha > 0.001F)
        renderCurrentMonitor(alpha);

    if (m_animation.running())
        damageAllMonitors();
}

void OverlayRenderer::renderCurrentMonitor(double alpha) {
    if (!g_pHyprRenderer || !g_pCompositor)
        return;

    const auto monitor = g_pHyprRenderer->renderData().pMonitor.lock();

    if (!monitor || !g_pCompositor->monitorExists(monitor))
        return;

    const auto width  = monitor->m_size.x;
    const auto height = monitor->m_size.y;

    if (width <= 0.0 || height <= 0.0)
        return;

    CRectPassElement::SRectData rect;
    rect.box   = CBox{0, 0, width, height};
    rect.color = CHyprColor{0.0F, 0.0F, 0.0F, static_cast<float>(alpha)};

    g_pHyprRenderer->draw(rect);
}

void OverlayRenderer::damageAllMonitors() const {
    if (!g_pCompositor || !g_pHyprRenderer)
        return;

    for (const auto& monitor : g_pCompositor->m_monitors) {
        if (!monitor || !g_pCompositor->monitorExists(monitor))
            continue;

        g_pHyprRenderer->damageMonitor(monitor);
        g_pCompositor->scheduleFrameForMonitor(monitor);
    }
}

} // namespace hypr_radiant
