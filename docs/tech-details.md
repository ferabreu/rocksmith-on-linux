# Additional technical details about rocksmith-on-linux

## Move to PipeWire + PipeASIO

From version 0.7.5, the recommended setup moved from WineASIO/JACK to **PipeWire + PipeASIO**. On a current Arch-based system (e.g. CachyOS) with a current Wine/Proton build, this combination is simpler to maintain: PipeASIO builds against current SysWOW64 support, needs no separate JACK server, and its registration/setup steps are more reliable than WineASIO's on recent Wine versions. See the [README](../README.md) and the [setup guide](setup-guide.md) for the full rationale and installation steps.

WineASIO remains supported as a driver option (`Driver=wineasio-rsasio` in `RS_ASIO.ini`) for systems that are already JACK-centric, but PipeASIO is the default going forward.

## RS_ASIO.ini configuration

The default `assets/RS_ASIO.ini` file requires the following configuration for Rocksmith:

```ini
[Config]
EnableWasapiInputs=1   ; required — enables WASAPI capture device enumeration

[Asio.Output]
Driver=PipeASIO  ; easier to install/register, works with SysWOW64; use wineasio-rsasio for a JACK-based setup

[Asio.Input.0]
Driver=PipeASIO
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

Verify the driver named in `Driver=` (`PipeASIO` by default, or `wineasio-rsasio` if you're using WineASIO) matches what appears in the `RS_ASIO.log` under `AsioHelpers::FindDrivers`.

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

The cable is required for **enumeration**: Rocksmith only opens a capture session when it finds and recognises the Real Tone Cable WASAPI device during its startup scan. Without the cable physically connected, Wine never enumerates that endpoint, so there is nothing for RS ASIO to intercept.

Once the game activates the cable's endpoint, RS ASIO redirects that activation to an ASIO-backed audio client. The **actual guitar audio then comes from the configured ASIO input channel** (`Channel=` in `RS_ASIO.ini`). Since the cable is itself a USB audio interface with its own 1/4" input jack, it already appears as its own PipeWire/JACK device — so the simplest and most common setup is to patch the cable's own input directly into that ASIO channel. You are not limited to this, though: any physical interface can be routed into the same ASIO channel through the PipeWire (or JACK) patchbay instead, exactly as with Rocksmith 2014.

In summary: the cable's role is to make Wine expose a WASAPI device with the right path, and — in the default setup — to also provide the guitar's audio signal itself. The audio path is:

```
Guitar → Real Tone Cable → PipeASIO (PipeWire) or WineASIO (JACK) → rocksmith-on-linux → Rocksmith
```

### The RS ASIO solution for Rocksmith

RS ASIO intercepts `IMMDevice::Activate` for `IAudioClient`. When the game activates the Real Tone Cable's WASAPI device, RS ASIO detects the match (via `WasapiDevice=` in the INI) and returns an ASIO-backed `RSAsioAudioClient` instead of Wine's broken implementation. The game receives audio from the configured ASIO input channel transparently.

Audio format negotiation is also handled: Rocksmith offers float32 mono as its preferred format, which is compatible with the ASIO driver's native `ASIOSTFloat32LSB` type (both PipeASIO and WineASIO use it) — no conversion needed.

The ASIO host is shared between output (already running for music playback) and input, using the same buffer size and sample rate. Only sample rate and buffer size are compared when a second client joins the shared host — format tag differences between output (PCM16) and input (float32 extensible) are intentionally ignored.

### The IAT patching approach

Rocksmith 2014's executable code is readable at load time, so RS ASIO finds injection points by scanning for known byte patterns. Rocksmith's executable is encrypted on disk by a custom packer stub (`PSFD00` section) and only decrypted in memory at runtime. By that time, RS ASIO's DLL is already loaded, making byte-pattern scanning impractical.

Instead, Rocksmith support patches the **Import Address Table (IAT)** directly. The IAT is part of the `.rdata` section, which is not encrypted, and is populated by the Windows loader before any DLL code runs. RS ASIO overwrites the IAT slots for the three COM functions Rocksmith uses to enumerate and access WASAPI devices:

- `CoCreateInstance` — used to create the WASAPI device enumerator
- `CoMarshalInterThreadInterfaceInStream` — used to pass COM objects across threads
- `CoGetInterfaceAndReleaseStream` — used to retrieve those objects on the other thread

Because Rocksmith does not use ASLR (its image base is fixed at `0x00400000`), the IAT slot addresses are constant across all runs, which makes this approach reliable.

---

## Cable detection failure under winepipewire (fixed 2026-07)

Starting with proton-cachyos 11.0-20260702, `winepipewire.drv` replaced `winepulse.drv` as the default Wine audio driver. This broke Rocksmith's cable detection in two separate, sequential ways — both had to be fixed before the game could see the cable again.

### 1. SetupAPI enumeration failure (device not found at all)

**The problem**: Before ever touching WASAPI, Rocksmith checks Wine's SetupAPI for the presence of a KS audio device. Under `winepulse`, Wine's audio backend registered the Real Tone Cable in the KS device class registry (`HKLM\SYSTEM\CurrentControlSet\Control\DeviceClasses\{KSCATEGORY_AUDIO}` and `Enum\USB\VID_12BA&PID_00FF`) as a side effect of its own USB PnP handling. `winepipewire` performs no such registration, so `SetupDiGetClassDevsA`/`SetupDiEnumDeviceInterfaces` find nothing — the tuner screen keeps asking for the cable even though it's plugged in and already visible to WASAPI.

Forcing `WINE_AUDIO_DRIVER=pulse` restores the registration, but is not a viable workaround: Wine 11.14's `winepulse` path calls the still-unimplemented `SetupDiGetDeviceInterfaceAlias`, aborting the process (`wine: Call from ... to unimplemented function setupapi.dll.SetupDiGetDeviceInterfaceAlias, aborting`). The driver choice is also cached in the Proton prefix's registry (`HKCU\Software\Wine\Drivers`), so reverting to `winepipewire` afterwards doesn't undo the crash until that cached value is cleared too.

**The fix**: `EnsureRealToneCableRegistered()` (`RS_ASIO/Patcher_e0f686e0.cpp`) runs before the game's IAT patches are installed and creates the missing KS device class registry entries itself, based on the fact that WASAPI already confirms the cable is present. Wine's real `SetupDiGetClassDevsA` then finds these entries naturally — no fake hooks, no synthesized SetupAPI responses. The `FriendlyName` used (`"Rocksmith USB Guitar Adapter"`) is the cable's actual USB descriptor product string (confirmed via `lsusb -v`).

### 2. IPropertyStore property count mismatch (device found but not identified)

**The problem**: Once SetupAPI finds the device, Rocksmith identifies it as the *specific* Real Tone Cable by scanning `IPropertyStore` properties on every enumerated WASAPI capture endpoint until it finds one containing the cable's USB VID/PID (`VID_12BA&PID_00FF`). It does this via the standard COM enumeration pattern: `GetCount()` to get the property count, then `GetAt(0..count-1)` to retrieve each property key, then `GetValue(key)` to read it.

Under `winepulse`, the cable's endpoint exposed 10 properties, two of which contained the VID/PID string. Under `winepipewire`, the same endpoint exposes only 8 properties — the two USB identification entries are absent. Since the game's loop only goes up to `count - 1`, it never reaches the properties that would have identified the cable, and detection silently fails with no error.

**The fix**: `DebugWrapperDevicePropertyStore` (the wrapper RS ASIO places around every WASAPI device's property store) now detects the cable and injects the two missing properties on demand:

1. **Cable identification** (`GetValue`): the first time a property value contains the substring `"Rocksmith"` in a property known to hold the device name (`PKEY_Device_FriendlyName`, or Wine's internal short-name property), the wrapper marks that device as the cable (`m_IsCableDevice`).
2. **Count override** (`GetCount`): for the cable device, the wrapper probes whether the real store already has the USB ID property. If not, it reports `realCount + 2` instead of the real count, so the game's enumeration loop reaches two extra indices.
3. **Key injection** (`GetAt`): for indices at or beyond the real count, the wrapper returns two synthetic property keys (`PKEY_Device_DeviceIdHiddenKey1`/`2`) instead of delegating to the real store.
4. **Value injection** (`GetValue`): when the game asks for those two synthetic keys, the wrapper returns literal strings containing `VID_12BA&PID_00FF` (the values a `winepulse` cable endpoint would have provided), satisfying the game's detection check.

This makes the fake properties indistinguishable from what the game already expected to find under `winepulse`, without needing to patch the game itself or depend on a specific Wine audio driver.

### Design notes for future maintenance

- Detection uses `"Rocksmith"` rather than a longer phrase, since it's the shortest substring guaranteed to appear in the cable's USB product string on every unit, regardless of USB port or system.
- The count override uses `realCount + 2` rather than a hardcoded value, and skips injection entirely if the real store already exposes the USB ID property — so the fix keeps working even if a future Wine/PipeWire version changes the baseline property count, or starts exposing the USB ID natively.
- This relies on the property enumeration always following `GetCount` → `GetAt` → `GetValue`, which is the only order that makes sense for a caller that doesn't already know the property count in advance.
