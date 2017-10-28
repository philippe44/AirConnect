# AirCast: Send audio to Chromecast using AirPlay

# AirUPnP: Send audio to UPnP players using AirPlay (works for Sonos)

=============================================

Use these applications to add AirPlay capabilities to Chromecast and UPnP players

Common information:
- Simply launch the application and after ~30s, Players (Chromecast or UPnP/Sonos) will appear in your AirPlay list (iOS devices, iTunes, AirFoil ...)
- Works for Windows, Linux (x86, x64, ARM) and MacOS 
- Volume changes made in other control applications are synchronized with AirPlay client
- Re-scan for new / lost players happens every 30s
- When started in interfactive mode (w/o -Z or -z option) a few commands can be typed at the prompt
	- exit
	- save <name> : save the current configuration in <name>
- Use -h for command line details	
- A config file (default config.xml) can be created fo advanced tweaking (a reference version can be generated using -i command line)
- Chromecast groups are supported

Parameters of importance of config file

- latency <[rtp][:http]> (0:0)	: buffering tweaking, needed when audio is shuttering or for bad networks (delay playback start)
	[rtp] 	: ms of buffering of RTP (AirPlay) audio. Below 500ms is not recommended. 0 = use value from AirPlay
	[http]	: ms of buffering silence for HTTP audio (not needed normaly, except for Sonos)
- remove_count < 0 .. n> (3)	: how many times a player must be missing during a search to be removed. 0 disables removal
- enabled <0|1>			: in common section, enables new discovered players by default. In a dedicated section, enables the player
- name 				: name under which player appear in AirPlay 
- log_limit <-1 | n> (-1)	: when using log file, limits its size (-1 = no limit)
- media_volume	<0..1> (0.5)	: in a Chromecast group, applies a scaling factor to all members volume

=============================================

latency parameters detailed explanation:

These bridges receive realtime "synchronous" audio from the AirPlay controler in the format of RTP frames and forward it to the Chromecast/UPnP/Sonos player in HTTP "asynchronous" audio. The AirPlay clients "push" the audio using RTP and the Chromecast/UPnP/Sonos players "pull" the audio using an HTTP GET request. 

A player using HTTP to get its audio expects to receive immediately a large portion of audio as the response to its GET and this creates a large enough buffer to handle any network congestion. The rest of the audio transmission is regulated by the player using TCP flow control. But when the source if an AirPlay RTP device, there is no such large portion of audio available in advance to be sent to the Player, as the audio comes in real time. Every 8ms, a RTP frame is received and is immediately forwarded as the HTTP body. If the CC/UPnP/Sonos players starts to play immediately the 1st received frame, expecting an initial burst to follow, then any network congestion delaying RTP audio will starve the player and create shuttering. 

The [http] parameter allow a certain amount of silence frames to be sent to the Chromecast/UPnP/Sonos player, in a burst at the beginning. Then, while this "artificial" silence is being played, it's possible to build a buffer of RTP frames that will then hide network delays that might happen in further RTP frame transmission

But RTP frames are sent using UDP, which means there is no guarantee of delivery, so frames migh be lost from time to time (happens often on WiFi networks). To allow detection of lost frames, they are numbered sequentially (1,2 ... n) so every time two receives frames are not consecutives, the missing ones can be asked again by the AirPlay receiver. 




=============================================

if you want to recompile, you'll need

https://github.com/nanopb/nanopb

https://github.com/akheron/jansson

https://github.com/philippe44/mDNS-SD

https://github.com/philippe44/TinySVCmDNS

needs libFLAC and libupnp as well

