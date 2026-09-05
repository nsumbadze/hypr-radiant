# hypr-radiant

A window overview for Hyprland. One keybind shows every window on every
workspace, so you jump straight to the one you want instead of cycling through
workspaces looking for it.

It reads your Omarchy theme, so it should match the rest of your desktop without
configuring anything.

The windows of the current workspace spread out across the screen, and a
workspace shelf slides in at the top edge when the pointer reaches it, so you can
move between workspaces without leaving the overview.

<!-- SCREENSHOT SLOT: assets/gifs/stage.gif -->
![Stage workspace view](assets/gifs/stage.gif)

## Requirements

- Omarchy 4 “Quattro”, or another Hyprland installation with the same development stack
- Hyprland 0.55.2 through 0.56.2, with development headers matching the compositor you run
- `hyprpm`
- CMake 3.25 or newer
- A C++23 compiler
- `pkg-config`

The plugin ABI is tied to the exact Hyprland build. If the headers do not match,
the plugin refuses to load, sends a notification, and Hyprland unloads it again.
Hyprland plugins also link directly to compositor libraries such as Aquamarine and
hyprutils. Rebuild the plugin after every Hyprland or compositor-library upgrade,
even when the Hyprland version string itself did not change.

The Quattro compatibility target is Hyprland 0.56.2 with Aquamarine 0.14.x,
hyprutils 0.14.x, hyprgraphics 0.5.x, and hyprlang 0.6.x. Confirm the installed
stack before building:

```sh
omarchy version
hyprctl version
pkg-config --modversion hyprland aquamarine hyprutils hyprgraphics hyprlang
```

The current source is build-tested against Hyprland 0.55.2, 0.55.4, and 0.56.2.
HyprPM pins the exact Hyprland 0.55.2 commit to a source revision verified with
that release; other supported builds compile the current source against their
own matching headers.

## Install

`hyprpm` needs superuser rights the first time, since it creates
`/var/cache/hyprpm/` and installs matching Hyprland headers. Make sure `sudo` or
`doas` is available before you start.

```sh
hyprpm update
hyprpm add https://github.com/nsumbadze/hypr-radiant
hyprpm enable hypr-radiant
hyprpm reload
```

`hyprpm enable` saves the plugin's enabled state. On Omarchy 4 Quattro, add this
once to `~/.config/hypr/autostart.lua` so enabled plugins load with Hyprland:

```lua
o.exec_on_start("hyprpm reload -n")
```

For another Lua-based Hyprland setup, put the equivalent callback in
`hyprland.lua`:

```lua
hl.on("hyprland.start", function()
    hl.exec_cmd("hyprpm reload -n")
end)
```

On a legacy `.conf` setup, use `autostart.conf` instead:

```ini
exec-once = hyprpm reload -n
```

The plugin registers its default shortcut whenever it loads, so no separate
keybind is needed after a restart.

After rebuilding or updating it:

```sh
hyprpm update
hyprpm reload
hyprctl reload
hyprctl configerrors
```

Run those commands again after an `omarchy update` that changes Hyprland,
Aquamarine, hyprutils, hyprgraphics, or hyprlang. Do not keep loading a binary
built against the previous stack.

To remove it:

```sh
hyprpm disable hypr-radiant
hyprpm remove hypr-radiant
hyprpm reload
```

## Usage

Press `SUPER+A` to open or close the overview. A three-finger swipe up opens it
and a swipe down closes it.

The plugin leaves an existing `SUPER+A` binding untouched. To use a different
shortcut on Quattro, add this after Omarchy's defaults in `hyprland.lua`:

```lua
if hl.plugin.radiant then
    hl.config({ plugin = { radiant = { shortcut_enabled = false } } })
    hl.unbind("SUPER + A")
    o.bind("SUPER + TAB", "Radiant overview", hl.plugin.radiant.toggle)
end
```

The guard lets the first config parse finish before the plugin loads, then
applies the option and binding during Radiant's queued reload. On a legacy
`.conf` setup, use:

```ini
plugin {
    radiant {
        shortcut_enabled = false
    }
}

bind = SUPER, TAB, exec, hyprctl dispatch radiant:toggle
```

