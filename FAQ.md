# FAQ for the Scarlett2 Mixer Driver and `alsa-scarlett-gui`

## What is this?

The Scarlett2 Protocol Driver (also known as the Scarlett2 Mixer
Driver) is a part of the Linux kernel, enhancing the ALSA kernel
driver with additional controls for Focusrite Scarlett and Clarett
interfaces.

To check your current version or for upgrade instructions, see the
[Control Panel Installation Prerequisites — Linux
Kernel](https://github.com/geoffreybennett/alsa-scarlett-gui/blob/master/INSTALL.md).

`alsa-scarlett-gui` is an easy-to-use application to adjust those
controls.

## Do I need the driver for my Focusrite interface?

In order to get audio working? No. Focusrite USB interfaces are
“plug-and-play” — they are USB Audio Class Compliant, meaning they
work out-of-the-box with the standard ALSA USB audio driver[^1].

[^1]: To get full functionality on Scarlett 3rd/4th Gen interfaces,
first deactivate MSD mode for by holding down the 48V button while
powering it on (MSD is the virtual disk on the Scarlett that has a
link to the Focusrite product registration page; until you turn that
off not all features will be available).

## What is the purpose of the driver then?

This driver is for users who want more control over their interface.
It allows for detailed manipulation of internal audio routing and
settings specific to Scarlett devices, beyond the basic audio I/O
functionality. Also, being able to monitor the audio levels seen by
interface is really useful.

## What interfaces are supported?

- All Scarlett 2nd Gen interfaces with software controls (there are no
  software controls on the 2nd Gen Solo and 2i2, so the mixer driver
  is irrelevant).

- All Scarlett 3rd Gen interfaces.

- All Clarett USB and Clarett+ interfaces.

## Where are the options to set the sample rate and buffer size?

It’s important to note that the Scarlett2 driver and
`alsa-scarlett-gui` have nothing to do with audio input/output to and
from the device. This task is managed by the generic part of the ALSA
USB soundcard driver.

Audio settings like the sample rate and buffer size are chosen by the
application which is using the soundcard. In most cases, that is a
sound server such as PulseAudio, JACK, or PipeWire.

Note that not all features are available at higher sample rates; refer
to the user manual of your interface for more information.

## Scarlett Gen 4? Vocaster?

Support coming soon. Email me if you want to help test.

## Help?!

For help with the driver:
https://github.com/geoffreybennett/scarlett-gen2/issues

For help with `alsa-scarlett-gui`:
https://github.com/geoffreybennett/alsa-scarlett-gui/issues

For general Linux audio help:
https://linuxmusicians.com
