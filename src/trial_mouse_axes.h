// Adjust relative mouse events before the SDK turns them into a right stick.
#pragma once
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/ui/window_listener.h>
#include "game_cvars.h"

REXCVAR_DECLARE(bool, mnk_mode);
REXCVAR_DECLARE(bool, mnk_mouse);

namespace trial_mouse {
inline void ScaleRelativeMotion(rex::ui::MouseEvent& event, double scale) {
  // Reconstruct through the public API; keep position, buttons and routing.
  const bool handled = event.is_handled();
  event = rex::ui::MouseEvent(event.target(), event.button(), event.x(), event.y(),
                            event.scroll_x(), event.scroll_y(), event.dx(),
                            static_cast<float>(event.dy() * scale));
  event.set_handled(handled);
}

class Axes final : public rex::ui::WindowInputListener,
                   public rex::ui::WindowListener {
 public:
  void Attach(rex::ui::Window* window) {
    if (!window || window_) return;
    window_ = window;
    // The SDK MnK driver uses layer 0. Later listeners on the same layer run
    // first, while higher layers (overlays) still get the unmodified event.
    window->AddInputListener(this, 0);
    window->AddListener(this);
  }
  void OnMouseMove(rex::ui::MouseEvent& event) override {
    if (!window_ || !window_->HasFocus() || event.is_handled() ||
        !REXCVAR_GET(mnk_mode) || !REXCVAR_GET(mnk_mouse) ||
        !rex::input::mnk::IsMouseLookActive()) return;
    ScaleRelativeMotion(event, REXCVAR_GET(badco_mouse_y_scale));
  }
  void OnClosing(rex::ui::UIEvent&) override {
    auto* window = window_;
    window_ = nullptr;
    if (window) {
      window->RemoveInputListener(this);
      window->RemoveListener(this);
    }
  }
 private:
  rex::ui::Window* window_ = nullptr;
};

// Process lifetime; the window closing callback detaches before destruction.
inline Axes axes;
}  // namespace trial_mouse
