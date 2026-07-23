#pragma once

#include <hypr-radiant/compositor/ActivationController.hpp>
#include <hypr-radiant/config/Config.hpp>
#include <hypr-radiant/input/GestureController.hpp>
#include <hypr-radiant/input/InputController.hpp>
#include <hypr-radiant/render/OverlayRenderer.hpp>
#include <hypr-radiant/compositor/StateCollector.hpp>

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
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
    SDispatchResult open(const std::string& args);
    SDispatchResult close(const std::string& args);
    SDispatchResult toggle(const std::string& args);
    SDispatchResult showApplication(const std::string& args);
    SDispatchResult shelf(const std::string& args);
    SDispatchResult status(const std::string& args);

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
    // A window closing while the overview is up leaves its card behind, so the layout is re-flowed
    // as soon as the compositor reports one gone, however it was closed.
    CHyprSignalListener m_windowDestroyListener;
    Clock::time_point m_lastOpenedAt = Clock::time_point::min();
    std::string       m_lastTransition = "plugin loaded";
};

} // namespace hypr_radiant