| Dispatcher | What it does |
| --- | --- |
| `radiant:toggle` | Open or close the overview |
| `radiant:open` | Open it, only if it is closed |
| `radiant:close` | Close it, only if it is open |
| `radiant:preferences` | Open the overview preferences |
| `radiant:app` | App Exposé for the focused application |
| `radiant:shelf show\|hide\|toggle` | Control the workspace shelf |
| `radiant:status` | Notification with the current state, for debugging |

On Quattro, the equivalent Lua functions are `hl.plugin.radiant.toggle()`,
`open()`, `close()`, `preferences()`, and `status()`.

While it is open, swipe left or right to preview the next workspace. Set
`gesture_enabled = false` if something else already owns that gesture.

## Preferences

Press `Ctrl+,` while the overview is open. The native Omarchy-style panel controls:

- Stage, Workspace Wall, Workspace Carousel, or the Omarchy-inspired Ribbon
- In Stage, Spatial, application-grouped, or hero-and-supporting Deck window arrangement
- In Stage, workspace bar: Config, Auto, Always, or Hidden
- Arrow-key behavior: Config, List, or Spatial (independent of the window arrangement)
- Default, Snap, Glitch, Lightcycle, Silk, Reduced, or Off overview animations
- Appearance: Config, Radiant, Match desktop, or Square
- Any installed Omarchy theme for Radiant, without changing the desktop theme
- App Exposé for the focused application

Changes are saved immediately to
`~/.config/hypr-radiant/preferences.conf` (or `$XDG_CONFIG_HOME` when set) and
survive plugin and Hyprland restarts. `CURRENT` follows Quattro's active
`~/.local/state/omarchy/current/theme/colors.toml`; selecting an installed theme
applies its palette to Radiant only.
The Appearance, Workspace bar, and Arrow-key behavior rows use `CONFIG` to follow their `plugin:radiant:*`
options; any other choice overrides the corresponding option. Workspace bar is shown
only for Stage.
Focusing these rows shows whether that choice follows Hyprland or is a saved
override. Appearance also reports when desktop decoration is unavailable and
Radiant is using Square instead.

### What the customization settings mean

| Setting | Meaning |
| --- | --- |
| Appearance (previously Chrome) | The borders, corner shape, and visual effects around previews—not the Chrome browser. |
| Radiant | Radiant's original rounded appearance, with shadows, glow, and blur. |
| Match desktop (`chrome = native`) | Uses Hyprland's corner radius, border thickness, and active/inactive border colors or gradients. Reads them when Radiant opens; effects are off by default. |
| Square (`chrome = flat`) | Square corners, a 2-pixel accent border, and no shadows, glow, or blur by default. |
| Workspace bar (previously Shelf) | The strip of workspace previews at the top of Stage. Auto reveals it on demand, Always keeps it visible, and Hidden prevents automatic reveals. The explicit `radiant:shelf` command still works. |
| Arrow-key behavior (previously Navigation) | How arrow keys select windows. List follows window order; Spatial selects by window position, preferring the same row or column. It does not change the window arrangement. |
| Config | Follow the corresponding Hyprland plugin option instead of a saved panel choice. |

The configuration keys and saved values have **not** changed. For example, the
panel's Match desktop choice still saves `chrome = native`. Explicit rounding,
border, and effects options can override an appearance preset.

<!-- SCREENSHOT SLOT: assets/gifs/settings.gif -->
![Quattro settings panel](assets/gifs/settings.gif)

## Views

Stage is the default. It spreads the current workspace across the screen and
keeps a workspace shelf at the top edge. Its screenshot is the main preview at
the top of this README.

Workspace Wall shows all workspaces at once as a grid of cards:

<!-- SCREENSHOT SLOT: assets/gifs/workspace-wall.gif -->
![Workspace Wall view](assets/gifs/workspace-wall.gif)

Workspace Carousel borrows Quattro's visual theme/background picker: the selected
workspace stays centered between readable 16:9 side previews for each real
workspace, followed by one explicit new-workspace target.

<!-- SCREENSHOT SLOT: assets/gifs/carousel.gif -->
![Workspace Carousel view](assets/gifs/carousel.gif)

Workspace Ribbon turns the same workspace sequence into a compact, fast-moving
strip inspired by Quattro's native pickers.

<!-- SCREENSHOT SLOT: assets/gifs/ribbon.gif -->
![Workspace Ribbon view](assets/gifs/ribbon.gif)

