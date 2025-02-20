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
you'll need the appropriate driver for your interface model.

## MSD Mode?

"MSD Mode" is the "Mass Storage Device Mode" that the Scarlett 3rd and
4th Gen interfaces ship in.

If MSD Mode is enabled, you need to disable it and restart your
interface to get access to its full functionality.

When you plug the interface in, there'll be a tiny read-only virtual
disk that has a link to the Focusrite product registration page; until
you turn off MSD Mode not all features of the interface will be
available.

You can turn off MSD Mode by holding down the 48V button while
powering on the interface, or by clicking the button in
`alsa-scarlett-gui` and rebooting it.

If you do the recommended/required (depending on the model) firmware
update, MSD Mode will automatically be turned off.

## What is the purpose of these drivers if they're not needed for basic audio?

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
don't have any software controls. All the controls are available from
the front panel, so they don't require the specialised drivers or this
GUI.

## Where are the options to set the sample rate and buffer size?

The ALSA Scarlett Control Panel doesn't handle audio input/output
settings like sample rate and buffer size. These settings are managed
by the application using the soundcard, typically a sound server such
as PulseAudio, JACK, or PipeWire.

The sample rate shown in the control panel is informative only and
displays the current rate being used by applications. If it shows
“N/A” then no application is using the interface.

Note that not all features are available at higher sample rates; refer
to the user manual of your interface for more information.

## Help?!

For help with the Scarlett2 and FCP kernel drivers:
https://github.com/geoffreybennett/linux-fcp/issues

For help with the FCP user-space side:
https://github.com/geoffreybennett/fcp-support/issues

For help with `alsa-scarlett-gui`:
https://github.com/geoffreybennett/alsa-scarlett-gui/issues

For general Linux audio help: https://linuxmusicians.com
