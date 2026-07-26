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
};

enum class WindowViewPreference {
    Spatial,
    Grouped,
};

enum class AccentPreference {
    FollowConfig,
    Green,
    Blue,
    Violet,
};

enum class MotionPreference {
    FollowConfig,
    Reduced,
    Off,
};

struct PreferencesState {
    WorkspaceViewPreference workspaceView = WorkspaceViewPreference::FollowConfig;
    WindowViewPreference    windowView    = WindowViewPreference::Spatial;
    AccentPreference        accent        = AccentPreference::FollowConfig;
    MotionPreference        motion        = MotionPreference::FollowConfig;

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
[[nodiscard]] std::string_view label(AccentPreference preference);
[[nodiscard]] std::string_view label(MotionPreference preference);

} // namespace hypr_radiant
