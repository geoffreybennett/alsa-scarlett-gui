# FAQ for the ALSA Scarlett Control Panel (`alsa-scarlett-gui`)

## What is this?

The ALSA Scarlett Control Panel (`alsa-scarlett-gui`) is an
easy-to-use application for adjusting the ALSA controls provided by
three Linux kernel drivers for Focusrite USB interfaces:

1. The Scarlett 1st Gen Mixer Driver (for 1st Gen 6i6, 8i6, 18i6, 18i8, 18i20)
2. The Scarlett2 Protocol Driver (for 2nd/3rd Gen interfaces, small 4th Gen, Clarett, and Vocaster)
3. The FCP (Focusrite Control Protocol) Driver (for big 4th Gen interfaces: 16i16, 18i16, 18i20)

To check if your kernel is already up-to-date, and how to upgrade if
not, see the [Control Panel Installation Prerequisites — Linux
Kernel](docs/INSTALL.md).

## Do I need these drivers for my Focusrite interface?

For basic audio functionality? No. Focusrite USB interfaces are
“plug-and-play” — they are USB Audio Class Compliant, meaning they
work out-of-the-box with the standard ALSA USB audio driver (to get
full functionality on Scarlett 3rd/4th Gen/Vocaster interfaces, first
deactivate MSD mode by holding down the 48V button while powering it
on).

However, to access the mixer, routing, and hardware-specific features,
you’ll need the appropriate driver for your interface model.

## MSD Mode?

"MSD Mode" is the "Mass Storage Device Mode" that the Scarlett 3rd and
4th Gen interfaces ship in.

If MSD Mode is enabled, you need to disable it and restart your
interface to get access to its full functionality.

When you plug the interface in, there’ll be a tiny read-only virtual
disk that has a link to the Focusrite product registration page; until
you turn off MSD Mode not all features of the interface will be
available.

You can turn off MSD Mode by holding down the 48V button while
powering on the interface, or by clicking the button in
`alsa-scarlett-gui` and rebooting it.

If you do the recommended/required (depending on the model) firmware
update, MSD Mode will automatically be turned off.

## What is the purpose of these drivers if they’re not needed for basic audio?

These drivers are for users who want more control over their
interface. They allow for detailed manipulation of:

- Internal audio routing
- Hardware-specific settings
- Mixer functionality
- Level monitoring
- Input/output configuration

These controls go beyond the basic audio I/O functionality provided by
the generic ALSA USB audio driver.

## What interfaces are supported?

The ALSA Scarlett Control Panel supports:

- **Scarlett 1st Gen**: 6i6, 8i6, 18i6, 18i8, 18i20
- **Scarlett 2nd Gen**: 6i6, 18i8, 18i20
- **Scarlett 3rd Gen**: Solo, 2i2, 4i4, 8i6, 18i8, 18i20
- **Scarlett 4th Gen**: Solo, 2i2, 4i4, 16i16, 18i16, 18i20
- **Clarett USB and Clarett+**: 2Pre, 4Pre, 8Pre
- **Vocaster**: One, Two

Note: The Scarlett 1st and 2nd Gen small interfaces (Solo, 2i2, 2i4)
don’t have any software controls. All the controls are available from
the front panel, so they don’t require the specialised drivers or this
GUI.

## Where are the options to set the sample rate and buffer size?

The ALSA Scarlett Control Panel doesn’t handle audio input/output
settings like sample rate and buffer size. These settings are managed
by the application using the soundcard, typically a sound server such
as PulseAudio, JACK, or PipeWire.

The sample rate shown in the control panel is informative only and
displays the current rate being used by applications. If it shows
“N/A” then no application is using the interface.

Note that not all features are available at higher sample rates; refer
to the user manual of your interface for more information.

## Why do my settings keep resetting?

The settings in the ALSA Scarlett Control Panel are automatically
saved in the interface itself (all series except 1st Gen), so they
should persist across reboots, power cycles, USB disconnect/reconnect,
and even across different computers. This includes all routing,
mixing, and other control panel settings.

If you find that your settings are reverting whenever you plug your
interface in, power it back on, or even if you reset to factory
defaults, the most likely cause is the `alsa-state` and `alsa-restore`
systemd services. These services save the state of ALSA controls on
system shutdown to `/var/lib/alsa/asound.state` and then restore it
each time the device is plugged in, potentially overwriting your
interface’s stored settings.

It can be rather annoying, wondering why your device is unusable or
needs to be reconfigured every time you plug it in or turn it on.

Presuming that you have no other sound card that needs this ALSA
service, then disable and stop these two services and remove the
`asound.state` file:

```sh
sudo systemctl mask alsa-state
sudo systemctl mask alsa-restore
sudo systemctl stop alsa-state
sudo systemctl stop alsa-restore
sudo rm /var/lib/alsa/asound.state
```

You can verify if this is the cause of your issues by:

1. Change some setting that is indicated on the device (the “Inst”
   setting is a good one to test with).
