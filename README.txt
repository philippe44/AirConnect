Use these applications to add AirPlay capabilities to Chromecast and UPnP players

====================================================================
Chromecast version:
====================================================================

- Simply launch the application and after ~30s, Chromecast devices will appear (under their CC name) in your AirPlay list
- Works for Windows, Linux (x86, x64, ARM) and MacOS 
- Groups are supported
- Volume changes in GoogleHome or other control application are synchronized to the AirPlay client
- Re-scan for new / lost CC players happens every 30s
- Custom configuration can be tweaked using XML config file (see command line - detailed format description to come)
- Use aircast -h for command line details
- When started in interfactive mode (w/o -Z or -z option) a few commands can be typed at the prompt
	- exit
	- save <name> : save the current configuration in <name>
- A config file (default config.xml) can be created fo advanced tweaking (a reference can be generated using -i command line)

There is a <common> section where parameters found apply to all players, unless the same parameter appears in an individual section, which takes precedence

--------------------------------------------------------------------
Parameters of importance

- latency <0 | 500..n> (0)	: buffering tweaking, needed when audio is shuttering or for bad networks (delay playback start). Set the amount of buffering for AirPlay realtime audio. Below 500ms is not recommended. 0 means automatic handling by AirPlay
- remove_count < 0 .. n> (3)	: how many times a player must be missing during a search to be removed. 0 disables removal
- media_volume	<0..1> (0.5)	: in a Chromecast group, applies a scaling factor to all members volume
- enabled <0|1>			: in common section, let newly discovered players to be enabled by default. In a dedicated section, enables the player
- name 				: name under which player appear in AirPlay 
- log_limit <-1 | n> (-1)	: when using log file, limits its size (-1 = no limit)

====================================================================
UPnP version:
====================================================================

- Simply launch the application and after ~30s, UPnP devices will appear (under their UPnP model name) in your AirPlay list
- Works for Windows, Linux (x86, x64, ARM) and MacOS 
- Volume changes on the player or other control application are synchronized to the AirPlay client
- Re-scan for new / lost CC players happens every 30s
- Custom configuration can be tweaked using XML config file (see command line - detailed format description to come)
- Use aircast -h for command line details
- When started in interfactive mode (w/o -Z or -z option) a few command can be typed at the prompt
	- exit
	- save <name> : save the current configuration in <name>
- A config file (default config.xml) can be created for advanced tweaking (a reference can be generated using -i command line)

There is a <common> section where parameters found apply to all players, unless the same parameter appears in an individual section, which takes precedence

--------------------------------------------------------------------
Parameters of importance

- use_flac <0|1> (0)		: send audio in flac format to player. Highly recommended and should work with all players. Otherwise, raw PCM is used but consumes more bandwidth works with fewer players.
- latency <[rtp][:http]> (0:0)	: buffering tweaking, needed when audio is shuttering or for bad networks (delay playback start)
	[rtp] 	: set the amount of buffering for AirPlay realtime audio. Below 500ms is not recommended. 0 means automatic handling by AirPlay
	[http]	: when a UPnP player does not buffer enough audio, shuttering happens. This parameter forces the buffering
- remove_count <0 .. n> (3)	: how many times a player must be missing during a search to be removed. 0 disables removal
- enabled <0|1>			: in common section, let newly discovered players to be enabled by default. In a dedicated section, enables the player
- name 				: name under which player appear in AirPlay 
- log_limit <-1 | n> (-1)	: when using log file, limits its size (-1 = no limit)

NB: for using with Sonos, set the latency http parameter to ~2000 (-latency :2000) otherwise you might have choppy sound.

====================================================================
Source code available here: https://github.com/philippe44/AirConnect
