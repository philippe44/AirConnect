# AirUPnP: Send audio to UPnP/Sonos players using AirPlay
# AirCast: Send audio to Chromecast using AirPlay

Use these applications to add AirPlay capabilities to Chromecast and UPnP players. 

<p>It can run on any machine that has access to your local network (Windows, MacOS, Linux -x86, x64 and ARM-), it does not need to be on your main computer (a Raspberry Pi does well). It will detect UPnP/Sonos (respectively Chromecast) players, create as many virtual AirPlay devices and act as a bridge/proxy between AirPlay clients and the real UPnP/Sonos/Chromecast players</p>
<p>Go in the bin/ sub-directory of this repository and download the version that matches your OS (e.g. airupnp-osx-multi for using UPnP/Sonos on MacOS). For Windows, download all the .dll as well.</p>
<p>Store the [executable] (e.g. airupnp-osx-multi) in any directory and on non-Windows machines, go where the executable is stored and using a command line window, do a "chmod +x [executable]". After that, double click the [executable] or launch it by typing "./[executable]" in the same command line window (never type the "" or the []).</p>
<p>After ~30s you should see lots of log on the screen and using on of your iOS/Mac/iTunes/Airfoil ... client, try to play something on newly available AirPlay device.</p>
<p>If it works type "exit" which terminates the executable and then relaunch it with -z (for non-Windows) so that it can run in background and you can close the command line window. You can also start it automatically using any startup script.</p> <p>Nothing else should be required, no library or anything to install.</p>

Common information:
- Volume changes made in native control applications are synchronized with AirPlay client
- Pause, Stop, Next, Prev using native control application are sent to AirPlay client - once paused, "native" play will not work
- Re-scan for new / lost players happens every 30s
- A config file (default config.xml) can be created for advanced tweaking (a reference version can be generated using -i command line)
- Chromecast groups are supported
- <strong>Use -h for command line details</strong>
- When started in interactive mode (w/o -Z or -z option) a few commands can be typed at the prompt
	- exit
	- save [name] : save the current configuration in <name>
- Under non-Windows OS, do not daemonize (&) the executable w/o disabling interactive mode (-Z). Best is to use -z

<strong>for Sonos players, set latency by adding "-l 1000:2000" on the command line</strong>

Here are the main parameters that can be adjusted as well in the config file

- latency <[rtp][:http]> (0:0)	: buffering tweaking, needed when audio is shuttering or for bad networks (delay playback start)
	* [rtp] 	: ms of buffering of RTP (AirPlay) audio. Below 500ms is not recommended. 0 = use value from AirPlay
	* [http]	: ms of buffering silence for HTTP audio (not needed normaly, except for Sonos)
- remove_count < 0 .. n> (3)	: how many times a player must be missing during a search to be removed. 0 disables removal
- enabled <0|1>			: in common section, enables new discovered players by default. In a dedicated section, enables the player
- name 				: name under which player appear in AirPlay 
- log_limit <-1 | n> (-1)	: when using log file, limits its size (-1 = no limit)
- codec <flac | wav | pcm>	: format used to send HTTP audio. FLAC is recommended but uses more CPU (pcm only available for UPnP)
- media_volume	<0..1> (0.5)	: in a Chromecast group, applies a scaling factor to all members volume

## latency parameters detailed explanation:

These bridges receive realtime "synchronous" audio from the AirPlay controler in the format of RTP frames and forward it to the Chromecast/UPnP/Sonos player in an HTTP "asynchronous" continuous audio binary format (notion of frames does not exist on that side). In other words, the AirPlay clients "push" the audio using RTP and the Chromecast/UPnP/Sonos players "pull" the audio using an HTTP GET request. 

A player using HTTP to get its audio expects to receive an initial large portion of audio as the response to its GET and this creates a large enough buffer to handle most further network congestion/delays. The rest of the audio transmission is regulated by the player using TCP flow control. But when the source if an AirPlay RTP device, there is no such large portion of audio available in advance to be sent to the Player, as the audio comes to the bridge in real time. Every 8ms, a RTP frame is received and is immediately forwarded as continued HTTP body. If the CC/UPnP/Sonos players starts to play immediately the 1st received audio sample, expecting an initial burst to follow, then any network congestion delaying RTP audio will starve the player and create shuttering. 

The [http] parameter allow a certain amount of silence frames to be sent to the Chromecast/UPnP/Sonos player, in a burst at the beginning. Then, while this "artificial" silence is being played, it's possible for the bridge to build a buffer of RTP frames that will then hide network delays that might happen in further RTP frames transmission. This delays the start of the playback by [http] ms.

But RTP frames are transmitted using UDP, which means there is no guarantee of delivery, so frames migh be lost from time to time (happens often on WiFi networks). To allow detection of lost frames, they are numbered sequentially (1,2 ... n) so every time two received frames are not consecutives, the missing ones can be asked again by the AirPlay receiver. 

But normally, the bridge forwards immediately every RTP frame using HTTP and again, in HTTP, the notion of frame numbers does not exit, it's just the continuous binary audio. 

For example, if received RTP frames are numbered 1,2,3,6, this bridge will forward (once decoded and transformed into raw audio) 1,2,3 immediately but when it receives 6, it must ask fror 4 and 5 to be resent and hold 6 while waiting (if 6 was transmitted immediately, the Chromecast/UPnP/Sonos will play 1,2,3,6 ... not nice). The [rtp] parameter says how long frame 6 shall be held before adding two silence frames for 4 and 5 and send sending 4,5,6. Obviously, if this delay is larger than the buffer in the Chromecast/UPnP/Sonos player, playback will stop by lack of audio. Note that [rtp] does not delay playback start.

NB: [rtp] and [http] could have been merged into a single [latency] parameter which would have set the max RTP frames holding time as well as the duration of the initial additional silence (delay), but because some UPnP players and all Chromecast devices do properly their own buffering of HTTP audio (i.e. they wait until they have received a certain amount of audio before starting to play), then adding silence would have introduced an extra un-necessary delay in playback. 

## recompilation

if you want to recompile, you'll need

https://github.com/nanopb/nanopb

https://github.com/akheron/jansson

https://github.com/philippe44/mDNS-SD

https://github.com/philippe44/TinySVCmDNS

needs libFLAC and libupnp as well

