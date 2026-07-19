#pragma once

#include <hypr-radiant/Config.hpp>
#include <hypr-radiant/FadeAnimation.hpp>
#include <hypr-radiant/RadiantState.hpp>
#include <hypr-radiant/HitTester.hpp>
#include <hypr-radiant/SearchMatcher.hpp>
#include <hypr-radiant/WorkspaceWallLayout.hpp>

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprutils/math/Region.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Render {
class ITexture;
}

namespace hypr_radiant {

enum class PointerActionType {
    None,
    Activate,
    MoveWindow,
};

struct PointerAction {
    PointerActionType type = PointerActionType::None;
    OverviewTarget target;
    std::uint64_t windowId = 0;
};

class OverlayRenderer {
  public:
    explicit OverlayRenderer(const RadiantConfig& config);

    void install();
    void uninstall();
    void show(RadiantState state);
    void showAppExpose(RadiantState state, std::string applicationClass);
    void toggle(RadiantState state);
    void hideImmediate();
    void moveSelection(NavigationDirection direction);
    void selectTargetAt(double x, double y);
    void appendSearchChar(char value);
    void beginSearch();
    void backspaceSearch();
    void clearSearchOrHide();
    void toggleGroupedMode();
    void pointerMoved(double x, double y);
    [[nodiscard]] PointerAction pointerButton(bool pressed, double x, double y);
    void refresh(RadiantState state);

    [[nodiscard]] bool           active() const noexcept;
    [[nodiscard]] bool           searchActive() const noexcept;
    [[nodiscard]] OverviewMode   mode() const noexcept;
    [[nodiscard]] OverviewTarget selectedTarget() const noexcept;
    [[nodiscard]] OverviewTarget hitTest(double x, double y) const;

  private:
    void onRenderStage(eRenderStage stage);
    void renderCurrentMonitor(double alpha);
    void damageAllMonitors() const;
    void rebuildFrames();
    void rebuildSearchMatches();
    void selectFirstSearchMatch();
    void moveSearchSelection(NavigationDirection direction);
    void clearSearch();
    void renderFrame(const WorkspaceWallFrame& frame, double alpha, const CRegion& damage);
    void renderStageFrame(const WorkspaceWallFrame& frame, double alpha, const CRegion& damage);
    void renderWindowPreview(const WindowCard& window, const CBox& clipBox, double alpha, const CRegion& damage);
    void renderSearchPanel(const WorkspaceWallFrame& frame, double alpha, const CRegion& damage);
    void renderLabel(const std::string& text, double x, double y, double maxWidth, int pointSize, double alpha, const CRegion& damage);

    [[nodiscard]] std::vector<OverviewTarget> matchingSearchTargets() const;
    [[nodiscard]] OverviewTarget searchTargetAt(const WorkspaceWallFrame& frame, double x, double y) const;
    [[nodiscard]] const WindowCard* findWindowCard(std::uint64_t windowId) const noexcept;
    [[nodiscard]] const WorkspaceCard* findWorkspaceCard(std::int64_t workspaceId) const noexcept;

    [[nodiscard]] const WorkspaceWallFrame* frameForMonitor(std::int64_t monitorId) const noexcept;
    [[nodiscard]] const WorkspaceWallFrame* frameForPoint(double x, double y, double& localX, double& localY) const noexcept;
    [[nodiscard]] const WorkspaceWallFrame* frameForSelectedTarget() const noexcept;
    [[nodiscard]] const WorkspaceWallFrame* activeMonitorFrame() const noexcept;
    [[nodiscard]] CHyprColor resolvedAccentColor() const;
    void resetPointerInteraction();

    const RadiantConfig&                                  m_config;
    FadeAnimation                                      m_animation;
    FadeAnimation                                      m_stageTransition;
    CHyprSignalListener                                m_renderStageListener;
    CHyprSignalListener                                m_monitorLayoutListener;
    RadiantState                                         m_state;
    WorkspaceWallLayout                                m_layout;
    HitTester                                          m_hitTester;
    SearchMatcher                                      m_searchMatcher;
    std::vector<WorkspaceWallFrame>                       m_frames;
    std::vector<WorkspaceWallFrame>                       m_previousFrames;
    std::unordered_map<std::int64_t, LayoutRect>          m_frameBoundsByMonitor;
    OverviewTarget                                        m_selectedTarget;
    std::int64_t                                          m_selectedFrameMonitorId = -1;
    std::string                                           m_searchQuery;
    bool                                                  m_searchActive = false;
    OverviewMode                                          m_mode = OverviewMode::Spatial;
    std::string                                           m_applicationFilter;
    OverviewTarget                                        m_preSearchTarget;
    std::int64_t                                          m_preSearchMonitorId = -1;
    std::unordered_set<std::uint64_t>                     m_searchMatches;
    std::unordered_map<std::string, SP<Render::ITexture>> m_textures;
    OverviewTarget                                        m_pointerDownTarget;
    OverviewTarget                                        m_dragTarget;
    RadiantPoint                                            m_pointerDownPosition;
    RadiantPoint                                            m_pointerPosition;
    bool                                                  m_pointerDown = false;
    bool                                                  m_dragging = false;
};

} // namespace hypr_radiant
