#!/bin/bash
#
# Launches Rocksmith or Rocksmith 2014 through Steam/Proton with the
# environment PipeASIO needs. Edit the "USER CONFIGURATION" section below
# to match your system, then run this script instead of launching the
# game directly from Steam.

set -euo pipefail

# ============================================================
#  USER CONFIGURATION — edit the values in this section only
# ============================================================

# Which game to launch: 'Rocksmith' or 'Rocksmith2014'
GAME='Rocksmith'

# PipeWire (and PipeASIO) quantum/sample rate for the game's audio. Lower values reduce
# latency but may cause dropouts; see the setup guide for tuning tips.
#
# NOTE: if PipeWire has a daemon-wide forced quantum/rate (checked and pinned
# below via pw-metadata), it overrides any per-app request - including
# PipeASIO's own. These two values are also used below to pin PipeWire's
# daemon-wide clock so they actually take effect.
export PIPEASIO_PREFERRED_BUFFERSIZE='128'
export PIPEASIO_SAMPLE_RATE='48000'

# Not needed for PipeASIO - kept for reference. To be removed in the future.
# export PIPEWIRE_LATENCY="${PIPEASIO_PREFERRED_BUFFERSIZE}/${PIPEASIO_SAMPLE_RATE}"

# Steam's default library, where Steam itself and its compat tools are
# installed. This is not necessarily where the game itself lives.
STEAM_HOME="${HOME}/.local/share/Steam"

# Steam Library where the game is installed. Change this if you use a
# separate library on another disk/mount than STEAM_HOME.
STEAMAPPS="${STEAM_HOME}/steamapps"

# Path to the Proton build to use. Pre-set for Proton-CachyOS.
# Other probable locations:
#   "${STEAMAPPS}/common/Proton 11.0/proton"
#   "${STEAM_HOME}/compatibilitytools.d/GE-Proton<VERSION>/proton"
PROTON_BIN='/usr/share/steam/compatibilitytools.d/proton-cachyos-slr/proton'

# Steam Linux Runtime to launch through. Both of these are known to work;
# use whichever one is installed on your system:
#   'SteamLinuxRuntime_4'
#   'SteamLinuxRuntime_sniper'
STEAM_RUNTIME_LAUNCHER="${STEAMAPPS}/common/SteamLinuxRuntime_4/pressure-vessel/bin/steam-runtime-launcher-interface-0"

# Required by PipeASIO — should not normally need changing.
export PROTON_USE_WOW64='1'
export WINEDLLPATH="${HOME}/.local/lib/wine"

# ============================================================
#  INTERNAL — derived automatically, no need to edit below this line
# ============================================================

GAME_DIR="${STEAMAPPS}/common/${GAME}"

# Steam's app ID for the selected game
if [[ "${GAME}" = 'Rocksmith' ]]; then
    APP='205190'
elif [[ "${GAME}" = 'Rocksmith2014' ]]; then
    APP='221680'
else
    echo 'Error: set the GAME variable to "Rocksmith" or "Rocksmith2014".'
    exit 1
fi

# Identify the game to Steam/Proton
export SteamAppId="${APP}"
export SteamGameId="${APP}"
export STEAM_COMPAT_APP_ID="${APP}"
export SteamOverlayGameId="${APP}"
export STEAM_COMPAT_CLIENT_INSTALL_PATH="${STEAM_HOME}"
export STEAM_COMPAT_SHADER_PATH="${STEAMAPPS}/shadercache/${APP}"
export STEAM_COMPAT_INSTALL_PATH="${GAME_DIR}"
export STEAM_COMPAT_DATA_PATH="${STEAMAPPS}/compatdata/${APP}"
export SteamClientLaunch='1'
export SteamEnv='1'

# PipeWire's daemon-wide forced quantum/rate (clock.force-quantum/force-rate in
# the "settings" metadata, e.g. set by the Cable app) overrides any per-app
# request, including PipeASIO's. Pin it to match PIPEASIO_PREFERRED_BUFFERSIZE/
# PIPEASIO_SAMPLE_RATE above so they actually take effect, and restore
# whatever was set before on exit.
ORIG_FORCE_QUANTUM=''
ORIG_FORCE_RATE=''
restore_pipewire_clock() {
    if [[ -n "${ORIG_FORCE_QUANTUM}" ]]; then
        pw-metadata -n settings 0 clock.force-quantum "${ORIG_FORCE_QUANTUM}" >/dev/null || true
    else
        pw-metadata -n settings -d 0 clock.force-quantum >/dev/null || true
    fi
    if [[ -n "${ORIG_FORCE_RATE}" ]]; then
        pw-metadata -n settings 0 clock.force-rate "${ORIG_FORCE_RATE}" >/dev/null || true
    else
        pw-metadata -n settings -d 0 clock.force-rate >/dev/null || true
    fi
}
if command -v pw-metadata >/dev/null; then
    ORIG_FORCE_QUANTUM=$(pw-metadata -n settings 0 clock.force-quantum 2>/dev/null | sed -n "s/.*value:'\([^']*\)'.*/\1/p")
    ORIG_FORCE_RATE=$(pw-metadata -n settings 0 clock.force-rate 2>/dev/null | sed -n "s/.*value:'\([^']*\)'.*/\1/p")
    trap restore_pipewire_clock EXIT
    pw-metadata -n settings 0 clock.force-quantum "${PIPEASIO_PREFERRED_BUFFERSIZE}" >/dev/null
    pw-metadata -n settings 0 clock.force-rate "${PIPEASIO_SAMPLE_RATE}" >/dev/null
else
    >&2 echo 'Warning: pw-metadata not found; cannot pin PipeWire'\''s daemon-wide quantum/rate.'
fi

# Launch the game through the runtime and Proton, from its own directory.
# Not exec'd: the EXIT trap above must run after the game exits to restore
# PipeWire's clock settings.
LAUNCH_CMD=(
  env --chdir="${GAME_DIR}" \
    "${STEAM_RUNTIME_LAUNCHER}" \
    container-runtime "${PROTON_BIN}" \
    waitforexitandrun "${GAME_DIR}/${GAME}.exe" -uplay_steam_mode
)

# game-performance (CachyOS) must wrap the whole chain above, not just the
# exe, so it can restore the power profile once everything under it exits.
if command -v game-performance >/dev/null; then
    LAUNCH_CMD=(game-performance "${LAUNCH_CMD[@]}")
fi

"${LAUNCH_CMD[@]}"
