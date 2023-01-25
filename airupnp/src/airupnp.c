/*
 * AirUPnP - AirPlay to uPNP gateway
 *
 * (c) Philippe, philippe_44@outlook.com
 *
 * see LICENSE
 *
 */

#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#endif

#include "platform.h"
#include "ixmlextra.h"
#include "airupnp.h"
#include "mdnssvc.h"
#include "upnpdebug.h"

#include "cross_ssl.h"
#include "cross_net.h"
#include "cross_log.h"
#include "cross_thread.h"

#include "avt_util.h"
#include "config_upnp.h"
#include "mr_util.h"

#define	AV_TRANSPORT 			"urn:schemas-upnp-org:service:AVTransport"
#define	RENDERING_CTRL 			"urn:schemas-upnp-org:service:RenderingControl"
#define	CONNECTION_MGR 			"urn:schemas-upnp-org:service:ConnectionManager"
#define TOPOLOGY				"urn:schemas-upnp-org:service:ZoneGroupTopology"
#define GROUP_RENDERING_CTRL	"urn:schemas-upnp-org:service:GroupRenderingControl"

#define DISCOVERY_TIME 		20
#define PRESENCE_TIMEOUT	(DISCOVERY_TIME * 6)

#define MAX_DEVICES			32
#define HTTP_FIXED_LENGTH	INT_MAX

/* for the haters of GOTO statement: I'm not a big fan either, but there are
cases where they make code more leightweight and readable, instead of tons of
if statements. In short function, I use them for loop exit and cleanup instead
of code repeating and break/continue
*/

/*----------------------------------------------------------------------------*/
/* globals																	  */
/*----------------------------------------------------------------------------*/
int32_t  			glLogLimit = -1;
UpnpClient_Handle 	glControlPointHandle;
struct sMR			*glMRDevices;
int					glMaxDevices = MAX_DEVICES;
uint16_t			glPortBase, glPortRange;
char				glBinding[128] = "?";

log_level	main_loglevel = lINFO;
log_level	raop_loglevel = lINFO;
log_level	util_loglevel = lWARN;
log_level	upnp_loglevel = lINFO;

tMRConfig			glMRConfig = {
							-1,      	// HTTPLength
							true,		// Enabled
							"",      	// Name
							1,			// UPnPMax
							true,		// SendMetaData
							false,		// SendCoverArt
							true,		// Flush
							100,		// MaxVolume
							"flc",	    // Codec
							true,		// Metadata
							"",			// RTP:HTTP Latency (0 = use AirPlay requested)
							false,		// drift
							{0, 0, 0, 0, 0, 0 }, // MAC
							"",			// artwork
							{   		// protocolInfo
								"http-get:*:audio/L16;rate=44100;channels=2:DLNA.ORG_PN=LPCM;DLNA.ORG_OP=00;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=0d500000000000000000000000000000",
								"http-get:*:audio/wav:DLNA.ORG_OP=00;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=0d500000000000000000000000000000",
								"http-get:*:audio/flac:DLNA.ORG_OP=00;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=0d500000000000000000000000000000",
								"http-get:*:audio/mpeg:DLNA.ORG_PN=MP3;DLNA.ORG_OP=00;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=0d500000000000000000000000000000",
							},
					};

/*----------------------------------------------------------------------------*/
/* local typedefs															  */
/*----------------------------------------------------------------------------*/
typedef struct sUpdate {
	enum { DISCOVERY, BYE_BYE, SEARCH_TIMEOUT } Type;
	char *Data;
} tUpdate;

/*----------------------------------------------------------------------------*/
/* consts or pseudo-const													  */
/*----------------------------------------------------------------------------*/
#define MEDIA_RENDERER	"urn:schemas-upnp-org:device:MediaRenderer"

static const struct cSearchedSRV_s
{
 char 	name[RESOURCE_LENGTH];
 int	idx;
 uint32_t  TimeOut;
} cSearchedSRV[NB_SRV] = {	{AV_TRANSPORT, AVT_SRV_IDX, 0},
						{RENDERING_CTRL, REND_SRV_IDX, 30},
						{CONNECTION_MGR, CNX_MGR_IDX, 0},
						{TOPOLOGY, TOPOLOGY_IDX, 0},
						{GROUP_RENDERING_CTRL, GRP_REND_SRV_IDX, 0},
				   };

/*----------------------------------------------------------------------------*/
/* locals																	  */
/*----------------------------------------------------------------------------*/
static log_level*		loglevel = &main_loglevel;
#if LINUX || FREEBSD || SUNOS
static bool	   			glDaemonize = false;
#endif
static bool				glMainRunning = true;
static struct in_addr 	glHost;
static struct mdnsd*	glmDNSServer = NULL;
static char*			glExcluded = NULL;
static char*			glExcludedModelNumber = NULL;
static char*			glIncludedModelNumbers = NULL;
static char				*glPidFile = NULL;
static bool	 			glAutoSaveConfigFile = false;
static bool				glGracefullShutdown = true;
static bool				glDiscovery = false;
static pthread_mutex_t 	glUpdateMutex;
static pthread_cond_t  	glUpdateCond;
static pthread_t 		glMainThread, glUpdateThread;
static cross_queue_t			glUpdateQueue;
static bool				glInteractive = true;
static char				*glLogFile;
static uint16_t			glPort;
static void				*glConfigID = NULL;
static char				glConfigName[STR_LEN] = "./config.xml";

static char usage[] =

			VERSION "\n"
		   "See -t for license terms\n"
		   "Usage: [options]\n"
		   "  -b <ip|iface>[:<port>]\tnetwork interface or interface and UPnP port to use\n"
		   "  -a <port>[:<count>]\tset inbound port and range for RTP and HTTP\n"
		   "  -c <mp3[:<rate>]|flc[:0..9]|wav|pcm>\taudio format send to player\n"
		   "  -g <-3|-1|0>\t\tHTTP content-length mode (-3:chunked, -1:none, 0:fixed)\n"
		   "  -u <version>\tset the maximum UPnP version for search (default 1)\n"
		   "  -x <config file>\tread config from file (default is ./config.xml)\n"
		   "  -i <config file>\tdiscover players, save <config file> and exit\n"
		   "  -I \t\t\tauto save config at every network scan\n"
		   "  -l <[rtp][:http][:f]>\tRTP and HTTP latency (ms), ':f' forces silence fill\n"
		   "  -r \t\t\tlet timing reference drift (no click)\n"
		   "  -f <logfile>\t\twrite debug to logfile\n"
		   "  -p <pid file>\t\twrite PID in file\n"
		   "  -m <n1,n2...>\t\texclude devices whose model include tokens\n"
		   "  -n <m1,m2,...>\texclude devices whose name includes tokens\n"
		   "  -o <m1,m2,...>\tinclude only listed models; overrides -m and -n (use <NULL> if player don't return a model)\n"
		   "  -d <log>=<level>\tSet logging level, logs: all|raop|main|util|upnp, level: error|warn|info|debug|sdebug\n"