2. Disconnect USB and notice the state of the setting on the device
   has not changed.
3. Power cycle the device and notice the state of the setting on the
   device has not changed.
4. Reconnect USB and notice the state of the setting on the device has
   changed.

If the setting on the device changes at step 4, then the `alsa-state`
and `alsa-restore` services are the likely cause of your issues and
you should disable them as above.

## Why is there no stereo output? I only see surround options.

By default, PulseAudio/PipeWire treats multi-channel interfaces as
surround sound devices. To get proper stereo output and individual
channel access, select the "Pro Audio" profile:

1. Open `pavucontrol` (PulseAudio Volume Control)
2. Go to the Configuration tab
3. Find your Scarlett/Clarett/Vocaster
4. Change the profile to "Pro Audio"

This gives you access to all channels individually instead of
mapping them to surround speaker positions.

Note: Some desktop sound settings panels (notably GNOME Settings
and Linux Mint's) don't handle multi-channel interfaces well. Use
`pavucontrol` instead for reliable configuration.

## My desktop audio is coming through my microphone input

This typically happens because the default PulseAudio/PipeWire
profile maps your interface's channels incorrectly. For example,
the Scarlett Solo gets treated as a 4.0 surround device where the
"Rear" channels are actually your loopback inputs (desktop audio
routed back to recording applications).

The fix is the same as above: select the "Pro Audio" profile in
`pavucontrol`. This prevents PulseAudio/PipeWire from conflating
hardware inputs with loopback channels.

If you need loopback (recording desktop audio), use the Routing
window in `alsa-scarlett-gui` to explicitly configure which PCM
inputs receive which sources.

## My mixer isn't working / no audio passes through

The hardware mixer in Scarlett interfaces is disabled at higher
sample rates. This is a hardware limitation:

- At **single-band** rates (44.1/48 kHz): mixer fully functional
- At **dual-band** rates (88.2/96 kHz): mixer available on most
  models (some have reduced mix buses)
- At **quad-band** rates (176.4/192 kHz): mixer entirely
  unavailable

If your sample rate is set high and no audio passes through the
mixer, this is expected behaviour. Lower the sample rate, or
route audio directly (bypassing the mixer) in the Routing window.

The mixer window will show a dimmed overlay message when the
mixer is unavailable at the current sample rate.

## My interface shows limited controls or wrong channel count

If your interface only shows a few inputs or missing controls,
the most likely cause is a firmware version mismatch. This
happens when:

- You updated firmware on Windows/Mac to a version newer than
  what the Linux tools support, or
- You're running an older version of `alsa-scarlett-gui` or
  `fcp-server` that doesn't match your firmware

**Solution:** Update `alsa-scarlett-gui` (and `fcp-server` if
using a Gen 4 large interface) to the latest version. If needed,
the GUI will prompt you to update your firmware to a compatible
version.

For Gen 4 large interfaces, ensure your `fcp-server` version
matches the firmware version — they must be from the same
release.

## What do I need for a Scarlett 4th Gen 16i16, 18i16, or 18i20?

The "big" 4th Gen interfaces use a different driver architecture
from all other supported models. You need:

1. **Linux kernel 6.14+** — contains the FCP kernel driver
2. **fcp-server** — the user-space daemon from the
   [fcp-support](https://github.com/geoffreybennett/fcp-support)
   repo
3. **Firmware** — from the
   [scarlett4-firmware](https://github.com/geoffreybennett/scarlett4-firmware)
   repo, installed to `/usr/lib/firmware/scarlett4`
4. **alsa-scarlett-gui** — version 1.0 or later

Verify your setup by checking `dmesg` after plugging in. You
should see:

```
Focusrite Control Protocol Driver v6.x.x ready
```

If that message is missing, your kernel doesn't have the FCP
driver. Check that you're actually booted into the correct kernel
with `uname -r`.

## ALSA UCM errors after a distro update

If you see errors mentioning "UCM" (Use Case Manager) after
updating your distribution or PipeWire, the issue is likely
outdated `alsa-ucm-conf` files shipped by your distro.

**Solution:** Install the latest alsa-ucm-conf from
https://github.com/geoffreybennett/alsa-ucm-conf — this fork
contains the correct UCM2 profiles for Focusrite interfaces.

This is not a bug in `alsa-scarlett-gui` or the kernel driver;
it's a packaging issue in some distributions. The fix usually
arrives in distro updates within a few days of being reported.

## USB connection problems / intermittent failures

If your interface intermittently fails to initialise, drops out,
or shows errors like `usb_set_interface failed (-71)` in `dmesg`:

1. **Try a different USB cable** — this is the most common cause.
   On interfaces with a front-panel LED, the LED should turn
   white during USB setup. If it stays red, the cable or port
   isn't providing adequate connection.
2. **Use a cable under 2 metres**
3. **Connect directly** — avoid USB hubs where possible
4. **Try a different USB port** — ideally one on its own USB bus
5. **Check USB autosuspend** — some systems aggressively suspend
   USB devices. Check if
   `/sys/bus/usb/devices/*/power/control` is set to `auto` for
   your interface and change it to `on` if so.

## My input gain keeps decreasing by itself

This is the Safe Mode feature working as intended. Safe Mode
automatically reduces gain when the input signal clips, to
prevent distortion in recordings.

If you're using the interface for voice calls, streaming, or
other non-recording purposes where occasional clipping isn't
critical, disable Safe Mode in the main window of
`alsa-scarlett-gui`.

## PipeWire/JACK conflicts

If you're running JACK alongside PipeWire (or starting JACK
manually), it will take exclusive control of your interface,
preventing other applications from using it.

Modern PipeWire includes full JACK compatibility. The simplest
solution is to not start JACK separately — PipeWire provides a
JACK-compatible interface that applications can use
transparently. Remove or disable any JACK autostart configuration
and use PipeWire's built-in JACK support instead.

## An application says it can't open my device

Applications that try to open the ALSA device directly (using
paths like `plughw:3,0` or `hw:2,0`) will fail if PipeWire
already has the device open.

**Solution:** Configure the application to use the default audio
device (usually shown as "Default" or "PipeWire" in its settings)
rather than a specific hardware path. In most applications, this
means selecting "default" or "PipeWire" as the audio device.

## Firefox/Wine/games crash or create excessive connections

Some applications can't handle interfaces with more than 8
channels, causing crashes or misbehaviour:

- **Firefox**: may create a new PipeWire connection every second,
  flooding your audio graph
- **Wine/games**: may crash with channel mask errors when the
  interface has more than 8 channels

**Solution:** Create a virtual stereo device that applications
can use instead of the raw multi-channel interface:

```sh
pactl load-module module-remap-sink \
  sink_name=scarlett-stereo \
  master=$(pactl list short sinks | grep -i scarlett | cut -f2) \
  channels=2
```

Or select the "Pro Audio" profile in `pavucontrol` and use
PipeWire's built-in routing to present a stereo device to
applications.

## The application won't start or shows rendering errors

If `alsa-scarlett-gui` fails to start, shows a blank window, or
crashes with Vulkan/rendering errors, try setting the GTK4
renderer:

```sh
GSK_RENDERER=ngl alsa-scarlett-gui
```

If that works, make it permanent by adding
`GSK_RENDERER=ngl` to your environment (e.g. in
`~/.bashrc` or a systemd environment file).

This is a GTK4/GPU driver issue, not specific to
`alsa-scarlett-gui`.

## Flatpak limitations

The Flatpak version of `alsa-scarlett-gui` has some limitations
due to sandboxing:

- **Firmware updates** cannot be performed from the Flatpak
  version because the sandbox prevents access to the firmware
  directory. Use the native build for firmware updates.
- **Configuration save/load** using `alsactl` format may not work
  within the Flatpak sandbox. The native `.conf` format works
  normally.

If you only need basic device control (routing, mixer, levels),
the Flatpak works fine. For firmware management, build and install
natively.

## How do I create virtual audio channels?

If you want named stereo outputs (like a "Music" output and a
"Voice" output routed to different physical channels), you can
create virtual PulseAudio/PipeWire sinks:

```sh
# Create a virtual stereo sink mapped to outputs 3-4
pactl load-module module-remap-sink \
  sink_name=music-out \
  master=alsa_output.usb-Focusrite_Scarlett_18i20-00.pro-output-0 \
  channels=2 \
  master_channel_map=front-left,front-right \
  channel_map=front-left,front-right \
  remix=no
```

These virtual sinks then appear in your application audio
settings as separate output devices. Use `qpwgraph` or
`pw-link` to route them to specific physical outputs on your
interface.

This is independent of the hardware routing in
`alsa-scarlett-gui` — it's PipeWire/PulseAudio-level virtual
routing on top of whatever hardware routing you've configured.

## Why does phantom power turn on when I plug in my interface?

This is the same `alsa-state`/`alsa-restore` issue described in
"Why do my settings keep resetting?" above. The systemd services
restore a previously-saved state that had phantom power enabled.

This can be a safety concern if you have ribbon microphones or
other equipment that can be damaged by unexpected phantom power.
Disable the `alsa-state` and `alsa-restore` services as described
above.

## Help?!

Have you read the User Guide for your interface? It’s available
online: https://downloads.focusrite.com/focusrite and contains a lot
of helpful/useful/important information about your device.

You can skip the “Easy Start” and “Setting up your DAW” sections, but
the rest is well worth reading. Even the information about Focusrite
Control is useful, although not directly applicable, because it will
help you understand more about the possibilities of what you can do
with your device.

For help with the Scarlett2 and FCP kernel drivers:
https://github.com/geoffreybennett/linux-fcp/issues

For help with the FCP user-space side:
https://github.com/geoffreybennett/fcp-support/issues

For help with `alsa-scarlett-gui`:
https://github.com/geoffreybennett/alsa-scarlett-gui/issues

For general Linux audio help: https://linuxmusicians.com
