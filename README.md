sadx-media-player
==============

This library enables playback of video through FFMPEG and audio through BASS and vgmstream in Sonic Adventure DX PC (2004).

The library is only meant to be used with the [SADX Mod Loader](https://github.com/X-Hax/sadx-mod-loader). For general use, refer to the projects linked below.

Dependencies
=================
 - [BASS audio library](https://www.un4seen.com/) version 2.4.17
 - [vgmstream](https://github.com/vgmstream/vgmstream) ([custom edit for this library](https://github.com/vgmstream/vgmstream/tree/abbd226b36e3ec895699dfdec3a33ce5c201998c))
 - [bass_vgmstream](https://github.com/angryzor/bass-vgmstream) ([fork used in this library](https://github.com/X-Hax/bass-vgmstream))
 - [FFMPEG](https://github.com/FFmpeg/FFmpeg) (Windows XP compatible build 7.1-596-5bc3b7f from [here](https://rwijnsma.home.xs4all.nl/files/ffmpeg/?C=M;O=D))

Notes
=================
1. The [vgmstream](https://github.com/vgmstream/vgmstream/tree/abbd226b36e3ec895699dfdec3a33ce5c201998c) submodule was altered to use generic FFMPEG 7.1 libraries instead of stripped 5.1.2 libraries that are used normally in vgmstream. This was done to reduce the number of required DLLs because the FFMPEG-based video player already uses `avcodec`, `avformat`, `avutil` etc.
2. Support for the following codecs and formats was removed in the `vgmstream` submodule to reduce the number of DLLs:
- `libg719_decode` (music from Namco games)
- `LibAtrac9` (format of PS4 and Vita music) 
- `libcelt` (.fsb and other related formats)
- `libspeex` (voice format in some EA games)
3. x64 configurations were removed because the game and the Mod Loader are 32-bit only.