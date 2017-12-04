# AirConnect: Send audio to UPnP/Sonos/Chromecast players using AirPlay

Use these applications to add AirPlay capabilities to Chromecast and UPnP (like Sonos) players. 

AirConnect can run on any machine that has access to your local network (Windows, MacOS, Linux -x86, x64 and ARM-); it does not need to be on your main computer.  (For example, a Raspberry Pi works well). It will detect UPnP/Sonos/Chromecast players, create as many virtual AirPlay devices as needed, and act as a bridge/proxy between AirPlay clients and the real UPnP/Sonos/Chromecast players.

## Installing

1. Pre-built binaries are in bin/ directory of this repository. Clone this repository using git, download a zip file, or go to the [bin/ folder in the web interface](https://github.com/philippe44/AirConnect/tree/master/bin) and download the version that matches your OS. 

* For **Chromecast**, the file is `aircast-[platform]` (so `aircast-osx-multi` for Chromecast on OS X.) 
* For **UPnP/Sonos**, the file is `airupnp-[platform]` (so `airupnp-osx-multi` for UPnP/Sonos on OS X.) 

2. For Windows, download all the .dll as well.

3. Store the [executable] (e.g. `airupnp-osx-multi`) in any directory. 

4. On non-Windows machines, open a terminal and change directories to where the executable is stored and run `chmod +x [executable]`. (Example: `chmod +x airupnp-osx-multi`)

Some Debian Stretch distributions (e.g. Raspian) only provide openssl1.0.2, but 1.0.0 is needed. Download the version for Jessie using `wget http://security.debian.org/debian-security/pool/updates/main/o/openssl/libssl1.0.0_1.0.1t-1+deb8u7_armhf.deb` then install it with `sudo dpkg -i libssl1.0.0_1.0.1t-1+deb8u7_armhf.deb`
	
## Running

Double click the [executable] or launch it by typing `./[executable]` in the same command line window. 

<strong>For Sonos players, set latency by adding `-l 1000:2000` on the command line.</strong> (Example: `./airupnp-osx-multi -l 1000:2000`) 

After ~30 seconds you should see lots of log messages on screen. Using your iOS/Mac/iTunes/Airfoil/other client, you should now see new AirPlay devices and can try to play audio to them. 

If it works, type `exit`, which terminates the executable, and then, on non-Windows machines, relaunch it with `-z` so that it can run in the background and you can close the command line window. You can also start it automatically using any startup script. Nothing else should be required, no library or anything to install.

## Common information:

<strong>Use `-h` for command line details</strong>
- When started in interactive mode (w/o -Z or -z option) a few commands can be typed at the prompt
	- `exit`
	- `save [name]` : save the current configuration in file named [name]
- Volume changes made in native control applications are synchronized with AirPlay client
- Pause, Stop, Next, Prev using native control application are sent to AirPlay client - once paused, "native" play will not work
- Re-scan for new / lost players happens every 30s
- A config file (default `config.xml`) can be created for advanced tweaking (a reference version can be generated using  the `-i <config file name>` command line)
- Chromecast groups are supported
- Under non-Windows OS, do not daemonize (&) the executable w/o disabling interactive mode (`-Z`). Best is to use `-z`
- A 'click' noise can be heard when timings are adjusted by adding or skipping one 8ms frame. Use `-r` to disable such adjustements, but that might cause overrun or underrun on long playbacks

## Config file parameters 

The default configuration file is `config.xml`, stored in the same directory as the [executable].

- `latency <[rtp][:http]>` 	: (default: (0:0))buffering tweaking, needed when audio is shuttering or for bad networks (delay playback start)
	* [rtp] 	: ms of buffering of RTP (AirPlay) audio. Below 500ms is not recommended. 0 = use value from AirPlay
	* [http]	: ms of buffering silence for HTTP audio (not needed normaly, except for Sonos)
- `remove_count < 0 .. n>` 	: (default 3) how many times a player must be missing during a search to be removed. 0 disables removal
- `enabled <0|1>`			: in common section, enables new discovered players by default. In a dedicated section, enables the player
- `name` 				: The name that will appear for the device in AirPlay. You can change the default name. [1]
- `log_limit <-1 | n>` 	: (default -1) when using log file, limits its size (-1 = no limit)
- `codec <flac | wav | pcm>`	: format used to send HTTP audio. FLAC is recommended but uses more CPU (pcm only available for UPnP)
- `media_volume	<0..1>` 	: (default 0.5) in a Chromecast group, applies a scaling factor to all members volume

[1] Hint: To identify your Sonos players, pick an identified IP address, and visit the Sonos status page in your browser, like `http://192.168.1.126:1400/status/topology`. Click `Zone Players` and you will see the identifiers for your players in the `UUID` column.

## Start automatically in Linux (crude example, I'm not a systemd expert)

1. Create a file in `/etc/systemd/system`, e.g. `airupnp.service` with the following content (assuming the airupnp binary is in `/var/lib/airconnect`)

	[Unit]  
	Description=AirUPnP bridge  
	After=network-online.target  
	Wants=network-online.target  

	[Service]  
	Type=forking  
	ExecStart=/var/lib/airconnect/airupnp-arm -m squeezebox -l 1000:2000 -z -f /var/log/airupnp.log   
	Restart=on-failure  
	RestartSec=30  

	[Install]  
	WantedBy=multi-user.target   

2. Enable service `systemctl enable airupnp.service`

3. Reboot

## Latency parameters explained:

These bridges receive realtime "synchronous" audio from the AirPlay controler in the format of RTP frames and forward it to the Chromecast/UPnP/Sonos player in an HTTP "asynchronous" continuous audio binary format (notion of frames does not exist on that side). In other words, the AirPlay clients "push" the audio using RTP and the Chromecast/UPnP/Sonos players "pull" the audio using an HTTP GET request. 

A player using HTTP to get its audio expects to receive an initial large portion of audio as the response to its GET and this creates a large enough buffer to handle most further network congestion/delays. The rest of the audio transmission is regulated by the player using TCP flow control. But when the source is an AirPlay RTP device, there is no such large portion of audio available in advance to be sent to the Player, as the audio comes to the bridge in real time. Every 8ms, a RTP frame is received and is immediately forwarded as the continuation of the HTTP body. If the CC/UPnP/Sonos players starts to play immediately the 1st received audio sample, expecting an initial burst to follow, then any network congestion delaying RTP audio will starve the player and create shuttering. 

The [http] parameter allow a certain amount of silence frames to be sent to the Chromecast/UPnP/Sonos player, in a burst at the beginning. Then, while this "artificial" silence is being played, it's possible for the bridge to build a buffer of RTP frames that will then hide network delays that might happen in further RTP frames transmission. This delays the start of the playback by [http] ms.

But RTP frames are transmitted using UDP, which means there is no guarantee of delivery, so frames migh be lost from time to time (happens often on WiFi networks). To allow detection of lost frames, they are numbered sequentially (1,2 ... n) so every time two received frames are not consecutives, the missing ones can be asked again by the AirPlay receiver. 

Normally, the bridge forwards immediately every RTP frame using HTTP and again, in HTTP, the notion of frame numbers does not exit, it's just the continuous binary audio. So it's not possible to send audio non-sequentially when using HTTP 

For example, if received RTP frames are numbered 1,2,3,6, this bridge will forward (once decoded and transformed into raw audio) 1,2,3 immediately using HTTP but when it receives 6, it will re-ask fror 4 and 5 to be resent and hold 6 while waiting (if 6 was transmitted immediately, the Chromecast/UPnP/Sonos will play 1,2,3,6 ... not nice). The [rtp] parameter says for how long frame 6 shall be held before adding two silence frames for 4 and 5 and send sending 4,5,6. Obviously, if this delay is larger than the buffer in the Chromecast/UPnP/Sonos player, playback will stop by lack of audio. Note that [rtp] does not delay playback start.

NB: [rtp] and [http] could have been merged into a single [latency] parameter which would have set the max RTP frames holding time as well as the duration of the initial additional silence (delay), but because some UPnP players and all Chromecast devices do properly their own buffering of HTTP audio (i.e. they wait until they have received a certain amount of audio before starting to play), then adding silence would have introduced an extra un-necessary delay in playback. 

## Compiling from source

If you want to recompile, you'll need:

https://github.com/nanopb/nanopb

https://github.com/akheron/jansson

https://github.com/philippe44/mDNS-SD

https://github.com/philippe44/TinySVCmDNS

https://github.com/macosforge/alac

https://github.com/mrjimenez/pupnp (I'm using 1.6.19)

https://xiph.org/flac/

http://www.sourceware.org/pthreads-win32/
