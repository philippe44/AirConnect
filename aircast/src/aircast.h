/*
 *  AirCast: Chromecast to AirPlay
 *
 *  (c) Philippe 2016-2017, philippe_44@outlook.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __AIRCAST_H
#define __AIRCAST_H

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>

#include "upnp.h"
#include "platform.h"
#include "pthread.h"
#include "raopcore.h"

/*----------------------------------------------------------------------------*/
/* typedefs */
/*----------------------------------------------------------------------------*/

#define MAX_PROTO		128
#define MAX_RENDERERS	32
#define	AV_TRANSPORT 	"urn:schemas-upnp-org:service:AVTransport:1"
#define	RENDERING_CTRL 	"urn:schemas-upnp-org:service:RenderingControl:1"
#define	CONNECTION_MGR 	"urn:schemas-upnp-org:service:ConnectionManager:1"
#define MAGIC			0xAABBCCDD
#define RESOURCE_LENGTH	250
#define	SCAN_TIMEOUT 	15
#define SCAN_INTERVAL	30


enum 	eMRstate { STOPPED, PLAYING, PAUSED };

typedef struct sMRConfig
{
	bool		Enabled;
	bool		StopReceiver;
	char		Name[_STR_LEN_];
	bool		SendMetaData;
	bool		SendCoverArt;
	int			RemoveCount;
	bool		UseFlac;
	double		MediaVolume;
	u8_t		mac[6];
	int 		Latency;
} tMRConfig;

struct sMR {
	u32_t Magic;
	bool  InUse;
	tMRConfig Config;
	struct raop_ctx_s *Raop;
	raop_event_t	RaopState;
	char UDN			[RESOURCE_LENGTH];
	char FriendlyName	[RESOURCE_LENGTH];
	enum eMRstate 	State;
	bool			ExpectStop;
	u32_t			Elapsed;
	unsigned		TrackPoll;
	bool			TimeOut;
	void			*CastCtx;
	pthread_mutex_t Mutex;
	pthread_t 		Thread;
	double			Volume;
	bool			Group;
	int				MissingCount;
	bool			Running;
};

extern unsigned int 		glPort;
extern char 				glIPaddress[];
extern char 				glUPnPSocket[];
extern u8_t		   			glMac[6];
extern s32_t				glLogLimit;
extern tMRConfig			glMRConfig;
extern u32_t				glScanInterval;
extern u32_t				glScanTimeout;
extern struct sMR			glMRDevices[MAX_RENDERERS];

#endif
