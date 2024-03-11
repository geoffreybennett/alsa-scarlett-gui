# ALSA Scarlett2 Usage With Old Kernels

Linux kernel 6.7 (check your version with `uname -r`) was the first
kernel version with this driver enabled by default. It’s recommended
that you run 6.7 or later, or build the backported driver for your
kernel. If you do, then these instructions aren’t relevant; continue
with [INSTALL.md](INSTALL.md) for prerequisites, how to build,
install, and run `alsa-scarlett-gui`.

If you’ve got a Scarlett Gen 2 or 3 or a Clarett+ 8Pre and don’t mind
the level meters not working, then the minimum kernel versions are:

- **Scarlett Gen 2**: Linux 5.4 (bugs fixed in Linux 5.14)
- **Scarlett Gen 3**: Linux 5.14
- **Clarett+ 8Pre**: Linux 6.1

## Linux Kernel with Backported Driver (recommended)

Install the latest version of the backported driver from here:

https://github.com/geoffreybennett/scarlett-gen2/releases

then you can ignore the instructions below.

## Linux Kernel before 6.7 without Backported Driver

If you’re running a kernel before 6.7 without the backported driver,
you need to enable it at module load time with the `device_setup=1`
option to insmod/modprobe. Create a file
`/etc/modprobe.d/scarlett.conf` containing the appropriate line for
your device:

Scarlett Gen 2:

- **6i6**: `options snd_usb_audio vid=0x1235 pid=0x8203 device_setup=1`
- **18i8**: `options snd_usb_audio vid=0x1235 pid=0x8204 device_setup=1`
- **18i20**: `options snd_usb_audio vid=0x1235 pid=0x8201 device_setup=1`

Scarlett Gen 3:

- **Solo**: `options snd_usb_audio vid=0x1235 pid=0x8211 device_setup=1`
- **2i2**: `options snd_usb_audio vid=0x1235 pid=0x8210 device_setup=1`
- **4i4**: `options snd_usb_audio vid=0x1235 pid=0x8212 device_setup=1`
- **8i6**: `options snd_usb_audio vid=0x1235 pid=0x8213 device_setup=1`
- **18i8**: `options snd_usb_audio vid=0x1235 pid=0x8214 device_setup=1`
- **18i20**: `options snd_usb_audio vid=0x1235 pid=0x8215 device_setup=1`

Clarett+:

- **8Pre**: `options snd_usb_audio vid=0x1235 pid=0x820c device_setup=1`

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
