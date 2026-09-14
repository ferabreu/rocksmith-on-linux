# Setup guide

## Recommended environment

**CachyOS Linux** (includes **PipeWire**)

- **Required packages**:
  - `cachyos-gaming-meta` and `cachyos-gaming-applications` (check the [Gaming with CachyOS Guide](https://wiki.cachyos.org/configuration/gaming))
  - `mingw-w64-gcc`

- **Suggested package from AUR**: `cable` ([https://github.com/magillos/Cable](https://github.com/magillos/Cable)). The package includes two useful applications:
  - Cable makes it easy setting PipeWire's quantum
  - Cables allows you to route the audio paths in real time, so you can correct any problems or route the signal to recording, splitting, processing on Guitarix, etc.

- **PipeASIO**, manually built from the git repository:
  - For running *Rocksmith* or *Rocksmith 2014*, the AUR version will not work while the 32-bit version of the driver is not included in the PKGBUILD. So, build it manually for now.
  - **Version 1.4.3 works perfectly.** I'm using version 1.7.0, but had problems:
    - I could not manually build version 1.7.5 on my system. I have fixed two files using AI so I could build it, and [filed an issue in PipeASIO's repository](https://github.com/M0n7y5/pipeasio/issues/27).
    - I could not register it in Rocksmith's prefix. I have [filed an issue in PipeASIO's repository](https://github.com/M0n7y5/pipeasio/issues/26) about the bug, in the registration script, that must be fixed so the registration works correctly.

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

4. Run the game once, so it's Proton prefix is created.

5. Clone the PipeASIO repository.

6. Build and install the driver by executing the following commands from the cloned repo's directory:
   ```console
   cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_WOW64_32=ON`
   cmake --build build
   cmake --install build --prefix "$HOME/.local"
   ```

7. Register the driver in Rocksmith's prefix (replace ${STEAMLIB_DIR} as required for your system):
   - **for PipeASIO v1.4.3**
   ```console
   env WINEPREFIX='${STEAMLIB_DIR}/SteamLibrary/steamapps/compatdata/205190/pfx' \
   ~/.local/bin/pipeasio-register
   ```
   - **for PipeASIO v1.6.0 and up**
   ```console
   env WINEPREFIX='${STEAMLIB_DIR}/SteamLibrary/steamapps/compatdata/205190/pfx' \
       WINE='umu-run' \
       PROTONPATH='/usr/share/steam/compatibilitytools.d/proton-cachyos-slr' \
       GAMEID='205190' \
       PIPEASIO_PREFIX="${HOME}/.local" \
   ${HOME}/.local/bin/pipeasio-register
   ```

8. Open Cable and set the Sample Rate to 48000. Leave the Quantum as it is, probably 1024 - you'll tweak it later.

9. Open Cables, and leave it running.

10. Run the game using the provided shell script. It should open a terminal window and then the game, with audio autput.

## Further optimizations

This is the initial setup I performed in my own system. From there, you can set up Rocksmith as you prefer - I like to run it on a window, so I can open other apps and adjust Cables as needed. I also create a desktop app entry on the applications menu pointing to the shell script, for convenience.

You should try reducing the latency, too. I prefer to do that by directly editing `Rocksmith.ini`: edit the file, adjust the Quantum on Cable to match, and run the game. Keep going until you get audio problems, then back up one step. Find what works for you.

## Additional information

**IMPORTANT**: when you alternate to Cables (or any other app), Rocksmith's audio will mute. That's normal. When reconnecting the signal paths, keep both ROcksmith outputs connected to something at all times: if you want to route both outputs to the right channel on your headphone, for instance, first create a new connection from "left Rocksmith" to "right headphone", then disconnect "left Rocksmith" from "left headphone".

This guide works for both *Rocksmith*, using Rocksmith on Linux, and *Rocksmith 2014*, using [RS ASIO](https://github.com/mdias/rs_asio). If you want to use the shell script provided here to run 2014, you will have to modify the game id number from 205190 to 221680.

`pipewire-jack` is **not** required for PipeASIO — it talks to PipeWire directly via `libpipewire-0.3`, with no JACK dependency. Only install it if you plan to use WineASIO/JACK instead. It would also be required to routing the audio signal to other JACK dependant applications, like Guitarix.

I originally relied on information from [Nizo's "Rocksmith 2014 on Linux"](https://codeberg.org/nizo/linux-rocksmith): the guide covers setting up Rocksmith 2014 on Arch, Debian, Fedora, SteamOS and NixOS-derived distributions, using PipeWire or JACK as the audio system. The guide includes instructions for Steam/Proton, troubleshooting tips and performance optimizations, and has good information on build suitable startup scripts. Check it out at [nizo/linux-rocksmith](https://codeberg.org/nizo/linux-rocksmith) (CC-BY-SA-4.0).
Since my current setup relies on PipeWire and PipeASIO, Nizo's guide is not really appliable here anymore - but it may be useful for those who prefer to use WineASIO, JACK, custom startup scripts, or alternative approaches.
