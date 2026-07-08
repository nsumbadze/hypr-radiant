#!/usr/bin/env bash
# Nested headless Hyprland audit harness for hypr-radiant.
#
# Usage:
#   bash tests/harness/nested-session.sh start
#   bash tests/harness/nested-session.sh load
#   bash tests/harness/nested-session.sh dispatch radiant:toggle
#   bash tests/harness/nested-session.sh screenshot <name>
#   bash tests/harness/nested-session.sh stop
#   bash tests/harness/nested-session.sh status
#   bash tests/harness/nested-session.sh run-happy-path
#
# Safety:
#   - Refuses to start when an active Hyprland socket responds to hyprctl.
#   - Refuses to start when hypr-radiant appears loaded in any active session log.
#   - The nested compositor is spawned with a fresh WAYLAND_DISPLAY and its own
#     HYPRLAND_INSTANCE_SIGNATURE so it can never be mistaken for the user's
#     real session.

set -euo pipefail

cd "$(dirname "$0")/../.."
PROJECT_ROOT="$PWD"

FIXTURE="$PROJECT_ROOT/tests/harness/fixtures/hyprland.conf"
SESSION_ENV="$PROJECT_ROOT/tests/harness/.session.env"
PLUGIN="$PROJECT_ROOT/build/hypr-radiant.so"
EVIDENCE_DIR="$PROJECT_ROOT/build/harness-evidence"
PLUGIN_LOG="$EVIDENCE_DIR/plugin.log"

mkdir -p "$EVIDENCE_DIR"

usage() {
    cat >&2 <<EOF
Usage: $0 {start|stop|load|dispatch <cmd>|screenshot <name>|status|run-happy-path}
EOF
}

