# hypr-radiant

`hypr-radiant` is a native workspace overview for Hyprland 0.55.x.
Its default stage layout combines live single-layer previews and translucent glass
with Omarchy's theme-driven palette, monospace interface language, and
keyboard-first workflow.

## Requirements

- Hyprland 0.55.x with development headers matching the running compositor
- `hyprpm`
- CMake 3.25 or newer
- A C++23-capable compiler
- `pkg-config`

If the plugin headers do not match the running Hyprland, hypr-radiant refuses to
load, shows a notification, logs the mismatch, and Hyprland ejects the plugin.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The plugin is built as:

```text
build/hypr-radiant.so
```

## Install With hyprpm

`hyprpm` needs superuser privileges to create its state/cache directory
(`/var/cache/hyprpm/`) and install matching Hyprland headers. Make sure
`sudo`, `doas`, or another supported superuser binary is available before
running the commands below.

From a clone of this repository:

```sh
hyprpm add "$PWD"
hyprpm enable hypr-radiant
hyprpm reload
```

From GitHub:

```sh
hyprpm add https://github.com/nsumbadze/hypr-radiant
hyprpm enable hypr-radiant
hyprpm reload
```

If the plugin is already installed and you rebuilt or updated it:

```sh
hyprpm update
hyprpm reload
```

To remove it:

```sh
hyprpm disable hypr-radiant
hyprpm remove hypr-radiant
hyprpm reload
```

### Recovery and development unload

If `hyprpm reload` loads the plugin and you need to unload it manually, or if
you are testing a local build without `hyprpm`, use:

```sh
hyprctl plugin unload "$PWD/build/hypr-radiant.so"
```

This was verified to unload the plugin cleanly from a nested Hyprland 0.55.2
session.

## Dispatcher

After loading the plugin, run:

```sh
hyprctl dispatch radiant:toggle
```

To open App Exposé for the currently focused application:

```sh
hyprctl dispatch radiant:app
```

The command opens or closes the overview. The default `stage` layout expands the
window stage across the screen and reveals a lightweight workspace shelf when
the pointer reaches the top edge. Windows use a stable, organic Exposé layout:
their aspect ratios and targets remain exact while scale and placement gain a
subtle spatial variation. The `workspace_wall` layout retains the original grid.

## Controls

- `hyprctl dispatch radiant:toggle`: open or close overview
- `hyprctl dispatch radiant:open`: open overview only if it is closed
- `hyprctl dispatch radiant:close`: close overview only if it is open
- `hyprctl dispatch radiant:shelf show|hide|toggle`: control the workspace shelf
- Move the pointer to the top edge or scroll up: reveal the workspace shelf
- Leave the shelf or scroll down: hide it; scrolling down again closes via gesture
- Move the pointer to the bottom edge: reveal the shortcut dock
- Move the pointer away from the bottom edge: hide the dock again
- Hover a rail workspace or stage window: move selection
- Drag a stage window onto a rail card: move it to that workspace
- Drag a stage window onto the trailing `+`: create a workspace and move it there
- Click the trailing `+`: create and enter a workspace
- `Tab`: toggle spatial and application-grouped views
- `Left`/`Right`: move through the workspace shelf
- `Down`: enter the highlighted workspace's stage windows or move forward
- `Up`: move backward through stage windows or return to the rail
- `/`: open the command palette, including all targets for an empty query
- Type letters: open and filter search; digits filter once search is open
- `1`–`9`: immediately activate that workspace when search is closed
- `Backspace`: delete one search character
- Click a rail workspace: switch workspace
- Click a stage window: switch workspace and focus window
- `Enter`: activate selection
- `Esc`: close search and restore its previous selection, or close the overview
- Three-finger swipe up/down: open or close the overview; while open, reveal or hide the shelf
- Three-finger swipe left/right while open: preview the previous or next workspace

## Configuration

```ini
plugin {
    radiant {
        opacity = 0.94
        animation_duration = 180
        layout = stage
        accent_color = auto
        background_color = 111c18
        foreground_color = C1C497
        font_family = JetBrainsMono Nerd Font
        gesture_enabled = true
        gesture_fingers = 3
        gesture_distance = 300
    }
}
```

- `opacity`: overlay opacity from `0.0` to `1.0`
- `animation_duration`: fade duration in milliseconds from `0` to `2000`
- `layout`: overview layout mode. Supported values are `stage` and `workspace_wall`.
- `accent_color`: `auto` inherits the focused Hyprland window border; explicit
  `#RRGGBB`, `#RRGGBBAA`, `rgb(RRGGBB)`, and `rgba(RRGGBBAA)` values override it.
- `background_color` / `foreground_color`: portable RGB/RGBA palette values.
- `font_family`: interface font; `JetBrainsMono Nerd Font` is the Omarchy default.
- `gesture_enabled`: interactive vertical swipe capture; enabled by default.
- `gesture_fingers`: `3` or `4` fingers.
- `gesture_distance`: travel in logical pixels, clamped from `120` to `800`.

Set `gesture_enabled = false` if another Hyprland binding owns the same
vertical three-finger gesture.

## Test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

For development without installing through `hyprpm`, build the plugin and load
the generated shared object directly:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
hyprctl plugin load "$PWD/build/hypr-radiant.so"
hyprctl dispatch radiant:toggle
hyprctl plugin unload "$PWD/build/hypr-radiant.so"
```
