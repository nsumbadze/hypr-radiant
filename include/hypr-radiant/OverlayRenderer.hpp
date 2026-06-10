#pragma once

#include <hypr-radiant/Config.hpp>
#include <hypr-radiant/FadeAnimation.hpp>
#include <hypr-radiant/RadiantState.hpp>
#include <hypr-radiant/HitTester.hpp>
#include <hypr-radiant/WorkspaceWallLayout.hpp>

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprutils/math/Region.hpp>

#include <string>
#include <unordered_map>

namespace Render {
class ITexture;
}

namespace hypr_radiant {

class OverlayRenderer {
  public:
    explicit OverlayRenderer(const RadiantConfig& config);

    void install();
    void uninstall();
    void show(RadiantState state);
    void toggle(RadiantState state);
    void hideImmediate();
    void moveSelection(NavigationDirection direction);
    void selectTargetAt(double x, double y);

    [[nodiscard]] bool           active() const noexcept;
    [[nodiscard]] OverviewTarget selectedTarget() const noexcept;
    [[nodiscard]] OverviewTarget hitTest(double x, double y) const;

  private:
    void onRenderStage(eRenderStage stage);
    void renderCurrentMonitor(double alpha);
    void damageAllMonitors() const;
    void rebuildFrames();
    void renderFrame(const WorkspaceWallFrame& frame, double alpha, const CRegion& damage);
    void renderLabel(const std::string& text, double x, double y, double maxWidth, int pointSize, double alpha, const CRegion& damage);

    [[nodiscard]] const WorkspaceWallFrame* frameForMonitor(std::int64_t monitorId) const noexcept;
    [[nodiscard]] const WorkspaceWallFrame* frameForPoint(double x, double y, double& localX, double& localY) const noexcept;
    [[nodiscard]] const WorkspaceWallFrame* frameForSelectedTarget() const noexcept;
    [[nodiscard]] const WorkspaceWallFrame* activeMonitorFrame() const noexcept;

    const RadiantConfig&                                  m_config;
    FadeAnimation                                      m_animation;
    CHyprSignalListener                                m_renderStageListener;
    CHyprSignalListener                                m_monitorLayoutListener;
    RadiantState                                         m_state;
    WorkspaceWallLayout                                m_layout;
    HitTester                                          m_hitTester;
    std::vector<WorkspaceWallFrame>                       m_frames;
    std::unordered_map<std::int64_t, LayoutRect>          m_frameBoundsByMonitor;
    OverviewTarget                                        m_selectedTarget;
    std::int64_t                                          m_selectedFrameMonitorId = -1;
    std::unordered_map<std::string, SP<Render::ITexture>> m_textures;
};

} // namespace hypr_radiant
