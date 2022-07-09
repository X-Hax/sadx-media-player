bass-vgmstream
==============

BASS plugin that plays vgmstream compatible music.
BASS is an easy-to-use cross platform audio interface. vgmstream is
a library that can render video game music formats.

This project is a simple plugin for BASS that allows people to
easily use vgmstream within their bass project. Simply call
`BASS_VGMSTREAM_StreamCreate` with a filename and some stream flags to
create a BASS stream from a file readable by vgmstream.

Required binaries
=================
Your application should have the following binaries available:

* BASS itself: `bass.dll`
* BASS_VGMSTREAM: `bass_vgmstream.dll`
* vgmstream related libraries:
`avcodec-vgmstream-58.dll`,
`avformat-vgmstream-58.dll`,
`avutil-vgmstream-56.dll`,
`libatrac9.dll`,
`libcelt-0061.dll`,
`libcelt-0110.dll`,
`libg719_decode.dll`,
`libmpg123-0.dll`,
`libspeex.dll`,
`libvorbis.dll`