#if LINUX || FREEBSD
		   "  -z \t\t\tDaemonize\n"
#endif
		   "  -Z \t\t\tNOT interactive\n"
		   "  -k \t\t\tImmediate exit on SIGQUIT and SIGTERM\n"
		   "  -t \t\t\tLicense terms\n"
   		   "  --noflush\t\tignore flush command (wait for teardown to stop)\n"
		   "\n"
		   "Build options:"
#if LINUX
		   " LINUX"
#endif
#if WIN
		   " WIN"
#endif
#if OSX
		   " OSX"
#endif
#if FREEBSD
		   " FREEBSD"
#endif
#if EVENTFD
		   " EVENTFD"
#endif
#if SELFPIPE
		   " SELFPIPE"
#endif
#if WINEVENT
		   " WINEVENT"
#endif
		   "\n\n";

static char license[] =
		   "This program is free software: you can redistribute it and/or modify\n"
		   "it under the terms of the GNU General Public License as published by\n"
		   "the Free Software Foundation, either version 3 of the License, or\n"
		   "(at your option) any later version.\n\n"
		   "This program is distributed in the hope that it will be useful,\n"
		   "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
		   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
		   "GNU General Public License for more details.\n\n"
		   "You should have received a copy of the GNU General Public License\n"
		   "along with this program.  If not, see <http://www.gnu.org/licenses/>.\n\n"
	;


/*----------------------------------------------------------------------------*/
/* prototypes */
/*----------------------------------------------------------------------------*/
static 	void*	MRThread(void *args);
static 	void*	UpdateThread(void *args);
static 	bool 	AddMRDevice(struct sMR *Device, char * UDN, IXML_Document *DescDoc,	const char *location);
static	bool 	isExcluded(char *Model, char *ModelNumber);
static bool 	Start(bool cold);
static bool 	Stop(bool exit);

// functions with _ prefix means that the device mutex is expected to be locked
static bool 	_ProcessQueue(struct sMR *Device);



/*----------------------------------------------------------------------------*/
#define TRACK_POLL  (1000)
#define STATE_POLL  (500)
#define MAX_ACTION_ERRORS (5)
#define MIN_POLL (min(TRACK_POLL, STATE_POLL))
static void *MRThread(void *args) {
	int elapsed, wakeTimer = MIN_POLL;
	unsigned last;
	struct sMR *p = (struct sMR*) args;

	last = gettime_ms();

	for (; p->Running; crossthreads_sleep(wakeTimer)) {
		elapsed = gettime_ms() - last;

		// context is valid as long as thread runs
		pthread_mutex_lock(&p->Mutex);

		wakeTimer = (p->State != STOPPED) ? MIN_POLL : MIN_POLL * 10;
		LOG_SDEBUG("[%p]: UPnP thread timer %d %d", p, elapsed, wakeTimer);

		p->StatePoll += elapsed;
		p->TrackPoll += elapsed;

		/*
		should not request any status update if we are stopped, off or waiting
		for an action to be performed or slave
		*/
		if (p->Master || (p->RaopState != RAOP_PLAY && p->State == STOPPED) ||
			p->ErrorCount > MAX_ACTION_ERRORS || p->WaitCookie) goto sleep;

		// get track position & CurrentURI
		if (p->TrackPoll > TRACK_POLL) {
			p->TrackPoll = 0;
			if (p->State != STOPPED && p->State != PAUSED) {
				AVTCallAction(p, "GetPositionInfo", p->seqN++);
			}
		}

		// do polling as event is broken in many uPNP devices
		if (p->StatePoll > STATE_POLL) {
			p->StatePoll = 0;
			AVTCallAction(p, "GetTransportInfo", p->seqN++);
		}

sleep:
		last = gettime_ms(),
		pthread_mutex_unlock(&p->Mutex);
	}

	// clean our stuff before exiting
	AVTActionFlush(&p->ActionQueue);

	return NULL;
}

/*----------------------------------------------------------------------------*/
void HandleRAOP(void *owner, raopsr_event_t event, void *param) {
	struct sMR *Device = (struct sMR*) owner;

	// this is async, so need to check context validity
	if (!CheckAndLock(owner)) return;

	switch (event) {
		case RAOP_STREAM:
			// a PLAY will come later, so we'll do the load at that time
			LOG_INFO("[%p]: Stream", Device);
			Device->RaopState = event;
			break;
		case RAOP_STOP:
			// this is TEARDOWN, so far there is always a FLUSH before
			LOG_INFO("[%p]: Stop", Device);
			if (Device->RaopState == RAOP_PLAY) {
				AVTStop(Device);
				Device->ExpectStop = true;
			}
			Device->RaopState = event;
			break;
		case RAOP_FLUSH:
			if (Device->Config.Flush) {
				LOG_INFO("[%p]: Flush", Device);
				AVTStop(Device);
				Device->ExpectStop = true;
				Device->RaopState = event;
            }
			break;
		case RAOP_PLAY: {
			char *uri, *mp3radio = "";

			if (Device->RaopState != RAOP_PLAY) {
				static int count;
				if (strcasestr(Device->Config.Codec, "mp3") && *Device->Service[TOPOLOGY_IDX].ControlURL) {
					mp3radio = "x-rincon-mp3radio://";
					LOG_INFO("[%p]: Sonos live stream", Device);
				}
				(void)!asprintf(&uri, "%shttp://%s:%u/stream-%u.%s", mp3radio, inet_ntoa(glHost),
								*((short unsigned*) param),	count++, Device->Config.Codec);
				AVTSetURI(Device, uri, &Device->MetaData, Device->ProtocolInfo);
				NFREE(uri);
			}

			AVTPlay(Device);

			// don't set volume, a RAOP_VOLUME will be sent by the controller
			Device->RaopState = event;
			break;
		}
		case RAOP_VOLUME: {
			// Volume sent by raop is normalized 0..1
			double RaopVolume = *(double*) param;
			int GroupVolume, i;
			uint32_t now = gettime_ms();

			// discard echo commands
			if (now < Device->VolumeStampRx + 1000) break;
			Device->VolumeStampTx = now;

			// Sonos group volume API is unreliable, need to create our own
			GroupVolume = CalcGroupVolume(Device);

			/* Volume is kept as a double in device's context to avoid relative
			values going to 0 and being stuck there. This works because although
			volume is echoed from UPnP event as an integer, timing check allows
			that echo to be discarded, so until volume is changed locally, it
			remains a floating value which will regrow from being < 1 */

			if (GroupVolume < 0) {
				Device->Volume = RaopVolume * Device->Config.MaxVolume;
				CtrlSetVolume(Device, Device->Volume + 0.5, Device->seqN++);
				LOG_INFO("[%p]: Volume[0..100] %d", Device, (int) Device->Volume);
			} else {
				double Ratio = GroupVolume ? (RaopVolume * Device->Config.MaxVolume) / GroupVolume : 0;

				// set volume for all devices
				for (i = 0; i < glMaxDevices; i++) {
					struct sMR *p = glMRDevices + i;
					if (!p->Running || (p != Device && p->Master != Device)) continue;

					// for standalone master, GroupVolume & Volume are identical
					if (GroupVolume) p->Volume = min(p->Volume * Ratio, p->Config.MaxVolume);
					else p->Volume = RaopVolume * p->Config.MaxVolume;

					CtrlSetVolume(p, p->Volume + 0.5, p->seqN++);
					LOG_INFO("[%p]: Volume[0..100] %d:%d", p, (int) p->Volume, GroupVolume);
				}
			}
			break;
		}
		default:
			break;
	}

	pthread_mutex_unlock(&Device->Mutex);
}


