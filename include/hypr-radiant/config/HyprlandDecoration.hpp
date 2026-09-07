#pragma once

#include <hypr-radiant/render/ChromeStyle.hpp>

#include <memory>
#include <optional>

namespace hypr_radiant {

class HyprlandDecorationReader {
  public:
    HyprlandDecorationReader();
    ~HyprlandDecorationReader();

    HyprlandDecorationReader(const HyprlandDecorationReader&) = delete;
    HyprlandDecorationReader& operator=(const HyprlandDecorationReader&) = delete;

    [[nodiscard]] std::optional<NativeDecoration> read() const;

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hypr_radiant
