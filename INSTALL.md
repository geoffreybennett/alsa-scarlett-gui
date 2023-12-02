# ALSA Scarlett2 Control Panel Installation

## Prerequisites

### Linux Kernel

You need to be running a Linux Kernel that has the ALSA Scarlett2
Protocol Driver. Use `uname -r` to check what kernel version you are
running.

- Use at least version 5.14 for Scarlett Gen 3 support and bug fixes
  for the Gen 2 support.
- For Clarett+ 8Pre support, you need 6.1.
- For the other Clarett USB and Clarett+ models, you need 6.7.
- For the level meters to work, you need 6.7.

If you don't have 6.7, you can get the driver from here and build it
for your current kernel:

https://github.com/geoffreybennett/scarlett-gen2/releases/tag/v6.5.11c1

### Gtk4

You need a Linux distribution with Gtk4 development libraries. If it
doesn't have them natively, try the Flatpak instructions below.

## Enabling the Driver

As of Linux 6.7 the driver is enabled by default and you can skip this
section.

If you're running a kernel before 6.7, the driver needs to be enabled
at module load time with the `device_setup=1` option to
insmod/modprobe. Create a file /etc/modprobe.d/scarlett.conf
containing the appropriate line for your device:

Scarlett Gen 2:

- 6i6: `options snd_usb_audio vid=0x1235 pid=0x8203 device_setup=1`
- 18i8: `options snd_usb_audio vid=0x1235 pid=0x8204 device_setup=1`
- 18i20: `options snd_usb_audio vid=0x1235 pid=0x8201 device_setup=1`

Scarlett Gen 3:

- Solo: `options snd_usb_audio vid=0x1235 pid=0x8211 device_setup=1`
- 2i2: `options snd_usb_audio vid=0x1235 pid=0x8210 device_setup=1`
- 4i4: `options snd_usb_audio vid=0x1235 pid=0x8212 device_setup=1`
- 8i6: `options snd_usb_audio vid=0x1235 pid=0x8213 device_setup=1`
- 18i8: `options snd_usb_audio vid=0x1235 pid=0x8214 device_setup=1`
- 18i20: `options snd_usb_audio vid=0x1235 pid=0x8215 device_setup=1`

Clarett+:

- 8Pre: `options snd_usb_audio vid=0x1235 pid=0x820c device_setup=1`

Or you can use a sledgehammer:
```
options snd_usb_audio device_setup=1,1,1,1
```
to pass that option to the first 4 USB audio devices.

To see if the driver is present and enabled: `dmesg | grep -i -A 5 -B
5 focusrite` should display information like:

```
New USB device found, idVendor=1235, idProduct=8215, bcdDevice= 6.0b
Product: Scarlett 18i20 USB
Focusrite Scarlett Gen 2/3 Mixer Driver enabled pid=0x8215
```

If the driver is disabled you’ll see a message like:

```
Focusrite Scarlett Gen 2/3 Mixer Driver disabled; use options
snd_usb_audio vid=0x1235 pid=0x8215 device_setup=1 to enable and
report any issues to g@b4.vu
```

## Building and Running

On Fedora, the packages `alsa-lib-devel` and `gtk4-devel` need to be
installed:

```
sudo dnf -y install alsa-lib-devel gtk4-devel
```

On OpenSUSE:

```
sudo zypper in git alsa-devel gtk4-devel
```

On Ubuntu 22.04:

```
sudo apt -y install git make gcc libgtk-4-dev libasound2-dev
```

To download from github:

```
git clone https://github.com/geoffreybennett/alsa-scarlett-gui
cd alsa-scarlett-gui
```

To build:

```
cd src
make -j4
```

To run:

```
./alsa-scarlett-gui
```

You can install it into `/usr/local` (binary, desktop file, and icon)
with:

```
sudo make install
```

And uninstall with:

```
sudo make uninstall
```

Continue on to reading [USAGE.md](USAGE.md) for usage information and
known issues.

## Flatpak

With Flatpak, in any distro:

```
flatpak-builder --user --install --force-clean flatpak-build \
    vu.b4.alsa-scarlett-gui.yml
```

Be sure to use `flatpak-build` as the directory where the flatpak is
built or hence you risk bundling the artifacts when comitting!

If you get messages like these:

```
Failed to init: Unable to find sdk org.gnome.Sdk version 45
Failed to init: Unable to find runtime org.gnome.Platform version 45
```

Then install them:

```
flatpak install org.gnome.Sdk
flatpak install org.gnome.Platform
```

If you get:

```
Looking for matches…
error: No remote refs found for ‘org.gnome.Sdk’
```

Then:

```
flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
```
