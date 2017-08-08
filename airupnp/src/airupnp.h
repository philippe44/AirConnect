/*
 *  AirUPnP - AirPlay to uPNP gateway
 *
 *	(c) Philippe 2017-, philippe_44@outlook.com
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

#ifndef __AIRUPNP_H
#define __AIRUPNP_H

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>

#include "platform.h"
#include "raopcore.h"
#include "upnp.h"
#include "pthread.h"
#include "util.h"

/*----------------------------------------------------------------------------*/
/* typedefs */
/*----------------------------------------------------------------------------*/

#define MAX_PROTO		128
#define MAX_RENDERERS	32
#define MAGIC			0xAABBCCDD
#define RESOURCE_LENGTH	250
#define	SCAN_TIMEOUT 	15
#define SCAN_INTERVAL	30

#define	HTTP_DEFAULT_MODE	-3

enum 	eMRstate { UNKNOWN, STOPPED, PLAYING, PAUSED, TRANSITIONING };
enum 	{ AVT_SRV_IDX = 0, REND_SRV_IDX, CNX_MGR_IDX, NB_SRV };

struct sService {
	char Id			[RESOURCE_LENGTH];
	char Type		[RESOURCE_LENGTH];
	char EventURL	[RESOURCE_LENGTH];
	char ControlURL	[RESOURCE_LENGTH];
	Upnp_SID		SID;
	s32_t			TimeOut;
};

typedef struct sMRConfig
{
	char		StreamLength[_STR_LEN_];
	bool		Enabled;
	char		Name[_STR_LEN_];
	bool		SendMetaData;
	bool		SendCoverArt;
	int			MaxVolume;
	int			RemoveCount;
	bool		UseFlac;
	int 		Latency;
	int			HttpLatency;
	u8_t		mac[6];
} tMRConfig;

struct sMR {
	u32_t Magic;
	bool  InUse;
	tMRConfig Config;
	char UDN			[RESOURCE_LENGTH];
	char DescDocURL		[RESOURCE_LENGTH];
	char FriendlyName	[RESOURCE_LENGTH];
	char PresURL		[RESOURCE_LENGTH];
	char Manufacturer	[RESOURCE_LENGTH];
	enum eMRstate 	State;
	bool			ExpectStop;
	struct raop_ctx_s *Raop;
	char			*CurrentURI, *NextURI;
	char*			ProtoInfo;
	metadata_t		MetaData;
	raop_event_t	RaopState;
	u32_t			Elapsed;
	u8_t			*seqN;
	void			*WaitCookie, *StartCookie;
	tQueue			ActionQueue;
	unsigned		TrackPoll, StatePoll;
	struct sService Service[NB_SRV];
	struct sAction	*Actions;
	pthread_mutex_t Mutex;
	pthread_t 		Thread;
	u8_t			Volume;
	bool			Muted;
	u16_t			ErrorCount;
	int				MissingCount;
	bool			TimeOut, Connected;
	bool			PlayWait;
	u32_t			PlayTime;
	bool			Running;
};

extern UpnpClient_Handle   	glControlPointHandle;
extern unsigned int 		glPort;
extern char 				glIPaddress[];
extern char 				glUPnPSocket[];
extern s32_t				glLogLimit;
extern tMRConfig			glMRConfig;
extern u32_t				glScanInterval;
extern u32_t				glScanTimeout;
extern struct sMR			glMRDevices[MAX_RENDERERS];

struct sMR 		*mr_File2Device(const char *FileName);
int 			CallbackEventHandler(Upnp_EventType EventType, void *Event, void *Cookie);
int 			CallbackActionHandler(Upnp_EventType EventType, void *Event, void *Cookie);


#endif
