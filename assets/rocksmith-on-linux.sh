#!/bin/bash

# 1. Define configuration variables
APP="205190"
STEAM_HOME="${HOME}/.local/share/Steam"
STEAMAPPS="${STEAM_HOME}/SteamLibrary/steamapps"
GAME_DIR="${STEAMAPPS}/common/Rocksmith"

# 2. Export environment variables for the game and runtime
export PIPEWIRE_LATENCY='128/48000'
export PROTON_USE_WOW64='1'                   # Required by PipeASIO
export WINEDLLPATH="${HOME}/.local/lib/wine"  # Required by PipeASIO
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

# 3. Use 'env --chdir' to execute natively from the game directory
exec env --chdir="${GAME_DIR}" \
  "${STEAM_HOME}/steamapps/common/SteamLinuxRuntime_sniper/pressure-vessel/bin/steam-runtime-launcher-interface-0" \
  container-runtime \
  '/usr/share/steam/compatibilitytools.d/proton-cachyos-slr/proton' \
  waitforexitandrun \
  "${GAME_DIR}/Rocksmith.exe" -uplay_steam_mode