/*----------------------------------------------------------------------------*/
void HandleHTTP(void *owner, struct key_data_s *headers, struct key_data_s *response) {
	struct sMR *Device = (struct sMR*) owner;
	char *p;

	if (kd_lookup(headers, "getcontentFeatures.dlna.org") && (p = strcasestr(Device->ProtocolInfo, "DLNA.ORG")) != NULL) {
		kd_add(response, "contentFeatures.dlna.org", p);
	}
	if ((p = kd_lookup(headers, "transferMode.dlna.org")) != NULL) {
		kd_add(response, "transferMode.dlna.org", p);
	}
}

/*----------------------------------------------------------------------------*/
static bool _ProcessQueue(struct sMR *Device) {
	struct sService *Service = &Device->Service[AVT_SRV_IDX];
	tAction *Action;
	int rc = 0;

	Device->WaitCookie = 0;
	if ((Action = queue_extract(&Device->ActionQueue)) == NULL) return false;

	Device->WaitCookie = Device->seqN++;
	rc = UpnpSendActionAsync(glControlPointHandle, Service->ControlURL, Service->Type,
							 NULL, Action->ActionNode, ActionHandler, Device->WaitCookie);

	if (rc != UPNP_E_SUCCESS) {
		LOG_ERROR("Error in queued UpnpSendActionAsync -- %d", rc);
	}

	ixmlDocument_free(Action->ActionNode);
	free(Action);

	return (rc == 0);
}

/*----------------------------------------------------------------------------*/
static void ProcessEvent(Upnp_EventType EventType, const void *_Event, void *Cookie) {
	UpnpEvent* Event = (UpnpEvent*)_Event;
	struct sMR *Device = SID2Device(UpnpEvent_get_SID(Event));
	IXML_Document *VarDoc = UpnpEvent_get_ChangedVariables(Event);
	char  *r = NULL;
	char  *LastChange = NULL;

	// this is async, so need to check context's validity
	if (!CheckAndLock(Device)) return;

	LastChange = XMLGetFirstDocumentItem(VarDoc, "LastChange", true);

	if ((!Device->Raop && !Device->Master) || !LastChange) {
		LOG_SDEBUG("no RAOP device (yet) or not change for %s", UpnpString_get_String(UpnpEvent_get_SID(Event)));
		pthread_mutex_unlock(&Device->Mutex);
		NFREE(LastChange);
		return;
	}

	// Feedback volume to AirPlay controller
	r = XMLGetChangeItem(VarDoc, "Volume", "channel", "Master", "val");
	if (r) {
		struct sMR *Master = Device->Master ? Device->Master : Device;
		double Volume = atoi(r), GroupVolume;
		uint32_t now = gettime_ms();

		if (Volume != (int) Device->Volume && now > Master->VolumeStampTx + 1000) {
			Device->Volume = Volume;
			Master->VolumeStampRx = now;
			GroupVolume = CalcGroupVolume(Master);
			LOG_INFO("[%p]: UPnP Volume local change %d:%d (%s)", Device, (int) Volume, (int) GroupVolume, Device->Master ? "slave": "master");
			Volume = GroupVolume < 0 ? Volume / Device->Config.MaxVolume : GroupVolume / 100;
			raopsr_notify(Master->Raop, RAOP_VOLUME, &Volume);
		}
	}

	NFREE(r);
	NFREE(LastChange);

	pthread_mutex_unlock(&Device->Mutex);
}

/*----------------------------------------------------------------------------*/
int ActionHandler(Upnp_EventType EventType, const void *Event, void *Cookie) {
	static int recurse = 0;
	struct sMR *p = NULL;

	LOG_SDEBUG("action: %i [%s] [%p] [%u]", EventType, uPNPEvent2String(EventType), Cookie, recurse);
	recurse++;

	switch ( EventType ) {
		case UPNP_CONTROL_ACTION_COMPLETE: 	{
			char* r;;
			
			p = CURL2Device(UpnpActionComplete_get_CtrlUrl(Event));
			if (!CheckAndLock(p)) return 0;

			LOG_SDEBUG("[%p]: ac %i %s (cookie %p)", p, EventType, UpnpString_get_String(UpnpActionComplete_get_CtrlUrl(Event)));

			// If waited action has been completed, proceed to next one if any
			if (p->WaitCookie) {
				const char *Resp = XMLGetLocalName(UpnpActionComplete_get_ActionResult(Event), 1);

				LOG_DEBUG("[%p]: Waited action %s", p, Resp ? Resp : "<none>");

				// discard everything else except waiting action
				if (Cookie != p->WaitCookie) break;

				p->StartCookie = p->WaitCookie;
				_ProcessQueue(p);

				/*
				when certain waited action has been completed, the state need
				to be re-acquired because a 'stop' state might be missed when
				(eg) repositionning where two consecutive status update will
				give 'playing', the 'stop' in the middle being unseen
				*/
				if (Resp && (!strcasecmp(Resp, "StopResponse") ||
							 !strcasecmp(Resp, "PlayResponse") ||
							 !strcasecmp(Resp, "PauseResponse"))) {
					p->State = UNKNOWN;
				}

				break;
			}

			// don't proceed anything that is too old
			if (Cookie < p->StartCookie) break;

			// transport state response
			if ((r = XMLGetFirstDocumentItem(UpnpActionComplete_get_ActionResult(Event), "CurrentTransportState", true)) != NULL) {
				if (!strcmp(r, "TRANSITIONING") && p->State != TRANSITIONING) {
					p->State = TRANSITIONING;
					LOG_INFO("[%p]: uPNP transition", p);
				} else if (!strcmp(r, "STOPPED") && p->State != STOPPED) {
					if (p->RaopState == RAOP_PLAY && !p->ExpectStop) raopsr_notify(p->Raop, RAOP_STOP, NULL);
					p->State = STOPPED;
					p->ExpectStop = false;
					LOG_INFO("[%p]: uPNP stopped", p);
				} else if (!strcmp(r, "PLAYING") && (p->State != PLAYING)) {
					p->State = PLAYING;
					if (p->RaopState != RAOP_PLAY) raopsr_notify(p->Raop, RAOP_PLAY, NULL);
					LOG_INFO("[%p]: uPNP playing", p);
				} else if (!strcmp(r, "PAUSED_PLAYBACK") && p->State != PAUSED) {
					p->State = PAUSED;
					if (p->RaopState == RAOP_PLAY) raopsr_notify(p->Raop, RAOP_PAUSE, NULL);
					LOG_INFO("[%p]: uPNP pause", p);
				}
			}

			NFREE(r);

			LOG_SDEBUG("Action complete : %i (cookie %p)", EventType, Cookie);

			if (UpnpActionComplete_get_ErrCode(Event) != UPNP_E_SUCCESS) {
				p->ErrorCount++;
				LOG_ERROR("Error in action callback -- %d (cookie %p)", UpnpActionComplete_get_ErrCode(Event), Cookie);
			} else {
				p->ErrorCount = 0;
			}

			break;
		}
		default:
			break;
	}

	if (p) {
		pthread_mutex_unlock(&p->Mutex);
    }

	recurse--;

	return 0;
}

