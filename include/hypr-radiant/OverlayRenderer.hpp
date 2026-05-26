#pragma once

#include <hypr-radiant/Config.hpp>
#include <hypr-radiant/FadeAnimation.hpp>

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>

namespace hypr_radiant {

class OverlayRenderer {
  public:
    explicit OverlayRenderer(const RadiantConfig& config);

    void install();
    void uninstall();
    void toggle();
    void hideImmediate();

    [[nodiscard]] bool active() const noexcept;

  private:
    void onRenderStage(eRenderStage stage);
    void renderCurrentMonitor(double alpha);
    void damageAllMonitors() const;

    const RadiantConfig&   m_config;
    FadeAnimation       m_animation;
    CHyprSignalListener m_renderStageListener;
};

} // namespace hypr_radiant
