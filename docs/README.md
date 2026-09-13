# Rocksmith on Linux

## Requirements

- The original Ubisoft/Hercules *Real Tone Cable* (also known as *Rocksmith USB Guitar Adapter*).
- A Linux-based OS correctly configured, including:
  - PipeWire (with the `pipewire-jack` module)
  - PipeASIO
  - Steam (and Proton)
- Steam's version of Rocksmith installed and [correctly set up](docs/setup.md).

From version 0.7.5, I'm focusing on running the game with PipeWire and PipeASIO - this setup has [made the whole config process much easier](docs/setup.md) for me, for running both Rocksmith and Rocksmith 2014.

I have tested only the Steam version of Rocksmith - so, I cannot assume other versions will work.

### Installation

- Copy the files `avrt.dll`, `RS_ASIO.dll`, `RS_ASIO.ini` and `rocksmith-on-linux.sh` from the [latest release](https://github.com/ferabreu/rocksmith-on-linux/releases/latest) (zip archive rocksmith-on-linux-\<VERSION\>.zip) to the game folder.
  - The `RS_ASIO.ini` file is pre-configured to be used with the recommended environment. You may alter it as needed.

### Setup/Config

Check the [setup guide](setup.md).

### Removal

- Remove the copied files (and `RS_ASIO.log`) from the game folder.

## Credits

Developed with the assistance of GitHub Copilot, Claude Sonnet 4.6/5 and Claude Haiku 4.5, based on my specs.

This project is a fork of [RS ASIO](https://github.com/mdias/rs_asio), by Micael Dias, without which this project would not be possible. RS ASIO is still being maintained and developed, and I recommend checking it out if you want to use ASIO with Rocksmith 2014.
