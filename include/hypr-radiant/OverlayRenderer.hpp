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
    CreateWorkspaceAndMoveWindow,
    CloseWindow,
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
    void setWorkspaceShelfVisible(bool visible);
    void setHintDockVisible(bool visible);
    void toggleWorkspaceShelf();
    void setWorkspaceShelfGestureProgress(bool revealing, double progress);
    void finishWorkspaceShelfGesture(bool revealing, bool commit);
    void pointerMoved(double x, double y);
    [[nodiscard]] PointerAction pointerButton(bool pressed, double x, double y);
    void refresh(RadiantState state);
    void beginGestureOpen(RadiantState state);
    void setGestureProgress(bool opening, double progress);
    void finishGesture(bool opening, bool commit);

    [[nodiscard]] bool           active() const noexcept;
    [[nodiscard]] bool           searchActive() const noexcept;
    [[nodiscard]] bool           workspaceShelfVisible() const noexcept;
    [[nodiscard]] bool           hintDockVisible() const noexcept;
    [[nodiscard]] OverviewMode   mode() const noexcept;
    [[nodiscard]] OverviewTarget selectedTarget() const noexcept;
    [[nodiscard]] OverviewTarget hitTest(double x, double y) const;

  private:
    void onRenderStage(eRenderStage stage);
    void renderCurrentMonitor(double alpha);
    void damageAllMonitors() const;
    void damageMonitorById(std::int64_t monitorId) const;
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
    void renderColoredLabel(const std::string& text, double x, double y, double maxWidth, int pointSize, CHyprColor color, double alpha,
        const CRegion& damage);
    void renderCenteredLabel(const std::string& text, const CBox& within, int pointSize, CHyprColor color, double alpha, const CRegion& damage);
    void renderRightAlignedLabel(const std::string& text, const CBox& within, int pointSize, CHyprColor color, double alpha, const CRegion& damage);
    [[nodiscard]] SP<Render::ITexture> labelTexture(const std::string& text, double maxWidth, int pointSize, CHyprColor color);
    [[nodiscard]] RadiantSize            measureLabel(const std::string& text, double maxWidth, int pointSize, CHyprColor color);

    [[nodiscard]] std::vector<OverviewTarget> matchingSearchTargets() const;
    [[nodiscard]] OverviewTarget searchTargetAt(const WorkspaceWallFrame& frame, double x, double y) const;
    [[nodiscard]] const WindowCard* findWindowCard(std::uint64_t windowId) const noexcept;
    [[nodiscard]] const WorkspaceCard* findWorkspaceCard(std::int64_t workspaceId) const noexcept;

    [[nodiscard]] const WorkspaceWallFrame* frameForMonitor(std::int64_t monitorId) const noexcept;
    [[nodiscard]] const WorkspaceWallFrame* frameForPoint(double x, double y, double& localX, double& localY) const noexcept;
    [[nodiscard]] const WorkspaceWallFrame* frameForSelectedTarget() const noexcept;
    [[nodiscard]] const WorkspaceWallFrame* activeMonitorFrame() const noexcept;
    [[nodiscard]] CHyprColor resolvedAccentColor() const;
    /// Surface derived from the active theme background, stepped `lift` toward its contrasting
    /// end. Lightens on dark themes and darkens on light ones.
    [[nodiscard]] CHyprColor surfaceColor(float lift, double alpha) const;
    void resetPointerInteraction();
    void animateSelection();
    /// Tracks which card owns the close button and whether the pointer is on it, so the button can
    /// animate instead of popping in and out as the pointer crosses cards.
    void updateCloseAffordance(double x, double y);
    void setPointerCursorOverride(bool pointerCursor);

    const RadiantConfig&                                  m_config;
    FadeAnimation                                      m_animation;
    FadeAnimation                                      m_stageTransition;
    FadeAnimation                                      m_selectionTransition;
    FadeAnimation                                      m_shelfTransition;
    FadeAnimation                                      m_dockTransition;
    FadeAnimation                                      m_closeButtonTransition;
    FadeAnimation                                      m_closeButtonHotTransition;
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
    // -1 animates the stage on every monitor (open/refresh); otherwise only this monitor animates,
    // so a hover-driven preview change stays local to the screen under the pointer.
    std::int64_t                                          m_stageTransitionMonitorId = -1;
    bool                                                  m_pointerInsideShelfBand = false;
    bool                                                  m_pointerInsideDockBand = false;
    std::uint64_t                                         m_closeButtonWindowId = 0;
    bool                                                  m_closeButtonHot = false;
    bool                                                  m_pointerCursorActive = false;
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
