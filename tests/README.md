# Test boundaries

This suite focuses on logic that can run without a live Hyprland process. Hyprland-facing glue is intentionally covered by integration/runtime checks instead of local unit mocks.

## Testable pure logic

These modules do not directly touch Hyprland globals and are suitable for unit tests:

- `WorkspaceWallLayout`: computes workspace/window card geometry from snapshots.
- `HitTester`: resolves selections and pointer hits inside computed frames.
- `SearchMatcher`: matches window labels and workspace names/numbers.
- `FadeAnimation`: advances visibility/alpha state from time deltas.
- Config parsing: converts plugin config strings into typed options.

## Non-testable without mocking

These modules directly read Hyprland compositor/renderer/input state or subscribe to EventBus signals:

- `StateCollector`: reads monitors, workspaces, and windows from `g_pCompositor`.
- `ActivationController`: resolves live windows/workspaces through `g_pCompositor` and Hyprland focus state.
- `InputController` listener wiring: installs mouse/keyboard listeners through `Event::bus()` and reads `g_pInputManager` for pointer activation.
- `OverlayRenderer` render path: subscribes to render/monitor events through `Event::bus()`, reads `g_pCompositor`, and submits damage/render pass work through `g_pHyprRenderer`.

## Policy

We do not manufacture mocks or stubs for Hyprland globals such as `g_pCompositor`, `g_pHyprRenderer`, `g_pInputManager`, or `Event::bus()`. Code that depends on those process-global services is validated through integration/runtime testing in Hyprland, while unit tests remain limited to meaningful pure logic.