Deck arrangement gives the first window a large hero position and packs the rest
into a supporting column. It is available in Stage alongside Spatial and Grouped.

App Exposé collects every window belonging to the focused application.

## Controls

With the mouse:

- Hover a workspace or window to move the selection; a short accent trace resolves into corner
  locks on the chosen card
- Click a workspace to switch to it, click a window to focus it
- Drag a window onto a workspace card in Stage, Wall, Carousel, or Ribbon to move it there: the card lifts and follows
  the pointer, the workspace under it runs the destination lock, and the drop settles the card into
  place. Releasing over the window's own workspace, or over nothing, sends it back where it came from
- Drag a window onto the trailing `+`, or just click it, to create a workspace
- Pointer at the top edge reveals the shelf, at the bottom edge the dock
- Scrolling shows and hides the shelf, `Ctrl` + wheel steps through workspaces
- Hover a window and click the button in its corner to close it

With the keyboard:

- `Left` / `Right` move along the workspace shelf
- `Down` drops into the windows of the selected workspace, `Up` goes back
- With `window_navigation = spatial`, arrows choose windows by geometry. Moving
  `Up` from the top row reveals an Auto shelf; moving back `Down` hides that
  keyboard-revealed shelf and returns to the window you left (or the first window
  if it has closed). Candidates overlapping the current row/column take priority,
  then the closest center wins; edges do not wrap between windows
- With `shelf_navigation = all`, Stage's `Left` / `Right` include empty workspaces
  and `+ New`, wrapping at either end. Keyboard workspace navigation reveals an
  Auto shelf, including in List mode; `Enter` activates the selected card.
  `shelf = hidden` still prevents automatic reveals
- `1`–`9` jump straight to a workspace
- Start typing to search windows by title or class
- `/` opens search with every window listed, and types a slash once search is open,
  so window titles that contain a path stay searchable
- `Tab` cycles Spatial, Grouped, and Deck window arrangements in Stage
- With `vim_keys = 1`, `h`/`j`/`k`/`l` mirror the arrows. This disables automatic
  type-to-search outside search; use `/` first, where all four letters type normally
- With `tab_cycles_windows = 1`, `Tab` and `Shift+Tab` cycle windows while
  `Ctrl+Tab` continues to cycle Stage arrangements
- `Ctrl+,` opens or closes preferences
- In preferences, `Left` / `Right` change and save a value; `Enter` confirms it and closes the panel
- `Enter` activates the selection
- `Esc` closes search first, the overview second

### Letter shortcuts and searching

`vim_keys` is an optional keyboard setting, **off by default**. It is independent
of Appearance, Arrow-key behavior, and the selected workspace view; selecting
Spatial does not enable letter shortcuts. There is currently no panel toggle
for `vim_keys` or `tab_cycles_windows`.

| State | What H / J / K / L do |
| --- | --- |
| `vim_keys = 0` (default) | Type normally and start search, like other letters. |
| `vim_keys = 1`, search closed | Move the selection: H = left, J = down, K = up, L = right. Other ordinary letters do not start search. |
| Search open, either setting | Type normally, including every H, J, K, and L in your search text. |

With letter shortcuts enabled, **press `/` first, then type your search**. For
example, press `/` and type `kitty`, `chrome`, or `jekyll`; none of those letters
move the selection while search is open. The opening slash is not added to the
query. Search matches open windows by title or application class; it is not an
installed-application launcher. `Esc` leaves search and restores letter navigation.

`tab_cycles_windows = 1` means **Tab selects the next window** and **Shift+Tab
selects the previous window**, wrapping at the ends. This moves the selection
only; press Enter to activate it. Ctrl+Tab still changes the window arrangement
in Stage. With the option off, Tab changes the Stage arrangement instead.
Tab does nothing while search is open.

## Configuration

All of it is optional. These are the defaults:

```lua
if hl.plugin.radiant then
    hl.config({
        plugin = {
            radiant = {
                opacity = 0.94,
                animation_duration = 180,
                layout = "stage",
                background_color = "auto",
                foreground_color = "auto",
                font_family = "JetBrainsMono Nerd Font",
                shortcut_enabled = true,
                gesture_enabled = true,
                gesture_fingers = 3,
                gesture_distance = 300,
                chrome = "radiant",
                rounding = -1,
                border_size = -1,
                border_color = "auto",
                effects = "auto",
                spacing = 1.0,
                shelf = "auto",
                window_navigation = "list",
                shelf_navigation = "occupied",
                vim_keys = false,
                tab_cycles_windows = false,
            },
        },
    })
end
```

