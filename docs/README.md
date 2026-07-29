# Rocksmith on Linux

## Requirements

- The original Ubisoft/Hercules *Real Tone Cable* (also known as *Rocksmith USB Guitar Adapter*).
- Steam's version of Rocksmith installed and set up according to [**Nizo's "Rocksmith 2014 on Linux"**](https://codeberg.org/nizo/linux-rocksmith) guide.

## Install/setup

- Copy the files `avrt.dll`, `RS_ASIO.dll` and `RS_ASIO.ini` of [latest release](https://github.com/ferabreu/rocksmith-on-linux/releases/latest) (zip archive rocksmith-on-linux-\<VERSION\>.zip ) to the game folder.
- The `RS_ASIO.ini` file is pre-configured to be used with the game running on Linux, with the usual Proton stack and WineASIO.
- Make sure `Rocksmith.ini` is set to run with `ExclusiveMode=1`. If in doubt, use default settings.
- Make sure your interface's sample rate is set to 48 kHz.
- An `RS_ASIO.log` file is generated inside the game directory which may help diagnosing issues.

## Uninstall

- Remove the files `avrt.dll`, `RS_ASIO.dll` and `RS_ASIO.ini` from the game folder.
