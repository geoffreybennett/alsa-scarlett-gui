# Release Notes

## 1.0

This release of alsa-scarlett-gui introduces substantial new features
across over 110 commits.

**For all supported devices** (except 2nd/3rd Gen Solo/2i2): a
configuration window with custom I/O naming and visibility settings,
device presets, stereo linking throughout the routing, mixer, and
configuration windows, and numerous visual and usability improvements.

**For Vocaster**: a full DSP window with parametric EQ and compressor
controls, with stereo linking support.

**For big 4th Gen interfaces** (16i16, 18i16, 18i20): monitor groups
and firmware update support.

## DSP Window

A new DSP window (View → DSP, or Ctrl-D) provides control over the
digital signal processing available on Vocaster interfaces. Each DSP
channel has three processing sections applied in order: pre-compressor
filter, compressor, and parametric EQ.

The Vocaster One has a single DSP channel; the Vocaster Two has two
channels. Channel names default to "Host" and "Guest" but can be
customised in the Configuration window — the DSP window headers update
to reflect custom names.

### Parametric EQ with Interactive Graph

Each filter section (pre-compressor and parametric EQ) includes an
interactive frequency response graph:

- **Draggable handles** — click and drag the numbered handles directly
  on the graph to adjust frequency and gain (or frequency and Q for
  filter types without gain)
- **Mouse wheel Q adjustment** — scroll over a handle to adjust Q
  logarithmically (scroll up for narrower, down for wider)
- **Hover highlighting** — hovering over a handle highlights the
  corresponding filter controls, and vice versa
- **Shaded curves** — each filter stage is drawn as a coloured curve
  with translucent shading between the curve and the 0 dB line
- **Text entry fields** — type exact values for frequency, Q, and gain
- **Combined response** — a white line shows the combined effect of
  all stages in the section
- **Display range toggle** — click the dB label to switch between ±12
  dB and ±24 dB vertical range; auto-selects ±12 when all gains fit,
  giving finer resolution for typical adjustments
- **Grid and labels** — frequency labels from 20 Hz to 20 kHz, dB
  labels every 6 dB, with fine grid lines at all decade subdivisions

### Filter Types

A filter type dropdown with response-shape icons is available for each
filter stage. Twelve filter types are available:

- Peaking, Low Shelf, High Shelf
- Lowpass (12 dB/oct), Highpass (12 dB/oct)
- Bandpass, Notch
- Gain (simple level adjustment, no frequency shaping)
- LP/HP/LS/HS 6 dB/oct (first-order, gentler slope)

### Compressor Visualisation

The compressor section displays a transfer curve showing input vs
output level. A real-time level dot on the curve shows the current
signal position, turning red when the signal clips. The curve updates
in real time as you adjust threshold, ratio, knee width, and makeup
gain. Attack and release controls set the compressor's timing
behaviour.

### DSP Section Presets

Each processing section has a Presets button offering quick
configurations:

- **Pre-Compressor Filter**: None, Rumble Reduction Low, Rumble
  Reduction High
- **Compressor**: Off, Low, Med, High
- **Parametric EQ**: Radio, Clean, Warm, Bright

### DSP Enable and Visualisation

The channel name button (e.g. "Host") toggles all DSP processing for
that channel — when disabled, all curves are dimmed and dashed.
Individual filter stages can be enabled or disabled with their
checkboxes; bypassed stages are shown dimmed and dashed while the
combined response (white line) adjusts to reflect only the active
stages.

### DSP Channel Linking

On the Vocaster Two, a link button between the two DSP channels lets
you apply identical processing to both. When linked, channel 2
controls are hidden and all parameters sync bidirectionally — changes
to channel 1 are mirrored to channel 2. The header shows the pair name
when linked. Link state persists across sessions.

### Parameter Persistence

Filter parameters (type, frequency, Q, gain, and enable state) are
persisted across application restarts. When a filter stage is
disabled, the hardware discards its parameters — the application saves
and restores them so your settings are preserved.

### External Coefficient Updates

If biquad coefficients are changed externally (e.g. by another
application), the DSP window analyses the new coefficients to
determine the filter type and parameters, and updates the controls and
graph accordingly.

## Configuration Window

A new Configuration window (View → Configuration, or Ctrl-G) provides
device customisation organised into tabs. The available tabs depend on
your interface's capabilities. The window is resizable, and your
selected tab is remembered between sessions.

### Device Name

Set a custom name for your interface. The name appears in the window
title bar, making it easy to identify multiple interfaces of the same
model.

### Device Settings

On Clarett interfaces, the Device Settings tab provides an S/PDIF
Source control for selecting the digital input source (None, Optical,
or RCA). This setting takes effect immediately without requiring a
reboot.

