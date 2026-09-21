# Setup guide

This guide works for **Rocksmith**, using *Rocksmith on Linux*, and **Rocksmith 2014**, using [*RS ASIO*](https://github.com/mdias/rs_asio). To use the shell script provided here to run the 2014 game, alter the value of the variable GAME to 'Rocksmith2014'.

## Recommended environment

**CachyOS Linux** (includes **PipeWire**)

- **Required packages**:
  - `cachyos-gaming-meta` and `cachyos-gaming-applications` (check the [Gaming with CachyOS Guide](https://wiki.cachyos.org/configuration/gaming))
  - `mingw-w64-gcc`
  - `yaml-cpp`

- **Suggested package from AUR**: `cable` ([https://github.com/magillos/Cable](https://github.com/magillos/Cable)). The package includes two useful applications:
  - Cable makes it easy to set PipeWire's quantum
  - Cables allows you to route the audio paths in real time, so you can correct any problems or route the signal to recording, splitting, processing on Guitarix, etc.

- **PipeASIO**, manually built from the git repository:
  - For running *Rocksmith* or *Rocksmith 2014*, the AUR version will not work while the 32-bit version of the driver is not included in the PKGBUILD. So, build it manually for now.
  - Versions verified to work: v1.4.3 and v1.8.0.

## Steps

1. Install the required packages.

2. Install Rocksmith on Steam.

3. Configure Rocksmith:
   1. Use Proton-CachyOS, or, at least, Proton 10.
      - Required for SysWOW64 support.
   2. Initial settings on `Rocksmith.ini` (leave the rest as it is):
      ```ini
      ExclusiveMode=1
      LatencyBuffer=1
      MaxOutputBufferSize=1024
      ```

4. Run the game once, so its Proton prefix is created.

5. Clone the PipeASIO repository.

6. Build and install the driver by executing the following commands from the cloned repo's directory:
   ```console
   cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_WOW64_32=ON
   cmake --build build
   cmake --install build --prefix "$HOME/.local"
   ```

7. Register the driver in Rocksmith's prefix (replace ${STEAMLIB_DIR} as required for your system):
   - **for PipeASIO v1.4.3**
   ```console
   env WINEPREFIX='${STEAMLIB_DIR}/SteamLibrary/steamapps/compatdata/205190/pfx' \
   ~/.local/bin/pipeasio-register
   ```
   - **for PipeASIO v1.8.0 (and up)**
   ```console
   env WINEPREFIX='${STEAMLIB_DIR}/SteamLibrary/steamapps/compatdata/205190/pfx' \
       WINE='umu-run' \
       PROTONPATH='/usr/share/steam/compatibilitytools.d/proton-cachyos-slr' \
       GAMEID='205190' \
       PIPEASIO_PREFIX="${HOME}/.local" \
   ${HOME}/.local/bin/pipeasio-register
   ```

8. Open Cable and leave the Sample Rate and Quantum as they are — the `rocksmith-on-linux.sh` shell script pins both for the duration of the game (see below), so they don't need to be set here.

9. Open Cables, and leave it running — in case you need to manually attach any connectors.

10. Run the game using `rocksmith-on-linux.sh`. It should open a terminal window and then the game, with audio output and input.

## Buffer size and sample rate

`rocksmith-on-linux.sh` sets `PIPEASIO_PREFERRED_BUFFERSIZE` and `PIPEASIO_SAMPLE_RATE` at the top of the file, under "USER CONFIGURATION". Besides configuring PipeASIO itself, the script also uses these two values to pin PipeWire's own daemon-wide quantum and sample rate (via `pw-metadata`) before launching the game, and restores whatever was set before once the game exits.

This matters because PipeWire's daemon-wide forced quantum (e.g. set by Cable) overrides any per-app request, PipeASIO's included — without this, editing `PIPEASIO_PREFERRED_BUFFERSIZE` alone can silently have no effect if it disagrees with the system-wide setting, causing crackling. Requires the `pw-metadata` command (ships with PipeWire on most distributions); if it's missing, the script prints a warning and skips pinning, falling back to whatever Cable/WirePlumber has set.

To change the buffer size or sample rate, edit those two variables directly in the script — not Cable's Quantum/Sample Rate fields, which are only pinned while the game is running.

**Don't use `PIPEWIRE_QUANTUM`/`PIPEWIRE_LATENCY`/`PIPEWIRE_RATE` for this.** These are documented, real PipeWire environment variables, but they only set *per-stream* suggestions on whatever PipeWire client reads them — PipeASIO already sets its own stream properties from `PIPEASIO_PREFERRED_BUFFERSIZE`/`PIPEASIO_SAMPLE_RATE` and ignores them, and even if it didn't, a daemon-wide forced quantum/rate (like the one the script sets) overrides per-stream requests anyway. They won't fix a mismatch here.

## Further optimizations

This is the initial setup I performed on my own system. From there, you can set up Rocksmith as you prefer — I like to run it in a window, so I can open other apps and adjust Cables as needed. I also create a desktop app entry on the applications menu pointing to the shell script, for convenience.

You should try reducing the latency, too. I prefer to do that by directly editing `Rocksmith.ini`: edit the file, and adjust `PIPEASIO_PREFERRED_BUFFERSIZE` in `rocksmith-on-linux.sh` to match (no need to touch Cable — the script pins the quantum itself). Keep going until you get audio problems, then back up one step. Find what works for you.

## Additional information

**IMPORTANT**: when you switch to Cables (or any other app), Rocksmith's audio will mute. That's normal. When reconnecting the signal paths, keep both Rocksmith outputs connected to something at all times: if you want to route both outputs to the right channel on your headphone, for instance, first create a new connection from "left Rocksmith" to "right headphone", then disconnect "left Rocksmith" from "left headphone".

`pipewire-jack` is **not** required for PipeASIO — it talks to PipeWire directly via `libpipewire-0.3`, with no JACK dependency. Only install it if you plan to use WineASIO/JACK instead. It would also be required for routing the audio signal to other JACK-dependent applications, like Guitarix.

I originally relied on information from [Nizo's "Rocksmith 2014 on Linux"](https://codeberg.org/nizo/linux-rocksmith): the guide covers setting up Rocksmith 2014 on Arch, Debian, Fedora, SteamOS and NixOS-derived distributions, using PipeWire or JACK as the audio system. The guide includes instructions for Steam/Proton, troubleshooting tips and performance optimizations, and has good information on building suitable startup scripts. Check it out at [nizo/linux-rocksmith](https://codeberg.org/nizo/linux-rocksmith) (CC-BY-SA-4.0).
Since my current setup relies on PipeWire and PipeASIO, Nizo's guide is not really applicable here anymore — but it may be useful for those who prefer to use WineASIO, JACK, custom startup scripts, or alternative approaches.