/*----------------------------------------------------------------------------*/
int MasterHandler(Upnp_EventType EventType, const void *_Event, void *Cookie) {
	// this variable is not thread_safe and not supposed to be
	static int recurse = 0;

	// libupnp makes this highly re-entrant so callees must protect themselves
	LOG_SDEBUG("event: %i [%s] [%p] (recurse %u)", EventType, uPNPEvent2String(EventType), Cookie, recurse);

	if (!glMainRunning) return 0;
	recurse++;

	switch ( EventType ) {
		case UPNP_DISCOVERY_ADVERTISEMENT_ALIVE:
			// probably not needed now as the search happens often enough and alive comes from many other devices
			break;
		case UPNP_DISCOVERY_SEARCH_RESULT: {
			tUpdate *Update = malloc(sizeof(tUpdate));

			Update->Type = DISCOVERY;
			Update->Data = strdup(UpnpString_get_String(UpnpDiscovery_get_Location(_Event)));
			queue_insert(&glUpdateQueue, Update);
			pthread_cond_signal(&glUpdateCond);

			break;
		}
		case UPNP_DISCOVERY_ADVERTISEMENT_BYEBYE: {
			tUpdate *Update = malloc(sizeof(tUpdate));

			Update->Type = BYE_BYE;
			Update->Data = strdup(UpnpString_get_String(UpnpDiscovery_get_DeviceID(_Event)));
			queue_insert(&glUpdateQueue, Update);
			pthread_cond_signal(&glUpdateCond);

			break;
		}
		case UPNP_DISCOVERY_SEARCH_TIMEOUT: {
			tUpdate *Update = malloc(sizeof(tUpdate));

			Update->Type = SEARCH_TIMEOUT;
			Update->Data = NULL;
			queue_insert(&glUpdateQueue, Update);
			pthread_cond_signal(&glUpdateCond);

			// if there is a cookie, it's a targeted Sonos search
			if (!Cookie) {
				static int Version;
				char SearchTopic[sizeof(MEDIA_RENDERER)+2];
				snprintf(SearchTopic, sizeof(SearchTopic), "%s:%i", MEDIA_RENDERER, (Version++ % glMRConfig.UPnPMax) + 1);
				UpnpSearchAsync(glControlPointHandle, DISCOVERY_TIME, SearchTopic, NULL);
			}

			break;
		}
		case UPNP_EVENT_RECEIVED:
			ProcessEvent(EventType, _Event, Cookie);
			break;
		case UPNP_EVENT_SUBSCRIPTION_EXPIRED:
		case UPNP_EVENT_AUTORENEWAL_FAILED: {
			struct sService *s;
			struct sMR *Device = SID2Device(UpnpEventSubscribe_get_SID(_Event));

			if (!CheckAndLock(Device)) break;

			s = EventURL2Service(UpnpEventSubscribe_get_PublisherUrl(_Event), Device->Service);
			if (s != NULL) {
				UpnpSubscribeAsync(glControlPointHandle, s->EventURL, s->TimeOut,
								   MasterHandler, (void*) strdup(Device->UDN));
				LOG_INFO("[%p]: Auto-renewal failed, re-subscribing", Device);
			}

			pthread_mutex_unlock(&Device->Mutex);

			break;
		}
		case UPNP_EVENT_RENEWAL_COMPLETE:
		case UPNP_EVENT_SUBSCRIBE_COMPLETE: {
			struct sMR *Device = UDN2Device((char*) Cookie);
			struct sService *s;

			free(Cookie);

			if (!CheckAndLock(Device)) break;

			s = EventURL2Service(UpnpEventSubscribe_get_PublisherUrl(_Event), Device->Service);
			if (s != NULL) {
				if (UpnpEventSubscribe_get_ErrCode(_Event) == UPNP_E_SUCCESS) {
					s->Failed = 0;
					strcpy(s->SID, UpnpString_get_String(UpnpEventSubscribe_get_SID(_Event)));
					s->TimeOut = UpnpEventSubscribe_get_TimeOut(_Event);
					LOG_INFO("[%p]: subscribe success", Device);
				} else if (s->Failed++ < 3) {
					LOG_INFO("[%p]: subscribe fail, re-trying %u", Device, s->Failed);
					UpnpSubscribeAsync(glControlPointHandle, s->EventURL, s->TimeOut,
									   MasterHandler, (void*) strdup(Device->UDN));
				} else {
					LOG_WARN("[%p]: subscribe fail, volume feedback will not work", Device);
				}
			}

			pthread_mutex_unlock(&Device->Mutex);

			break;
		}
		case UPNP_EVENT_SUBSCRIPTION_REQUEST:
		case UPNP_CONTROL_ACTION_REQUEST:
		case UPNP_EVENT_UNSUBSCRIBE_COMPLETE:
		case UPNP_CONTROL_GET_VAR_REQUEST:
		case UPNP_CONTROL_ACTION_COMPLETE:
		case UPNP_CONTROL_GET_VAR_COMPLETE:
		break;
	}

	recurse--;

	return 0;
}

/*----------------------------------------------------------------------------*/
static void FreeUpdate(void *_Item) {
	tUpdate *Item = (tUpdate*) _Item;
	NFREE(Item->Data);
	free(Item);
}