For a legacy `.conf` setup, the equivalent is:

```ini
plugin {
    radiant {
        opacity = 0.94
        animation_duration = 180
        layout = stage
        background_color = auto
        foreground_color = auto
        font_family = JetBrainsMono Nerd Font
        shortcut_enabled = true
        gesture_enabled = true
        gesture_fingers = 3
        gesture_distance = 300
        chrome = radiant
        rounding = -1
        border_size = -1
        border_color = auto
        effects = auto
        spacing = 1.0
        shelf = auto
        window_navigation = list
        shelf_navigation = occupied
        vim_keys = 0
        tab_cycles_windows = 0
    }
}
```

| Option | Notes |
| --- | --- |
| `opacity` | Overlay opacity, `0.0` to `1.0` |
| `animation_duration` | Fade duration in ms, `0` to `2000` |
| `layout` | `stage`, `workspace_wall`, `carousel`, or `ribbon` |
| `background_color`, `foreground_color` | `auto` follows the Omarchy theme, or set them yourself |
| `font_family` | Interface font |
| `shortcut_enabled` | Register `SUPER+A` when it is not already bound |
| `gesture_enabled` | Trackpad swipe capture |
| `gesture_fingers` | `3` or `4` |
| `gesture_distance` | Swipe travel in pixels, `120` to `800` |
| `chrome` | `radiant`, `native`, or `flat`; Native mirrors Hyprland decoration values when the overview opens |
| `rounding` | `-1` follows the preset, otherwise `0` to `40` |
| `border_size` | `-1` follows the preset, otherwise `0` to `12` |
| `border_color` | `auto` follows the preset, or use any supported colour string for selected borders |
| `effects` | `auto`, `on`, or `off`; Auto enables effects for Radiant and disables them for Native and Flat |
| `spacing` | Card padding and gap multiplier, `0.5` to `2.0` |
| `shelf` | `auto`, `always`, or `hidden`; `radiant:shelf` remains an explicit override |
| `window_navigation` | `list` preserves the original order-based behavior; `spatial` uses window geometry |
| `shelf_navigation` | `occupied` (default) preserves Stage workspace stepping; `all` includes empty workspaces and the + New card and reveals an Auto shelf during keyboard workspace navigation |
| `vim_keys` | Enable `h`, `j`, `k`, and `l` navigation |
| `tab_cycles_windows` | Make Tab cycle windows and keep arrangement switching on Ctrl+Tab |

Radiant's accent always follows the selected Omarchy theme. If no Omarchy theme
can be read, the colours fall back to a neutral grey. The palette is re-read
every time the overview or preferences open, so switching themes does not need
a reload. Installed themes are discovered from Omarchy's stock and user theme
directories.

### Omarchy-native recipe

`native` follows `decoration:rounding`, `general:border_size`, and the active
and inactive border gradients, including their angle. Values are sampled when
the overview opens, so close and reopen it after changing them:

```ini
plugin {
    radiant {
        chrome = native
        spacing = 0.8
        shelf = always
        window_navigation = spatial
    }
}
```

For an explicitly square variant independent of the active Hyprland theme, use
`chrome = flat`, `rounding = 0`, and `border_size = 3`. To preview Native from
the command line with Omarchy's Lua config:

```sh
hyprctl eval 'hl.config({ plugin = { radiant = { chrome = "native" } } })'
```

With a legacy `.conf` config, use
`hyprctl keyword plugin:radiant:chrome native` instead. Close and reopen Radiant
after either command. Native falls back to Flat for the session if the running
Hyprland version does not expose its decoration values.

The settings panel starts by following these Hyprland values. Choosing Stage,
Wall, Carousel, or Ribbon saves that view as the preference. `CURRENT` follows
Omarchy's active theme; choosing an installed theme applies its palette to
Radiant. Default preserves the existing smooth motion. Snap punches cards
forward from depth, Glitch arrives in staggered digital cuts, Lightcycle sweeps
cards horizontally like a signal, and Silk uses a slower floating settle across
every layout. Reduced caps transitions at 90 ms, while Off makes them immediate. The old
`quattro`, `cyberpunk`, `tron`, and `elegant` saved values remain compatible.

