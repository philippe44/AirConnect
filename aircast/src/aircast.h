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

#include "platform.h"
#include "pthread.h"
#include "raop_server.h"

#define	STR_LEN	256

/*----------------------------------------------------------------------------*/
/* typedefs */
/*----------------------------------------------------------------------------*/

typedef struct metadata_s {
	char artist[STR_LEN];
	char album[STR_LEN];
	char title[STR_LEN];
	char genre[STR_LEN];
	char path[STR_LEN];
	char artwork[STR_LEN];
	char remote_title[STR_LEN];
	uint32_t track;
	uint32_t duration;
	uint32_t track_hash;
	uint32_t sample_rate;
	uint8_t  sample_size;
	uint8_t  channels;
} metadata_t;

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
	char		Name[STR_LEN];
	char		Codec[STR_LEN];
	bool		Metadata;
	bool		Flush;
	double		MediaVolume;
	uint8_t		mac[6];
	char		Latency[STR_LEN];
	bool		Drift;
	char		ArtWork[4*STR_LEN];
} tMRConfig;

struct sMR {
	uint32_t Magic;
	bool  Running;
	tMRConfig Config;
	struct raopsr_s *Raop;
	raopsr_event_t	RaopState;
	char UDN	   	[RESOURCE_LENGTH];
	char Name		[STR_LEN];
	enum eMRstate 	State;
	bool			ExpectStop;
	uint32_t			Elapsed;
	unsigned		TrackPoll;
	void			*CastCtx;
	pthread_mutex_t Mutex;
	pthread_t 		Thread;
	double			Volume;
	uint32_t			VolumeStampRx, VolumeStampTx;
	bool			Group;
	struct sGroupMember {
		struct sGroupMember	*Next;
		struct in_addr		Host;
		uint16_t				Port;
   } *GroupMaster;
   bool Remove;
};

extern int32_t				glLogLimit;
extern tMRConfig			glMRConfig;
extern struct sMR			*glMRDevices;
extern int					glMaxDevices;
extern unsigned short		glPortBase, glPortRange;
extern char					glBinding[16];

#endif