/*----------------------------------------------------------------------------*/
static void *UpdateThread(void *args) {
	while (glMainRunning) {
		tUpdate *Update;
		bool Updated = false;

		pthread_mutex_lock(&glUpdateMutex);
		pthread_cond_wait(&glUpdateCond, &glUpdateMutex);
		pthread_mutex_unlock(&glUpdateMutex);

		for (; glMainRunning && (Update = queue_extract(&glUpdateQueue)) != NULL; queue_free_item(&glUpdateQueue, Update)) {
			struct sMR *Device;
			uint32_t now = gettime_ms() / 1000;

			// UPnP end of search timer
			if (Update->Type == SEARCH_TIMEOUT) {

				LOG_DEBUG("Presence checking", NULL);

				for (int i = 0; i < glMaxDevices; i++) {
					Device = glMRDevices + i;
					if (Device->Running && (Device->State != PLAYING || Device->RaopState != RAOP_PLAY) &&
						((Device->LastSeen + PRESENCE_TIMEOUT) - now > PRESENCE_TIMEOUT	||
						Device->ErrorCount > MAX_ACTION_ERRORS)) {

						pthread_mutex_lock(&Device->Mutex);
						LOG_INFO("[%p]: removing unresponsive player (%s)", Device, Device->Config.Name);
						raopsr_delete(Device->Raop);
						// device's mutex returns unlocked
						DelMRDevice(Device);
					}
				}

			// device removal request
			} else if (Update->Type == BYE_BYE) {

				Device = UDN2Device(Update->Data);

				// Multiple bye-bye might be sent
				if (!CheckAndLock(Device)) continue;

				LOG_INFO("[%p]: renderer bye-bye: %s", Device, Device->Config.Name);
				raopsr_delete(Device->Raop);
				// device's mutex returns unlocked
				DelMRDevice(Device);

			// device keepalive or search response
			} else if (Update->Type == DISCOVERY) {
				IXML_Document *DescDoc = NULL;
				char *UDN = NULL, *ModelName = NULL, *ModelNumber = NULL;

				// it's a Sonos group announce, just do a targeted search and exit
				if (strstr(Update->Data, "group_description")) {
					for (int i = 0; i < glMaxDevices; i++) {
   						Device = glMRDevices + i;
						if (Device->Running && *Device->Service[TOPOLOGY_IDX].ControlURL)
							UpnpSearchAsync(glControlPointHandle, 5, Device->UDN, Device);
					}
					continue;
				}

				// existing device ?
				for (int i = 0; i < glMaxDevices; i++) {
					Device = glMRDevices + i;
					if (Device->Running && !strcmp(Device->DescDocURL, Update->Data)) {
						char *friendlyName = NULL;
						struct sMR *Master = GetMaster(Device, &friendlyName);

						Device->LastSeen = now;
						LOG_DEBUG("[%p] UPnP keep alive: %s", Device, Device->Config.Name);

						// check for name change
						UpnpDownloadXmlDoc(Update->Data, &DescDoc);
						if (!friendlyName) friendlyName = XMLGetFirstDocumentItem(DescDoc, "friendlyName", true);

						if (friendlyName && strcmp(friendlyName, Device->friendlyName) &&
							!strncmp(Device->friendlyName, Device->Config.Name, strlen(Device->friendlyName)) &&
							Device->Config.Name[strlen(Device->Config.Name) - 1] == '+') {
							LOG_INFO("[%p]: Device name change %s %s", Device, friendlyName, Device->friendlyName);
							strcpy(Device->friendlyName, friendlyName);
							sprintf(Device->Config.Name, "%s+", friendlyName);
							raopsr_update(Device->Raop, Device->Config.Name, "airupnp");
							Updated = true;
						}

						// we are a master (or not a Sonos)
						if (!Master && Device->Master) {
							// slave becoming master again
							LOG_INFO("[%p]: Sonos %s is now master", Device, Device->Config.Name);
							pthread_mutex_lock(&Device->Mutex);
							Device->Master = NULL;
							Device->Raop = raopsr_create(glHost, glmDNSServer, Device->Config.Name,
								   "airupnp", Device->Config.mac, Device->Config.Codec,
								   Device->Config.Metadata, Device->Config.Drift, Device->Config.Flush,
								   Device->Config.Latency, Device,
								   HandleRAOP, HandleHTTP, glPortBase, glPortRange,
								   Device->Config.HTTPLength ? Device->Config.HTTPLength : HTTP_FIXED_LENGTH);
							pthread_mutex_unlock(&Device->Mutex);
						} else if (Master && (!Device->Master || Device->Master == Device)) {
							pthread_mutex_lock(&Device->Mutex);
							LOG_INFO("[%p]: Sonos %s is now slave", Device, Device->Config.Name);
							Device->Master = Master;
							raopsr_delete(Device->Raop);
							Device->Raop = NULL;
							pthread_mutex_unlock(&Device->Mutex);
						}

						NFREE(friendlyName);
						goto cleanup;
					}
				}

				// this can take a very long time, too bad for the queue...
				int rc;
				if ((rc = UpnpDownloadXmlDoc(Update->Data, &DescDoc)) != UPNP_E_SUCCESS) {
					LOG_DEBUG("Error obtaining description %s -- error = %d\n", Update->Data, rc);
					goto cleanup;
				}

				// not a media renderer but maybe a Sonos group update
				if (!XMLMatchDocumentItem(DescDoc, "deviceType", MEDIA_RENDERER, false)) {
					goto cleanup;
				}

				ModelName = XMLGetFirstDocumentItem(DescDoc, "modelName", true);
				ModelNumber = XMLGetFirstDocumentItem(DescDoc, "modelNumber", true);
				UDN = XMLGetFirstDocumentItem(DescDoc, "UDN", true);

				// excluded device
				if (isExcluded(ModelName, ModelNumber)) {
					goto cleanup;
				}

				// new device so search a free spot - as this function is not called
				// recursively, no need to lock the device's mutex
				for (Device = glMRDevices; Device->Running && Device < glMRDevices + glMaxDevices; Device++);

				// no more room !
				if (Device == glMRDevices + glMaxDevices) {
					LOG_ERROR("Too many uPNP devices (max:%u)", glMaxDevices);
					goto cleanup;
				}
				
				Updated = true;

				if (AddMRDevice(Device, UDN, DescDoc, Update->Data) && !glDiscovery) {
					// create a new AirPlay
					Device->Raop = raopsr_create(glHost, glmDNSServer, Device->Config.Name,
									   "airupnp", Device->Config.mac, Device->Config.Codec,
									   Device->Config.Metadata, Device->Config.Drift, Device->Config.Flush,
									   Device->Config.Latency, Device,
									   HandleRAOP, HandleHTTP, glPortBase, glPortRange,
									   Device->Config.HTTPLength ? Device->Config.HTTPLength : HTTP_FIXED_LENGTH);
					if (!Device->Raop) {
						LOG_ERROR("[%p]: cannot create RAOP instance (%s)", Device, Device->Config.Name);
						DelMRDevice(Device);
					}
				}

cleanup:
				if (Updated && (glAutoSaveConfigFile || glDiscovery)) {
					LOG_DEBUG("Updating configuration %s", glConfigName);
					SaveConfig(glConfigName, glConfigID, false);
				}

				NFREE(UDN);
				NFREE(ModelName);
				NFREE(ModelNumber);
				if (DescDoc) ixmlDocument_free(DescDoc);
			}
		}
	}

	return NULL;
}

