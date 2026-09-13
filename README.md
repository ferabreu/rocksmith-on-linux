<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/rocksmith-on-linux-logo-plain-dark.svg">
    <img alt="Rocksmith on Linux" src="docs/assets/rocksmith-on-linux-logo-plain-light.svg" width="600">
  </picture>[^1]
</p>

## Intro

*Rocksmith on Linux* is a patch that adds ASIO support to Ubisoft's **Rocksmith**, allowing the game to be run on Linux using Proton, PipeWire and an ASIO driver for Wine.

This project is a fork of [RS ASIO](https://github.com/mdias/rs_asio), by Micael Dias, modified to work on Rocksmith. Rocksmith is different from *Rocksmith 2014* in how it handles audio, and the original RS ASIO mod doesn't work with it. 

Using Ubisoft's Real Tone Cable is still required. Rocksmith does not work without it. Because of that, the only practical purpose of this mod is to allow the game to run on Linux with low latency and good performance.

In my experience, using this patch while running the game on a relatively modern Linux system makes it playable as if it were running on Windows. On a modern machine, PipeWire makes it possible to get really low latency and redirect the audio output in useful/creative ways, like for recording or listening to your playing's "raw tone" (this is especially useful, since Rocksmith makes your tone better than it would sound in a real live scenario).

## How to use

### Requirements

- The original Ubisoft/Hercules *Real Tone Cable* (also known as *Rocksmith USB Guitar Adapter*).
- A Linux-based OS correctly configured, including:
  - PipeWire (with the `pipewire-jack` module)
  - [PipeASIO](https://github.com/M0n7y5/pipeasio)
  - Steam (and Proton)
- Steam's version of Rocksmith installed and [correctly set up](docs/setup-guide.md).

From version 0.7.5 onwards, I'm focusing on running the game with PipeWire and PipeASIO - this setup has [made the whole config process much easier](docs/setup-guide.md) for me, for running both Rocksmith and Rocksmith 2014.

I have tested only the Steam version of Rocksmith - so, I cannot assume other versions will work.

### Installation

- Copy the files `avrt.dll`, `RS_ASIO.dll`, `RS_ASIO.ini` and `rocksmith-on-linux.sh` from the [latest release](https://github.com/ferabreu/rocksmith-on-linux/releases/latest) (zip archive rocksmith-on-linux-\<VERSION\>.zip) to the game folder.
  - The `RS_ASIO.ini` file is pre-configured to be used with the recommended environment.

### Configuration

Check the [setup guide](docs/setup-guide.md).

### Removal

- Remove the copied files (and `RS_ASIO.log`) from the game folder.

---

## Known issues

- Only works with the original Ubisoft/Hercules *Real Tone Cable* (also known as *Rocksmith USB Guitar Adapter*).
- 32-bit game, 32-bit problems. PipeASIO simplifies this greatly, because it allows using Windows' SysWOW64 under Proton versions that support it. Check the [setup guide](docs/setup-guide.md).
- Hardware hotplugging while the game is running won't be noticed by the game.
- Sometimes, the game will not start. That's probably a momentary issue. Just try again and it should work.
- I'll attempt to follow the upstream RS ASIO versioning scheme and bring its improvements to this fork. However, because of the local changes, not all updates will be appliable/carried over.
  
---

## Disclaimer

**Testing and Limitations:**
- I made this software for my personal use.
- I tested this software only for me. It has not been exhaustively tested.
- Basic functionality has been verified:
  - with a single Real Tone Cable
  - in a single Linux environment based on CachyOS Linux, using Steam/Proton, PipeWire, and PipeASIO.
- It may not work in all possible environments and configurations.

**No Warranty:**
- This software is provided as-is without any warranty of any kind, express or implied.
- The authors are not responsible for any problems, damages, data loss, or other issues arising from the use of this software.
- Use at your own risk.

**Legal Notice:**
- *Rocksmith* and *Rocksmith 2014* are trademarks and properties of Ubisoft.
- This project is an independent effort and is not affiliated with, endorsed by, or approved by Ubisoft.

---

## Additional technical details

Additional information about how *Rocksmith on Linux* works, and how to configure it, can be found in the [technical details document](docs/tech-details.md). This includes information about how the WASAPI redirect works and how to match the `WasapiDevice` setting.

I have also included a good portion of my "development conversations" with GitHub Copilot in [docs/copilot-sessions](docs/copilot-sessions):
- [01-copilot-chat.md](docs/copilot-sessions/01-copilot-chat.md) covers the initial specifications I gave to Copilot, and the subsequent conversation where I asked for help with implementation details, debugging and testing (including a section on trying to replicate full ASIO support in Wine, which was ultimately unsuccessful).
- [02-gpt53codex.md](docs/copilot-sessions/02-gpt53codex.md) and [03-cable-detect-failure.md](docs/copilot-sessions/03-cable-detect-failure.md) cover later sessions diagnosing and fixing cable-detection regressions caused by Wine/Proton updates.

These may be interesting for those who want to understand how the project was developed, or how to use GitHub Copilot for similar projects.

---

## Credits

Developed with the assistance of GitHub Copilot, Claude Sonnet 4.6/5 and Claude Haiku 4.5, based on my specs.

This project is a fork of [RS ASIO](https://github.com/mdias/rs_asio), by Micael Dias, without which this project would not be possible. RS ASIO is still being maintained and developed, and I recommend checking it out if you want to use ASIO with Rocksmith 2014.

[^1]: The logo is a modified version of the original Rocksmith logo, created by Ubisoft. The logo and branding are protected, meaning they cannot be used for commercial purposes without authorization from Ubisoft. This project is an independent effort and is not affiliated with, endorsed by, or approved by Ubisoft. The logo is used here for informational purposes only, to indicate that the project is related to Rocksmith.