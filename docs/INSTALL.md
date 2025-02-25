# ALSA Scarlett Control Panel Installation

## Prerequisites

### Linux Kernel

You need to be running a Linux Kernel that contains the appropriate
driver for your interface. Use `uname -r` to check what kernel version
you are running.

Check the following table to see which driver your interface uses and
the first kernel version that the driver was included in:

| Series    | Models | Driver | Kernel Version |
|-----------|--------|--------|:----------------------:|
| Scarlett 1st Gen | Solo, 2i2, 2i4 | N/A* | Any |
| Scarlett 1st Gen | 6i6, 8i6, 18i6, 18i8, 18i20 | Scarlett 1st Gen Mixer Driver | 3.19+ |
| Scarlett 2nd Gen | Solo, 2i2, 2i4 | N/A* | Any |
| Scarlett 2nd Gen | 6i6, 18i8, 18i20 | Scarlett2 Mixer Driver | 6.7+ |
| Scarlett 3rd Gen | Solo, 2i2, 4i4, 8i6, 18i8, 18i20 | Scarlett2 Mixer Driver | 6.7+ |
| Scarlett 4th Gen | Solo, 2i2, 4i4 | Scarlett2 Mixer Driver | 6.8+ |
| Scarlett 4th Gen | 16i16, 18i16, 18i20 | FCP (Focusrite Control Protocol) Driver | 6.14+ |
| Clarett USB and Clarett+ | 2Pre, 4Pre, 8Pre | Scarlett2 Mixer Driver | 6.7+ |
| Vocaster | One, Two | Scarlett2 Mixer Driver | 6.10+ |

\* The small 1st Gen and 2nd Gen models don’t have any proprietary
software controls so they don’t need a driver beyond the standard ALSA
USB Audio driver. This means that this application (alsa-scarlett-gui)
is not needed, useful, or supported for these models.

If your distribution doesn’t include a recent-enough kernel for your
interface, you can get the latest driver from here and build it for
your current kernel if it’s not too old (the Scarlett2 and FCP drivers
are both maintained in the same tree here):
https://github.com/geoffreybennett/linux-fcp/releases

Kernel 6.7 and later have the Scarlett2 driver enabled by default. The
Scarlett 1st Gen driver and the FCP drivers are always enabled.

#### Enabling the Scarlett2 Driver

Some kernels before 6.7 have an earlier version of the Scarlett2
driver which is disabled by default. If this is you, check the driver
status (after plugging your interface in) with this command:

```
dmesg | grep -i -A 5 -B 5 focusrite
```

If all is good you’ll see messages like this:

```
New USB device found, idVendor=1235, idProduct=8215, bcdDevice= 6.0b
Product: Scarlett 18i20 USB
Focusrite Scarlett Gen 3 Mixer Driver enabled (pid=0x8215); ...
```

If you don’t see the “Mixer Driver” message or if it shows “disabled”
then check the [OLDKERNEL.md](OLDKERNEL.md) instructions (or,
preferably, upgrade your distro/kernel!).

### Gtk4

You need a Linux distribution with Gtk4 development libraries. If it
doesn’t have them natively, try the Flatpak instructions below.

### Firmware

#### Scarlett2 Driver

As of Linux 6.8, firmware updates of all supported interfaces from the
2nd Gen onwards can be done through Linux. This is mandatory for
Scarlett 4th Gen and Vocaster interfaces (unless you’ve already
updated it using the manufacturer’s software), and optional but
recommended for Scarlett 2nd and 3rd Gen, Clarett USB, and Clarett+
interfaces.

Download the firmware from
https://github.com/geoffreybennett/scarlett2-firmware and place it in
`/usr/lib/firmware/scarlett2` or use the RPM/deb package.

#### FCP Driver

Firmware updates for the big Scarlett 4th Gen interfaces is currently
only possible through the CLI `fcp-tool` utility available in the
[fcp-support](https://github.com/geoffreybennett/fcp-support). You
need to install this package and update the firmware before
alsa-scarlett-gui will work.

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
make -j$(nproc)
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
