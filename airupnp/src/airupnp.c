/*
 * AirUPnP - AirPlay to uPNP gateway
 *
 *	(c) Philippe 2015-2019, philippe_44@outlook.com
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

#include <math.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#if WIN
#include <process.h>
#endif

#include "platform.h"
#include "airupnp.h"
#include "upnpdebug.h"
#include "upnptools.h"
#include "util.h"
#include "avt_util.h"
#include "config_upnp.h"
#include "mr_util.h"
#include "log_util.h"
#include "sslsym.h"

#define VERSION "v0.2.10.0"" ("__DATE__" @ "__TIME__")"

#define	AV_TRANSPORT 			"urn:schemas-upnp-org:service:AVTransport"
#define	RENDERING_CTRL 			"urn:schemas-upnp-org:service:RenderingControl"
#define	CONNECTION_MGR 			"urn:schemas-upnp-org:service:ConnectionManager"
#define TOPOLOGY				"urn:schemas-upnp-org:service:ZoneGroupTopology"
#define GROUP_RENDERING_CTRL	"urn:schemas-upnp-org:service:GroupRenderingControl"

#define DISCOVERY_TIME 		20
#define PRESENCE_TIMEOUT	(DISCOVERY_TIME * 6)

/* for the haters of GOTO statement: I'm not a big fan either, but there are
cases where they make code more leightweight and readable, instead of tons of
if statements. In short function, I use them for loop exit and cleanup instead
of code repeating and break/continue
*/

/*----------------------------------------------------------------------------*/
/* globals																	  */
/*----------------------------------------------------------------------------*/
s32_t  				glLogLimit = -1;
UpnpClient_Handle 	glControlPointHandle;
struct sMR			glMRDevices[MAX_RENDERERS];

log_level	main_loglevel = lINFO;
log_level	raop_loglevel = lINFO;
log_level	util_loglevel = lWARN;
log_level	upnp_loglevel = lINFO;

