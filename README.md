# HFP AG Helper Plugin for BlueALSA

## Introduction

This project is an extension to the [bluez-alsa project](https://github.com/arkq/bluez-alsa) and requires the installation of BlueALSA.

The Bluetooth Hands-Free Profile specification states that it is only optional for a HFP device to allow audio connections outside of a call. As a result, some HFP-HF devices require that the AG have an active call in progress before they will accept an audio connection.

BlueALSA does not implement HFP call management functions, and therefore it is not possible to use HFP from a host using BlueALSA in the HFP-AG role to send or receive audio from such HF devices unless oFono is used for call management.

In use cases such as VOIP, web-based video conferencing, etc., there is no need for a phone modem and therefore oFono is not a good solution.

The purpose of this project is to allow applications to simply open an ALSA PCM device to achieve audio output and input via a HFP-HF device that requires an in-progess call. It achieves this by providing a wrapper around the BlueALSA PCM which sends the necessary HFP signalling to convince the HF device that a call is in progress. The wrapper is itself an ALSA PCM device called "hfpag" which is used in the same way as the `bluealsa` device. For example:
```
aplay -D hfpag:00:11:22:33:44:55 audio.wav
arecord -D hfpag:00:11:22:33:44:55 -f s16_le -c 1 -r 8000 recording.wav
```

The parameters of the `hfpag` PCM device are the same as for the `bluealsa` PCM device, except that `PROFILE` is not supported; the profile is always `sco`. Note that this PCM does not support HSP. See the [BlueALSA ALSA plugins manual page](https://github.com/arkq/bluez-alsa/blob/master/doc/bluealsa-plugins.7.rst) for more information on using BlueALSA plugins.


## Installation

```
meson setup builddir
cd builddir
meson compile
sudo meson install
```

## License

This project is licensed under the terms of the MIT license.

It includes copies of source code files from the bluez-alsa project, which are also licensed under the MIT license and all rights to those files remain with the original author.
