#pragma once

#include <hypr-radiant/Config.hpp>
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
    HANDLE          m_handle = nullptr;
    RadiantConfig     m_config;
    StateCollector  m_stateCollector;
    OverlayRenderer m_overlay;
};

} // namespace hypr_radiant
