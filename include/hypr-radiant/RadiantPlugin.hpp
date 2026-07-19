#pragma once

#include <hypr-radiant/ActivationController.hpp>
#include <hypr-radiant/Config.hpp>
#include <hypr-radiant/GestureController.hpp>
#include <hypr-radiant/InputController.hpp>
#include <hypr-radiant/OverlayRenderer.hpp>
#include <hypr-radiant/StateCollector.hpp>

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <chrono>
#include <string>
#include <string_view>

namespace hypr_radiant {

class RadiantPlugin {
  public:
    explicit RadiantPlugin(HANDLE handle);

    bool            initialize();
    void            shutdown();
    SDispatchResult toggle(const std::string& args);
    SDispatchResult showApplication(const std::string& args);
    SDispatchResult status(const std::string& args);
    [[nodiscard]] bool active() const noexcept;

  private:
    using Clock = std::chrono::steady_clock;

    void activate(OverviewTarget target, std::string_view source);
    void recordTransition(std::string message, bool notify = false);

    HANDLE          m_handle = nullptr;
    ActivationController m_activation;
    RadiantConfig     m_config;
    InputController m_input;
    GestureController m_gestures;
    StateCollector  m_stateCollector;
    OverlayRenderer m_overlay;
    Clock::time_point m_lastOpenedAt = Clock::time_point::min();
    std::string       m_lastTransition = "plugin loaded";
};

} // namespace hypr_radiant
