#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace hypr_radiant {

enum class WorkspaceViewPreference {
    FollowConfig,
    Stage,
    WorkspaceWall,
    Carousel,
    Ribbon,
};

enum class WindowViewPreference {
    Spatial,
    Grouped,
    Deck,
};

enum class MotionPreference {
    FollowConfig,
    Quattro,
    Cyberpunk,
    Tron,
    Elegant,
    Reduced,
    Off,
};

enum class ChromePreference { FollowConfig, Radiant, Native, Flat };
enum class ShelfPreference { FollowConfig, Auto, Always, Hidden };

struct PreferencesState {
    WorkspaceViewPreference workspaceView = WorkspaceViewPreference::FollowConfig;
    WindowViewPreference    windowView    = WindowViewPreference::Spatial;
    MotionPreference motion = MotionPreference::FollowConfig;
    ChromePreference chrome = ChromePreference::FollowConfig;
    ShelfPreference  shelf  = ShelfPreference::FollowConfig;
    /// Empty follows the desktop's active Omarchy theme. Otherwise this is an installed theme
    /// slug whose palette is applied to Radiant only.
    std::string nativeTheme;

    bool operator==(const PreferencesState&) const = default;
};

[[nodiscard]] PreferencesState parsePreferences(std::string_view contents);
[[nodiscard]] std::string      serializePreferences(const PreferencesState& preferences);
[[nodiscard]] std::filesystem::path defaultPreferencesPath();

class PreferencesStore {
  public:
    explicit PreferencesStore(std::filesystem::path path = defaultPreferencesPath());

    void load();
    [[nodiscard]] bool save() const;

    [[nodiscard]] const PreferencesState& state() const noexcept;
    [[nodiscard]] PreferencesState&       state() noexcept;
    [[nodiscard]] const std::filesystem::path& path() const noexcept;

  private:
    std::filesystem::path m_path;
    PreferencesState      m_state;
};

[[nodiscard]] std::string_view label(WorkspaceViewPreference preference);
[[nodiscard]] std::string_view label(WindowViewPreference preference);
[[nodiscard]] std::string_view label(MotionPreference preference);
[[nodiscard]] std::string_view label(ChromePreference preference);
[[nodiscard]] std::string_view label(ShelfPreference preference);

} // namespace hypr_radiant