tMRConfig			glMRConfig = {
							"-3",      	// StreamLength
							true,		// Enabled
							"",      	// Name
							true,		// SendMetaData
							false,		// SendCoverArt
							100,		// MaxVolume
							"flc",	    // Codec
							true,		// Metadata
							"",			// RTP:HTTP Latency (0 = use AirPlay requested)
							false,		// drift
							{0, 0, 0, 0, 0, 0 }, // MAC
							"",			// artwork
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
static const char 	MEDIA_RENDERER[] 	= "urn:schemas-upnp-org:device:MediaRenderer:1";

static const struct cSearchedSRV_s
{
 char 	name[RESOURCE_LENGTH];
 int	idx;
 u32_t  TimeOut;
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
static char				glHostName[_STR_LEN_];
static struct mdnsd*	glmDNSServer = NULL;
static char*			glExcluded = NULL;
static char*			glExcludedModelNumber = NULL;
static char				*glPidFile = NULL;
static bool	 			glAutoSaveConfigFile = false;
static bool				glGracefullShutdown = true;
static bool				glDiscovery = false;
static pthread_mutex_t 	glUpdateMutex;
static pthread_cond_t  	glUpdateCond;
static pthread_t 		glMainThread, glUpdateThread;
static tQueue			glUpdateQueue;
static bool				glInteractive = true;
static char				*glLogFile;
static char				glUPnPSocket[128] = "?";
static u32_t			glPort;
static void				*glConfigID = NULL;
static char				glConfigName[_STR_LEN_] = "./config.xml";

static char usage[] =

			VERSION "\n"
		   "See -t for license terms\n"
		   "Usage: [options]\n"
		   "  -b <server>[:<port>]\tnetwork interface and UPnP port to use \n"
   		   "  -c <mp3[:<rate>]|flc[:0..9]|wav|pcm>\taudio format send to player\n"
		   "  -x <config file>\tread config from file (default is ./config.xml)\n"
		   "  -i <config file>\tdiscover players, save <config file> and exit\n"
		   "  -I \t\t\tauto save config at every network scan\n"
		   "  -l <[rtp][:http][:f]>\tRTP and HTTP latency (ms), ':f' forces silence fill\n"
   		   "  -r \t\t\tlet timing reference drift (no click)\n"
		   "  -f <logfile>\t\twrite debug to logfile\n"
		   "  -p <pid file>\t\twrite PID in file\n"
		   "  -m <name1,name2...>\texclude from search devices whose model name contains name1 or name 2 ...\n"
		   "  -n <name1,name2...>\texclude from search devices whose model number contains name1 or name 2 ...\n"
		   "  -d <log>=<level>\tSet logging level, logs: all|raop|main|util|upnp, level: error|warn|info|debug|sdebug\n"

#if LINUX || FREEBSD
		   "  -z \t\t\tDaemonize\n"
#endif
		   "  -Z \t\t\tNOT interactive\n"
		   "  -k \t\t\tImmediate exit on SIGQUIT and SIGTERM\n"
		   "  -t \t\t\tLicense terms\n"
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
static void *MRThread(void *args)
{
	int elapsed, wakeTimer = MIN_POLL;
	unsigned last;
	struct sMR *p = (struct sMR*) args;

	last = gettime_ms();

	for (; p->Running; WakeableSleep(wakeTimer)) {
		elapsed = gettime_ms() - last;

		// context is valid as long as thread runs
		pthread_mutex_lock(&p->Mutex);
		
		wakeTimer = (p->State != STOPPED) ? MIN_POLL : MIN_POLL * 10;
		LOG_SDEBUG("[%p]: UPnP thread timer %d %d", p, elapsed, wakeTimer);

		p->StatePoll += elapsed;
		p->TrackPoll += elapsed;

		/*
		should not request any status update if we are stopped, off or waiting
		for an action to be performed
		*/
		if ((p->RaopState != RAOP_PLAY && p->State == STOPPED) ||
			 p->ErrorCount > MAX_ACTION_ERRORS ||
			 p->WaitCookie) {
			last = gettime_ms();
			pthread_mutex_unlock(&p->Mutex);
			continue;
		}

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

		last = gettime_ms(),
		pthread_mutex_unlock(&p->Mutex);
	}

	return NULL;
}

/*----------------------------------------------------------------------------*/
void callback(void *owner, raop_event_t event, void *param)
{
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
			LOG_INFO("[%p]: Flush", Device);
			AVTStop(Device);
			Device->RaopState = event;
			Device->ExpectStop = true;
			break;
		case RAOP_PLAY: {
			char *ProtoInfo;
			char *uri, *mp3radio = NULL;

			if (Device->RaopState != RAOP_PLAY) {
				if (!strcasecmp(Device->Config.Codec, "pcm"))
					ProtoInfo = "http-get:*:audio/L16;rate=44100;channels=2:DLNA.ORG_PN=LPCM;DLNA.ORG_OP=00;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=0d500000000000000000000000000000";
				else if (!strcasecmp(Device->Config.Codec, "wav"))
					ProtoInfo = "http-get:*:audio/wav:DLNA.ORG_OP=00;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=0d500000000000000000000000000000";
				else if (stristr(Device->Config.Codec, "mp3")) {
					if (*Device->Service[TOPOLOGY_IDX].ControlURL) {
						mp3radio = "x-rincon-mp3radio://";
						LOG_INFO("[%p]: Sonos live stream", Device);
					}
					ProtoInfo = "http-get:*:audio/mp3:DLNA.ORG_OP=00;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=0d500000000000000000000000000000";

				} else

					ProtoInfo = "http-get:*:audio/flac:DLNA.ORG_OP=00;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=0d500000000000000000000000000000";

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
				asprintf(&uri, "%shttp://%s:%u/stream.%s", mp3radio ? mp3radio : "",
								inet_ntoa(glHost), *((short unsigned*) param),
								Device->Config.Codec);
#pragma GCC diagnostic pop

				AVTSetURI(Device, uri, &Device->MetaData, ProtoInfo);
				NFREE(uri);
			}

			AVTPlay(Device);

			CtrlSetVolume(Device, Device->Volume, Device->seqN++);
			Device->RaopState = event;
			break;
		}
		case RAOP_VOLUME: {
			Device->Volume = *((double*) param) * Device->Config.MaxVolume;
			CtrlSetVolume(Device, Device->Volume, Device->seqN++);
			LOG_INFO("[%p]: Volume[0..100] %d", Device, Device->Volume);
			break;
		}
		default:
			break;
	}

	pthread_mutex_unlock(&Device->Mutex);
}


/*----------------------------------------------------------------------------*/
static bool _ProcessQueue(struct sMR *Device)
{
	struct sService *Service = &Device->Service[AVT_SRV_IDX];
	tAction *Action;
	int rc = 0;

	Device->WaitCookie = 0;
	if ((Action = QueueExtract(&Device->ActionQueue)) == NULL) return false;

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
static void ProcessEvent(Upnp_EventType EventType, void *_Event, void *Cookie)
{
	struct Upnp_Event *Event = (struct Upnp_Event*) _Event;
	struct sMR *Device = SID2Device(Event->Sid);
	IXML_Document *VarDoc = Event->ChangedVariables;
	char  *r = NULL;
	char  *LastChange = NULL;

	// this is async, so need to check context's validity
	if (!CheckAndLock(Device)) return;

	LastChange = XMLGetFirstDocumentItem(VarDoc, "LastChange", true);

	if (!Device->Raop || !LastChange) {
		LOG_SDEBUG("no RAOP device (yet) or not change for %s", Event->Sid);

		pthread_mutex_unlock(&Device->Mutex);

		NFREE(LastChange);

		return;

	}


	// Feedback volume to AirPlay controller

	r = XMLGetChangeItem(VarDoc, "Volume", "channel", "Master", "val");
	if (r) {
		double Volume;
		int GroupVolume = GetGroupVolume(Device);

		Volume = (GroupVolume > 0) ? GroupVolume : atof(r);

		if ((int) Volume != Device->Volume) {
			LOG_INFO("[%p]: UPnP Volume local change %d", Device, (int) Volume);
			Volume /=  Device->Config.MaxVolume;
			raop_notify(Device->Raop, RAOP_VOLUME, &Volume);
		}
	}

	NFREE(r);
	NFREE(LastChange);

	pthread_mutex_unlock(&Device->Mutex);
}


/*----------------------------------------------------------------------------*/
int ActionHandler(Upnp_EventType EventType, void *Event, void *Cookie)
{
	static int recurse = 0;
	struct sMR *p = NULL;

	LOG_SDEBUG("action: %i [%s] [%p] [%u]", EventType, uPNPEvent2String(EventType), Cookie, recurse);
	recurse++;

	switch ( EventType ) {
		case UPNP_CONTROL_ACTION_COMPLETE: 	{
			struct Upnp_Action_Complete *Action = (struct Upnp_Action_Complete *)Event;
			char   *r;

			p = CURL2Device(Action->CtrlUrl);

			if (!CheckAndLock(p)) return 0;

			LOG_SDEBUG("[%p]: ac %i %s (cookie %p)", p, EventType, Action->CtrlUrl, Cookie);

			// If waited action has been completed, proceed to next one if any
			if (p->WaitCookie) {
				const char *Resp = XMLGetLocalName(Action->ActionResult, 1);

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
			if ((r = XMLGetFirstDocumentItem(Action->ActionResult, "CurrentTransportState", true)) != NULL) {
				if (!strcmp(r, "TRANSITIONING") && p->State != TRANSITIONING) {
					p->State = TRANSITIONING;
					LOG_INFO("[%p]: uPNP transition", p);
				} else if (!strcmp(r, "STOPPED") && p->State != STOPPED) {
					if (p->RaopState == RAOP_PLAY && !p->ExpectStop) raop_notify(p->Raop, RAOP_STOP, NULL);
					p->State = STOPPED;
					p->ExpectStop = false;
					LOG_INFO("[%p]: uPNP stopped", p);
				} else if (!strcmp(r, "PLAYING") && (p->State != PLAYING)) {
					p->State = PLAYING;
					if (p->RaopState != RAOP_PLAY) raop_notify(p->Raop, RAOP_PLAY, NULL);
					LOG_INFO("[%p]: uPNP playing", p);
				} else if (!strcmp(r, "PAUSED_PLAYBACK") && p->State != PAUSED) {
					p->State = PAUSED;
					if (p->RaopState == RAOP_PLAY) raop_notify(p->Raop, RAOP_PAUSE, NULL);
					LOG_INFO("[%p]: uPNP pause", p);
				}
			}

			NFREE(r);

			LOG_SDEBUG("Action complete : %i (cookie %p)", EventType, Cookie);

			if (Action->ErrCode != UPNP_E_SUCCESS) {
				p->ErrorCount++;
				LOG_ERROR("Error in action callback -- %d (cookie %p)",	Action->ErrCode, Cookie);
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
int MasterHandler(Upnp_EventType EventType, void *_Event, void *Cookie)
{
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
			struct Upnp_Discovery *Event = (struct Upnp_Discovery *) _Event;
			tUpdate *Update = malloc(sizeof(tUpdate));

			Update->Type = DISCOVERY;
			Update->Data  = strdup(Event->Location);
			QueueInsert(&glUpdateQueue, Update);
			pthread_cond_signal(&glUpdateCond);

			break;
		}
		case UPNP_DISCOVERY_ADVERTISEMENT_BYEBYE: {
			struct Upnp_Discovery *Event = (struct Upnp_Discovery *) _Event;
			tUpdate *Update = malloc(sizeof(tUpdate));

			Update->Type = BYE_BYE;
			Update->Data	 = strdup(Event->DeviceId);
			QueueInsert(&glUpdateQueue, Update);
			pthread_cond_signal(&glUpdateCond);

			break;
		}
		case UPNP_DISCOVERY_SEARCH_TIMEOUT: {
			tUpdate *Update = malloc(sizeof(tUpdate));

			Update->Type = SEARCH_TIMEOUT;
			Update->Data = NULL;
			QueueInsert(&glUpdateQueue, Update);
			pthread_cond_signal(&glUpdateCond);

			// if there is a cookie, it's a targeted Sonos search
			if (!Cookie)
				UpnpSearchAsync(glControlPointHandle, DISCOVERY_TIME, MEDIA_RENDERER, NULL);

			break;
		}
		case UPNP_EVENT_RECEIVED:
			ProcessEvent(EventType, _Event, Cookie);
			break;
		case UPNP_EVENT_SUBSCRIPTION_EXPIRED:
		case UPNP_EVENT_AUTORENEWAL_FAILED: {
			struct Upnp_Event_Subscribe *Event = (struct Upnp_Event_Subscribe *)_Event;
			struct sService *s;
			struct sMR *Device = SID2Device(Event->Sid);

			if (!CheckAndLock(Device)) break;

			s = EventURL2Service(Event->PublisherUrl, Device->Service);
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
			struct Upnp_Event_Subscribe *Event = (struct Upnp_Event_Subscribe *)_Event;
			struct sMR *Device = UDN2Device((char*) Cookie);
			struct sService *s;

			free(Cookie);

			if (!CheckAndLock(Device)) break;

			s = EventURL2Service(Event->PublisherUrl, Device->Service);
			if (s != NULL) {
				if (Event->ErrCode == UPNP_E_SUCCESS) {
					s->Failed = 0;
					strcpy(s->SID, Event->Sid);
					s->TimeOut = Event->TimeOut;
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
static void FreeUpdate(void *_Item)
{
	tUpdate *Item = (tUpdate*) _Item;
	NFREE(Item->Data);
	free(Item);
}


/*----------------------------------------------------------------------------*/
static void *UpdateThread(void *args)
{
	while (glMainRunning) {
		tUpdate *Update;

		pthread_mutex_lock(&glUpdateMutex);
		pthread_cond_wait(&glUpdateCond, &glUpdateMutex);
		pthread_mutex_unlock(&glUpdateMutex);

		for (; glMainRunning && (Update = QueueExtract(&glUpdateQueue)) != NULL; FreeUpdate(Update)) {
			struct sMR *Device;
			int i;
			u32_t now = gettime_ms() / 1000;

			// UPnP end of search timer
			if (Update->Type == SEARCH_TIMEOUT) {

				LOG_DEBUG("Presence checking", NULL);

				for (i = 0; i < MAX_RENDERERS; i++) {
					Device = glMRDevices + i;
					if (Device->Running &&
						((Device->LastSeen + PRESENCE_TIMEOUT) - now > PRESENCE_TIMEOUT	||
						Device->ErrorCount > MAX_ACTION_ERRORS)) {

						pthread_mutex_lock(&Device->Mutex);
						LOG_INFO("[%p]: removing unresponsive player (%s)", Device, Device->Config.Name);
						raop_delete(Device->Raop);
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
				raop_delete(Device->Raop);
				// device's mutex returns unlocked
				DelMRDevice(Device);

			// device keepalive or search response
			} else if (Update->Type == DISCOVERY) {
				IXML_Document *DescDoc = NULL;
				char *UDN = NULL, *ModelName = NULL, *ModelNumber = NULL;
				int i, rc;


				// it's a Sonos group announce, just do a targeted search and exit

				if (strstr(Update->Data, "group_description")) {

					for (i = 0; i < MAX_RENDERERS; i++) {

						Device = glMRDevices + i;
						if (Device->Running && *Device->Service[TOPOLOGY_IDX].ControlURL)
							UpnpSearchAsync(glControlPointHandle, 5, Device->UDN, Device);
					}
					continue;
				}


				// existing device ?
				for (i = 0; i < MAX_RENDERERS; i++) {
					Device = glMRDevices + i;
					if (Device->Running && !strcmp(Device->DescDocURL, Update->Data)) {
						// special case for Sonos: remove non-master players
						if (!isMaster(Device->UDN, &Device->Service[TOPOLOGY_IDX], NULL)) {
							pthread_mutex_lock(&Device->Mutex);
							LOG_INFO("[%p]: remove Sonos slave: %s", Device, Device->Config.Name);
							raop_delete(Device->Raop);
							DelMRDevice(Device);
						} else {
							Device->LastSeen = now;
							LOG_DEBUG("[%p] UPnP keep alive: %s", Device, Device->Config.Name);
						}
						goto cleanup;
					}
				}

				// this can take a very long time, too bad for the queue...
				if ((rc = UpnpDownloadXmlDoc(Update->Data, &DescDoc)) != UPNP_E_SUCCESS) {
					LOG_INFO("Error obtaining description %s -- error = %d\n", Update->Data, rc);
					goto cleanup;
				}

				// not a media renderer but maybe a Sonos group update
				if (!XMLMatchDocumentItem(DescDoc, "deviceType", MEDIA_RENDERER)) {
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
				for (i = 0; i < MAX_RENDERERS && glMRDevices[i].Running; i++);

				// no more room !
				if (i == MAX_RENDERERS) {
					LOG_ERROR("Too many uPNP devices (max:%u)", MAX_RENDERERS);
					goto cleanup;
				}

				Device = &glMRDevices[i];

				if (AddMRDevice(Device, UDN, DescDoc, Update->Data) && !glDiscovery) {
					// create a new AirPlay
					Device->Raop = raop_create(glHost, glmDNSServer, Device->Config.Name,
									   "airupnp", Device->Config.mac, Device->Config.Codec,
									   Device->Config.Metadata, Device->Config.Drift, Device->Config.Latency,
									   Device, callback);
					if (!Device->Raop) {
						LOG_ERROR("[%p]: cannot create RAOP instance (%s)", Device, Device->Config.Name);
						DelMRDevice(Device);
					}
				}

				if (glAutoSaveConfigFile || glDiscovery) {
					LOG_DEBUG("Updating configuration %s", glConfigName);
					SaveConfig(glConfigName, glConfigID, false);
				}
cleanup:
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
static void *MainThread(void *args)
{
	while (glMainRunning) {

		WakeableSleep(30*1000);				
		if (!glMainRunning) break;

		if (glLogFile && glLogLimit != - 1) {
			u32_t size = ftell(stderr);

			if (size > glLogLimit*1024*1024) {
				u32_t Sum, BufSize = 16384;
				u8_t *buf = malloc(BufSize);

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

		// try to detect IP change when auto-detect
		if (strstr(glUPnPSocket, "?")) {
			struct in_addr Host;
			Host.s_addr = get_localhost(NULL);
			if (Host.s_addr != INADDR_ANY && Host.s_addr != glHost.s_addr) {
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
static bool AddMRDevice(struct sMR *Device, char *UDN, IXML_Document *DescDoc, const char *location)
{
	char *friendlyName = NULL;
	int i;
	unsigned long mac_size = 6;
	in_addr_t ip;

	// read parameters from default then config file
	memcpy(&Device->Config, &glMRConfig, sizeof(tMRConfig));
	LoadMRConfig(glConfigID, UDN, &Device->Config);


	if (!Device->Config.Enabled) return false;

	// Read key elements from description document
	friendlyName = XMLGetFirstDocumentItem(DescDoc, "friendlyName", true);
	if (!friendlyName || !*friendlyName) friendlyName = strdup(UDN);

	LOG_SDEBUG("UDN:\t%s\nFriendlyName:\t%s", UDN,  friendlyName);

	Device->ExpectStop 	= false;
	Device->TimeOut 	= false;
	Device->WaitCookie 	= Device->StartCookie = NULL;
	Device->Magic 		= MAGIC;
	Device->Muted		= true;	//assume device is muted
	Device->RaopState	= RAOP_STOP;
	Device->State 		= STOPPED;
	Device->LastSeen	= gettime_ms() / 1000;
	Device->Raop 		= NULL;
	Device->Elapsed		= 0;
	Device->seqN		= NULL;
	Device->TrackPoll 	= Device->StatePoll = 0;
	Device->Volume 		= 0;
	Device->Actions 	= NULL;

	strcpy(Device->UDN, UDN);
	strcpy(Device->DescDocURL, location);

	memset(&Device->MetaData, 0, sizeof(metadata_t));
	memset(&Device->Service, 0, sizeof(struct sService) * NB_SRV);

	/* find the different services */
	for (i = 0; i < NB_SRV; i++) {
		char *ServiceId = NULL, *ServiceType = NULL;
		char *EventURL = NULL, *ControlURL = NULL;

		strcpy(Device->Service[i].Id, "");
		if (XMLFindAndParseService(DescDoc, location, cSearchedSRV[i].name, &ServiceType, &ServiceId, &EventURL, &ControlURL)) {
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
	}

	if (!isMaster(UDN, &Device->Service[TOPOLOGY_IDX], &friendlyName) ) {
		LOG_DEBUG("[%p] skipping Sonos slave %s", Device, friendlyName);
		NFREE(friendlyName);
		return false;
	}

	LOG_INFO("[%p]: adding renderer (%s)", Device, friendlyName);

	// set remaining items now that we are sure
	if (*Device->Service[TOPOLOGY_IDX].ControlURL) Device->MetaData.duration = 1;
	Device->MetaData.title = strdup("Streaming from AirConnect");
	if (*Device->Config.ArtWork) Device->MetaData.artwork = strdup(Device->Config.ArtWork);
	Device->Running 	= true;
	if (!*Device->Config.Name) sprintf(Device->Config.Name, "%s+", friendlyName);
	QueueInit(&Device->ActionQueue, false, NULL);

	ip = ExtractIP(location);
	if (!memcmp(Device->Config.mac, "\0\0\0\0\0\0", mac_size)) {
		if (SendARP(ip, INADDR_ANY, Device->Config.mac, &mac_size)) {
			u32_t hash = hash32(UDN);

			LOG_ERROR("[%p]: cannot get mac %s, creating fake %x", Device, Device->Config.Name, hash);
			memcpy(Device->Config.mac + 2, &hash, 4);
		}
		memset(Device->Config.mac, 0xbb, 2);
	}

	MakeMacUnique(Device);

	NFREE(friendlyName);

	pthread_create(&Device->Thread, NULL, &MRThread, Device);

	/* subscribe here, not before */
	for (i = 0; i < NB_SRV; i++) if (Device->Service[i].TimeOut)
		UpnpSubscribeAsync(glControlPointHandle, Device->Service[i].EventURL,
						   Device->Service[i].TimeOut, MasterHandler,
						   (void*) strdup(UDN));

	return true;
}


/*----------------------------------------------------------------------------*/
bool isExcluded(char *Model, char *ModelNumber)
{
	char item[_STR_LEN_];
	char *p = glExcluded;
	char *q = glExcludedModelNumber;

	if (glExcluded) {
	    do {
		    sscanf(p, "%[^,]", item);
		    if (stristr(Model, item)) return true;
		    p += strlen(item);
	    } while (*p++);
	}
	
	if (glExcludedModelNumber) {
	    do {
		    sscanf(q, "%[^,]", item);
		    if (stristr(ModelNumber, item)) return true;
		    q += strlen(item);
	    } while (*q++);
	}

	return false;
}


/*----------------------------------------------------------------------------*/
static bool Start(bool cold)
{
	char hostname[_STR_LEN_];
	int i, rc;
	char IP[16] = "";


	glHost.s_addr = INADDR_ANY;

	if (!strstr(glUPnPSocket, "?")) sscanf(glUPnPSocket, "%[^:]:%u", IP, &glPort);

	if (!*IP) {
		struct in_addr host;
		host.s_addr = get_localhost(NULL);
		strcpy(IP, inet_ntoa(host));
	}

	UpnpSetLogLevel(UPNP_ALL);
	rc = UpnpInit(IP, glPort);

	if (rc != UPNP_E_SUCCESS) {
		LOG_ERROR("UPnP init failed: %d", rc);
		goto Error;
	}

	UpnpSetMaxContentLength(60000);

	S_ADDR(glHost) = inet_addr(IP);
	gethostname(glHostName, _STR_LEN_);
	if (!glPort) glPort = UpnpGetServerPort();

	LOG_INFO("Binding to %s:%d", IP, glPort);

	if (cold) {
		// manually load openSSL symbols to accept multiple versions
		if (!load_ssl_symbols()) {
			LOG_ERROR("Cannot load SSL libraries", NULL);
			goto Error;
		}

		// initialize MR utils
		InitUtils();

		// mutex should *always* be valid
		memset(&glMRDevices, 0, sizeof(glMRDevices));
		for (i = 0; i < MAX_RENDERERS; i++)	pthread_mutex_init(&glMRDevices[i].Mutex, 0);

		/* start the main thread */
		pthread_create(&glMainThread, NULL, &MainThread, NULL);
	}

	if (glHost.s_addr != INADDR_ANY) {
		pthread_mutex_init(&glUpdateMutex, 0);
		pthread_cond_init(&glUpdateCond, 0);
		QueueInit(&glUpdateQueue, true, FreeUpdate);
		pthread_create(&glUpdateThread, NULL, &UpdateThread, NULL);

		rc = UpnpRegisterClient(MasterHandler, NULL, &glControlPointHandle);

		if (rc != UPNP_E_SUCCESS) {
			LOG_ERROR("Error registering ControlPoint: %d", rc);
			goto Error;
		}

		snprintf(hostname, _STR_LEN_, "%s.local", glHostName);
		if ((glmDNSServer = mdnsd_start(glHost)) == NULL) goto Error;
		mdnsd_set_hostname(glmDNSServer, hostname, glHost);

		UpnpSearchAsync(glControlPointHandle, DISCOVERY_TIME, MEDIA_RENDERER, NULL);
	}

	return true;

Error:
	UpnpFinish();
	return false;

}


/*----------------------------------------------------------------------------*/
static bool Stop(bool exit)
{
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
		QueueFlush(&glUpdateQueue);

		// stop broadcasting devices
		mdnsd_stop(glmDNSServer);
	} else {
		LOG_INFO("terminate libupnp", NULL);
		UpnpFinish();
	}

	if (exit) {
		// simple log size management thread
		LOG_INFO("terminate main thread ...", NULL);
		WakeAll();
		pthread_join(glMainThread, NULL);

		// these are for sure unused now that libupnp cannot signal anything
		for (i = 0; i < MAX_RENDERERS; i++) pthread_mutex_destroy(&glMRDevices[i].Mutex);

		EndUtils();
		
		if (glConfigID) ixmlDocument_free(glConfigID);
#if WIN
		winsock_close();
#endif

		free_ssl_symbols();

	}

	return true;
}

/*---------------------------------------------------------------------------*/
static void sighandler(int signum) {
	int i;

	if (!glGracefullShutdown) {
		for (i = 0; i < MAX_RENDERERS; i++) {
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
	int i;

#define MAXCMDLINE 256
	char cmdline[MAXCMDLINE] = "";

	for (i = 0; i < argc && (strlen(argv[i]) + strlen(cmdline) + 2 < MAXCMDLINE); i++) {
		strcat(cmdline, argv[i]);
		strcat(cmdline, " ");
	}

	while (optind < argc && strlen(argv[optind]) >= 2 && argv[optind][0] == '-') {
		char *opt = argv[optind] + 1;
		if (strstr("bxdpifmnlc", opt) && optind < argc - 1) {
			optarg = argv[optind + 1];
			optind += 2;
		} else if (strstr("tzZIkr", opt)) {
			optarg = NULL;
			optind += 1;
		}
		else {
			printf("%s", usage);
			return false;
		}

		switch (opt[0]) {
		case 'b':
			strcpy(glUPnPSocket, optarg);
			break;
		case 'f':
			glLogFile = optarg;
			break;
		case 'c':
			strcpy(glMRConfig.Codec, optarg);
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
		case 'l':
			strcpy(glMRConfig.Latency, optarg);
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
		default:
			break;
		}
	}

	return true;
}


/*----------------------------------------------------------------------------*/
/*																			  */
/*----------------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
	int i;
	char resp[20] = "";

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

#if WIN
	winsock_init();
#endif

	// first try to find a config file on the command line
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-x")) {
			strcpy(glConfigName, argv[i+1]);
		}
	}

	// load config from xml file
	glConfigID = (void*) LoadConfig(glConfigName, &glMRConfig);

	// potentially overwrite with some cmdline parameters
	if (!ParseArgs(argc, argv)) exit(1);

	if (glLogFile) {
		if (!freopen(glLogFile, "a", stderr)) {
			fprintf(stderr, "error opening logfile %s: %s\n", glLogFile, strerror(errno));
		}
	}

	LOG_ERROR("Starting airupnp version: %s", VERSION);

	if (strtod("0.30", NULL) != 0.30) {
		LOG_ERROR("Wrong GLIBC version, use -static build", NULL);
		exit(1);
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
			fprintf(pid_file, "%d", getpid());
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

	while (strcmp(resp, "exit")) {

#if LINUX || FREEBSD || SUNOS
		if (!glDaemonize && glInteractive)
			i = scanf("%s", resp);
		else
			pause();
#else
		if (glInteractive)
			i = scanf("%s", resp);
		else
#if OSX
			pause();
#else
			Sleep(INFINITE);
#endif
#endif

		if (!strcmp(resp, "raopdbg"))	{
			char level[20];
			i = scanf("%s", level);
			raop_loglevel = debug2level(level);
		}

		if (!strcmp(resp, "maindbg"))	{
			char level[20];
			i = scanf("%s", level);
			main_loglevel = debug2level(level);
		}

		if (!strcmp(resp, "utildbg"))	{
			char level[20];
			i = scanf("%s", level);
			util_loglevel = debug2level(level);
		}

		if (!strcmp(resp, "upnpdbg"))	{
			char level[20];
			i = scanf("%s", level);
			upnp_loglevel = debug2level(level);
		}

		if (!strcmp(resp, "save"))	{
			char name[128];
			i = scanf("%s", name);
			SaveConfig(name, glConfigID, true);
		}

		if (!strcmp(resp, "dump") || !strcmp(resp, "dumpall"))	{
			u32_t now = gettime_ms() / 1000;
			bool all = !strcmp(resp, "dumpall");

			for (i = 0; i < MAX_RENDERERS; i++) {
				struct sMR *p = &glMRDevices[i];
				bool Locked = pthread_mutex_trylock(&p->Mutex);

				if (!Locked) pthread_mutex_unlock(&p->Mutex);
				if (!p->Running && !all) continue;
				printf("%20.20s [r:%u] [l:%u] [s:%u] Last:%u eCnt:%u\n",
						p->Config.Name, p->Running, Locked, p->State,
						now - p->LastSeen, p->ErrorCount);
			}
		}

	}

	// must be protected in case this interrupts a UPnPEventProcessing
	LOG_INFO("stopping devices ...", NULL);
	Stop(true);
	LOG_INFO("all done", NULL);

	return true;
}