### I/O Configuration

The I/O Configuration tab lets you customise port names, visibility,
and stereo linking across all port types. It is organised into
sub-tabs by port type (Analogue, S/PDIF, ADAT, PCM, DSP, Mixer), with
your selected sub-tab also remembered between sessions.

#### Custom Port Names

Every input and output port can be given a custom name. Generic labels
(e.g. "Analogue 1") are shown alongside the port, with device-specific
default names (e.g. "Mic", "Headphone") shown as placeholder text in
the entry field. Custom names propagate throughout the application —
routing labels, mixer headers, and DSP window titles all update to
reflect your names.

#### Show/Hide Ports

A checkbox next to each port controls its visibility. Hiding ports
that you don't use declutters the routing and mixer windows. Each port
type sub-tab has an "all" checkbox in its tab label that toggles
visibility for the entire category at once — it shows a mixed state
when some ports are visible and others hidden.

On the big 4th Gen interfaces (16i16, 18i16, 18i20) which have fixed
mixer inputs (permanently connected to specific sources), the Mixer
sub-tab splits inputs into columns by port type for easier navigation.

#### Stereo Linking

A link button (chain icon) between adjacent port pairs lets you pair
them as stereo. When linked:

- The two individual channels collapse into a single stereo row
- A pair name field replaces the individual name entries, with
  device-specific default pair names (e.g. "Stereo Mic" for DSP 1–2)
  shown as placeholder text
- Enable state is synchronised between the paired channels
- The routing window shows a single stereo connection
- Mixer controls display averaged gain for the pair

For fixed mixer inputs, the link state is read-only and follows the
source channel's link state.

Linking and unlinking updates all windows immediately. The left
channel's settings take priority (routing source, enable state,
monitor group assignments); mixer gains are averaged. Any routing that
doesn't follow a valid L→L/R→R pattern is cleared to Off. Link state
propagates: linking one end of a stereo connection automatically links
the other end, and unlinking propagates similarly.

On first use with a device, the application analyses existing routing
and mixer connections to determine which channels appear to be used as
stereo pairs, and initialises link state accordingly.

### Autogain

The Autogain tab provides gain faders for configuring the target
levels that the Autogain feature aims for. The 4th Gen 2i2/4i4 has Mean
and Peak targets; the Vocaster has a Hot target.

### Monitor Groups

On 4th Gen 16i16/18i16/18i20 interfaces, the Monitor Groups tab lets you
organise analogue outputs into Main and Alt groups. This replaces the
limited main/alt speaker switching of earlier big interfaces (which
could only swap between line outputs 1–2 and 3–4) with fully flexible
output grouping, and also replaces the old HW/SW volume control
switch.

Each output has:

- **Enable** checkbox — include this output in the group
- **Source** dropdown — select which input feeds this output when the
  group is active; shows custom port names and is stereo-aware
  (stereo-linked outputs only show stereo sources; mono outputs hide
  stereo sources)
- **Trim** fader — per-output level calibration

Switch between Main and Alt groups using the Alt button on your
interface or the toggle in the main window. The main volume knob
controls the outputs in the active group. Outputs that belong only to
the inactive group are muted — outputs present in both groups are
never muted, they follow whichever group is currently active.

This enables use cases like:

- **A/B speaker comparison** — assign two sets of speakers to Main and
  Alt, switch between them with the Alt button; use trim to match
  their volumes
- **Surround monitoring** — put all surround speakers in the Main
  group for unified volume control from the knob; use trim to
  calibrate individual speaker levels
- **Surround/stereo switching** — surround speakers in Main, stereo
  pair in Alt, for quick format comparison
- **Stereo/mono comparison** — stereo mix in Main, mono mixdown in
  Alt, for checking mono compatibility
- **DAW/direct monitoring switch** — Main from PCM (hearing yourself
  through the DAW with processing), Alt from a mixer output that
  combines PCM playback with hardware inputs (zero-latency direct
  monitoring while still hearing your backing tracks)

When outputs are stereo-linked, the Monitor Groups tab shows a single
row for the pair with a stereo trim widget. Enable and source settings
apply to both channels; selecting a stereo source routes L to L and R
to R.

## Routing Window

The routing window (View → Routing, or Ctrl-R) has received
substantial visual and functional improvements.

### Signal Level Glow

Routing lines now display a real-time glow effect that reflects the
audio signal level passing through the connection. The glow colour
transitions from green (low level) through yellow to red (near
clipping), with intensity proportional to the signal level. This makes
it easy to see at a glance which connections are carrying audio and at
what level.

On big 4th Gen interfaces, sources that have signal but are not routed
to any output display a circular glow around their socket. (Other
interfaces only report levels at sink nodes, so unconnected sources
have no level data.)

