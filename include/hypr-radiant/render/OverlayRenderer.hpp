#pragma once

#include <hypr-radiant/config/Config.hpp>
#include <hypr-radiant/config/Preferences.hpp>
#include <hypr-radiant/overview/PreferencesPanelGeometry.hpp>
#include <hypr-radiant/render/FadeAnimation.hpp>
#include <hypr-radiant/RadiantState.hpp>
#include <hypr-radiant/overview/HitTester.hpp>
#include <hypr-radiant/render/LabelRenderer.hpp>
#include <hypr-radiant/overview/SearchMatcher.hpp>
#include <hypr-radiant/overview/SearchSuggestions.hpp>
#include <hypr-radiant/overview/WorkspaceWallLayout.hpp>

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprutils/math/Region.hpp>

#include <chrono>
#include <functional>
#include <optional>
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
    ShowAppExpose,
};

struct PointerAction {
    PointerActionType type = PointerActionType::None;
    OverviewTarget target;
    std::uint64_t windowId = 0;
};

class OverlayRenderer {
  public:
    OverlayRenderer(const RadiantConfig& config, PreferencesStore& preferences);

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
    void togglePreferences();
    [[nodiscard]] PointerAction activatePreference();
    void setWorkspaceShelfVisible(bool visible);
    void setHintDockVisible(bool visible);
    void toggleWorkspaceShelf();
    void setWorkspaceShelfGestureProgress(bool revealing, double progress);
    void finishWorkspaceShelfGesture(bool revealing, bool commit);
    void pointerMoved(double x, double y);
    [[nodiscard]] PointerAction pointerButton(bool pressed, double x, double y);
    [[nodiscard]] std::optional<std::chrono::milliseconds> beginWindowClose(std::uint64_t windowId);
    void cancelWindowClose(std::uint64_t windowId);
    void completeWindowClose(std::uint64_t windowId);
    void refresh(RadiantState state);
    void beginGestureOpen(RadiantState state);
    void setGestureProgress(bool opening, double progress);
    void finishGesture(bool opening, bool commit);

    [[nodiscard]] bool           active() const noexcept;
    [[nodiscard]] bool           searchActive() const noexcept;
    [[nodiscard]] bool           preferencesVisible() const noexcept;
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
    void renderHintDock(const WorkspaceWallFrame& frame, double contentAlpha, CHyprColor accent, const CRegion& damage);
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
    /// The card the pointer carries during a drag, and the same card settling into place after the
    /// button comes up. `draggedSlot` is where the dragged window sits in this frame, so the lift
    /// can start from the card itself; it is empty when the window belongs to another monitor.
    void renderDragOverlay(const WorkspaceWallFrame& frame, std::optional<LayoutRect> draggedSlot, double alpha, const CRegion& damage);
    void renderDragCard(const WindowCard& window, const LayoutRect& rect, double alpha, double lift, const CRegion& damage);
    void renderSearchPanel(const WorkspaceWallFrame& frame, double alpha, const CRegion& damage);
    void renderPreferencesPanel(const WorkspaceWallFrame& frame, double alpha, const CRegion& damage);

    [[nodiscard]] std::vector<OverviewTarget> matchingSearchTargets() const;
    [[nodiscard]] std::vector<SearchSuggestion> matchingSearchSuggestions() const;
    [[nodiscard]] OverviewTarget searchTargetAt(const WorkspaceWallFrame& frame, double x, double y) const;
    [[nodiscard]] const WindowCard* findWindowCard(std::uint64_t windowId) const noexcept;
    [[nodiscard]] const WorkspaceCard* findWorkspaceCard(std::int64_t workspaceId) const noexcept;

    [[nodiscard]] const WorkspaceWallFrame* frameForMonitor(std::int64_t monitorId) const noexcept;
    [[nodiscard]] const WorkspaceWallFrame* frameForPoint(double x, double y, double& localX, double& localY) const noexcept;
    [[nodiscard]] const WorkspaceWallFrame* frameForSelectedTarget() const noexcept;
    [[nodiscard]] const WorkspaceWallFrame* activeMonitorFrame() const noexcept;
    [[nodiscard]] CHyprColor resolvedAccentColor() const;
    [[nodiscard]] LayoutMode effectiveLayoutMode() const;
    [[nodiscard]] int        effectiveAnimationDurationMs() const;
    [[nodiscard]] OverviewMode defaultOverviewMode() const;
    [[nodiscard]] PreferenceHit preferenceControlAt(double x, double y) const;
    [[nodiscard]] PointerAction applyPreference(PreferenceControl control, int value = -1);
    void rebuildAfterPreferenceChange();
    /// Surface derived from the active theme background, stepped `lift` toward its contrasting
    /// end. Lightens on dark themes and darkens on light ones.
    [[nodiscard]] CHyprColor surfaceColor(float lift, double alpha) const;
    void resetPointerInteraction();
    void animateSelection();
    /// Duration for a drag affordance, as a fraction of the configured overview animation. Returns
    /// 0 whenever motion is off, so every drag animation collapses to an instant state change.
    [[nodiscard]] int dragDurationMs(double scale) const;
    /// Promotes a held press into a drag: the card lifts out of its slot and the slot fades.
    void beginDrag();
    /// The workspace a card released over `hit` would land on, or nothing when there is no
    /// destination there. Window hits resolve to the workspace holding them.
    [[nodiscard]] OverviewTarget dropTargetFor(OverviewTarget hit) const;
    /// Re-plays the destination highlight whenever the workspace under the dragged card changes.
    void updateDropTarget(OverviewTarget target);
    /// Hands the released card to the settle animation, which flies it into the workspace it was
    /// dropped on, or back into its own slot when the drop had no destination.
    void beginDragSettle(double x, double y);
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

    // Where a released drag card flies to while it fades out. Kept apart from the live drag state
    // because the settle outlives the drag: the button is already up and the pointer interaction
    // reset by the time it plays.
    struct DragSettle {
        std::int64_t  monitorId = -1;
        std::uint64_t windowId  = 0;
        LayoutRect    from;
        LayoutRect    to;
    };

    const RadiantConfig&                                  m_config;
    PreferencesStore&                                     m_preferences;
    FadeAnimation                                      m_animation;
    FadeAnimation                                      m_stageTransition;
    FadeAnimation                                      m_selectionTransition;
    FadeAnimation                                      m_shelfTransition;
    FadeAnimation                                      m_dockTransition;
    FadeAnimation                                      m_closeButtonTransition;
    FadeAnimation                                      m_closeButtonHotTransition;
    FadeAnimation                                      m_windowCloseTransition;
    FadeAnimation                                      m_pressTransition;
    FadeAnimation                                      m_dragLiftTransition;
    FadeAnimation                                      m_dropTargetTransition;
    FadeAnimation                                      m_dragSettleTransition;
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
    std::uint64_t                                         m_closingWindowId = 0;
    std::int64_t                                          m_closingWindowMonitorId = -1;
    bool                                                  m_closeButtonHot = false;
    bool                                                  m_pointerCursorActive = false;
    std::string                                           m_searchQuery;
    bool                                                  m_searchActive = false;
    bool                                                  m_preferencesVisible = false;
    std::int64_t                                          m_preferencesMonitorId = -1;
    PreferenceControl                                     m_selectedPreference = PreferenceControl::WorkspaceView;
    PreferenceHit                                         m_pointerDownPreference;
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
    DragSettle                                            m_dragSettle;
    bool                                                  m_pointerDown = false;
    bool                                                  m_dragging = false;
};

} // namespace hypr_radiant
