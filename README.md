# ALSA Scarlett Control Panel (`alsa-scarlett-gui`) v1.0

<img src="img/alsa-scarlett-gui.png" align="right">

A Linux control panel for Focusrite Scarlett, Clarett, and Vocaster
USB audio interfaces — the equivalent of Focusrite Control /
Scarlett MixControl / Vocaster Hub on Linux.

Configure routing, mixing, input/output levels, DSP processing,
and firmware updates through a graphical interface.

## Supported Interfaces

- Scarlett 1st Gen 6i6, 8i6, 18i6, 18i8, 18i20
- Scarlett 2nd Gen 6i6, 18i8, 18i20
- Scarlett 3rd Gen Solo, 2i2, 4i4, 8i6, 18i8, 18i20
- Scarlett 4th Gen Solo, 2i2, 4i4, 16i16, 18i16, 18i20
- Clarett 2Pre, 4Pre, 8Pre USB
- Clarett+ 2Pre, 4Pre, 8Pre
- Vocaster One and Vocaster Two

![Demonstration](img/demo.gif)

## Features

- **Drag-and-drop routing** — visual audio routing matrix with
  real-time signal level glow
- **Matrix mixer** — full mixer with per-crosspoint gain dials and
  post-gain level metering
- **DSP controls** — interactive parametric EQ and compressor with
  draggable response graph (Vocaster)
- **Configuration** — custom port names, stereo linking, port
  visibility, and autogain targets
- **Monitor groups** — Main/Alt output groups with per-output source
  and trim (Gen 4 large)
- **Presets** — quick save/load of named configurations, plus
  file-based save/load
- **Level meters** — real-time level display for all routing points
- **Firmware updates** — integrated firmware upgrade support

## Documentation

Refer to [INSTALL.md](docs/INSTALL.md) for prerequisites, how to
build, install, and run.

Refer to [USAGE.md](docs/USAGE.md) for general usage information and
known issues.

Information specific to various models:

- [Scarlett 1st Gen 6i6+](docs/iface-1st-gen.md)

- [Scarlett 3rd Gen Solo and 2i2](docs/iface-small.md)

- [Scarlett 2nd/3rd Gen 4i4+, Clarett USB, and
  Clarett+](docs/iface-large.md)

- [Scarlett 4th Gen Solo, 2i2, 4i4](docs/iface-4th-gen-small.md)

- [Scarlett 4th Gen 16i16, 18i16,
  18i20](docs/iface-4th-gen-big.md)

- [Vocaster One and Two](docs/iface-vocaster.md)

Additional documentation:

- [Configuration Window](docs/configuration.md)
- [Presets and Configuration Files](docs/presets.md)
- [DSP Window](docs/dsp.md) (Vocaster)
- [Digital I/O Availability](docs/digital-io-availability.md)
- [FAQ](FAQ.md) — Troubleshooting and common questions
- [Release Notes](RELEASE-NOTES.md) — What's new in each version

## Donations

This software — the Linux kernel driver, this control panel, and the
documentation — represents over a thousand hours of independent
development: reverse-engineering Focusrite's USB protocols,
developing and upstreaming the kernel driver, and building a
complete replacement for Scarlett MixControl, Focusrite Control,
Focusrite Control 2, and Vocaster Hub — often providing more
functionality than Focusrite's own software.

If you've found it valuable, a donation is a nice way to say thanks:

- https://liberapay.com/gdb
- https://paypal.me/gdbau
- Zelle: g@b4.vu

## License

Copyright 2022-2026 Geoffrey D. Bennett

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at
your option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.

## Disclaimer Third Parties

Focusrite, Scarlett, Clarett, and Vocaster are trademarks or
registered trademarks of Focusrite Audio Engineering Limited in
England, USA, and/or other countries. Use of these trademarks does not
imply any affiliation or endorsement of this software.
