#pragma once

#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>

#include <functional>

namespace hypr_radiant {

class ShortcutController {
  public:
    using EnabledFn = std::function<bool()>;

    void install(EnabledFn enabled);
    void uninstall();

  private:
    void installBinding();
    void removeBinding();

    EnabledFn          m_enabled;
    SP<SKeybind>       m_binding;
    CHyprSignalListener m_preReloadListener;
    CHyprSignalListener m_reloadedListener;
};

} // namespace hypr_radiant
