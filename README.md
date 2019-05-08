# AirConnect: Send audio to UPnP/Sonos/Chromecast players using AirPlay

Use these applications to add AirPlay capabilities to Chromecast and UPnP (like Sonos) players, to make them appear as AirPlay devices.

AirConnect can run on any machine that has access to your local network (Windows, MacOS, Linux -x86, x64 and ARM, Solaris and FreeBSD). It does not need to be on your main computer. (For example, a Raspberry Pi works well). It will detect UPnP/Sonos/Chromecast players, create as many virtual AirPlay devices as needed, and act as a bridge/proxy between AirPlay clients (iPhone, iPad, iTunes, MacOS, AirFoil ...) and the real UPnP/Sonos/Chromecast players.

The audio, after being decoded from alac, can be sent in plain, or re-encoded using mp3 or flac. Most players will not display metadata (artist, title, album ...) except when mp3 re-encoding is used and for UPnP/DLNA devices that support icy protocol. Chromecast players do not support this (yet).

## Installing

1. Pre-built binaries are in bin/ directory of this repository. You can download the whole repository as a zip file, clone it using git, or go to the [bin/ folder in the web interface](https://github.com/philippe44/AirConnect/tree/master/bin) and download the version that matches your OS. It's also possible to download files manually in a terminal by typing (e.g. for aircast arm version)<br/>`wget https://raw.githubusercontent.com/philippe44/AirConnect/master/bin/aircast-arm` 

* For **Chromecast**, the file is `aircast-[platform]` (so `aircast-osx-multi` for Chromecast on OS X.) 
* For **UPnP/Sonos**, the file is `airupnp-[platform]` (so `airupnp-osx-multi` for UPnP/Sonos on OS X.) 

2. For Windows, download all the .dll as well.

3. Store the [executable] (e.g. `airupnp-osx-multi`) in any directory. 

4. On non-Windows machines, open a terminal and change directories to where the executable is stored and run `chmod +x [executable]`. (Example: `chmod +x airupnp-osx-multi`). Note that if you choose to download the whole repository (instead of individual files) from you web browser and then unzip it, then in the bin/ sub-directory, file permissions should be already set.

## Running

Double click the [executable] or launch it by typing `./[executable]` in the same command line window. 

<strong>For Sonos & Heos players, set latency by adding `-l 1000:2000` on the command line.</strong> (Example: `./airupnp-osx-multi -l 1000:2000`) 

You should start to see lots of log messages on screen. Using your iOS/Mac/iTunes/Airfoil/other client, you should now see new AirPlay devices and can try to play audio to them. 

If it works, type `exit`, which terminates the executable, and then, on non-Windows/MacOS machines, relaunch it with `-z` so that it can run in the background and you can close the command line window. You can also start it automatically using any startup script or a Linux service as explained below. Nothing else should be required, no library or anything to install.

## Common information:

<strong>Use `-h` for command line details</strong>
- When started in interactive mode (w/o -Z or -z option) a few commands can be typed at the prompt
	- `exit`
	- `save [name]` : save the current configuration in file named [name]
- Volume changes made in native control applications are synchronized with AirPlay client
- Pause, Stop, Next, Prev using native control application are sent to AirPlay client - once paused, "native" play will not work
- Re-scan for new / lost players happens every 30s
- A config file (default `config.xml`) can be created for advanced tweaking (a reference version can be generated using  the `-i [config file name]` command line)
- Chromecast groups are supported
- When you have more than one ethernet card, you case use `-b [ip]` to set what card to bind to. Note that 0.0.0.0 is not authorized
- Use of -z disables interactive mode (no TTY) **and** self-daemonizes (use -p <file> to get the PID). Use of -Z only disables interactive mode 
- <strong>Do not daemonize (using & or any other method) the executable w/o disabling interactive mode (`-Z`), otherwise it will consume all CPU. On Linux, FreeBSD and Solaris, best is to use `-z`. Note that -z option is not available on MacOS or Windows</strong>
- A 'click' noise can be heard when timings are adjusted by adding or skipping one 8ms frame. Use `-r` to disable such adjustements (or use `<drift>` option in config file), but that might cause overrun or underrun on long playbacks
- <strong>This is an audio-only application. Do not expect to play a video on your device and have the audio from UPnP/Sonos or ChromeCast synchronized. It does not, cannot and will not work, regardless of any latency parameter. Please do not open tickets requesting this (see details below to understand why)</strong>

## Config file parameters 

The default configuration file is `config.xml`, stored in the same directory as the [executable]. Each of parameters below can be set in the `<common>` section to apply to all devices. It can also be set in any `<device>` section to apply only to a specific device and overload the value set in `<common>`

- `latency <[rtp][:http][:f]>` 	: (default: (0:0))buffering tweaking, needed when audio is shuttering or for bad networks (delay playback start)
	* [rtp] 	: ms of buffering of RTP (AirPlay) audio. Below 500ms is not recommended. 0 = use value from AirPlay. A negative value force sending of silence frames when no AirPlay audio has been received after 'RTP' ms, to force a continuous stream. If not, the UPnP/CC player will be not receive audio and some might close the connection after a while, although most players will simply be silent until stream restarts. This shall not be necessary in most of the case.
	* [http]	: ms of buffering silence for HTTP audio (not needed normaly, except for Sonos)
	* [f]		: when network congestion happens, source frames will not be received at all. Set this parameter to force sending silence frame then. Otherwise, no HTTP data will be sent and player might close the connection
- `drift <0|1>`	: enable adding or dropping a frame when case source frames producion is too fast or too slow
- `enabled <0|1>`	: in common section, enables new discovered players by default. In a dedicated section, enables the player
- `name` 		: The name that will appear for the device in AirPlay. You can change the default name.
- `log_limit <-1 | n>` 	: (default -1) when using log file, limits its size to 'n' MB (-1 = no limit)
- `codec <mp3[:<bitrate>] | flc[:0..9] | wav | pcm>`	: format used to send HTTP audio. FLAC is recommended but uses more CPU (pcm only available for UPnP). For example, `mp3:320` for 320Kb/s MP3 encoding.
- `metadata <0|1>`	: send metadata to player (only for mp3 codec and if player supports ICY protocol)
- `media_volume	<0..1>` : (default 0.5) Applies a scaling factor to device's hardware volume (chromecast only)
- `artwork`		: an URL to an artwork to be displayed on player

## Start automatically in Linux

1. Create a file in `/etc/systemd/system`, e.g. `airupnp.service` with the following content (assuming the airupnp binary is in `/var/lib/airconnect`)

```
[Unit]  
Description=AirUPnP bridge  
After=network-online.target  
Wants=network-online.target  

[Service]  
ExecStart=/var/lib/airconnect/airupnp-arm -l 1000:2000 -Z -x /var/lib/airconnect/airupnp.xml   
Restart=on-failure  
RestartSec=30  

[Install]  
WantedBy=multi-user.target   
```
2. Enable the service `sudo systemctl enable airupnp.service`

3. Start the service `sudo service airupnp start`

To start or stop manually the service, type `sudo service airupnp start|stop` in a command line window

To disable the service, type `sudo systemctl disable airupnp.service`

To view the log, `journalctl -u airupnp.service`

Obviously, from the above example, only use -x if you want a custom configuration. Thanks [@cactus](https://github.com/cactus) for systemd cleaning

[@1activegeek](https://github.com/1activegeek) has made a docker container [here](https://github.com/1activegeek/docker-airconnect) that follows the update of this repository - thanks!

## Start automatically in MacOS (credits @aiwipro)

Create the file com.aircast.bridge.plist in ~/Library/LaunchAgents/ 

```
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.aircast.bridge</string>
    <key>ProgramArguments</key>
    <array>
        <string>/[path]/aircast-osx-multi</string>
	<string>-Z</string>
        <string>-x</string>
        <string>/[path]/aircast.xml</string>
        <string>-f</string>
        <string>/[path]/aircast.log</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>LaunchOnlyOnce</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
</dict>
</plist>
```

Where `[path]` is the path where you've stored the aircast executable (without the []). It can be for example `Users/xxx/airconnect` where `xxx` is your user name 

## Start automatically under Windows

There are many tools that allow an application to be run as a service. You can try this [one](http://nssm.cc/)

## Synology installation

[@bandesz](https://github.com/bandesz) has made a nice package for automatic installation & launch of airupnp on Syno's

https://github.com/bandesz/AirConnect-Synology

## Sonos hints

The upnp version is often used with Sonos players. When a Sonos group is created, only the master of that group will appear as an AirPlay player and others will be removed if they were already detected. If the group is later split, then individual players will re-appear. 

Volume is set for the whole group, but the same level applies to all members. If you need to change individual volumes, you need to use a Sonos native controller. Note that these will be overridden if the group volume is changed later from an iXXX device.

To identify your Sonos players, pick an identified IP address, and visit the Sonos status page in your browser, like `http://192.168.1.126:1400/support/review`. Click `Zone Players` and you will see the identifiers for your players in the `UUID` column.

## Other players

- [@chpusch](https://github.com/chpusch) has found that Bose SoundTouch work well including synchonisation (as for Sonos, you need to use Bose's native application for grouping / ungrouping). I don't have a SoundTouch system so I cannot do the level of slave/master detection I did for Sonos

## Misc tips
 
- When players disappear regularly, it might be that your router is filtering out multicast packets. For example, for a Asus AC-RT68U, you have to login by ssh and run echo 0 > /sys/class/net/br0/bridge/multicast_snooping but it does not stay after a reboot.     

## Delay when switching track or source

I've received that question many times: why is there (sometimes) many seconds of delay when I switch track (or source) from my iPhone before I hear the change?

To understand, it's better that you read the next paragraph, but as you probably won't, here is a quick summary of how AirPlay works. As far as the sender (e.g. your iPhone) is concerned, once the connection with an AirPlay 'speaker' is established, this connection is almost like a analogue wire with a delay (buffer) of 1 or 2 seconds. 

What iOS does nowadays is that when you switch between tracks, instead of closing the connection and re-creating one, it just pushes the new audio through the existing connection, so you might have the 1~2 seconds of previous audio in the pipe before the new audio plays. Same thing when stopping/pausing playback, iOS simply stops pushing audio through the wire. 

There is a function to "flush" the audio in the pipe so that new audio plays immediately, but I've seen that recent versions of iOS don't use it anymore (or some applications decide to not flush while they could). That's not a big deal with most AirPlay speakers, it's a 1~2 second delay.

But with AirConnect, the AirPlay speaker is not a speaker, it's a UPnP or Chromecast player. They do not at all act like virtual wires, they instead expect to have the whole track available as a file and retrieve data from it as needed. In fact, one of the key functions that AirConnect does is looking like a wire to iPhone and looking like a file to the UPnP/CC. 

Usually, UPnP/CC players consume a large chunk of that 'file' before they start to play to handle network congestion, but some don't and simply start playing at the first received byte, counting that the large chunk will come quickly. But that chunk/buffer does not exist for AirConnect as audio is produced in real time by the iPhone. So if a player starts at the first byte, it will very likely lack audio data when a network congestion occurs and playback will stutter. The parameter `http latency` solves this issue by creating a silence buffer sent in a burst when establishing a connection, but this creates a permanent delay between the iPhone and the player. Some UPnP/CC players wait to have buffered enough data before they start playing and again, because that data is built in real time by AirConnect, this other delay adds up to the latency parameter (even if http latency is 0). 

When you switch between tracks or sources (or pause/stop), if your iPhone sends this "flush" command, then AirConnect immediately stops the UPnP/CC player. But if there is no flush command, it will play until these silence + self buffers are consumed ... that can be more than a few seconds.

In addition the delay can increase with time depending of clock speed difference between the iPhone and the UPnP/CC. Say that the iPhone's clock is 1% faster than the player's clock, then when it has produced 300s (5mins) of audio, the player has received it all but it has only played 297s, so there is an additional delay of 3s. If the iPhone moves track without the flush command, then the UPnP/CC player will start playing new audio (or stop) `http latency` + self-buffer length + 3 seconds later ... that can be a lot!

Unfortunately, there is nothing I can do about that. By not using the "flush" command, iOS or application using AirPlay create an issue that AirConnect has no way to identify or avoid.

## Latency parameters explained:

These bridges receive realtime "synchronous" audio from the AirPlay controller in the format of RTP frames and forward it to the Chromecast/UPnP/Sonos player in an HTTP "asynchronous" continuous audio binary format (notion of frames does not exist on that side). In other words, the AirPlay clients "push" the audio using RTP and the Chromecast/UPnP/Sonos players "pull" the audio using an HTTP GET request. 

A player using HTTP to get its audio expects to receive an initial large portion of audio as the response to its GET and this creates a large enough buffer to handle most further network congestion/delays. The rest of the audio transmission is regulated by the player using TCP flow control. But when the source is an AirPlay RTP device, there is no such large portion of audio available in advance to be sent to the Player, as the audio comes to the bridge in real time. Every 8ms, a RTP frame is received and is immediately forwarded as the continuation of the HTTP body. If the CC/UPnP/Sonos players starts to play immediately the 1st received audio sample, expecting an initial burst to follow, then any network congestion delaying RTP audio will starve the player and create shuttering. 

The [http] parameter allow a certain amount of silence frames to be sent to the Chromecast/UPnP/Sonos player, in a burst at the beginning. Then, while this "artificial" silence is being played, it's possible for the bridge to build a buffer of RTP frames that will then hide network delays that might happen in further RTP frames transmission. This delays the start of the playback by [http] ms.

But RTP frames are transmitted using UDP, which means there is no guarantee of delivery, so frames might be lost from time to time (happens often on WiFi networks). To allow detection of lost frames, they are numbered sequentially (1,2 ... n) so every time two received frames are not consecutive, the missing ones can be requested again by the AirPlay receiver. 

Normally, the bridge forwards immediately every RTP frame using HTTP and again, in HTTP, the notion of frame numbers does not exit, it's just the continuous binary audio. So it's not possible to send audio non-sequentially when using HTTP 

For example, if received RTP frames are numbered 1,2,3,6, this bridge will forward (once decoded and transformed into raw audio) 1,2,3 immediately using HTTP but when it receives 6, it will re-request 4 and 5 to be resent and hold 6 while waiting (if 6 were to be transmitted immediately, the Chromecast/UPnP/Sonos will play 1,2,3,6 ... not nice). The [rtp] parameter sets for how long frame 6 shall be held before adding two silence frames for 4 and 5 and send sending 4,5,6. Obviously, if this delay is larger than the buffer in the Chromecast/UPnP/Sonos player, playback will stop by lack of audio. Note that [rtp] does not delay playback start.

When [f] is set, silence frames will be inserted as soon as no RTP frames have been received during [rtp] ms. This ensure that a continuous stream of audio is available on the HTTP server. This might be necessary for some players that close the HTTP connection if they have not received data for some time. It's unlikely though. Note that otherwise when RTP stream is interrupted for more than [http] ms, the UPnP/CC player will stop anyway as it will have empty buffers. Still, as soon as the RTP stream resumes, the bridge will receive frame N, where the last received one might be N-500. So it will request the (up to) [rtp] missing ones (might be less than 500), and restart playing at N-[http], so potentially silence will be inserted. 

Many have asked for a way to do video/audio synchronisation so that UPnP (Sonos) players can be used as speakers when playing video on a computer or tablet (YouTube for example). Due to this RTP-to-HTTP bridging, this cannot be done as the exact time when an audio frame is played cannot be controlled on the HTTP client. AirPlay speakers can achieve that because the iPhone/iPad/MAC player will  "delay" the video by a known amount, send the audio in advance (usually 2 sec) and then control the exact time when this audio is output by the speaker. But although AirConnect has the exact request timing and maintains synchronization with the player, it cannot "relay" that synchronization to the speakers. UPnP protocol does not allow this and Sonos has not made their protocol public. Sometimes you might get lucky because the video-to-audio delay will almost match the HTTP player delay, but it is not reproductible and will not be stable over time.

## Compiling from source

If you want to recompile, you'll need:

- https://github.com/nanopb/nanopb
- https://github.com/akheron/jansson
- https://github.com/philippe44/mDNS-SD (use fork v2)
- https://github.com/philippe44/TinySVCmDNS
- https://github.com/macosforge/alac
- https://github.com/mrjimenez/pupnp (I'm using 1.6.19)
- https://xiph.org/flac/
- http://www.sourceware.org/pthreads-win32/
- https://github.com/toots/shine
- https://github.com/mattstevens/dmap-parser