/*----------------------------------------------------------------------------*/
static void *MainThread(void *args) {
	while (glMainRunning) {

		crossthreads_sleep(30*1000);				
		if (!glMainRunning) break;

		if (glLogFile && glLogLimit != - 1) {
			uint32_t size = ftell(stderr);

			if (size > glLogLimit*1024*1024) {
				uint32_t Sum, BufSize = 16384;
				uint8_t *buf = malloc(BufSize);

				FILE *rlog = fopen(glLogFile, "rb");
				FILE *wlog = fopen(glLogFile, "r+b");
				LOG_DEBUG("Resizing log", NULL);
				for (Sum = 0, fseek(rlog, size - (glLogLimit*1024*1024) / 2, SEEK_SET);
					 (BufSize = fread(buf, 1, BufSize, rlog)) != 0;
					 Sum += BufSize, fwrite(buf, 1, BufSize, wlog)) {}

				Sum = fresize(wlog, Sum);
				fclose(wlog);
				fclose(rlog);
				NFREE(buf);
				if (!freopen(glLogFile, "a", stderr)) {
					LOG_ERROR("re-open error while truncating log", NULL);
				}
			}
		}

		// try to detect IP change when not forced
		if (inet_addr(glBinding) == INADDR_NONE) {
			struct in_addr host;
			host = get_interface(!strchr(glBinding, '?') ? glBinding : NULL, NULL, NULL);
			if (host.s_addr != INADDR_NONE && host.s_addr != glHost.s_addr) {
				LOG_INFO("IP change detected %s", inet_ntoa(glHost));
				Stop(false);
				glMainRunning = true;
				Start(false);
			}
		}
	}

	return NULL;
}

/*----------------------------------------------------------------------------*/
static bool AddMRDevice(struct sMR *Device, char *UDN, IXML_Document *DescDoc, const char *location) {
	char *friendlyName = NULL;
	uint32_t now = gettime_ms();

	// read parameters from default then config file
	memcpy(&Device->Config, &glMRConfig, sizeof(tMRConfig));
	LoadMRConfig(glConfigID, UDN, &Device->Config);

	if (!Device->Config.Enabled) return false;

	// Read key elements from description document (NB: glMRConfig is fully initialized, including strings)
	friendlyName = XMLGetFirstDocumentItem(DescDoc, "friendlyName", true);
	if (!friendlyName || !*friendlyName) friendlyName = strdup(UDN);

	LOG_SDEBUG("UDN:\t%s\nFriendlyName:\t%s", UDN,  friendlyName);

	Device->Magic 		= MAGIC;
	Device->RaopState	= RAOP_STOP;
	Device->State 		= STOPPED;
	Device->LastSeen	= now / 1000;
	Device->VolumeStampRx = Device->VolumeStampTx = now - 2000;
	Device->ExpectStop 	= false;
	Device->TimeOut 	= false;
	Device->WaitCookie 	= Device->StartCookie = NULL;
	Device->Raop 		= NULL;
	Device->Elapsed		= 0;
	Device->seqN		= NULL;
	Device->TrackPoll 	= Device->StatePoll = 0;
	Device->Volume 		= 0;
	Device->Actions 	= NULL;
	Device->Master		= NULL;

	strcpy(Device->UDN, UDN);
	strcpy(Device->DescDocURL, location);

	memset(&Device->MetaData, 0, sizeof(Device->MetaData));
	memset(&Device->Service, 0, sizeof(struct sService) * NB_SRV);

	/* find the different services */
	for (int i = 0; i < NB_SRV; i++) {
		char *ServiceId = NULL, *ServiceType = NULL;
		char *EventURL = NULL, *ControlURL = NULL, *ServiceURL = NULL;

		strcpy(Device->Service[i].Id, "");
		if (XMLFindAndParseService(DescDoc, location, cSearchedSRV[i].name, &ServiceType, &ServiceId, &EventURL, &ControlURL, &ServiceURL)) {
			struct sService *s = &Device->Service[cSearchedSRV[i].idx];
			LOG_SDEBUG("\tservice [%s] %s %s, %s, %s", cSearchedSRV[i].name, ServiceType, ServiceId, EventURL, ControlURL);

			strncpy(s->Id, ServiceId, RESOURCE_LENGTH-1);
			strncpy(s->ControlURL, ControlURL, RESOURCE_LENGTH-1);
			strncpy(s->EventURL, EventURL, RESOURCE_LENGTH - 1);
			strncpy(s->Type, ServiceType, RESOURCE_LENGTH - 1);
			s->TimeOut = cSearchedSRV[i].TimeOut;
		}

		NFREE(ServiceId);
		NFREE(ServiceType);
		NFREE(EventURL);
		NFREE(ControlURL);
		NFREE(ServiceURL);
	}

	Device->Master = GetMaster(Device, &friendlyName);
	Device->Volume = CtrlGetVolume(Device);

	// set remaining items now that we are sure
	if (*Device->Service[TOPOLOGY_IDX].ControlURL) {
		Device->MetaData.duration = 1;
		Device->MetaData.title = "Streaming from AirConnect";
	} else {
		Device->MetaData.remote_title = "Streaming from AirConnect";
    }
	if (*Device->Config.ArtWork) Device->MetaData.artwork = Device->Config.ArtWork;

	Device->Running = true;
	// string is already zero-terminated
	if (friendlyName) strncpy(Device->friendlyName, friendlyName, sizeof(Device->friendlyName) - 1);
	if (!*Device->Config.Name) sprintf(Device->Config.Name, "%s+", friendlyName);
	queue_init(&Device->ActionQueue, false, NULL);

	// set protocolinfo (will be used for some HTTP response)
	if (!strcasecmp(Device->Config.Codec, "pcm")) Device->ProtocolInfo = Device->Config.ProtocolInfo.pcm;
	else if (!strcasecmp(Device->Config.Codec, "wav")) Device->ProtocolInfo = Device->Config.ProtocolInfo.wav;
	else if (strcasestr(Device->Config.Codec, "mp3")) Device->ProtocolInfo = Device->Config.ProtocolInfo.mp3;
	else Device->ProtocolInfo = Device->Config.ProtocolInfo.flac;

	if (!memcmp(Device->Config.mac, "\0\0\0\0\0\0", 6)) {
		char ip[32];
		uint32_t mac_size = 6;

		sscanf(location, "http://%[^:]", ip);
		if (SendARP(inet_addr(ip), INADDR_ANY, Device->Config.mac, &mac_size)) {
			*(uint32_t*) (Device->Config.mac + 2) = hash32(Device->UDN);
			LOG_INFO("[%p]: creating MAC", Device);
		}
		memset(Device->Config.mac, 0xbb, 2);
	}

	// make sure MAC is unique	
	for (int i = 0; i < glMaxDevices; i++) {
		if (glMRDevices[i].Running && Device != glMRDevices + i && !memcmp(&glMRDevices[i].Config.mac, &Device->Config.mac, 6)) {
			memset(Device->Config.mac, 0xcc, 2);
			*(uint32_t*)(Device->Config.mac + 2) = hash32(Device->UDN);
			LOG_INFO("[%p]: duplicated mac ... updating", Device);
		}
	}

	if (Device->Master) {
		LOG_INFO("[%p] skipping Sonos slave %s", Device, friendlyName);
	} else {
		LOG_INFO("[%p]: adding renderer (%s) with mac %hX%X", Device, friendlyName, *(uint16_t*)Device->Config.mac, *(uint32_t*)(Device->Config.mac + 2));
	}

	NFREE(friendlyName);
	pthread_create(&Device->Thread, NULL, &MRThread, Device);

	/* subscribe here, not before */
	for (int i = 0; i < NB_SRV; i++) if (Device->Service[i].TimeOut)
		UpnpSubscribeAsync(glControlPointHandle, Device->Service[i].EventURL,
						   Device->Service[i].TimeOut, MasterHandler,
						   (void*) strdup(UDN));

	return (Device->Master == NULL);
}

