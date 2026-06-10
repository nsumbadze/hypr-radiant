#include <hypr-radiant/OverlayRenderer.hpp>
#include <hypr-radiant/Log.hpp>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>
#include <utility>

namespace hypr_radiant {
namespace {

bool sameTarget(OverviewTarget lhs, OverviewTarget rhs) {
    return lhs.type == rhs.type && lhs.workspaceId == rhs.workspaceId && lhs.windowId == rhs.windowId;
}

CBox boxFor(const LayoutRect& rect) {
    return CBox{std::round(rect.x), std::round(rect.y), std::round(rect.width), std::round(rect.height)};
}

void drawRect(const CBox& box, CHyprColor color, const CRegion& damage, int round = 0) {
    if (!g_pHyprRenderer || box.w <= 0.0 || box.h <= 0.0)
        return;

    CRectPassElement::SRectData data;
    data.box   = box;
    data.color = color;
    data.round = round;

    g_pHyprRenderer->draw(data, damage);
}

void drawBorder(const CBox& box, double size, CHyprColor color, const CRegion& damage) {
    if (box.w <= 0.0 || box.h <= 0.0 || size <= 0.0)
        return;

    const auto border = std::min({size, box.w / 2.0, box.h / 2.0});
    drawRect(CBox{box.x, box.y, box.w, border}, color, damage);
    drawRect(CBox{box.x, box.y + box.h - border, box.w, border}, color, damage);
    drawRect(CBox{box.x, box.y, border, box.h}, color, damage);
    drawRect(CBox{box.x + box.w - border, box.y, border, box.h}, color, damage);
}

const MonitorSnapshot* findMonitorSnapshot(const RadiantState& state, std::int64_t id) {
    for (const auto& monitor : state.monitors) {
        if (monitor.id == id)
            return &monitor;
    }

    return nullptr;
}

MonitorSnapshot snapshotForCurrentMonitor(const PHLMONITOR& monitor) {
    MonitorSnapshot snapshot;
    snapshot.id       = monitor->m_id;
    snapshot.name     = monitor->m_name;
    snapshot.geometry = {.position = {}, .size = {.width = monitor->m_size.x, .height = monitor->m_size.y}};
    snapshot.scale    = monitor->m_scale;

    if (monitor->m_activeWorkspace) {
        snapshot.activeWorkspaceId   = monitor->m_activeWorkspace->m_id;
        snapshot.activeWorkspaceName = monitor->m_activeWorkspace->m_name;
    }

    return snapshot;
}

} // namespace

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

void OverlayRenderer::show(RadiantState state) {
    m_state = std::move(state);
    computeFrames();
    m_selectedTarget = m_frames.empty() ? OverviewTarget{} : m_hitTester.initialSelection(m_frames.front());
    m_textures.clear();
    m_animation.animateTo(true, m_config.animationDurationMs());
    damageAllMonitors();
}

void OverlayRenderer::toggle(RadiantState state) {
    if (m_animation.targetVisible()) {
        m_state = std::move(state);
        computeFrames();
        m_animation.animateTo(false, m_config.animationDurationMs());
        damageAllMonitors();
        return;
    }

    show(std::move(state));
}

void OverlayRenderer::moveSelection(NavigationDirection direction) {
    const auto* frame = firstFrame();
    if (!frame)
        return;

    m_selectedTarget = m_hitTester.moveSelection(*frame, m_selectedTarget, direction);
    damageAllMonitors();
}

void OverlayRenderer::hideImmediate() {
    const auto wasRenderable = m_animation.renderable();
    m_animation.hideImmediate();
    m_frames.clear();
    m_textures.clear();
    m_selectedTarget = {};

    if (wasRenderable)
        damageAllMonitors();
}

bool OverlayRenderer::active() const noexcept {
    return m_animation.targetVisible();
}

OverviewTarget OverlayRenderer::selectedTarget() const noexcept {
    return m_selectedTarget;
}

OverviewTarget OverlayRenderer::hitTest(double x, double y) const {
    const auto* frame = firstFrame();
    if (!frame)
        return {};

    return m_hitTester.hitTest(*frame, x, y);
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

    const auto width  = monitor->m_transformedSize.x;
    const auto height = monitor->m_transformedSize.y;

    if (width <= 0.0 || height <= 0.0)
        return;

    auto box = CBox{0, 0, width, height};

    const CRegion fullMonitorDamage{box};

    drawRect(box, CHyprColor{0.0F, 0.0F, 0.0F, static_cast<float>(alpha)}, fullMonitorDamage);

    auto snapshot = snapshotForCurrentMonitor(monitor);
    if (const auto* collected = findMonitorSnapshot(m_state, monitor->m_id))
        snapshot = *collected;

    auto frame = m_layout.compute(m_state, snapshot, {.width = width, .height = height});
    auto storedFrame = std::ranges::find_if(m_frames, [&](const WorkspaceWallFrame& stored) { return stored.monitorId == frame.monitorId; });
    if (storedFrame != m_frames.end())
        *storedFrame = frame;
    else
        m_frames.push_back(frame);

    if (m_selectedTarget.type == OverviewTargetType::None)
        m_selectedTarget = m_hitTester.initialSelection(frame);

    renderFrame(frame, alpha, fullMonitorDamage);
}

void OverlayRenderer::computeFrames() {
    m_frames.clear();

    for (const auto& monitor : m_state.monitors) {
        const auto renderSize = RadiantSize{
            .width  = std::max(1.0, monitor.geometry.size.width),
            .height = std::max(1.0, monitor.geometry.size.height),
        };
        m_frames.push_back(m_layout.compute(m_state, monitor, renderSize));
    }
}

void OverlayRenderer::renderFrame(const WorkspaceWallFrame& frame, double alpha, const CRegion& damage) {
    const auto cardFill      = CHyprColor{0.10F, 0.12F, 0.16F, static_cast<float>(0.82 * alpha)};
    const auto emptyFill     = CHyprColor{0.07F, 0.08F, 0.11F, static_cast<float>(0.58 * alpha)};
    const auto windowFill    = CHyprColor{0.18F, 0.22F, 0.29F, static_cast<float>(0.88 * alpha)};
    const auto border        = CHyprColor{0.55F, 0.62F, 0.72F, static_cast<float>(0.50 * alpha)};
    const auto activeAccent  = CHyprColor{0.37F, 0.78F, 1.0F, static_cast<float>(0.95 * alpha)};
    const auto selectAccent  = CHyprColor{0.95F, 0.80F, 0.32F, static_cast<float>(1.0 * alpha)};
    const auto windowBorder  = CHyprColor{0.70F, 0.78F, 0.90F, static_cast<float>(0.42 * alpha)};

    for (const auto& workspace : frame.workspaces) {
        const auto workspaceBox = boxFor(workspace.rect);
        drawRect(workspaceBox, workspace.empty ? emptyFill : cardFill, damage, 14);
        drawBorder(workspaceBox, 1.0, border, damage);

        if (workspace.active) {
            drawRect(CBox{workspaceBox.x, workspaceBox.y, workspaceBox.w, 4.0}, activeAccent, damage);
            drawBorder(workspaceBox, 2.0, activeAccent, damage);
        }

        if (sameTarget(m_selectedTarget, {.type = OverviewTargetType::Workspace, .workspaceId = workspace.workspaceId}))
            drawBorder(workspaceBox, 4.0, selectAccent, damage);

        renderLabel(workspace.name, workspace.rect.x + 14.0, workspace.rect.y + 12.0, std::max(1.0, workspace.rect.width - 28.0), 14, alpha, damage);

        for (const auto& window : workspace.windows) {
            const auto windowBox = boxFor(window.rect);
            drawRect(windowBox, windowFill, damage, 8);
            drawBorder(windowBox, 1.0, windowBorder, damage);

            if (sameTarget(m_selectedTarget, {.type = OverviewTargetType::Window, .workspaceId = window.workspaceId, .windowId = window.stableId}))
                drawBorder(windowBox, 3.0, selectAccent, damage);

            renderLabel(window.label, window.rect.x + 8.0, window.rect.y + 7.0, std::max(1.0, window.rect.width - 16.0), 11, alpha, damage);
        }
    }
}

void OverlayRenderer::renderLabel(const std::string& text, double x, double y, double maxWidth, int pointSize, double alpha, const CRegion& damage) {
    if (!g_pHyprRenderer || text.empty() || maxWidth <= 0.0 || alpha <= 0.001)
        return;

    const auto key = std::format("{}:{}", pointSize, text);
    auto       it  = m_textures.find(key);
    if (it == m_textures.end()) {
        it = m_textures.emplace(key, g_pHyprRenderer->renderText(text, CHyprColor{0.92F, 0.95F, 1.0F, 1.0F}, pointSize, false, "", static_cast<int>(maxWidth))).first;
    }

    const auto& texture = it->second;
    if (!texture || !texture->ok() || texture->m_size.x <= 0.0 || texture->m_size.y <= 0.0)
        return;

    CTexPassElement::SRenderData data;
    data.tex      = texture;
    data.box      = CBox{std::round(x), std::round(y), std::min(texture->m_size.x, maxWidth), texture->m_size.y};
    data.overallA = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
    data.damage   = damage;

    g_pHyprRenderer->draw(data, damage);
}

const WorkspaceWallFrame* OverlayRenderer::firstFrame() const noexcept {
    if (m_frames.empty())
        return nullptr;

    return &m_frames.front();
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