### Arrow Indicators for Hidden Ports

When a routing connection has one visible and one hidden port, an
arrow indicator appears at the visible end, showing that audio is
flowing to or from a port that is not currently displayed.

### Stereo Linking

Adjacent channels that are stereo-linked in the Configuration window
are displayed as a single stereo port in the routing window, using a
dedicated stereo socket icon that shows the L and R sub-ports.

Routing lines for stereo pairs are drawn as two parallel lines (L→L
and R→R), each with its own independent glow level. When dragging a
connection from a stereo port, two dashed lines follow the cursor.

### Monitor Group Indicators (Big 4th Gen)

On 4th Gen 16i16/18i16/18i20 interfaces with monitor groups configured,
the routing window shows the effective audio routing rather than just
the raw ALSA control values. When speaker switching is active:

- Outputs in the active group show their monitor group source as the
  effective connection, with a green "Main" or red "Alt" label beside
  the output name
- Outputs in the inactive group are shown with strikethrough text and
  no routing line (they are muted)
- Outputs not assigned to either group show their normal individual
  routing

This means the routing display always reflects what you actually hear,
regardless of whether the routing comes from individual settings or
the monitor group override.

## Mixer Window

The mixer window (View → Mixer, or Ctrl-M) has received several visual
and functional improvements.

### Signal Level Glow on Labels

Mixer input and output labels now display a real-time horizontal glow
bar reflecting the signal level at that port. The glow colour
transitions from green through yellow to red as the signal approaches
0 dBFS, matching the routing window's glow effect.

When channels are stereo-linked, each label shows split L/R glow bars
growing outward from the centre — giving independent visual feedback
of both channel levels.

### Orientation Legend

A corner label in the top-left of the mixer grid displays "Inputs →"
and "Outputs ↓", making it clear which axis is which.

### Level Metering on Gain Dials

