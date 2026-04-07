# Additional technical details about rocksmith-on-linux

## RS_ASIO.ini configuration

The default `dist/RS_ASIO.ini` file requires the following additional configuration for Rocksmith:

```ini
[Config]
EnableWasapiInputs=1   ; required — enables WASAPI capture device enumeration

[Asio.Output]
Driver=wineasio-rsasio

[Asio.Input.0]
Driver=wineasio-rsasio
WasapiDevice=Rocksmith  ; matches the Real Tone Cable's friendly name on Wine/Proton
```

### WasapiDevice matching

The `WasapiDevice=` value is matched **case-insensitively** against both:
- The full WASAPI device ID string (e.g. `{0.0.1.00000000}.{21D5646C-D708-4E90-A57A-E1956015D4F3}`)
- The device's friendly name (e.g. `Rocksmith Guitar Adapter Mono`)

The value `Rocksmith` matches all known Real Tone Cable variants on Wine/Proton by friendly name.

If this does not match on your system, run the game once, open `RS_ASIO.log`, and look for lines like:

```
{0.0.1.00000000}.{21D5646C-...} friendly name: "Rocksmith Guitar Adapter Mono"
```

Use any substring of the friendly name, or a fragment of the GUID, as the `WasapiDevice=` value.

## Rocksmith.ini

`Rocksmith.ini` (in the game folder) must have:

```ini
[Audio]
ExclusiveMode=1
```

---

## Troubleshooting

**Game asks to connect the Real Tone Cable at the tuner screen**

The WASAPI redirect did not activate. Check `RS_ASIO.log` for a line containing:
```
redirecting IAudioClient to ASIO input
```

The full log line includes the device ID and function name, e.g.:
```
{0.0.1.00000000}.{21D5646C-...} Activate - redirecting IAudioClient to ASIO input
```

If this line is absent, the `WasapiDevice=` value did not match any enumerated capture device. See the [WasapiDevice matching](#wasapidevice-matching) section above.

**No audio output**

Verify WineASIO is installed and the `Driver=wineasio-rsasio` name matches what appears in the `RS_ASIO.log` under `AsioHelpers::FindDrivers`.

**Crackling or dropouts**

If you experience dropouts, adjust your PipeWire quantum and buffer settings. The typical setup uses 256 frames at 48kHz (5ms latency), set via `PIPEWIRE_LATENCY="256/48000"` — but these are recommendations, not hard constraints. You may need to increase buffer size if dropouts persist.

---

## How Rocksmith audio differs from Rocksmith 2014

Understanding these differences explains why extra configuration is needed.

### Output

Both games use WASAPI exclusive mode for audio output. RS ASIO intercepts the WASAPI device enumeration and injects fake WASAPI devices backed by ASIO. This mechanism works identically for both games — no special configuration is needed for output.

### Guitar input

This is where Rocksmith and Rocksmith 2014 diverge fundamentally.

**Rocksmith 2014** selects its guitar input through the same COM device enumeration that RS ASIO already intercepts. When Rocksmith 2014 asks for a list of WASAPI capture devices, RS ASIO returns its own fake ASIO-backed devices in place of real ones, and the game picks from those.

**Rocksmith** does not go through device enumeration for input. Instead, it scans the system for WASAPI capture devices at startup, identifies the Real Tone Cable by its specific WASAPI device path (a `{flow}.{endpoint-GUID}` string assigned by Wine/Proton), and then opens it **directly by path** in exclusive mode. It never asks for a list — it already knows the address it wants.

Rocksmith also uses WASAPI in **polling mode**: rather than requesting event-driven callbacks (`AUDCLNT_STREAMFLAGS_EVENTCALLBACK`), it calls `GetCurrentPadding` in a loop to detect when new audio samples are available. Under Wine/Proton, RS ASIO would otherwise show a spurious "Did you set `Win32UltraLowLatencyMode=1`?" warning dialog when it sees a client that doesn't use event callbacks; this warning is suppressed for Rocksmith because its `Rocksmith.ini` file does not contain the `Win32UltraLowLatencyMode` setting.

Wine/Proton's WASAPI implementation rejects all audio formats offered by the game for the Real Tone Cable in exclusive mode — making the cable unusable for audio capture without RS ASIO.

### Why the Real Tone Cable is required

The cable is required for **enumeration**, not for audio. Rocksmith only opens a capture session when it finds and recognises the Real Tone Cable WASAPI device during its startup scan. Without the cable physically connected, Wine never enumerates that endpoint, so there is nothing for RS ASIO to intercept.

Once the game activates the cable's endpoint, RS ASIO redirects that activation to an ASIO-backed audio client. The **actual guitar audio then comes from WineASIO channel 0**, which can be fed from any physical interface through the PipeWire or JACK patchbay — exactly as with Rocksmith 2014. The Real Tone Cable's audio signal is never used for anything.

In summary: the cable's role is only to make Wine expose a WASAPI device with the right path. The audio path is:

```
Guitar → your audio interface → WineASIO (JACK/PipeWire) → rocksmith-on-linux → Rocksmith
```

### The RS ASIO solution for Rocksmith

RS ASIO intercepts `IMMDevice::Activate` for `IAudioClient`. When the game activates the Real Tone Cable's WASAPI device, RS ASIO detects the match (via `WasapiDevice=` in the INI) and returns an ASIO-backed `RSAsioAudioClient` instead of Wine's broken implementation. The game receives audio from the configured ASIO input channel transparently.

Audio format negotiation is also handled: Rocksmith offers float32 mono as its preferred format, which is compatible with WineASIO's native `ASIOSTFloat32LSB` type — no conversion needed.

The ASIO host is shared between output (already running for music playback) and input, using the same buffer size and sample rate. Only sample rate and buffer size are compared when a second client joins the shared host — format tag differences between output (PCM16) and input (float32 extensible) are intentionally ignored.

### The IAT patching approach

Rocksmith 2014's executable code is readable at load time, so RS ASIO finds injection points by scanning for known byte patterns. Rocksmith's executable is encrypted on disk by a custom packer stub (`PSFD00` section) and only decrypted in memory at runtime. By that time, RS ASIO's DLL is already loaded, making byte-pattern scanning impractical.

Instead, Rocksmith support patches the **Import Address Table (IAT)** directly. The IAT is part of the `.rdata` section, which is not encrypted, and is populated by the Windows loader before any DLL code runs. RS ASIO overwrites the IAT slots for the three COM functions Rocksmith uses to enumerate and access WASAPI devices:

- `CoCreateInstance` — used to create the WASAPI device enumerator
- `CoMarshalInterThreadInterfaceInStream` — used to pass COM objects across threads
- `CoGetInterfaceAndReleaseStream` — used to retrieve those objects on the other thread

Because Rocksmith does not use ASLR (its image base is fixed at `0x00400000`), the IAT slot addresses are constant across all runs, which makes this approach reliable.