# Abort if the user's active Hyprland session is reachable.  We also verify
# that hypr-radiant is not loaded in any other running Hyprland instance.
guard_active_session() {
    if [[ -n "${HYPRLAND_INSTANCE_SIGNATURE:-}" ]]; then
        if hyprctl version >/dev/null 2>&1; then
            echo "error: active Hyprland session detected (HYPRLAND_INSTANCE_SIGNATURE=$HYPRLAND_INSTANCE_SIGNATURE)" >&2
            echo "       refusing to start a nested session while the real session is reachable." >&2
            return 1
        fi
    fi

    local lock
    for lock in /run/user/"$UID"/hypr/*/hyprland.lock; do
        [[ -f "$lock" ]] || continue
        local pid sig
        pid=$(sed -n '1p' "$lock" 2>/dev/null)
        sig=$(basename "$(dirname "$lock")")
        if ! [[ "$pid" =~ ^[0-9]+$ ]] || ! kill -0 "$pid" 2>/dev/null; then
            continue
        fi
        if HYPRLAND_INSTANCE_SIGNATURE="$sig" hyprctl plugin list 2>/dev/null | grep -q "hypr-radiant"; then
            echo "error: hypr-radiant is loaded in running session $sig" >&2
            return 1
        fi
    done
}

require_session_env() {
    if [[ ! -f "$SESSION_ENV" ]]; then
        echo "error: no nested session; run '$0 start' first" >&2
        return 1
    fi
    # shellcheck source=/dev/null
    source "$SESSION_ENV"
}

write_session_env() {
    local pid="$1"
    local instance="$2"
    local socket="$3"
    local display="$4"

    cat > "$SESSION_ENV" <<EOF
HYPR_RADIANT_HARNESS_PID=$pid
HYPR_RADIANT_HARNESS_INSTANCE=$instance
HYPR_RADIANT_HARNESS_SOCKET=$socket
HYPR_RADIANT_HARNESS_WAYLAND_DISPLAY=$display
EOF
}

# Hyprland 0.55.2 (Aquamarine) determines the Wayland socket itself.  We do
# NOT export WAYLAND_DISPLAY before spawning, because Aquamarine's Wayland
# backend interprets it as the parent compositor to nest under, while the
# headless backend needs the parent's WAYLAND_DISPLAY inherited so that a
# DRM-less allocator can come from the Wayland fallback.  After startup we
# read the nested socket name from hyprland.lock.
read_nested_wayland_display() {
    local instance="$1"
    local lock="/run/user/$UID/hypr/$instance/hyprland.lock"
    if [[ -f "$lock" ]]; then
        sed -n '2p' "$lock" 2>/dev/null
    fi
}

# Record all Hyprland instance signatures present before we spawn.
existing_hypr_sigs() {
    ls /run/user/"$UID"/hypr/ 2>/dev/null | sort || true
}

cmd_start() {
    guard_active_session || return 1

    if [[ -f "$SESSION_ENV" ]]; then
        # Defensive: stale env file with a dead process is OK to overwrite.
        local stale_pid
        stale_pid="$(grep '^HYPR_RADIANT_HARNESS_PID=' "$SESSION_ENV" | cut -d= -f2)"
        if ! kill -0 "$stale_pid" 2>/dev/null; then
            rm -f "$SESSION_ENV"
        else
            echo "error: a harness session appears to be running already (PID $stale_pid)" >&2
            return 1
        fi
    fi

    if [[ ! -f "$PLUGIN" ]]; then
        echo "error: plugin not found: $PLUGIN" >&2
        echo "       run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build" >&2
        return 1
    fi

    if [[ -n "${WLR_BACKENDS:-}" && "${WLR_BACKENDS:-}" != "headless" ]]; then
        echo "error: WLR_BACKENDS must be headless for the audit harness, got: $WLR_BACKENDS" >&2
        return 1
    fi

    local existing_sigs
    existing_sigs="$(existing_hypr_sigs)"

    # Spawn Hyprland in a subshell so we can unset the inherited instance
    # signature and force the nested compositor to create its own identity.
    # WAYLAND_DISPLAY is intentionally inherited (not set) so Aquamarine can
    # either use the headless backend with a DRM allocator, or fall back to
    # nesting under the parent's Wayland compositor to obtain an allocator.
    (
        unset HYPRLAND_INSTANCE_SIGNATURE
        export WLR_BACKENDS="${WLR_BACKENDS:-headless}"
        export WLR_LIBINPUT_NO_DEVICES="${WLR_LIBINPUT_NO_DEVICES:-1}"
        export HYPRLAND_NO_XDG="${HYPRLAND_NO_XDG:-1}"
        exec Hyprland -c "$FIXTURE" > "$PLUGIN_LOG" 2>&1
    ) &
    local pid=$!

    echo "spawned Hyprland PID $pid" | tee -a "$PLUGIN_LOG"

    local nested_sig=""
    local socket_path=""
    local start_time=$SECONDS
    local deadline=20

    while (( SECONDS - start_time < deadline )); do
        if ! kill -0 "$pid" 2>/dev/null; then
            echo "error: Hyprland (PID $pid) exited before its socket appeared" >&2
            echo "       see $PLUGIN_LOG for compositor output" >&2
            return 1
        fi

        local current_sigs
        current_sigs="$(existing_hypr_sigs)"
        nested_sig="$(comm -13 <(echo "$existing_sigs") <(echo "$current_sigs") | head -n1)"

        if [[ -n "$nested_sig" ]]; then
            socket_path="/run/user/$UID/hypr/$nested_sig/.socket.sock"
            if [[ -S "$socket_path" ]]; then
                break
            fi
        fi

        sleep 0.2
    done

    if [[ -z "$nested_sig" ]] || [[ ! -S "$socket_path" ]]; then
        echo "error: timed out waiting for Hyprland socket" >&2
        kill -TERM "$pid" 2>/dev/null || true
        sleep 1
        kill -KILL "$pid" 2>/dev/null || true
        return 1
    fi

    # Read the socket name Hyprland actually created (e.g. wayland-1).
    local wayland_display
    wayland_display="$(read_nested_wayland_display "$nested_sig")"
    if [[ -z "$wayland_display" ]]; then
        # Lock file may be slightly delayed; retry briefly.
        local wl_start_time=$SECONDS
        while (( SECONDS - wl_start_time < 3 )) && [[ -z "$wayland_display" ]]; do
            wayland_display="$(read_nested_wayland_display "$nested_sig")"
            sleep 0.1
        done
    fi
    if [[ -z "$wayland_display" ]]; then
        echo "warn: could not read nested WAYLAND_DISPLAY from lock file" | tee -a "$PLUGIN_LOG"
    fi

    write_session_env "$pid" "$nested_sig" "$socket_path" "$wayland_display"
    echo "ok socket=$socket_path instance=$nested_sig display=$wayland_display"
}

cmd_stop() {
    require_session_env

    echo "stopping Hyprland PID $HYPR_RADIANT_HARNESS_INSTANCE..." | tee -a "$PLUGIN_LOG"

    # Ask the compositor to exit gracefully.
    HYPRLAND_INSTANCE_SIGNATURE="$HYPR_RADIANT_HARNESS_INSTANCE" \
        hyprctl dispatch exit >> "$PLUGIN_LOG" 2>&1 || true

    local start_time=$SECONDS
    local deadline=10
    while (( SECONDS - start_time < deadline )); do
        if ! kill -0 "$HYPR_RADIANT_HARNESS_PID" 2>/dev/null; then
            break
        fi
        sleep 0.2
    done

    if kill -0 "$HYPR_RADIANT_HARNESS_PID" 2>/dev/null; then
        echo "warn: graceful exit timed out, sending TERM" | tee -a "$PLUGIN_LOG"
        kill -TERM "$HYPR_RADIANT_HARNESS_PID" 2>/dev/null || true
        sleep 1
        if kill -0 "$HYPR_RADIANT_HARNESS_PID" 2>/dev/null; then
            echo "warn: TERM timed out, sending KILL" | tee -a "$PLUGIN_LOG"
            kill -KILL "$HYPR_RADIANT_HARNESS_PID" 2>/dev/null || true
        fi
    fi

    rm -f "$SESSION_ENV"

    # Append the complete nested Hyprland log to plugin.log before removing
    # the instance directory, ensuring plugin messages are captured regardless
    # of when they were flushed during the session.
    if [[ -n "${HYPR_RADIANT_HARNESS_INSTANCE:-}" ]]; then
        local hypr_log="/run/user/$UID/hypr/$HYPR_RADIANT_HARNESS_INSTANCE/hyprland.log"
        if [[ -f "$hypr_log" ]]; then
            echo "--- full hyprland.log ($HYPR_RADIANT_HARNESS_INSTANCE) ---" >> "$PLUGIN_LOG"
            grep -E "\[hypr-radiant\]|Plugin hypr-radiant loaded|Hyprctl: dispatcher radiant:toggle" "$hypr_log" >> "$PLUGIN_LOG" 2>&1 || true
        fi
        rm -rf "/run/user/$UID/hypr/$HYPR_RADIANT_HARNESS_INSTANCE"
    fi

    # Confirm our PID is gone.
    if kill -0 "$HYPR_RADIANT_HARNESS_PID" 2>/dev/null; then
        echo "error: failed to stop Hyprland PID $HYPR_RADIANT_HARNESS_PID" >&2
        return 1
    fi

    echo "ok"
}

cmd_status() {
    if [[ ! -f "$SESSION_ENV" ]]; then
        echo "no session"
        return 1
    fi
    require_session_env
    if kill -0 "$HYPR_RADIANT_HARNESS_PID" 2>/dev/null; then
        echo "running pid=$HYPR_RADIANT_HARNESS_PID instance=$HYPR_RADIANT_HARNESS_INSTANCE display=$HYPR_RADIANT_HARNESS_WAYLAND_DISPLAY"
        return 0
    fi
    echo "stale session env"
    rm -f "$SESSION_ENV"
    return 1
}

# Hyprland 0.55.2 disables stdout logs after early init; plugin log lines
# (including "radiant overlay opened") are written to the instance log file.
# Append the latest chunk of that log to plugin.log so evidence is centralized.
append_nested_log() {
    require_session_env
    local log_file="/run/user/$UID/hypr/$HYPR_RADIANT_HARNESS_INSTANCE/hyprland.log"
    if [[ -f "$log_file" ]]; then
        echo "--- hypr-radiant log lines ($HYPR_RADIANT_HARNESS_INSTANCE) ---" >> "$PLUGIN_LOG"
        grep -E "\[hypr-radiant\]|Plugin hypr-radiant loaded" "$log_file" >> "$PLUGIN_LOG" 2>&1 || true
    fi
}

cmd_load() {
    require_session_env
    echo "loading plugin $PLUGIN" | tee -a "$PLUGIN_LOG"
    HYPRLAND_INSTANCE_SIGNATURE="$HYPR_RADIANT_HARNESS_INSTANCE" \
        hyprctl plugin load "$PLUGIN" 2>&1 | tee -a "$PLUGIN_LOG"
    append_nested_log
}

cmd_dispatch() {
    require_session_env
    local cmd="${1:-}"
    if [[ -z "$cmd" ]]; then
        echo "error: dispatch requires a command" >&2
        return 1
    fi
    echo "dispatch $cmd" | tee -a "$PLUGIN_LOG"
    HYPRLAND_INSTANCE_SIGNATURE="$HYPR_RADIANT_HARNESS_INSTANCE" \
        hyprctl dispatch "$cmd" 2>&1 | tee -a "$PLUGIN_LOG"
    # Give Hyprland a moment to flush plugin log lines before tailing them.
    sleep 0.5
    append_nested_log
}

cmd_screenshot() {
    require_session_env
    local name="${1:-}"
    if [[ -z "$name" ]]; then
        echo "error: screenshot requires a name" >&2
        return 1
    fi
    local output="$EVIDENCE_DIR/$name.png"
    echo "screenshot $name -> $output" | tee -a "$PLUGIN_LOG"
    WAYLAND_DISPLAY="$HYPR_RADIANT_HARNESS_WAYLAND_DISPLAY" \
        grim -t png "$output" 2>&1 | tee -a "$PLUGIN_LOG"
    if [[ -s "$output" ]]; then
        echo "ok $output"
    else
        echo "error: screenshot is empty" >&2
        return 1
    fi
}

cmd_run_happy_path() {
    # If the harness is invoked from inside the user's active Hyprland session,
    # warn but proceed by spawning a truly nested session. We never target the
    # active session; all hyprctl calls below use the nested instance signature.
    if [[ -n "${HYPRLAND_INSTANCE_SIGNATURE:-}" ]]; then
        echo "warn: active Hyprland session detected ($HYPRLAND_INSTANCE_SIGNATURE); running nested session only" | tee -a "$PLUGIN_LOG"
    fi

    # Run the nested lifecycle in a subshell with the inherited signature
    # removed so the nested compositor gets its own identity.
    (
        unset HYPRLAND_INSTANCE_SIGNATURE
        cmd_start
        cmd_load
        cmd_dispatch "radiant:toggle"
        cmd_screenshot "screenshot-baseline-toggle"
        cmd_dispatch "radiant:toggle"
        cmd_screenshot "screenshot-baseline-closed"
        cmd_stop
    )
}

case "${1:-}" in
    start) cmd_start ;;
    stop) cmd_stop ;;
    load) cmd_load ;;
    dispatch) cmd_dispatch "${2:-}" ;;
    screenshot) cmd_screenshot "${2:-}" ;;
    status) cmd_status ;;
    run-happy-path) cmd_run_happy_path ;;
    *) usage; exit 1 ;;
esac
