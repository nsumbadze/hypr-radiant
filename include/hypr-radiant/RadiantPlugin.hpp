#pragma once

#include <hypr-radiant/ActivationController.hpp>
#include <hypr-radiant/Config.hpp>
#include <hypr-radiant/InputController.hpp>
#include <hypr-radiant/OverlayRenderer.hpp>
#include <hypr-radiant/StateCollector.hpp>

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <string>

namespace hypr_radiant {

class RadiantPlugin {
  public:
    explicit RadiantPlugin(HANDLE handle);

    bool            initialize();
    void            shutdown();
    SDispatchResult toggle(const std::string& args);
    [[nodiscard]] bool active() const noexcept;

  private:
    void activate(OverviewTarget target);

    HANDLE          m_handle = nullptr;
    ActivationController m_activation;
    RadiantConfig     m_config;
    InputController m_input;
    StateCollector  m_stateCollector;
    OverlayRenderer m_overlay;
};

} // namespace hypr_radiant
