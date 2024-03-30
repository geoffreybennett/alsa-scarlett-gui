# ALSA Scarlett2 Control Panel Installation

## Prerequisites

### Linux Kernel

You need to be running a Linux Kernel that has the ALSA Scarlett2
Protocol Driver. Use `uname -r` to check what kernel version you are
running.

- For reasonable functionality of Scarlett 2nd and 3rd Gen and Clarett
  interfaces, you need at least Linux kernel version 6.7
- For Scarlett 4th Gen support and firmware updates from Linux, you
  need at least 6.8
- For Vocaster support, you’ll need to build an updated
  `snd-usb-audio` driver (or wait for 6.10)

If you’ve got a Vocaster, or if your distribution doesn’t include a
recent-enough kernel for your interface, you can get the latest driver
from here and build it for your current kernel:

https://github.com/geoffreybennett/scarlett-gen2/releases

#### Enabling the Driver

As of Linux 6.7 the driver is enabled by default. Check the driver
status (after plugging your interface in) with this command:

```
dmesg | grep -i -A 5 -B 5 focusrite
```

If all is good you’ll see messages like this:

```
New USB device found, idVendor=1235, idProduct=8215, bcdDevice= 6.0b
Product: Scarlett 18i20 USB
Focusrite Scarlett Gen 3 Mixer Driver enabled (pid=0x8215); report
any issues to https://github.com/geoffreybennett/scarlett-gen2/issues
```

If you don’t see the “Mixer Driver” message or if it shows “disabled”
then check the [OLDKERNEL.md](OLDKERNEL.md) instructions.

### Gtk4

You need a Linux distribution with Gtk4 development libraries. If it
doesn’t have them natively, try the Flatpak instructions below.

### Firmware

As of Linux 6.8, firmware updates of all the supported interfaces can
be done through Linux. This is mandatory for Scarlett 4th Gen and
Vocaster interfaces (unless you’ve already updated it using the
manufacturer’s software), and optional for Scarlett 2nd and 3rd Gen,
Clarett USB, and Clarett+ interfaces.

Download the firmware from
https://github.com/geoffreybennett/scarlett2-firmware and place it in
`/usr/lib/firmware/scarlett2` or use the RPM/deb package.

## Building and Running

On Fedora, these packages need to be installed:

```
sudo dnf -y install alsa-lib-devel gtk4-devel openssl-devel
```

On OpenSUSE:

```
sudo zypper in git alsa-devel gtk4-devel libopenssl-devel
```

On Ubuntu:

```
sudo apt -y install git make gcc libgtk-4-dev libasound2-dev libssl-dev
```

On Arch:

```
sudo pacman -S gtk4
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

Continue on to reading [USAGE.md](USAGE.md) for how to use the GUI.

## Flatpak

With Flatpak, in any distro:

```
flatpak-builder --user --install --force-clean flatpak-build \
    vu.b4.alsa-scarlett-gui.yml
```

Be sure to use `flatpak-build` as the directory where the flatpak is
built or hence you risk bundling the artifacts when committing!

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