/*----------------------------------------------------------------------------*/
bool isExcluded(char *Model, char *ModelNumber) {
	char item[STR_LEN];
	char *p = glExcluded;
	char *q = glExcludedModelNumber;
	char *o = glIncludedModelNumbers;

	if (glIncludedModelNumbers) {
		if (!ModelNumber) {
			if (strcasestr(glIncludedModelNumbers, "<NULL>")) return false;
			else return true;
		}
		do {
			sscanf(o, "%[^,]", item);
			if (!strcmp(ModelNumber, item)) return false;
			o += strlen(item);
		} while (*o++);
		return true;
	}

	if (glExcluded && Model) {
		do {
			sscanf(p, "%[^,]", item);
			if (strcasestr(Model, item)) return true;
			p += strlen(item);
		} while (*p++);
	}

	if (glExcludedModelNumber && ModelNumber) {
	    do {
		    sscanf(q, "%[^,]", item);
			if (strcasestr(ModelNumber, item)) return true;
		    q += strlen(item);
	    } while (*q++);
	}

	return false;
}

/*----------------------------------------------------------------------------*/
static bool Start(bool cold) {
	char addr[128] = "";

	// sscanf does not capture empty strings
	if (!strchr(glBinding, '?') && !sscanf(glBinding, "%[^:]:%hu", addr, &glPort)) sscanf(glBinding, ":%hu", &glPort);
	
	char* iface = NULL;
	glHost = get_interface(addr, &iface, NULL);

	// can't find a suitable interface
	if (glHost.s_addr == INADDR_NONE) {
		NFREE(iface);
		return false;
	}

	UpnpSetLogLevel(UPNP_CRITICAL);
	// only set iface name if it's a name
	int rc = UpnpInit2(iface, glPort);

	LOG_INFO("Binding to iface %s@%s:%hu", iface, inet_ntoa(glHost), glPort);
	NFREE(iface);
	
	if (rc != UPNP_E_SUCCESS) {
		LOG_ERROR("UPnP init in %s (%s) failed: %d", inet_ntoa(glHost), addr, rc);
		goto Error;
	}

	UpnpSetMaxContentLength(60000);
	glPort = UpnpGetServerPort();

	if (cold) {
		// manually load openSSL symbols to accept multiple versions
		if (!cross_ssl_load()) {
			LOG_ERROR("Cannot load SSL libraries", NULL);
			goto Error;
		}

		// mutex should *always* be valid
		glMRDevices = calloc(glMaxDevices, sizeof(struct sMR));
		for (int i = 0; i < glMaxDevices; i++) pthread_mutex_init(&glMRDevices[i].Mutex, 0);

		/* start the main thread */
		pthread_create(&glMainThread, NULL, &MainThread, NULL);
	}

	pthread_mutex_init(&glUpdateMutex, 0);
	pthread_cond_init(&glUpdateCond, 0);
	queue_init(&glUpdateQueue, true, FreeUpdate);
	pthread_create(&glUpdateThread, NULL, &UpdateThread, NULL);

	rc = UpnpRegisterClient(MasterHandler, NULL, &glControlPointHandle);
	if (rc != UPNP_E_SUCCESS) {
		LOG_ERROR("Error registering ControlPoint: %d", rc);
		goto Error;
	}

	char hostname[STR_LEN];
	gethostname(hostname, sizeof(hostname));
	strcat(hostname, ".local");
	if ((glmDNSServer = mdnsd_start(glHost, false)) == NULL) goto Error;
	mdnsd_set_hostname(glmDNSServer, hostname, glHost);

	for (int i = 0; i < glMRConfig.UPnPMax; i++) {
		char SearchTopic[sizeof(MEDIA_RENDERER)+2];
		snprintf(SearchTopic, sizeof(SearchTopic), "%s:%i", MEDIA_RENDERER, i + 1);
		UpnpSearchAsync(glControlPointHandle, DISCOVERY_TIME, SearchTopic, NULL);
	}

	return true;

Error:
	UpnpFinish();
	return false;

}

/*----------------------------------------------------------------------------*/
static bool Stop(bool exit) {
	int i;

	glMainRunning = false;

	if (glHost.s_addr != INADDR_ANY) {
		// once main is finished, no risk to have new players created
		LOG_INFO("terminate update thread ...", NULL);
		pthread_cond_signal(&glUpdateCond);
		pthread_join(glUpdateThread, NULL);

		// remove devices and make sure that they are stopped to avoid libupnp lock
		LOG_INFO("flush renderers ...", NULL);
		FlushMRDevices();

		LOG_INFO("terminate libupnp", NULL);
		UpnpUnRegisterClient(glControlPointHandle);
		UpnpFinish();

		pthread_mutex_destroy(&glUpdateMutex);
		pthread_cond_destroy(&glUpdateCond);

		// remove discovered items
		queue_flush(&glUpdateQueue);

		// stop broadcasting devices
		mdnsd_stop(glmDNSServer);
	} else {
		LOG_INFO("terminate libupnp", NULL);
		UpnpFinish();
	}

	if (exit) {
		// simple log size management thread
		LOG_INFO("terminate main thread ...", NULL);
		crossthreads_wake();
		pthread_join(glMainThread, NULL);

		// these are for sure unused now that libupnp cannot signal anything
		for (i = 0; i < glMaxDevices; i++) pthread_mutex_destroy(&glMRDevices[i].Mutex);

		if (glConfigID) ixmlDocument_free(glConfigID);
		netsock_close();
		cross_ssl_free();
	}

	return true;
}

/*---------------------------------------------------------------------------*/
static void sighandler(int signum) {
	if (!glGracefullShutdown) {
		for (int i = 0; i < glMaxDevices; i++) {
			struct sMR *p = &glMRDevices[i];
			if (p->Running && p->State == PLAYING) AVTStop(p);
		}
		LOG_INFO("forced exit", NULL);
		exit(0);
	}

	Stop(true);
	exit(0);
}

