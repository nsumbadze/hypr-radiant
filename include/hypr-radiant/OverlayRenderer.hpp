#pragma once

#include <hypr-radiant/Config.hpp>
#include <hypr-radiant/FadeAnimation.hpp>
#include <hypr-radiant/RadiantState.hpp>
#include <hypr-radiant/HitTester.hpp>
#include <hypr-radiant/LabelRenderer.hpp>
#include <hypr-radiant/SearchMatcher.hpp>
#include <hypr-radiant/WorkspaceWallLayout.hpp>

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprutils/math/Region.hpp>

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

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
    void renderHintDock(const WorkspaceWallFrame& frame, double contentAlpha, CHyprColor accent, CHyprColor railSurface, const CRegion& damage);
    // Derived per-frame values the stage-window pass reads; bundled so the pass takes one named
    // argument instead of eight positional doubles and colours that could be transposed unnoticed.
    struct StageContext {
        double     contentAlpha;
        double     stageAlpha;
        double     selectionTransition;
        CHyprColor accent;
        CHyprColor stageSurface;
        CHyprColor railSurface;
        LayoutRect displayedStageBounds;
        LayoutRect pushedStageBounds;
    };
    void renderStageWindows(const WorkspaceWallFrame& frame, const StageContext& ctx, const CRegion& damage);
    void renderWindowPreview(const WindowCard& window, const CBox& clipBox, double alpha, const CRegion& damage);
    void renderSearchPanel(const WorkspaceWallFrame& frame, double alpha, const CRegion& damage);

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
    /// Drops the affordances that must not outlive a visible overlay — chiefly the pointer cursor
    /// override, which would otherwise leave a hand cursor on the desktop after closing.
    void releaseHoverAffordances();
    /// Shared body of show()/showAppExpose(): both reset identical state and differed only in mode,
    /// filter, initial selection and stage-entrance duration. `selectInitial` picks the opening
    /// selection for the active frame.
    void beginSession(RadiantState state, OverviewMode mode, std::string applicationFilter, int stageDurationMs,
        const std::function<OverviewTarget(const WorkspaceWallFrame&)>& selectInitial);

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
    LabelRenderer                                         m_labels;
    OverviewTarget                                        m_pointerDownTarget;
    OverviewTarget                                        m_dragTarget;
    RadiantPoint                                            m_pointerDownPosition;
    RadiantPoint                                            m_pointerPosition;
    bool                                                  m_pointerDown = false;
    bool                                                  m_dragging = false;
};

} // namespace hypr_radiant
