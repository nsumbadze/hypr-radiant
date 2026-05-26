# hypr-radiant

`hypr-radiant` is a minimal Hyprland C++ plugin skeleton targeting Hyprland
0.55.x. It is intentionally small: the only runtime behavior is a dispatcher
that toggles internal plugin state and logs the new state.

No overview UI, rendering, input capture, or window layout behavior is
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

The command toggles an internal boolean and logs whether radiant is now active or
inactive.

For development without installing through `hyprpm`, build the plugin and load
the generated shared object directly:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
hyprctl plugin load "$PWD/build/hypr-radiant.so"
hyprctl dispatch radiant:toggle
hyprctl plugin unload "$PWD/build/hypr-radiant.so"
```
