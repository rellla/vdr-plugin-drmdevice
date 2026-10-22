DRMDevice
=========

An output plugin for VDR using DRM/KMS and hardware accelerated multimedia APIs
(https://github.com/rellla/vdr-plugin-drmdevice)


Features
--------
- Hardware accelerated video decoding (depending on hardware and FFmpeg support)
  - MPEG2
  - H.264
  - HEVC (10bit)
- Hardware accelerated video deinterlacing (depending on hardware and FFmpeg support)
- Software fallback for decoding and deinterlacing
- Optional GPU accelerated OSD composing (OpenGL/ES)
- Audio decoding with alsa (PCM)
- Audio passthrough support (AC3, E-AC3, DTS, TrueHD, DTS-HD), optionally
  with the formats reported by the HDMI sink
- UHD support
- HDR display support
- Auto-switch display mode to video source (optional)
- Zoom levels to crop black bars of the broadcast
- Picture-in-picture support (depending on hardware)
- Integrated minimalistic media player
- Grabbing support - video, osd and pip - mpeg2, h264, hevc (8/10bit)
- Detach/Attach support (release display, audio and GPU resources, but keep VDR running)
- Many adjustable (expert) settings via setup menu


Supported Hardware
------------------
In general, any device that provides a DRM/KMS output and is supported by FFmpeg should work.
This is true for most of the devices, that are supported by LibreELEC. 

Current development happens on a **Raspberry PI 4** and a **Radxa Rock 4B Plus (RK3399)**.

|                   | 576i MPEG2 | 720p H.264 | 1080i H.264 | 1080p HEVC |
| ----------------- | ---------- | ---------- | ----------- | ---------- |
| Allwinner         | Not tested | Not tested | Not tested  | Not tested |
| Amlogic           | ✅         | ✅         | ✅          | ✅         |
| Raspberry Pi 2    | ❌         | SW         | SW          | SW         |
| Raspberry Pi 3    | Not tested | Not tested | Not tested  | Not tested |
| Raspberry Pi 4    | SW         | ✅         | ✅          | ✅         |
| Raspberry Pi 5    | SW         | SW         | SW          | ✅         |
| Rockchip RK3399   | ✅         | ✅         | ✅          | ✅         |
| Rockchip RK3399T  | ✅         | ✅         | ✅          | ✅         |
| Rockchip RK3568B2 | ✅         | ✅         | ✅          | ✅         |

✅= Hardware decoding  
SW = Device does not support hardware decoding. Software decoding is used.  
❌= Device supports hardware decoding, but it is not implemented in this plugin.  


Why do we need another output device version?
---------------------------------------------
This is a continuation of [softhddevice-drm-gles](https://github.com/rellla/vdr-plugin-softhddevice-drm-gles.git). In October 2026 softhddevice-drm-gles was simply renamed to drmdevice and got major renaming in file and class names without any functional changes.
The original softhddevice-drm-gles was basically a fork of https://github.com/zillevdr/vdr-plugin-softhddevice-drm.git but has received major rewrites over the time.

The target of this version are embedded devices, see supported hardware.

As a principle, this output device version is only dealing with mainline versions and standards,
which means you can (and have to) use mainline (or to be mainlined) kernel, FFmpeg and mesa version.
This code does not work with vendor provided software or even closed source binaries.

www.LibreELEC.tv is good source to look for what is possible with mainlined media related software.
In the LibreELEC project you can find at least patches for all the software, even if some piece of code
isn't able to be mainlined or simply not already there. Because LibreELEC is a distribution to run
kodi on, you'll find everything you need.


How does it internally work?
----------------------------
- FFmpeg for video decoding and deinterlacing
- Alsa for audio output
- Kernel mode setting with DRM (zero-copy)
- OSD rendering with OpenGL/ES

Video decoding is done with FFmpeg. If your hardware has some hardware decoder
and/or deinterlacer which is supported by FFmpeg, the video is hardware decoded
(depending on the codec), otherwise software decoding/ deinterlacing is done.
For software deinterlacing FFmpeg's bwdif (Bob Weaver Deinterlacing Filter) is
used. This is the highest quality software deinterlacer available in FFmpeg.
The OSD is either composed with OpenGL/ES or with CPU (see below).
Both, video and OSD are rendered directly on seperate drm planes with kms.
When the video is hardware decoded, we don't need much CPU, because everything is done
with a zero-copy approach. That's the same for OpenGL/ES OSD.
Audio output is handled by Alsa.

See the [developer page](DEVELOPER/README.md) for helpful descriptions, graphs and diagrams
of VDR and drmdevice internals to see how things work together.

A doxygen documentation is available [here](https://rellla.github.io/vdr-plugin-drmdevice/)
and is updated on every commit automatically.


Known Bugs/ TODO
----------------
- rpi avcodec_flush_buffers is broken in [rpi-ffmpeg](https://github.com/jc-kynesim/rpi-ffmpeg) (used in LibreELEC), use a workaround for now
- see https://github.com/rellla/vdr-plugin-drmdevice/issues


Requirements
------------

- No running X!

- VDR (version >=2.6.6)

	Video Disk Recorder - turns a pc into a powerful set top box
	for DVB (http://www.tvdr.de/).

- FFmpeg

	Depending on upstreaming efforts a patched FFmpeg version may be needed to take advantage of
	hardware accelerated decoding and deinterlacing (see below).

- Kernel

	Depending on upstreaming efforts a patched kernel may be needed (WIP LE version)

	Have a look at https://github.com/LibreELEC/LibreELEC.tv/tree/master/packages/linux
	and choose the linux source and patches which matches your platform.
	Some of the needed patches have not yet been upstreamed, so you probably need to build the kernel on your own.
	LibreELEC supports Rockchip, Allwinner, Raspberry PI and (kind of) Amlogic.
	Have a look at https://github.com/LibreELEC/LibreELEC.tv/tree/master/projects to find platform
	specific patches.

- alsa-lib

	Advanced Linux Sound Architecture Library
	http://www.alsa-project.org

- OpenGL/ES support (optional):

	- gles2 (Mesa)
	- egl (Mesa)
	- gbm (Mesa)
	- freetype2
	- glm - OpenGL Mathematics (GLM)


Installation
------------

	git clone https://github.com/rellla/vdr-plugin-drmdevice.git
	cd vdr-plugin-drmdevice
	make
	make install


Commandline Arguments
---------------------
Use vdr -h to see the command line arguments supported by the plugin.
All command line arguments are optional. The plugin tries to set suitable default values.

	-a arg     audio device (e.g. hw:0,1)
	-c arg     audio mixer channel name (e.g. PCM)
	-o arg     drm device (e.g. /dev/dri/card0)
	-x arg     drm connector name (e.g. HDMI-A-1)
	-d arg     display resolution (e.g. 1920x1080@50)
	-g arg     osd (rendering) resolution (e.g. 1920x1080)
	-D         start plugin in detached state
	-w workaround:
	        disable-ogl-osd (to disable HW accelerated OSD)
	        disable-pip     (to force disabling the pip feature)


Setup: Environment Variables
----------------------------
Environment values have a higher priority than command line arguments.

	ALSA_DEVICE=default
		alsa PCM device name
	ALSA_MIXER=default
		alsa control device name
	ALSA_MIXER_CHANNEL=PCM
		alsa control channel name


Setup: /etc/vdr/setup.conf
--------------------------

	drmdevice.HideMainMenuEntry = 0
		0 = show drmdevice main menu entry, 1 = hide entry

	drmdevice.VideoEnableHdr = 0
		0 = disable HDR, 1 = enable HDR

	drmdevice.VideoDisplayMode = 0
		0  = use the display mode, which was either set by command line or autodetected at plugin start
		1  = display mode follows video resolution (width, height, refresh rate)
		     If the output device offers a display mode, that *exactly* matches the video mode
		     in terms of width - height - refresh rate, choose that one.
		     For example, a stream in 1920x1080@50 will choose 1920x1080@25 (interlaced modes halve the refresh rate in the description)
		     Caveat: A 576i video stream will always be mapped to default mode.
		2  = display mode follows video resolution respecting interlaced modes (width, height, refresh rate, interlaced flag)
		     Same as above, but now the display also chooses an interlaced mode for interlaced input (except for 576i) if available

	drmdevice.ZoomLevel1 = 344
	drmdevice.ZoomLevel2 = 125
	drmdevice.ZoomLevel3 = 0
	drmdevice.ZoomLevel4 = 0
	drmdevice.ZoomLevel5 = 0
		0 - 499 = zoom-in factor of the zoom level in 1/10 % (0 = level disabled)
		The zoom magnifies the picture to crop black bars, which are part of
		the broadcast. The aspect ratio is kept and the overflow is cropped
		equally off all sides. The defaults fill a 2.39:1 (+34.4%) and a 2.00:1
		(+12.5%) picture on a 16:9 screen.
		The active zoom level is reset with every channel switch and replay
		start and isn't stored. Select it in the plugin menu, with the key
		macros or with SVDRP ZOOM.

	drmdevice.VideoChannelSwitchMode = 0
		0 = fast channel switch is disabled
		1 = enable fast channel switch: video is presented immediately as a still-picture
		2 = enable fast channel switch: video is presented immediately as a still-picture and audio starts as soon as possible

	drmdevice.AudioSoftvol = 0
		0 = off, use hardware volume control
		1 = on, use software volume control

	drmdevice.AudioDownmix = 0
		0 = none, 1 = downmix
		Use FFmpeg downmix of AC-3/EAC-3 audio to stereo.

	drmdevice.AudioPassthrough = 0
		0 = none, 4 = AC-3, 8 = EAC-3, 16 = DTS, 32 = TrueHD, 64 = DTS-HD,
		128 = automatic

		For AC-3/EAC-3/DTS/TrueHD/DTS-HD the passthrough device is used and the
		audio stream is passed undecoded to the output device.
		Add the values above: e.g. 12 = AC-3 + EAC-3
		negating the value disables passthrough but remembers the setting
		automatic (128, "Passthrough: automatic" in the setup menu): the codecs
		are taken from the ELD of the HDMI sink (TV or AV receiver), which
		tells the formats the sink can decode. The other codec bits are
		ignored. The ELD is read again with every codec change and channel
		switch, so a receiver, which is switched on later, is recognized.
		Without an ELD (e.g. no HDMI device or the sink is off), the audio is
		decoded. If the ELD is wrong (e.g. an AV receiver behind a TV, which
		only reports its own formats), use the manual selection.
		The passthrough can be switched on and off in the plugin menu, with
		the key macro "@drmdevice Yellow 1" or with SVDRP PASS. This isn't
		stored, the setup value is the default at startup.
		TrueHD and DTS-HD Master Audio need a device with 8 channels and 192kHz
		(HDMI), DTS-HD High Resolution Audio needs 192kHz. DTS-HD is only
		recognized by the mediaplayer, otherwise (and if the device doesn't
		offer the format) only the DTS core is passed through with DTS enabled.

	drmdevice.AudioDisableAutoAES = 0
		0 = default: append the AES parameters (IEC 60958 channel status) to
		    the passthrough device (e.g. the non-audio flag and the sample rate,
		    as required by most receivers). A device, which doesn't accept
		    AES parameters, is used without them.
		1 = don't append the AES parameters
		(replaces AudioAutoAES, which was disabled by default)

	drmdevice.AudioDelay = 0
		+n or -n ms
		delay audio or delay video

	drmdevice.AudioNormalize = 0
		0 = off, 1 = enable audio normalize

	drmdevice.AudioMaxNormalize = 0
		maximal volume factor/1000 of the normalize filter

	drmdevice.AudioCompression = 0
		0 = off, 1 = enable audio compression

	drmdevice.AudioMaxCompression = 0
		maximal volume factor/1000 of the compression filter

	drmdevice.AudioStereoDescent = 0
		reduce volume level (/1000) for stereo sources

	drmdevice.AudioEq = 0
		0 = Equalizer disabled
		1 = Equalizer enabled

	drmdevice.AudioEqBand[01b..18b] = 0
		-15 to 1 = equalizer band gain (see Setup menu)

	drmdevice.PipScalePercent = 25
		10 - 100 = scale factor for pip (%)

	drmdevice.PipLeftPercent = 100
		0 - 100 = video left (%)
		0 = left aligned, 100 = right aligned

	drmdevice.PipTopPercent = 0
		0 - 100 = video top (%)
		0 = top aligned, 100 = bottom aligned

	drmdevice.PipUseAlt = 0
		0 = use standard (Pip*) position
		0 = use alternative (PipAlt*) position

	drmdevice.PipAltScalePercent = 25
		10 - 100 = scale factor for alternative pip (%)

	drmdevice.PipAltLeftPercent = 0
		0 - 100 = video left for alternative pip (%)
		0 = left aligned, 100 = right aligned

	drmdevice.PipAltTopPercent = 0
		0 - 100 = video top for alternative pip (%)
		0 = top aligned, 100 = bottom aligned

	drmdevice.LogLevel = 0
		0 = default (no debug logs)
		value is the sum of the following levels
			1     Standard debug logs
			2     AV-Sync debug logs
			4     Sound/Audio debug logs
			8     Osd debug logs
			16    DRM debug logs
			32    Codec (audio+video) debug logs
			64    Stillpicture debug logs
			128   Trickspeed debug logs
			256   Mediaplayer debug logs
			512   OpenGL/ES debug logs
			1024  OpenGL/ES Osd flush time measurement
			2048  OpenGL/ES Osd single command time measurement
			4096  Packet tracking logs (decoder + display)
			8192  Grabbing debug logs
			16384 FFmpeg debug logs

	drmdevice.LogFFmpegLevel = 0
		max. level of the FFmpeg logs (only used with the FFmpeg debug logs)
		0 = default (info, warnings and errors)
		1 = verbose
		2 = debug
		3 = trace

	drmdevice.AdditionalBufferLengthMs = 0
		0 = default (min 450ms fixed)
		-300 - 1000 = length of additional buffering duration in ms
		              (negative values reduce the buffer)

	drmdevice.DisableDeint = 0
		0 = deinterlacer active if available
		1 = deinterlacer is disabled

	drmdevice.DecoderFallbackToSw = 0
		0 = always use hardware decoder if available
		1 = fallback to software decoder if hardware decoder
		    fails after num packets (see below)

	drmdevice.DecoderFallbackToSwNumPkts = 22
		maximum number of packets sent to the hardware decoder
		until the software fallback jumps in

	drmdevice.ParseH264StreamStart = 0
		0 = disable parsing, 1-20 = parse H.264 stream up to the given number of I-Frames

	drmdevice.DropInvalidH264PFrames = 0
		1 = drop H.264 P-Frames with an invalid backwards reference

	drmdevice.MaxSizeGPUImageCache = 128
		how many GPU memory should be used for image caching

	drmdevice.ShowChannelSwitchDurationMessage = 0
		1 = the skin shows an info message of the channel switch duration


SVDRP Commands
--------------

	PLAY Url    Play the media from the given url.
		Tested extension: *.mp3, *.mp4, *.m3u, *.m3u8

		Url can be a
			- local file: /path_to_file/media_file.mp4
			- playlist: playlist_name.m3u
			- link: http://www.media-server/path_to_file/media_file.mp4

	DETA         Detach the plugin.
		An "ATTA" is needed in order to exit a detached state and to continue playback.

	ATTA         Attach the plugin again.

	STAT         Get attached/detached status.
		910 -> ATTACHED
		911 -> DETACHED

	PION         Enable picture-in-picture.

	PIOF         Disable picture-in-picture.

	PITO         Toggle picture-in-picture on/off.

	PIPU         Switch one channel up in pip window.

	PIPD         Switch one channel down in pip window.

	PIPC         Swap pip with main stream.

	PIPS         Switch main stream to pip stream and close the pip window.

	PIPP         Swap pip position between normal and alternative.
	PASS [on | off | toggle]
		Switch the audio passthrough on or off (not stored).
		Without an argument the passthrough state is returned.
	ZOOM [next | 0-5]
		Select the next zoom level (next) or the given zoom level (0 = off).
		Without an argument the active zoom level is returned.


Keymacros
---------

See keymacros.conf how to setup the macros.


Currently supported:  

	@drmdevice Blue 1      Toggle pip
	@drmdevice Blue 2      Pip channel +
	@drmdevice Blue 3      Pip channel -
	@drmdevice Blue 4      Swap pip and live stream
	@drmdevice Blue 5      Swap pip position
	@drmdevice Blue 6      Switch main stream to pip channel and close pip
	@drmdevice Red 1       Detach device
	@drmdevice Red 2       Attach device
	@drmdevice Green 1     Next zoom level
	@drmdevice Green 2     Zoom off
	@drmdevice Yellow 1    Switch the audio passthrough on/off


OpenGL/ES
---------
OpenGL/ES support is based on the work of Stefan Braun
(https://github.com/louisbraun/softhddevice-openglosd)

Using OpenGL/ES is optional but recommended.

This enables GPU accelerated OSD rendering.
OpenGL/ES support is enabled, if gles2, egl and gbm are found on the system
To disable OpenGL/ES support (if autodetected), simply build with

	GLES=0 make

In this case, VDR is using CPU based OSD rendering.


FFmpeg
------
The plugin should work with any recent upstream FFmpeg version but may lack
hardware decoder acceleration. If you want to drive the plugin with a hardware
acclerated decoder, you may need to build FFmpeg on your own.

LibreELEC is a good source to find out the current status of what is already upstreamed and supported.
Have a look at https://github.com/LibreELEC/LibreELEC.tv/tree/master/packages/multimedia/ffmpeg
and choose the FFmpeg source and patches which match your platform.
Most of them have not yet been upstreamed, so you probably need to build FFmpeg on your own.
LibreELEC supports Rockchip, Allwinner, Raspberry PI and (kind of) Amlogic.
Have a look at https://github.com/LibreELEC/LibreELEC.tv/tree/master/projects to find platform
specific patches.

The following instructions may help you to setup FFmpeg (may be outdated):

- Raspberry Pi
	- [rpi-ffmpeg](https://github.com/jc-kynesim/rpi-ffmpeg) is the very recent version for RPI4/RPI5 and Amlogic (For Raspberry Pi LibreELEC normally patches the upstream FFmpeg version to get the version from jc-kynesim)
	- check out a recent branch (note: the master branch is not the one you want to have)
	- most likely this branch has everything you need and you don't need any further patches (in doubt, check [how it is handled in LibreELEC](https://github.com/LibreELEC/LibreELEC.tv/tree/master/packages/multimedia/ffmpeg))
	- build FFmpeg as usual with the "configure - make - make install" logic
	- For Raspberry Pi building with the following configure options should enable hardware acceleration:

			--disable-static --enable-shared --enable-pic --enable-bsfs --enable-filters --enable-v4l2_m2m --enable-libdrm --enable-libudev --enable-v4l2-request --enable-sand --enable-hwaccels --enable-neon --disable-vdpau --disable-vaapi --disable-mmal

- Rockchip, Allwinner, Amlogic:
	- use mainline FFmpeg with version mentioned in [LibreELEC package file](https://github.com/LibreELEC/LibreELEC.tv/blob/master/packages/multimedia/ffmpeg/package.mk)
	- patch the mainline version with the [patchsets](https://github.com/LibreELEC/LibreELEC.tv/tree/master/packages/multimedia/ffmpeg/patches) which where mentioned for your platform in the package.mk
	- build FFmpeg as usual with the "configure - make - make install" logic
	- building with the following configure options should enable hardware acceleration:

			--disable-static --enable-shared --enable-pic --enable-bsfs --enable-filters --enable-v4l2_m2m --enable-libdrm --enable-libudev --enable-v4l2-request --enable-hwaccels --enable-neon --disable-vdpau --disable-vaapi


Display Resolution
------------------
Setting the display resolution is possible with the -d switch (see below).
If no specific resolution is stated, the plugin searches for resolutions
with a refresh rate of 50Hz. If one is found, the highest possbile is used.
If no resolution at 50Hz is found, the plugin searches for resolutions at
60Hz and uses the highest possbile. If neither 50Hz or 60Hz can be found,
the plugin uses the highest possbile resolution with a refresh rate which is
first discovered.

When the plugin is unable to detect any resolution (e.g. if there is no TV/Monitor
attached, the plugin will not start. You can workaorund this by submitting
a fixed EDID to your config (see Documentation, way differs from architecture)

When using the -d switch, make sure you use the syntax of "widthXheight@refreshrate",
so for example: "-d 3840x2160@50" for 4K at 50Hz.


Picture-in-Picture (PiP)
------------------------
The plugins supports the picture-in-picture feature. This allows you to move your current
stream into a small overlay window and continue watching TV as usual. You can change channels
on the main and in the pip window. It's also possible to swap both windows. Audio is only
available in the main stream. The pip window is always opened with the current channel and does
not remember the last pip channel.
You can define your individual pip window size and position and an alternative window. It's possible
to swap between the two positions.
PiP can be controlled via SVDRP, menu or via remote control keys, which are defined as hotkeys in
the keymacros.conf.
If your VDR has enough devices assigned, you can watch channels on different transponders. On one-tuner
systems, pip should only show the channels like if you are doing a concurrent recording.


Fast channel switch modes
-------------------------
The plugin offers three different modes for handling the audio and video playback at a channel switch:

- off:

	Audio and video start as soon as all buffers are filled enough and audio and video is in sync.

- video:

	Same as above, with the difference, that the very first video frame is presented immediately
	as a stillpicture as soon the decoder releases it.

- video and audio:

	Same as above, with the difference, that audio starts as soon as the audio buffer is filled enough,
	but not before a video frame was presented on the screen.
	Video exits stillpicture and starts running as soon as audio and video is in sync.

The fast channel switch mode can be set with drmdevice.VideoChannelSwitchMode.
The buffering mechanism and A/V-Sync is documented in the [developer readme](DEVELOPER/README.md#buffering---av-sync).


Mediaplayer
-----------
The plugin has an integrated mediaplayer reduced to the most important player functions.
Playback can be started with svdrp or via the main menu. If the mediaplayer is called via
the main menu, you can either choose a previously created playlist, create a new playlist or
simply play a single file.

The replay works like the replay of a VDR recording, the keys and the replay display
behave like in VDR. The settings "Multi speed mode", "Show replay mode", "Show remaining time",
"Progress display time" and the skip distances with the Green/Yellow keys of VDR's replay setup
are used. In replay mode, you can control playback with the following keys:

	Ok:            Show/ Hide replay display
	Play/Up:       Play
	Pause/Down:    Pause, play again if paused
	PlayPause:     Toggle between play and pause
	FastRew/Left:  Fast backward, slow backward if paused
	FastFwd/Right: Fast forward, slow forward if paused
	Red:           Jump to a time (enter hh:mm with the digit keys, then
	               Play/Up: play, Pause/Down/Ok: show the frame,
	               Left/Right: skip the time backward/ forward)
	Green:         Skip backward (skip distance, the repeat distance if held)
	Yellow:        Skip forward (skip distance, the repeat distance if held)
	Stop/Blue:     Stop replay and end player
	Back:          Stop replay and open the file menu
	Prev/Next:     Previous/ next file in playlist

The fast/slow modes and the positioning need a seekable file. The replay of a local file
continues at the position, where it was stopped (backed up by 10s). The positions of the last
100 files are stored in the file "resume" in the plugin's cache directory.


Hints for Raspberry Pi 4/5
--------------------------
It is highly recommended to use the kms driver with Raspberry Pi 4/5 and not the outdated
fkms driver. Be sure you have the following dtoverlay activated in your /boot/config.txt:

	dtoverlay=vc4-kms-v3d,cma-512

vc4-kms-v3d enables the new kms driver, cma-512 sets a custom cma size of 512MB.
This is the recommended size. Only change it when you exactly know what you are doing.

While using the outdated fkms driver still works with this plugin, there might be
some problems when viewing interlaced material and displaying the OSD.

To enable 4k resolution at 50Hz or 60Hz you have to set "hdmi_enable_4kp60=1" in
/boot/config.txt. Otherwise the Raspberry Pi is only capable of displaying 4K at
30Hz and the plugin will use 1080p at 50Hz according to the algorithm described above.

In case of permission problems under Raspberry Pi OS 5 with VDR installed via apt,
you might need to add the vdr user to the audio and render groups:

	usermod -a -G audio vdr
	usermod -a -G render vdr


Copyright
---------

Copyright 2011 - 2013 by Johns.    All Rights Reserved.  
Copyright 2018 - 2021 by zillevdr. All Rights Reserved.  
Copyright 2020 - 2026 by rellla.   All Rights Reserved.  


License
-------

[AGPL version 3.0 or later](LICENSE.md)  