## Building it yourself

Use a separate build directory so an older plugin binary is not mistaken for the
Quattro build:

```sh
cmake -S . -B build/quattro -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/quattro -j"$(nproc)"
ctest --test-dir build/quattro --output-on-failure
ldd build/quattro/hypr-radiant.so
```

Do not load the module if `ldd` reports any library as `not found`. A Quattro
build should resolve the current Aquamarine and hyprutils sonames. Load it
directly, then let Hyprland reparse the plugin options it registered:

```sh
hyprctl plugin unload "$PWD/build/quattro/hypr-radiant.so"
hyprctl plugin load "$PWD/build/quattro/hypr-radiant.so"
hyprctl reload
hyprctl plugin list
hyprctl configerrors
hyprctl getoption plugin:radiant:gesture_distance
```

If the module was loaded from another path, unload that exact path first; Hyprland
will not load the same plugin twice. Unloading is clean, so development builds can
be replaced without restarting the compositor. Direct loading is temporary and
does not survive a Hyprland restart; use the `hyprpm` installation and startup
line above for persistent loading. While developing an uncommitted build, put its
absolute path in Quattro's `~/.config/hypr/autostart.lua`:

```lua
o.exec_on_start("hyprctl plugin load /absolute/path/to/hypr-radiant/build/quattro/hypr-radiant.so")
```

On a legacy `.conf` setup, use:

```ini
exec-once = hyprctl plugin load /absolute/path/to/hypr-radiant/build/quattro/hypr-radiant.so
```

## Troubleshooting

First check the compositor, loaded plugin, and current configuration:

```sh
hyprctl version
hyprctl plugin list
hyprctl configerrors
hyprctl devices
hyprctl getoption plugin:radiant:gesture_enabled
hyprctl getoption plugin:radiant:gesture_fingers
hyprctl getoption plugin:radiant:gesture_distance
hyprctl eval 'hl.plugin.radiant.status()'
```

On Hyprland's legacy parser, the final command is
`hyprctl dispatch radiant:status` instead.

The expected gesture values are `int: 1`, `int: 3`, and `float: 300`. A
`set: false` line means the plugin is using its default value; it does not mean
the option is disabled. A configured `gesture_distance = 120` should report
`float: 120.000000` and `set: true`.

If `hyprctl configerrors` reports `Invalid value 120 for finger count`, Radiant's
configuration was parsed while the plugin was absent. It is not rejecting the
distance: Hyprland has interpreted `plugin:radiant:gesture_distance` as a
built-in gesture option because Radiant did not register the key. Check
`hyprctl plugin list`, run `ldd` on the exact module being loaded, rebuild it
against the current stack if a soname changed, load it, and then run
`hyprctl reload`. The `hyprctl plugin load` error is the authoritative startup
diagnostic; it includes a missing-library or plugin-initialization failure.

If `hl.plugin.radiant.toggle()` opens the overview but a swipe does not, follow
Hyprland's input log and then make one deliberate three-finger swipe up:

```sh
hyprctl rollinglog --follow
```

Look for a libinput `gesture: [3fg]` line. If the log only reports `[2fg]`, the
touchpad or libinput did not recognize three fingers, so the gesture never
reached the plugin. To collect a smaller report after reproducing the problem:

```sh
hyprctl rollinglog | rg -i 'hypr-radiant|gesture|swipe'
```

If the log shows the third contact entering `BUTTON_STATE_BOTTOM`, libinput is
treating the bottom of the pad as a software button instead of part of the
gesture. Enable clickfinger behavior in Hyprland:

```lua
hl.config({ input = { touchpad = { clickfinger_behavior = true } } })
```

On a legacy `.conf` setup, use:

```ini
input {
    touchpad {
        clickfinger_behavior = true
    }
}
```

You can also test four fingers or a shorter swipe distance without editing any
files:

```sh
hyprctl eval 'hl.config({ plugin = { radiant = { gesture_fingers = 4, gesture_distance = 120 } } })'
```

With Hyprland's legacy parser, use:

```sh
hyprctl keyword plugin:radiant:gesture_fingers 4
hyprctl keyword plugin:radiant:gesture_distance 120
```

Run `hyprctl reload` afterward to restore the values from your configuration.

## Tests

The layout, geometry, search, gesture, chrome-style and theme logic is kept separate from
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
