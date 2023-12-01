---
name: Issue
about: Open an issue for help, to report a bug, or request a feature
title: ''
labels: ''
assignees: ''

---

# `alsa-scarlett-gui` Issue Template

Thank you for taking the time to contribute to the `alsa-scarlett-gui` project. Before you submit your issue, please ensure you have checked the FAQ and provide the necessary information below.

## Confirmation
- [ ] I confirm that I have read the [FAQ](https://github.com/geoffreybennett/alsa-scarlett-gui/blob/master/FAQ.md).

## Issue Category
Please select the category that best describes your issue:
- [ ] Help Request
- [ ] Bug Report
- [ ] Feature Request

## Environment Details
Please provide the following details about your environment.

### Linux Distribution and Version
(paste output from `cat /etc/redhat-release` or `cat /etc/lsb_release` here)
- Distribution:
- Version:

### Kernel Version
(paste output from `uname -r` here)
- Kernel version:

### Kernel Messages
(paste output from `dmesg | grep -A 5 -B 5 -i focusrite` here)

### Focusrite Interface Series and Model
(maybe shown in kernel messages, or paste output from `lsusb -d1235:` if unsure)
- Series (e.g., Scarlett 2nd/3rd/4th Gen, Clarett USB, Clarett+):
- Model (e.g., Solo, 2i2, 4i4, etc.):

### Audio System
(use `ps aux | grep -E "pulseaudio|jackd|pipewire"` to check)
- [ ] PulseAudio
- [ ] JACK
- [ ] PipeWire

## Issue Description
Please provide a detailed description of the issue or feature request, including steps to reproduce (if applicable), expected behavior, and actual behavior:

---

Thank you for helping improve `alsa-scarlett-gui`!