/*---------------------------------------------------------------------------*/
bool ParseArgs(int argc, char **argv) {
	char *optarg = NULL;
	int optind = 1;
	char cmdline[256] = "";

	for (int i = 0; i < argc && (strlen(argv[i]) + strlen(cmdline) + 2 < sizeof(cmdline)); i++) {
		strcat(cmdline, argv[i]);
		strcat(cmdline, " ");
	}

	while (optind < argc && strlen(argv[optind]) >= 2 && argv[optind][0] == '-') {
		char *opt = argv[optind] + 1;
		if (strstr("abxdpifmnolcug", opt) && optind < argc - 1) {
			optarg = argv[optind + 1];
			optind += 2;
		} else if (strstr("tzZIkr", opt) || opt[0] == '-') {
			optarg = NULL;
			optind += 1;
		} else {
			printf("%s", usage);
			return false;
		}

		switch (opt[0]) {
		case 'b':
			strcpy(glBinding, optarg);
			break;
		case 'a':
			sscanf(optarg, "%hu:%hu", &glPortBase, &glPortRange);
			break;
		case 'f':
			glLogFile = optarg;
			break;
		case 'c':
			strcpy(glMRConfig.Codec, optarg);
			break;
		case 'u':
			glMRConfig.UPnPMax = atoi(optarg);
			break;
		case 'i':
			strcpy(glConfigName, optarg);
			glDiscovery = true;
			break;
		case 'I':
			glAutoSaveConfigFile = true;
			break;
		case 'p':
			glPidFile = optarg;
			break;
		case 'Z':
			glInteractive = false;
			break;
		case 'k':
			glGracefullShutdown = false;
			break;
		case 'r':
			glMRConfig.Drift = true;
			break;
		case 'm':
			glExcluded = optarg;
			break;
		case 'n':
			glExcludedModelNumber = optarg;
			break;
		case 'o':
			glIncludedModelNumbers = optarg;
			break;
		case 'l':
			strcpy(glMRConfig.Latency, optarg);
			break;
		case 'g':
			glMRConfig.HTTPLength = atoi(optarg);
			break;
#if LINUX || FREEBSD
		case 'z':
			glDaemonize = true;
			break;
#endif
		case 'd':
			{
				char *l = strtok(optarg, "=");
				char *v = strtok(NULL, "=");
				log_level new = lWARN;
				if (l && v) {
					if (!strcmp(v, "error"))  new = lERROR;
					if (!strcmp(v, "warn"))   new = lWARN;
					if (!strcmp(v, "info"))   new = lINFO;
					if (!strcmp(v, "debug"))  new = lDEBUG;
					if (!strcmp(v, "sdebug")) new = lSDEBUG;
					if (!strcmp(l, "all") || !strcmp(l, "raop")) 	  	raop_loglevel = new;
					if (!strcmp(l, "all") || !strcmp(l, "main"))     	main_loglevel = new;
					if (!strcmp(l, "all") || !strcmp(l, "util"))    	util_loglevel = new;
					if (!strcmp(l, "all") || !strcmp(l, "upnp"))    	upnp_loglevel = new;
				}
				else {
					printf("%s", usage);
					return false;
				}
			}
			break;
		case 't':
			printf("%s", license);
			return false;
		case '-':
			if (!strcmp(opt + 1, "noflush")) glMRConfig.Flush = false;
			break;
		default:
			break;
		}
	}

	return true;
}

/*----------------------------------------------------------------------------*/
/*																			  */
/*----------------------------------------------------------------------------*/
int main(int argc, char *argv[]) {
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);
#if defined(SIGQUIT)
	signal(SIGQUIT, sighandler);
#endif
#if defined(SIGHUP)
	signal(SIGHUP, sighandler);
#endif
#if defined(SIGPIPE)
	signal(SIGPIPE, SIG_IGN);
#endif

	netsock_init();

	// first try to find a config file on the command line
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-x")) {
			strcpy(glConfigName, argv[i+1]);
		}
	}

	// load config from xml file
	glConfigID = (void*) LoadConfig(glConfigName, &glMRConfig);

	// potentially overwrite with some cmdline parameters
	if (!ParseArgs(argc, argv)) exit(1);

	// make sure port range is correct
	if (glPortBase && !glPortRange) glPortRange = glMaxDevices*4;

	if (glLogFile) {
		if (!freopen(glLogFile, "a", stderr)) {
			fprintf(stderr, "error opening logfile %s: %s\n", glLogFile, strerror(errno));
		}
	}

	LOG_WARN("Starting airupnp version: %s", VERSION);

	if (strtod("0.30", NULL) != 0.30) {
		LOG_WARN("weird GLIBC, try -static build in case of failure");
	}

	if (!glConfigID) {
		LOG_WARN("no config file, using defaults", NULL);
	}

	// just do discovery and exit
	if (glDiscovery) {
		Start(true);
		sleep(DISCOVERY_TIME + 1);
		Stop(true);
		return(0);
	}

#if LINUX || FREEBSD
	if (glDaemonize) {
		if (daemon(1, glLogFile ? 1 : 0)) {
			fprintf(stderr, "error daemonizing: %s\n", strerror(errno));
		}
	}
#endif

	if (glPidFile) {
		FILE *pid_file;
		pid_file = fopen(glPidFile, "wb");
		if (pid_file) {
			fprintf(pid_file, "%ld", (long) getpid());
			fclose(pid_file);
		}
		else {
			LOG_ERROR("Cannot open PID file %s", glPidFile);
		}
	}

	if (!Start(true)) {
		LOG_ERROR("Cannot start", NULL);
		exit(1);
	}

	for (char resp[20] = ""; strcmp(resp, "exit");) {
#if LINUX || FREEBSD || SUNOS
		if (!glDaemonize && glInteractive)
			(void)! scanf("%s", resp);
		else
			pause();
#else
		if (glInteractive)
			(void)! scanf("%s", resp);
		else
#if OSX
			pause();
#else
			Sleep(INFINITE);
#endif
#endif
		char level[20];

		if (!strcmp(resp, "raopdbg"))	{
			(void)! scanf("%s", level);
			raop_loglevel = debug2level(level);
		}

		if (!strcmp(resp, "maindbg"))	{
			(void)! scanf("%s", level);
			main_loglevel = debug2level(level);
		}

		if (!strcmp(resp, "utildbg"))	{
			(void)! scanf("%s", level);
			util_loglevel = debug2level(level);
		}

		if (!strcmp(resp, "upnpdbg"))	{
			(void)! scanf("%s", level);
			upnp_loglevel = debug2level(level);
		}

		if (!strcmp(resp, "save"))	{
			char name[128];
			(void)! scanf("%s", name);
			SaveConfig(name, glConfigID, true);
		}

		if (!strcmp(resp, "dump") || !strcmp(resp, "dumpall"))	{
			uint32_t now = gettime_ms() / 1000;
			bool all = !strcmp(resp, "dumpall");

			for (int i = 0; i < glMaxDevices; i++) {
				struct sMR *p = &glMRDevices[i];
				bool Locked = pthread_mutex_trylock(&p->Mutex);

				if (!Locked) pthread_mutex_unlock(&p->Mutex);
				if (!p->Running && !all) continue;
				printf("%20.20s [r:%u] [l:%u] [s:%u] Last:%u eCnt:%u\n",
						p->Config.Name, p->Running, Locked, p->State,
						now - p->LastSeen, p->ErrorCount);
			}
		}

	};

	// must be protected in case this interrupts a UPnPEventProcessing
	LOG_INFO("stopping devices ...", NULL);
	Stop(true);
	LOG_INFO("all done", NULL);

	return true;
}
