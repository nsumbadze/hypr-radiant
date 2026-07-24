# hypr-radiant

A window overview for Hyprland. One keybind shows every window on every
workspace, so you jump straight to the one you want instead of cycling through
workspaces looking for it.

It reads your Omarchy theme, so it should match the rest of your desktop without
configuring anything.

![The stage layout with the workspace shelf open](assets/stage.webp)

## Requirements

- Hyprland 0.55.x, with development headers matching the compositor you run
- `hyprpm`
- CMake 3.25 or newer
- A C++23 compiler
- `pkg-config`

The plugin ABI is tied to the exact Hyprland build. If the headers do not match,
the plugin refuses to load, sends a notification, and Hyprland unloads it again.
Nothing breaks, but you do need to rebuild after a Hyprland update.

## Install

`hyprpm` needs superuser rights the first time, since it creates
`/var/cache/hyprpm/` and installs matching Hyprland headers. Make sure `sudo` or
`doas` is available before you start.

```sh
hyprpm add https://github.com/nsumbadze/hypr-radiant
hyprpm enable hypr-radiant
hyprpm reload
```

After rebuilding or updating it:

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

## Usage

Bind the toggle to whatever you like:

```ini
bind = SUPER, TAB, exec, hyprctl dispatch radiant:toggle
```

| Dispatcher | What it does |
| --- | --- |
| `radiant:toggle` | Open or close the overview |
| `radiant:open` | Open it, only if it is closed |
| `radiant:close` | Close it, only if it is open |
| `radiant:app` | App Exposé for the focused application |
| `radiant:shelf show\|hide\|toggle` | Control the workspace shelf |
| `radiant:status` | Notification with the current state, for debugging |

A three-finger swipe up opens it and a swipe down closes it. While it is open,
swipe left or right to preview the next workspace. Set `gesture_enabled = false`
if something else already owns that gesture.

## Layouts

There are two, picked with `layout`.

`stage` is the default. It spreads the windows of the current workspace across
the screen and keeps a workspace shelf at the top edge, which slides in when the
pointer goes there or when you scroll up. That is the screenshot above.

`workspace_wall` shows all workspaces at once, as a grid of cards:

![The workspace_wall layout](assets/workspaces.webp)

`radiant:app` collects every window of the focused application:

![App Exposé](assets/app-expose.webp)

## Controls

With the mouse:

- Hover a workspace or window to move the selection
- Click a workspace to switch to it, click a window to focus it
- Drag a window onto a workspace card to move it there
- Drag a window onto the trailing `+`, or just click it, to create a workspace
- Pointer at the top edge reveals the shelf, at the bottom edge the dock
- Scrolling shows and hides the shelf, `Ctrl` + wheel steps through workspaces
- Hover a window and click the button in its corner to close it

With the keyboard:

- `Left` / `Right` move along the workspace shelf
- `Down` drops into the windows of the selected workspace, `Up` goes back
- `1`–`9` jump straight to a workspace
- Start typing to search windows by title or class
- `/` opens search with every window listed
- `Tab` switches between the spatial and application-grouped views
- `Enter` activates the selection
- `Esc` closes search first, the overview second

## Configuration

All of it is optional. These are the defaults:

```ini
plugin {
    radiant {
        opacity = 0.94
        animation_duration = 180
        layout = stage
        accent_color = auto
        background_color = auto
        foreground_color = auto
        font_family = JetBrainsMono Nerd Font
        gesture_enabled = true
        gesture_fingers = 3
        gesture_distance = 300
    }
}
```

| Option | Notes |
| --- | --- |
| `opacity` | Overlay opacity, `0.0` to `1.0` |
| `animation_duration` | Fade duration in ms, `0` to `2000` |
| `layout` | `stage` or `workspace_wall` |
| `accent_color` | `auto` follows the focused window border; or `#RRGGBB`, `#RRGGBBAA`, `rgb()`, `rgba()` |
| `background_color`, `foreground_color` | `auto` follows the Omarchy theme, or set them yourself |
| `font_family` | Interface font |
| `gesture_enabled` | Trackpad swipe capture |
| `gesture_fingers` | `3` or `4` |
| `gesture_distance` | Swipe travel in pixels, `120` to `800` |

If no Omarchy theme can be read, the colours fall back to a neutral grey. The
palette is re-read every time the overview opens, so switching themes does not
need a reload.

## Building it yourself

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

That gives you `build/hypr-radiant.so`. You can load it directly instead of
going through `hyprpm`, which is much faster while working on it:

```sh
hyprctl plugin load "$PWD/build/hypr-radiant.so"
hyprctl plugin unload "$PWD/build/hypr-radiant.so"
```

Unloading is clean, so you can reload as often as you want.

## Tests

The layout, geometry, search, gesture and theme logic is kept separate from
Hyprland, so most of it runs without a compositor:

```sh
cmake -S . -B build/test -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DHYPR_RADIANT_BUILD_PLUGIN=OFF
cmake --build build/test
ctest --test-dir build/test --output-on-failure
```

There is also a harness that starts a nested headless Hyprland, loads the
plugin and takes screenshots, without touching your real session:

```sh
bash tests/harness/nested-session.sh run-happy-path
```

## License

MIT, see [LICENSE](LICENSE).
