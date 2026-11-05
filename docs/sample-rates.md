# Higher Sample Rates with PipeWire

If PipeWire won't run your interface above 48 kHz, first check that
it's allowed to use the higher rates. If it is, then it's likely
because your interface has fewer channels at higher sample rates.
PipeWire's Pro Audio profile opens the interface with all its
channels, and if the interface supports that many only at 44.1/48
kHz, PipeWire can stay at 48 kHz even when a higher rate is allowed
or forced.

## Allowing the higher rates

By default, PipeWire uses only 48 kHz. Create
`~/.config/pipewire/pipewire.conf.d/90-rates.conf` listing every rate
you want it to be able to use:

```
context.properties = {
  default.clock.allowed-rates = [ 44100 48000 88200 96000 ]
}
```

Then restart PipeWire and WirePlumber:

```
systemctl --user restart pipewire pipewire-pulse wireplumber
```

## Checking the channel counts

Find your interface's card number with:

```
cat /proc/asound/cards
```

For example, in this line the card number is 3:

```
 3 [C8X            ]: USB-Audio - ISA C8X
```

`/proc/asound/card3/stream0` then lists the formats the interface
supports, under Playback for its outputs and under Capture for its
inputs. Each altset has a channel count and the rates it supports
with that many channels:

```
Playback:
  ...
  Interface 1
    Altset 2
    Format: S32_LE
    Channels: 18
    ...
    Rates: 88200, 96000
```

Across its altsets, the ISA C8X lists 28 outputs and 26 inputs at
44.1/48 kHz, 18 of each at 88.2/96 kHz, and 10 of each at
176.4/192 kHz.

## Opening the interface with fewer channels

PipeWire sets up the Pro Audio profile by opening the interface at
the rate in `api.acp.probe-rate` (48 kHz by default) with the number
of channels in `api.acp.pro-channels` (all of them by default).

Pick the highest rate you want to use, and find the Playback altset
with the most channels whose Rates include it. Set
`api.acp.probe-rate` to that rate and `api.acp.pro-channels` to that
channel count.

Find your interface's device name with:

```
pactl list short cards
```

Create `~/.config/wireplumber/wireplumber.conf.d/90-channels.conf`
with the device name, rate, and channel count. For the ISA C8X at up
to 96 kHz:

```
monitor.alsa.rules = [
  {
    matches = [ { device.name = "~alsa_card.usb-Focusrite_ISA_C8X_.*" } ]
    actions = {
      update-props = {
        api.acp.probe-rate = 96000
        api.acp.pro-channels = 18
      }
    }
  }
]
```

Then restart PipeWire and WirePlumber as above, and select the Pro
Audio profile.

The interface then runs only at the rates its altset lists, and the
remaining channels are unavailable. To get all the channels back,
delete `90-channels.conf` and restart them again.
