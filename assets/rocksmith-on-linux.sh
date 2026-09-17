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

# PipeWire quantum/sample rate for the game's audio. Lower values reduce
# latency but may cause dropouts; see the setup guide for tuning tips.
export PIPEWIRE_LATENCY='128/48000'

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

# Launch the game through the runtime and Proton, from its own directory
exec env --chdir="${GAME_DIR}" \
  "${STEAM_RUNTIME_LAUNCHER}" \
  container-runtime "${PROTON_BIN}" \
  waitforexitandrun "${GAME_DIR}/${GAME}.exe" -uplay_steam_mode
