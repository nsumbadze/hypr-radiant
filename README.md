# hypr-radiant

`hypr-radiant` is a small Hyprland C++ plugin targeting Hyprland 0.55.x. It
provides a `radiant:toggle` dispatcher that opens a native Workspace Wall
overview for switching between workspaces and open windows.

Window previews, search, and workspace/window rearrangement are not implemented
yet.

## Requirements

- Hyprland 0.55.x development headers
- `hyprpm`
- CMake 3.25 or newer
- A C++23-capable compiler
- `pkg-config`

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

## Dispatcher

After loading the plugin, run:

```sh
hyprctl dispatch radiant:toggle
```

The command opens or closes the Workspace Wall overview. The overview shows
workspace cards and labeled window cards from the current compositor state.
Typing while the overview is open searches open window labels without changing
the grid layout.

## Controls

- `hyprctl dispatch radiant:toggle`: open or close overview
- Hover workspace or window card: move selection
- Type letters, digits, or space: search open window labels
- `Backspace`: delete one search character
- Click workspace card: switch workspace
- Click window card: switch workspace and focus window
- Arrow keys: move selection
- `Enter`: activate selection
- `Esc`: clear search, or close overview when search is empty

## Configuration

```ini
plugin {
    radiant {
        opacity = 0.55
        animation_duration = 180
        layout = stage
    }
}
```

- `opacity`: overlay opacity from `0.0` to `1.0`
- `animation_duration`: fade duration in milliseconds from `0` to `2000`
- `layout`: overview layout mode. Supported values are `stage` and `workspace_wall`.

For development without installing through `hyprpm`, build the plugin and load
the generated shared object directly:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
hyprctl plugin load "$PWD/build/hypr-radiant.so"
hyprctl dispatch radiant:toggle
hyprctl plugin unload "$PWD/build/hypr-radiant.so"
```