Gain knobs throughout the application now include a signal level meter
inside the dial, with peak hold (except 1st Gen, where the kernel
driver doesn't support level metering). Main window input/output knobs
show the signal level at that point in the chain; mixer knobs show the
post-gain level (input signal plus applied gain). This gives immediate
visual feedback without needing to open the Levels window.

### Stereo-Aware Controls

When channels are stereo-linked (via the Configuration window), the
mixer adapts:

- Linked inputs or outputs appear as a single stereo fader rather than
  two separate mono controls
- Stereo-to-stereo crosspoints show a single gain control for the
  L→L and R→R path (averaged), with L→R and R→L crosstalk muted to
  maintain stereo separation
- Level meters show the maximum level across both stereo channels
- Labels update to show the stereo pair name

### Custom Port Names

Mixer output labels (Mix A, Mix B, etc.) and input labels reflect any
custom names set in the Configuration window. Names are ellipsised if
they exceed the available space, with the full name shown as a
tooltip. Hover over a gain knob to highlight its input and output
labels at the edge.

### Dynamic Port Visibility

Ports hidden via the Configuration window's show/hide checkboxes are
removed from the mixer grid. The layout automatically adjusts so only
enabled ports appear, keeping the mixer as compact as possible and
relevant to your workflow.

### Mixer Unavailability at High Sample Rates

At quad-band sample rates (176.4 or 192 kHz), the hardware mixer is
not available. The mixer window indicates this by dimming all controls
and displaying an overlay message. Controls remain functional for
pre-configuration — adjustments take effect when you return to a
supported sample rate.

## Sample Rate and Digital I/O Availability

Some ports become unavailable due to hardware bandwidth limits.
Unavailable ports are shown with grey strikethrough text and a tooltip
explaining the limitation.

- **PCM channels** — the number of available channels decreases at
  higher sample rates
- **Digital I/O** (S/PDIF, ADAT) — available port counts depend on the
  sample rate and the Digital I/O mode or S/PDIF Source setting
- **Mixer** — at quad-band rates (176.4/192 kHz), the mixer is
  entirely unavailable; mixer labels show in strikethrough, and the
  mixer window dims with an overlay message

PCM and mixer availability updates in real time as the sample rate
changes. Digital I/O availability behaviour differs by interface:

- **Clarett** (4Pre, 8Pre) — the S/PDIF Source setting (None, Optical,
  RCA) takes effect immediately; availability indicators update in
  real time. Selecting "Optical" makes ADAT inputs unavailable since
  the optical port is shared.
- **Scarlett** (3rd/4th Gen) — the Digital I/O Mode requires a device
  reboot to take effect. The GUI shows availability based on the mode
  that was active at startup.

## Presets

A Presets button in the main window provides quick save and load of
your device configuration. Presets are stored per device (identified
by serial number) in `~/.config/alsa-scarlett-gui/`.

### Saving Presets

Click the Presets button and select "Save as Preset..." to save the
current device state. Enter a name in the dialog and press Save (or
Enter). The preset captures all device settings: routing, mixer
levels, custom port names, visibility, stereo linking, monitor groups,
and DSP parameters.

### Loading Presets

Click the Presets button to see a list of saved presets for the
current device. Click a preset name to load it immediately — all
windows update to reflect the restored configuration.

### Deleting Presets

Each preset in the list has a delete button (×) on the right. Click it
to remove the preset file.

## Configuration Save and Load

The File menu provides Load Configuration (Ctrl-O) and Save
Configuration (Ctrl-S) for saving and loading device settings to
arbitrary files. Two formats are supported:

- **Native format (.conf)** — saves all device settings including
  custom names, port visibility, stereo linking, and DSP parameters.
  This is the default format and the same format used by presets.
- **alsactl state format (.state)** — saves ALSA control values in the
  format used by `alsactl`. This format is also used for interface
  simulation (File → Interface Simulation, Ctrl-I).

The file chooser dialog shows both formats with the native format
selected by default. When loading, the application detects the format
from the file extension.

## Firmware Update (Big 4th Gen)

Firmware update support has been expanded with the multi-step upgrade
capability required for big 4th Gen interfaces (16i16/18i16/18i20):

1. **Leapfrog** — a special firmware that can perform ESP updates is
   uploaded, then the device is rebooted into it
2. **ESP** — after reboot, the ESP firmware is updated (no reboot
   needed between this and the next step)
3. **Application** — the main application firmware is uploaded,
   followed by a second reboot to load it

The upgrade prompt appears automatically when a newer firmware is
available. If the process is interrupted after the leapfrog stage
(e.g. by the device rebooting), the application detects the
mid-upgrade state and resumes automatically when the device reappears.

Progress is displayed in a modal dialog throughout each stage.

## Window State Persistence

Window visibility is remembered across sessions — windows that were
open when you quit are automatically restored on startup.

## Keyboard Shortcuts

All keyboard shortcuts now work from any window.

### File

| Shortcut | Action |
|----------|--------|
| Ctrl-O   | Load Configuration |
| Ctrl-S   | Save Configuration |
| Ctrl-I   | Interface Simulation |
| Ctrl-Q   | Exit |

### View

| Shortcut | Action |
|----------|--------|
| Ctrl-R   | Routing Window |
| Ctrl-M   | Mixer Window |
| Ctrl-L   | Levels Window |
| Ctrl-D   | DSP Window |
| Ctrl-G   | Configuration Window |
| Ctrl-T   | Startup Window |

### Help

| Shortcut | Action |
|----------|--------|
| Ctrl-H   | Supported Hardware |
| Ctrl-/   | About |

Pressing Escape in any subwindow closes it.

## Device Support

### Scarlett Big 4th Gen (16i16, 18i16, 18i20)

Full support for big 4th Gen interfaces has been added, including
hardware identification, monitor groups, and output volume/mute/dim
controls. These devices require Linux 6.14+, the `fcp-server` daemon
from [fcp-support](https://github.com/geoffreybennett/fcp-support),
and the firmware from
[scarlett4-firmware](https://github.com/geoffreybennett/scarlett4-firmware).
See the Firmware Update section for details on the multi-step upgrade
process.

### Vocaster Speaker and Headphone Mute

Vocaster interfaces now have dedicated mute controls for speaker and
headphone outputs in the main window:

- **Speaker mute** — a speaker icon button that toggles muting of the
  speaker output
- **Headphone mute** — a headphone icon button for each headphone
  output (Vocaster Two has multiple headphone outputs)

### Device-Specific Default Port Names

The Configuration window's name entry fields show device-specific
default port names as placeholder text (e.g. "Mic", "Inst",
"Headphone", "Monitor"), replacing the generic "Analogue 1", "PCM 1"
labels.

## Local Configuration Storage

Custom port names, port visibility, stereo linking, and DSP filter
parameters are all stored locally per device (identified by serial
number) in `~/.config/alsa-scarlett-gui/`. These settings persist
across application restarts and are separate from the device's own
firmware state.

Resetting the device configuration (via the startup screen's Reset
button) also removes this local configuration file, so custom names,
visibility, and stereo link settings are reset along with the device.

## Bug Fixes

Numerous bug fixes and stability improvements, including fixes to the
4th Gen master volume display, drop-down and popover styling, modal
window layering, gain dial redraw, and level meter display for devices
with implicit level maps.

## Known Issues

- The scarlett2 kernel driver reports incorrect level meter values at
  dual-band and quad-band sample rates.
