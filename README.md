# hypr-radiant

`hypr-radiant` is a small Hyprland C++ plugin targeting Hyprland 0.55.2 (commit
39d7e209c79d451efab1b21151d5938289da838d). It provides a `radiant:toggle`
dispatcher that opens a native Workspace Wall overview for switching between
workspaces and open windows.

Workspace/window rearrangement is not implemented yet.

## Requirements

- Hyprland 0.55.2 (commit 39d7e209c79d451efab1b21151d5938289da838d) development headers
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

The command opens or closes the overview. The default `stage` layout shows an
equal-size workspace rail at the top and reconstructs the highlighted
workspace spatially below it from live window textures. Moving through the
rail previews a workspace without switching to it. The `workspace_wall`
layout retains the original grid.

## Controls

- `hyprctl dispatch radiant:toggle`: open or close overview
- Hover a rail workspace or stage window: move selection
- Mouse wheel or `Left`/`Right`: move through the workspace rail
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

## Configuration

```ini
plugin {
    radiant {
        opacity = 0.94
        animation_duration = 180
        layout = stage
        accent_color = auto
    }
}
```

- `opacity`: overlay opacity from `0.0` to `1.0`
- `animation_duration`: fade duration in milliseconds from `0` to `2000`
- `layout`: overview layout mode. Supported values are `stage` and `workspace_wall`.
- `accent_color`: `auto` inherits the focused Hyprland window border; explicit
  `#RRGGBB`, `#RRGGBBAA`, `rgb(RRGGBB)`, and `rgba(RRGGBBAA)` values override it.

For development without installing through `hyprpm`, build the plugin and load
the generated shared object directly:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
hyprctl plugin load "$PWD/build/hypr-radiant.so"
hyprctl dispatch radiant:toggle
hyprctl plugin unload "$PWD/build/hypr-radiant.so"
```
