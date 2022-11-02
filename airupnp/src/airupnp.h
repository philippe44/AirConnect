/*
 *  AirUPnP - AirPlay to uPNP gateway
 *
 *	(c) Philippe, philippe_44@outlook.com
 *
 * See LICENSE
 *
 */

#pragma once

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>

#include "pthread.h"
#include "upnp.h"

#include "platform.h"
#include "raop_server.h"
#include "cross_util.h"
#include "metadata.h"

#define VERSION "v1.0.4"" ("__DATE__" @ "__TIME__")"

/*----------------------------------------------------------------------------*/
/* typedefs */
/*----------------------------------------------------------------------------*/

#define STR_LEN	256

#define MAX_PROTO		128
#define MAX_RENDERERS	32
#define MAGIC			0xAABBCCDD
#define RESOURCE_LENGTH	250

enum 	eMRstate { UNKNOWN, STOPPED, PLAYING, PAUSED, TRANSITIONING };
enum 	{ AVT_SRV_IDX = 0, REND_SRV_IDX, CNX_MGR_IDX, TOPOLOGY_IDX, GRP_REND_SRV_IDX, NB_SRV };

struct sService {
	char Id			[RESOURCE_LENGTH];
	char Type		[RESOURCE_LENGTH];
	char EventURL	[RESOURCE_LENGTH];
	char ControlURL	[RESOURCE_LENGTH];
	Upnp_SID		SID;
	int32_t			TimeOut;
	uint32_t			Failed;
};

typedef struct sMRConfig
{
	int			HTTPLength;
	bool		Enabled;
	char		Name[STR_LEN];
	int			UPnPMax;
	bool		SendMetaData;
	bool		SendCoverArt;
	bool 		Flush;
	int			MaxVolume;
	char		Codec[STR_LEN];
	bool		Metadata;
	char		Latency[STR_LEN];
	bool		Drift;
	uint8_t		mac[6];
	char		ArtWork[4*STR_LEN];
	struct {
		char pcm[STR_LEN];
		char wav[STR_LEN];
		char flac[STR_LEN];
		char mp3[STR_LEN];
	} ProtocolInfo;
} tMRConfig;

struct sMR {
	uint32_t Magic;
	bool  Running;
	tMRConfig Config;
	char UDN			[RESOURCE_LENGTH];
	char DescDocURL		[RESOURCE_LENGTH];
	char friendlyName	[STR_LEN];
	enum eMRstate 	State;
	bool			ExpectStop;
	struct raopsr_s *Raop;
	metadata_t		MetaData;
	raopsr_event_t	RaopState;
	uint32_t		Elapsed;
	uint32_t		LastSeen;
	uint8_t			*seqN;
	void			*WaitCookie, *StartCookie;
	cross_queue_t	ActionQueue;
	unsigned		TrackPoll, StatePoll;
	struct sService Service[NB_SRV];
	struct sAction	*Actions;
	struct sMR		*Master;
	pthread_mutex_t Mutex;
	pthread_t 		Thread;
	double			Volume;		// to avoid int volume being stuck at 0
	uint32_t		VolumeStampRx, VolumeStampTx;
	uint16_t		ErrorCount;
	bool			TimeOut;
	char 			*ProtocolInfo;
};

extern UpnpClient_Handle   	glControlPointHandle;
extern int32_t				glLogLimit;
extern tMRConfig			glMRConfig;
extern struct sMR			*glMRDevices;
extern int					glMaxDevices;
extern char					glBinding[128];
extern unsigned short		glPortBase, glPortRange;

int MasterHandler(Upnp_EventType EventType, const void *Event, void *Cookie);
int ActionHandler(Upnp_EventType EventType, const void *Event, void *Cookie);
