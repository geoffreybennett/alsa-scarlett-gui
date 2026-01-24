# Vocaster Interfaces

This document describes how to use alsa-scarlett-gui with
the Focusrite Vocaster interfaces:

- Vocaster One
- Vocaster Two

The Vocaster interfaces are designed for podcasting and
streaming, with built-in DSP processing, podcast-oriented
routing, and video call integration.

**Kernel requirement:** Linux 6.10 or later.

## Main Window

![Vocaster Main Window](../img/iface-vocaster-main.png)

The main window is organised into sections:

- Global Controls (top)
- Input Controls (left/middle)
- Output Controls (right)

### Global Controls

#### Sync Status

Displays whether the interface is locked to a valid clock.
This should only briefly show "Unlocked" when the sample
rate changes.

#### Sample Rate

Shows the current sample rate when the interface is in use.
The sample rate is set by the application using the
interface (PulseAudio, JACK, PipeWire, etc.).

### Input Controls

The Vocaster One has a single microphone input (Host),
while the Vocaster Two has two (Host and Guest). Both have
additional inputs for auxiliary sources.

#### Gain

Adjusts the input gain. Click and drag up/down, use your
mouse scroll wheel, or click to select and use arrow keys.
The gain dial includes a post-gain level meter with peak
hold display.

#### Autogain

When enabled, the interface listens to the input signal
and automatically adjusts the gain to achieve optimal
levels. The target level can be configured in the
[Configuration window](configuration.md).

#### Phantom Power (48V)

Sends phantom power to the XLR microphone input. Required
for condenser microphones.

#### Mute

Mutes the microphone input. The Vocaster Two has a
physical mute button for each channel.

### Output Controls

#### Speaker/Headphones

Volume and mute controls for the speaker and headphone
outputs. The speaker mute uses a speaker icon toggle, and
each headphone output has a headphone icon toggle.

#### Bluetooth (Vocaster Two)

The Vocaster Two has Bluetooth connectivity for connecting
mobile devices. This appears as an additional input and
output in the routing.

## DSP Processing

The Vocaster interfaces include per-channel DSP with:

- Pre-compressor filter (high-pass to remove rumble)
- Compressor (dynamic range control)
- Parametric EQ (tone shaping)

Open the DSP window with View → DSP or press Ctrl-D. See
the [DSP documentation](dsp.md) for full details.

The Vocaster Two has two independent DSP channels (Host and
Guest), allowing different processing for each microphone.

## Routing

Open the routing window with View → Routing or press
Ctrl-R.

The Vocaster routing is designed around podcast/streaming
workflows:

### Sources (Inputs)

| Source | Description |
|--------|-------------|
| Host/Guest | Microphone inputs (via DSP) |
| Aux | Auxiliary line input |
| Bluetooth | Mobile device audio (Vocaster Two) |
| Video Call | Audio from video call software |
| Playback | General audio playback from computer |

### Sinks (Outputs)

| Sink | Description |
|------|-------------|
| Spkr/Headphones | Main monitoring output |
| Aux | Auxiliary output |
| Bluetooth | Send audio to mobile device (Two) |
| Video Call | Send to video call software |
| Show Mix | Complete podcast mix |
| Loopback | Record audio playing on computer |

### Mixer Outputs

The mixer provides pre-configured outputs for common
podcast scenarios:

- **Show Mix Pre** — Mix before DSP processing
- **Show Mix Post** — Mix after DSP processing
- **Video Call** — Mix for video call participants
- **Aux** — Auxiliary monitor mix

## Mixer

Open the mixer window with View → Mixer or press Ctrl-M.

The mixer allows you to create custom mixes by adjusting
the level of each source going to each output. This is
useful for:

- Creating a separate headphone mix for guests
- Setting up mix-minus for video calls (so callers don't
  hear themselves)
- Balancing playback audio against microphone levels

Mixer input and output labels show real-time signal level
glow (green → yellow → red).

## Configuration

Open the Configuration window with View → Configuration or
press Ctrl-G. See [Configuration](configuration.md) for
details on:

- Setting a custom device name
- Renaming inputs and outputs
- Configuring Autogain target levels
- Showing/hiding unused ports
- Stereo linking

## Levels

The Levels window shows real-time audio levels at every
routing point. Open with View → Levels or press Ctrl-L.

## Presets

Save and recall your complete interface configuration using
the Presets button or File menu. See
[Presets](presets.md) for details.

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl-O | Load Configuration |
| Ctrl-S | Save Configuration |
| Ctrl-I | Interface Simulation |
| Ctrl-Q | Exit |
| Ctrl-R | Routing Window |
| Ctrl-M | Mixer Window |
| Ctrl-L | Levels Window |
| Ctrl-D | DSP Window |
| Ctrl-G | Configuration Window |
| Ctrl-T | Startup Window |
| Ctrl-H | Supported Hardware |
| Ctrl-/ | About |

Keyboard shortcuts work from any window — subwindows
forward unhandled shortcuts to the main window.
