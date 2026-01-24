# ALSA Scarlett Control Panel

## Scarlett Big 4th Gen Interfaces

This document describes how to use the ALSA Scarlett Control Panel
with the big Scarlett 4th Gen interfaces:

- Scarlett 4th Gen 16i16, 18i16, 18i20

### FCP Driver

The big 4th Gen interfaces are supported by the “FCP” (Focusrite
Control Protocol) driver introduced in Linux 6.14. You must be
running at least Linux 6.14 (check with `uname -a`) and install
[fcp-support](https://github.com/geoffreybennett/fcp-support) before
you can use alsa-scarlett-gui with these interfaces.

Firmware update files are available at
[scarlett4-firmware](https://github.com/geoffreybennett/scarlett4-firmware).
When the GUI detects that a newer firmware is available, it will
prompt you to update. The update process takes about two minutes and
is multi-step (leapfrog, ESP, then application firmware), involving
a device reboot mid-upgrade — the GUI handles this automatically
and will resume where it left off when the device reappears.

## Main Window

The main window is divided into three sections:
- Global Controls
- Analogue Input Controls
- Analogue Output Controls

The main window for the 16i16 interface is shown below. The 18i16 and
18i20 interfaces are similar, but with more controls.

![Main Window](../img/iface-4th-gen-big.png)

### Global Controls

#### Clock Source (interfaces with S/PDIF or ADAT inputs)

Clock Source selects where the interface receives its digital clock
from. If you aren’t using S/PDIF or ADAT inputs, set this to Internal.

#### Sync Status

Sync Status indicates if the interface is locked to a valid digital
clock. If you aren’t using S/PDIF or ADAT inputs and the Sync Status
is Unlocked, change the Clock Source to Internal.

#### Sample Rate

Sample Rate is informative only, and displays the current sample rate
if the interface is currently in use. In ALSA, the sample rate is set
by the application using the interface, which is usually a sound
server such as PulseAudio, JACK, or PipeWire.

#### Speaker Switching / Monitor Group

All three big 4th Gen interfaces support monitor groups, allowing you
to assign outputs to Main and Alt groups with independent sources and
trim. Configure which outputs belong to each group in the
[Configuration window](configuration.md).

The presentation varies by model:
- **18i16**: A “Speaker Switching” control with On/Off and Main/Alt
  buttons (On enables grouping, Main/Alt switches between groups)
- **16i16, 18i20**: A “Monitor Group” button that toggles between Main
  and Alt (only enabled when at least one output is assigned to the
  Alt group)

Outputs in the inactive group are muted.

#### Talkback (18i20 only)

The 18i20 has a Talkback feature that routes a designated input to
selected mixer outputs, typically used for communication from a
control room to performers in the live room.

### Analogue Input Controls

#### Input Select

The “Input Select” control allows you to choose which channel the
hardware 48V, Inst, Air, Auto, and Safe buttons control.

#### Link

The “Link” control links the 48V, Inst, Air, Auto, and Safe controls
together so that they control a stereo pair of channels
simultaneously.

#### Gain

The “Gain” controls adjust the input gain for the selected channel.
Click and drag up/down on the control to adjust the gain, use your
mouse scroll wheel, or click the control to select it and use the
arrow keys, Page Up, Page Down, Home, and End keys.

The gain dial includes a built-in level meter that shows the real-time
signal level with a peak hold indicator. This gives immediate visual
feedback on the input signal without needing to open the Levels
window.

#### Autogain

When the “Autogain” control is enabled, the interface will listen to
the input signal for ten seconds and automatically adjust the gain to
get the best signal level. When autogain is not running, the
most-recent autogain exit status is shown below the “Autogain”
control.

#### Safe

”Safe” mode is a feature that automatically reduces the gain if the
signal is too loud. This can be useful to prevent clipping.

#### Instrument

The Inst button(s) are used to select between Mic/Line and Instrument
level/impedance. When plugging in microphones or line-level equipment
(such as a synthesiser, external preamp, or effects processor) to the
input, set it to “Line”. The “Inst” setting is for instruments with
pickups such as guitars.

#### Air

The Scarlett 3rd Gen introduced Air mode which transformed your
recordings and inspired you while making music by boosting the
signal’s high-end. The 4th Gen interfaces now call that “Air Presence”
and add a new mode “Air Presence+Drive” which boosts mid-range
harmonics in your sound.

#### Phantom Power (48V)

Turning the “48V” switch on sends “Phantom Power” to the XLR
microphone input. This is required for some microphones (such as
condenser microphones), and damaging to some microphones (particularly
vintage ribbon microphones).

### Analogue Output Controls

Each analogue output has its own volume fader with a level meter
showing the output signal level and peak hold. These are
software-controlled volume knobs.

When an output is assigned to a monitor group (Main or Alt), its
individual volume control is disabled — the output level is managed by
the group’s source and trim settings configured in the [Configuration
window](configuration.md).

A coloured “Main” or “Alt” label appears below each output number when
monitor groups are configured:
- **Green “Main”** — output is in the Main group, which is currently
  active
- **Red “Alt”** — output is in the Alt group, which is currently
  active
- **Grey** — output is in a group that is currently inactive (its
  audio is muted)

#### Master Volume

The Master volume knob shows the position of the hardware volume knob
on the front panel, which controls Analogue Outputs 1 and 2.

#### Mute and Dim

The speaker icon button mutes the hardware-controlled outputs
(Analogue Outputs 1 and 2). The Dim button reduces their volume. These
correspond to the front-panel buttons on the interface.

## Routing

The routing window provides (almost) complete control of signal
routing between the hardware inputs/outputs, internal mixer, and PCM
(USB) inputs/outputs.

The mixer inputs on the big 4th Gen interfaces are fixed and not shown
in the routing window, as there are too many to sensibly display.

Open the Routing window with View → Routing or press Ctrl-R.

![4th Gen 16i16 Routing](../img/scarlett-4th-gen-16i16-routing.png)

To adjust the routing:

- Click and drag from a source to a sink or a sink to a source to
  connect them. Audio from the source will then be sent to that sink.

- Click on a source or a sink to clear the links connected to that
  source/sink.

Note that a sink can only be connected to one source, but one source
can be connected to many sinks. If you want a sink to receive input
from more than one source, connect the sinks to mixer outputs:

- Connect mixer outputs to the sinks that you want to receive the
  mixed audio
- Use the Mixer window to set the amount of each mixer input that is
  sent to each mixer output

The Presets menu can be used to clear all connections, or to set up
common configurations:

- The “Direct” preset connects all Hardware Inputs to PCM Inputs and
  all PCM Outputs to Hardware Outputs.
- The “Preamp” preset connects all Hardware Inputs to Hardware
  Outputs.
- The “Stereo Out” preset connects PCM 1 and 2 Outputs to pairs of
  Hardware Outputs.

### Signal Level Glow

Active routing connections are overlaid with a glow effect showing the
real-time signal level flowing through them. The glow colour
transitions from green (normal) through yellow (high) to red
(clipping). Sources with an active signal but no connected sink show a
circular glow around the source port.

### Arrow Indicators

When a port has been hidden via the [Configuration
window](configuration.md), any routing connections involving that port
are shown with an arrow indicator at the visible end. This lets you
see that a hidden port has an active connection without needing to
show all ports.

### Stereo Linking

Adjacent ports can be stereo-linked via the [Configuration
window](configuration.md). Linked ports appear as a single stereo
socket with L and R sub-ports. Routing lines for stereo pairs are
drawn in parallel, each with independent signal glow.

When dragging a stereo-linked source to a sink (or vice versa), the
connection is made stereo-aware: the left source connects to the left
sink and the right source to the right sink.

### Monitor Group Indicators

When monitor groups are configured, the routing window shows the
*effective* audio routing — not just the routing control setting, but
the actual audio path considering which group is active.

Hardware output labels display coloured indicators:
- **Green “Main”** when the output is in the active Main group
- **Red “Alt”** when the output is in the active Alt group
- **Strikethrough** when the output is in the inactive group (audio is
  muted to that output)

The routing lines update in real time as you switch between Main and
Alt groups, reflecting the actual audio flow.

### I/O Availability

Some ports may become unavailable depending on the current sample rate
or Digital I/O mode:
- **PCM channels** reduce at higher sample rates
- **S/PDIF and ADAT ports** depend on the Digital I/O mode and sample
  rate
- **Mixer ports** are unavailable at 176.4/192 kHz (quad-band)

Unavailable ports are shown with strikethrough text and a tooltip
explaining why.

## Mixer

Open the Mixer window with View → Mixer or press Ctrl-M.

The mixer is a matrix where any combination of inputs can be mixed to
any output at adjustable levels. Mixer levels can be adjusted with
your keyboard or mouse in the same way as the [Gain Controls](#gain).

### Signal Level Glow

Mixer input and output labels display a real-time glow effect behind
them, showing the signal level with green, yellow, and red colour
transitions. For stereo-linked channels, the glow is split into
separate L and R bars.

### Level Metering on Gain Dials

Each mixer gain knob includes a post-gain level meter inside the dial,
showing the signal level at that point in the mix with a peak hold
indicator.

### Custom Port Names

Mixer labels reflect any custom names set in the [Configuration
window](configuration.md). Long names are ellipsised with the full
name shown in a tooltip on hover.

### Stereo-Aware Controls

When adjacent mixer inputs or outputs are stereo-linked, their gain
controls are ganged together with an averaged value. Adjusting one
updates both channels. For a stereo-linked input going to a
stereo-linked output, the gain is computed as a diagonal average
across the four crosspoints.

### Port Visibility

Ports hidden in the [Configuration window](configuration.md) are
removed from the mixer grid, keeping the display focused on the ports
you’re actively using.

### Mixer Unavailability

At quad-band sample rates (176.4/192 kHz), the hardware mixer is
disabled. The mixer window displays a message indicating the mixer is
unavailable at the current sample rate.

## Configuration

The Configuration window provides settings for customising port names,
visibility, stereo linking, autogain targets, and monitor groups. Open
it with View → Configuration or press Ctrl-G. See [Configuration
Window](configuration.md) for full details.

Available tabs for the big 4th Gen interfaces:

| Tab | Description |
|-----|-------------|
| Device Name | Set a custom name for the interface |
| I/O Configuration | Port names, visibility, stereo linking |
| Autogain | Target levels for autogain calibration |
| Monitor Groups | Main/Alt group output assignment |

### Custom Port Names

Give meaningful names to your ports (e.g. “Vocal Mic”, “Guitar”,
”Monitors”) in the I/O Configuration tab. These names appear
throughout the application — in the routing window, mixer labels,
level meters, and the main window’s monitor group labels.

### Port Visibility

Show or hide individual ports to reduce clutter in the routing and
mixer windows. Hidden ports with active connections show arrow
indicators in the routing window.

### Stereo Linking

Link adjacent ports together for stereo operation. Linked ports share
names, appear as stereo sockets in the routing window, and have ganged
controls in the mixer.

### Autogain Targets

Set the target levels for the autogain feature: Mean Target and Peak
Target. These determine what signal level the autogain algorithm aims
for when calibrating input gain.

### Monitor Groups

The Monitor Groups tab lets you assign each analogue output to the
Main group, the Alt group, or neither. For each output in a group, you
can set:
- **Source** — which audio source feeds the output when the group is
  active
- **Trim** — a volume offset relative to the group’s master volume

When outputs are stereo-linked, they appear as a single pair with a
stereo-aware source selector and averaged trim.

## Presets

The Presets button in the main window provides quick save/load of
named configurations. See [Presets and Configuration
Files](presets.md) for full details.

You can also save and load configurations to files via the File menu
(Ctrl-S to save, Ctrl-O to load).

## Levels

The meters show the levels seen by the interface at every routing
source as well as the analogue outputs. Open this window by selecting
View → Levels or pressing Ctrl-L.

![Levels](../img/window-levels-4th-gen-big.png)

Look at this in conjunction with the routing window to understand
which meter corresponds to which source or sink.

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
| Ctrl-G | Configuration Window |
| Ctrl-T | Startup Window |
| Ctrl-H | Supported Hardware |
| Ctrl-/ | About |

Keyboard shortcuts work from any window — subwindows forward unhandled
shortcuts to the main window.

---

Thanks for reading this far! This software represents over a thousand
hours of independent work — reverse-engineering, kernel development,
and building a complete replacement for Focusrite's proprietary apps.
If you've found it valuable, please consider a
[donation](../README.md#donations).
