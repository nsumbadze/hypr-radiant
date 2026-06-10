# hypr-radiant

`hypr-radiant` is a small Hyprland C++ plugin targeting Hyprland 0.55.x. It
currently provides a `radiant:toggle` dispatcher that fades a fullscreen overlay
and collects a snapshot of monitor, workspace, and window state for upcoming
overview UI work.

Window previews, input capture, and workspace/window rearrangement are not
implemented yet.

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

The command toggles internal overlay state, logs whether radiant is active, and
logs a concise state snapshot count for monitors, workspaces, windows, and
mapped windows. The snapshot is the foundation for future overview rendering.

## Configuration

```ini
plugin {
    radiant {
        opacity = 0.55
        animation_duration = 180
        layout = workspace_wall
    }
}
```

- `opacity`: overlay opacity from `0.0` to `1.0`
- `animation_duration`: fade duration in milliseconds from `0` to `2000`
- `layout`: overview layout mode. Only `workspace_wall` is supported for now.

For development without installing through `hyprpm`, build the plugin and load
the generated shared object directly:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
hyprctl plugin load "$PWD/build/hypr-radiant.so"
hyprctl dispatch radiant:toggle
hyprctl plugin unload "$PWD/build/hypr-radiant.so"
```
