# Audio Test Streams

Short elementary streams (a 440Hz sine) for the tests in `test_audiostreams.cpp`.
They were encoded with FFmpeg 6.1:

```bash
F="ffmpeg -y -f lavfi -i sine=f=440:r=48000:d=0.25"
$F -ac 2 -c:a mp2 -b:a 192k -f mp2 mp2_48k.mp2
$F -ac 2 -c:a aac -b:a 128k -f adts aac_adts_48k.aac
$F -ac 2 -c:a aac -b:a 128k -f latm aac_latm_48k.latm
$F -ac 6 -c:a ac3 -b:a 448k -f ac3 ac3_48k.ac3
$F -ac 6 -c:a eac3 -b:a 384k -f eac3 eac3_48k.eac3
$F -ac 6 -c:a truehd -strict -2 -f truehd truehd_48k.thd

F="ffmpeg -y -f lavfi -i sine=f=440:r=48000:d=0.15"
$F -ac 6 -c:a dca -strict -2 -b:a 768k -f dts dts_48k.dts
F="ffmpeg -y -f lavfi -i sine=f=440:r=44100:d=0.15"
$F -ac 2 -c:a dca -strict -2 -b:a 768k -f dts dts_44k.dts
```

FFmpeg can't encode DTS-HD, the tests append a synthetic extension substream to
the DTS core frames instead.
