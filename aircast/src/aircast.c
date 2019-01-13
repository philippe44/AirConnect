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

#include <math.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "aircast.h"
#include "log_util.h"
#include "util.h"
#include "cast_util.h"
#include "cast_parse.h"
#include "castitf.h"
#include "mdnssd-itf.h"
#include "mdnsd.h"
#include "raopcore.h"
#include "config_cast.h"

#define VERSION "v0.2.2.6"" ("__DATE__" @ "__TIME__")"

#define DISCOVERY_TIME 20

/*----------------------------------------------------------------------------*/
/* globals initialized */
/*----------------------------------------------------------------------------*/
s32_t		glLogLimit = -1;

log_level	main_loglevel = lINFO;
log_level	raop_loglevel = lINFO;
log_level	util_loglevel = lWARN;
log_level	cast_loglevel = lINFO;

tMRConfig			glMRConfig = {
							true,	// enabled
							false,	// stop_receiver
							"",		// name
							"flc",	// use_flac
							true,	// metadata
							0.5,	// media volume (0..1)
							{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
							"",		// rtp/http_latency (0 = use client's request)
							"", 	// artwork
					};

/*----------------------------------------------------------------------------*/
/* globals */
/*----------------------------------------------------------------------------*/
struct sMR			glMRDevices[MAX_RENDERERS];

/*----------------------------------------------------------------------------*/
/* consts or pseudo-const*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* locals */
/*----------------------------------------------------------------------------*/
static log_level 			*loglevel = &main_loglevel;
#if LINUX || FREEBSD || SUNOS
static bool					glDaemonize = false;
#endif
static bool					glMainRunning = true;
static struct mDNShandle_s	*glmDNSsearchHandle;
static struct in_addr 		glHost;
static pthread_t 			glMainThread, glmDNSsearchThread;
static char					*glLogFile;
static char					*glHostName = NULL;
static bool					glDiscovery = false;
static pthread_mutex_t		glMainMutex;
static pthread_cond_t		glMainCond;
static bool					glInteractive = true;
static char					*glPidFile = NULL;
static bool					glAutoSaveConfigFile = false;
static bool					glGracefullShutdown = true;
static char 				glInterface[16] = "?";
static void					*glConfigID = NULL;
static char					glConfigName[_STR_LEN_] = "./config.xml";
static struct mdnsd*		glmDNSServer = NULL;
static bool 				glDrift = false;

static char usage[] =
			VERSION "\n"
		   "See -t for license terms\n"
		   "Usage: [options]\n"
		   "  -b <address>\t\tnetwork address to bind to\n"
		   "  -x <config file>\tread config from file (default is ./config.xml)\n"
		   "  -i <config file>\tdiscover players, save <config file> and exit\n"
		   "  -I \t\t\tauto save config at every network scan\n"
		   "  -l <[rtp][:http]>\tset RTP and HTTP latency (ms)\n"
		   "  -r \t\t\tlet timing reference drift (no click)\n"
		   "  -f <logfile>\t\tWrite debug to logfile\n"
		   "  -p <pid file>\t\twrite PID in file\n"
		   "  -d <log>=<level>\tSet logging level, logs: all|raop|main|util|cast, level: error|warn|info|debug|sdebug\n"
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
static void *MRThread(void *args);
static bool  AddCastDevice(struct sMR *Device, char *Name, char *UDN, bool Group, struct in_addr ip, u16_t port);
static void  RemoveCastDevice(struct sMR *Device);

void callback(void *owner, raop_event_t event, void *param)
{
	struct sMR *Device = (struct sMR*) owner;

	pthread_mutex_lock(&Device->Mutex);

	// this is async, so player might have been deleted
	if (!Device->Running) {
		LOG_WARN("[%p]: device has been removed", owner);
		pthread_mutex_unlock(&Device->Mutex);
		return;
	}

	switch (event) {
		case RAOP_STREAM:
			// a PLAY will come later, so we'll do the load at that time
			LOG_INFO("[%p]: Stream", Device);
			Device->RaopState = event;
			break;
		case RAOP_STOP:
			LOG_INFO("[%p]: Stop", Device);
			if (Device->RaopState == RAOP_PLAY) {
				CastStop(Device->CastCtx);
				Device->ExpectStop = true;
			}
			Device->RaopState = event;
			break;
		case RAOP_FLUSH:
			LOG_INFO("[%p]: Flush", Device);
			CastStop(Device->CastCtx);
			Device->ExpectStop = true;
			Device->RaopState = event;
			break;
		case RAOP_PLAY: {
			metadata_t MetaData = { "", "", "Streaming from AirConnect",
									"", "", NULL, 0, 0, 0 };

			if (*Device->Config.ArtWork) MetaData.artwork = Device->Config.ArtWork;

			LOG_INFO("[%p]: Play", Device);
			if (Device->RaopState != RAOP_PLAY) {
				char *uri;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
				asprintf(&uri, "http://%s:%u/stream", inet_ntoa(glHost), *((short unsigned*) param));
#pragma GCC diagnostic pop
				CastLoad(Device->CastCtx, uri, !strcasecmp(Device->Config.Codec, "flc") ? "audio/flac" : "audio/wav", &MetaData);
				free(uri);
			}

			CastPlay(Device->CastCtx);

			CastSetDeviceVolume(Device->CastCtx, Device->Volume, true);
			Device->RaopState = event;
			break;
		}
		case RAOP_VOLUME: {
			Device->Volume = *((double*) param);
			CastSetDeviceVolume(Device->CastCtx, Device->Volume, false);
			LOG_INFO("[%p]: Volume[0..1] %0.4lf", Device, Device->Volume);
			break;
		}
		default:
			break;
	}

	pthread_mutex_unlock(&Device->Mutex);
}


/*----------------------------------------------------------------------------*/
#define TRACK_POLL  (1000)
#define MAX_ACTION_ERRORS (5)
static void *MRThread(void *args)
{
	int elapsed;
	unsigned last = gettime_ms();
	struct sMR *p = (struct sMR*) args;
	json_t *data;

	while (p->Running) {
		double Volume = -1;

		// context is valid until this thread ends, no deletion issue
		data = GetTimedEvent(p->CastCtx, 250);
		elapsed = gettime_ms() - last;

		// need to protect against events from CC threads, not from deletion
		pthread_mutex_lock(&p->Mutex);

		LOG_SDEBUG("Cast thread timer %d", elapsed);

		// a message has been received
		if (data) {
			json_t *val = json_object_get(data, "type");
			const char *type = json_string_value(val);

			// a mediaSessionId has been acquired
			if (type && !strcasecmp(type, "MEDIA_STATUS")) {
				const char *state = GetMediaItem_S(data, 0, "playerState");

				if (state && !strcasecmp(state, "PLAYING") && p->State != PLAYING) {
					LOG_INFO("[%p]: Cast playing", p);
					p->State = PLAYING;
					if (p->RaopState != RAOP_PLAY) raop_notify(p->Raop, RAOP_PLAY, NULL);
				}

				if (state && !strcasecmp(state, "PAUSED") && p->State == PLAYING) {
					LOG_INFO("[%p]: Cast pause", p);
					p->State = PAUSED;
					if (p->RaopState == RAOP_PLAY) raop_notify(p->Raop, RAOP_PAUSE, NULL);
				}

				if (state && !strcasecmp(state, "IDLE")  && p->State != STOPPED) {
					const char *cause = GetMediaItem_S(data, 0, "idleReason");
					if (cause && !p->ExpectStop) {
						LOG_INFO("[%p]: Cast stopped by other remote", p);
						if (p->RaopState == RAOP_PLAY) raop_notify(p->Raop, RAOP_STOP, NULL);
						p->ExpectStop = false;
					}
					p->State = STOPPED;
				}
			}

			// check for volume at the receiver level, but only record the change
			if (type && !strcasecmp(type, "RECEIVER_STATUS")) {
				double volume;
				bool muted;

				if (!p->Group && GetMediaVolume(data, 0, &volume, &muted)) {
					if (volume != -1 && !muted && volume != p->Volume) Volume = volume;
				}
			}

			// now apply the volume change if any
			if (Volume != -1 && fabs(Volume - p->Volume) >= 0.01) {
				LOG_INFO("[%p]: Volume local change %0.4lf", p, Volume);
				raop_notify(p->Raop, RAOP_VOLUME, &Volume);
				Volume = -1;
			}

			// always set volume done
			Volume = -1;

			// Cast devices has closed the connection
			if (type && !strcasecmp(type, "CLOSE")) {
				LOG_INFO("[%p]: Cast peer closed connection", p);
				if (p->State != STOPPED) raop_notify(p->Raop, RAOP_STOP, NULL);
				p->State = STOPPED;
			}

			json_decref(data);
		}


		// get track position & CurrentURI
		p->TrackPoll += elapsed;
		if (p->TrackPoll > TRACK_POLL) {
			p->TrackPoll = 0;
			if (p->State != STOPPED) CastGetMediaStatus(p->CastCtx);
		}

		pthread_mutex_unlock(&p->Mutex);
		last = gettime_ms();
	}

	return NULL;
}


/*----------------------------------------------------------------------------*/
char *GetmDNSAttribute(txt_attr_t *p, int count, char *name)
{
	int j;

	for (j = 0; j < count; j++)
		if (!strcasecmp(p[j].name, name))
			return strdup(p[j].value);

	return NULL;
}


/*----------------------------------------------------------------------------*/
static struct sMR *SearchUDN(char *UDN)
{
	int i;

	for (i = 0; i < MAX_RENDERERS; i++) {
		if (glMRDevices[i].Running && !strcmp(glMRDevices[i].UDN, UDN))
			return glMRDevices + i;
	}

	return NULL;
}


/*----------------------------------------------------------------------------*/
bool mDNSsearchCallback(mDNSservice_t *slist, void *cookie, bool *stop)
{
	struct sMR *Device;
	mDNSservice_t *s;

	if (*loglevel == lDEBUG) {
		LOG_DEBUG("----------------- round ------------------", NULL);
		for (s = slist; s && glMainRunning; s = s->next) {
			char *host = strdup(inet_ntoa(s->host));
			LOG_DEBUG("[%s] host %s, srv %s, name %s ", s->expired  ? "EXPIRED" : "ACTIVE",
						host, inet_ntoa(s->addr), s->name);
			free(host);
		}
	}

	/*
	cast groups creation is difficult - as storm of mDNS message is sent during
	master's election and many masters will claim the group then will "retract"
	one by one. The logic below works well if no announce is missed, which is
	not the case under high traffic, so in that case, either the actual master
	is missed and it will be discovered at the next 20s search or some retractions
	are missed and if the group is destroyed right after creation, then it will
	hang around	until the retractations timeout (2mins) - still correct as the
	end result is with the right master and group is ultimately removed, but not
	very user-friendy
	*/

	for (s = slist; s && glMainRunning; s = s->next) {
		char *UDN = NULL, *Name = NULL;
		char *Model;
		bool Group;
		int j;

		// is the mDNS record usable or announce made on behalf
		if ((UDN = GetmDNSAttribute(s->attr, s->attr_count, "id")) == NULL || s->host.s_addr != s->addr.s_addr) continue;

		// is that device already here
		if ((Device = SearchUDN(UDN)) != NULL) {
			// a service is being removed
			if (s->expired) {
				bool Remove = true;
				// groups need to find if the removed service is the master
				if (Device->Group) {
					// there are some other master candidates
					if (Device->GroupMaster->Next) {
						Remove = false;
						// changing the master, so need to update cast params
						if (Device->GroupMaster->Host.s_addr == s->host.s_addr) {
							free(pop_item((list_t**) &Device->GroupMaster));
							UpdateCastDevice(Device->CastCtx, Device->GroupMaster->Host, Device->GroupMaster->Port);
							Remove = false;
						} else {
							struct sGroupMember *Member = Device->GroupMaster;
							while (Member && (Member->Host.s_addr != s->host.s_addr)) Member = Member->Next;
							if (Member) free(remove_item((list_t*) Member, (list_t**) &Device->GroupMaster));
						}
					}
				}
				if (Remove) {
					LOG_INFO("[%p]: removing renderer (%s) %d", Device, Device->Config.Name);
					raop_delete(Device->Raop);
					RemoveCastDevice(Device);
				}
			// device update - when playing ChromeCast update their TXT records
			} else {
				// new master in election, update and put it in the queue
				if (Device->Group && Device->GroupMaster->Host.s_addr != s->addr.s_addr) {
					struct sGroupMember *Member = calloc(1, sizeof(struct sGroupMember));
					Member->Host = s->host;
					Member->Port = s->port;
					push_item((list_t*) Member, (list_t**) &Device->GroupMaster);
				}
				UpdateCastDevice(Device->CastCtx, s->addr, s->port);
			}
			NFREE(UDN);
			continue;
		}

		// disconnect of an unknown device
		if (!s->port && !s->addr.s_addr) {
			LOG_ERROR("Unknown device disconnected %s", s->name);
			NFREE(UDN);
			continue;
		}

		// device creation so search a free spot.
		for (j = 0; j < MAX_RENDERERS && glMRDevices[j].Running; j++);

		// no more room !
		if (j == MAX_RENDERERS) {
			LOG_ERROR("Too many Cast devices", NULL);
			NFREE(UDN);
			break;
		}

		Device = glMRDevices + j;

		// if model is a group
		Model = GetmDNSAttribute(s->attr, s->attr_count, "md");
		if (Model && !stristr(Model, "Group")) Group = false;
		else Group = true;
		NFREE(Model);

		Name = GetmDNSAttribute(s->attr, s->attr_count, "fn");
		if (!Name) Name = strdup(s->hostname);

		if (AddCastDevice(Device, Name, UDN, Group, s->addr, s->port) && !glDiscovery) {
			Device->Raop = raop_create(glHost, glmDNSServer, Device->Config.Name,
										"aircast", Device->Config.mac, Device->Config.Codec,
										Device->Config.Metadata, glDrift, Device->Config.Latency,
										Device, callback);
			if (!Device->Raop) {
				LOG_ERROR("[%p]: cannot create RAOP instance (%s)", Device, Device->Config.Name);
				RemoveCastDevice(Device);
			}
		}

		NFREE(UDN);
		NFREE(Name);
	}

	if (glAutoSaveConfigFile || glDiscovery) {
		LOG_DEBUG("Updating configuration %s", glConfigName);
		SaveConfig(glConfigName, glConfigID, false);
	}

	// we have not released the slist
	return false;
}


/*----------------------------------------------------------------------------*/
static void *mDNSsearchThread(void *args)
{
	// launch the query,
	query_mDNS(glmDNSsearchHandle, "_googlecast._tcp.local", 120,
			   glDiscovery ? DISCOVERY_TIME : 0, &mDNSsearchCallback, NULL);
	return NULL;
}


/*----------------------------------------------------------------------------*/
static void *MainThread(void *args)
{
	while (glMainRunning) {

		pthread_mutex_lock(&glMainMutex);
		pthread_cond_reltimedwait(&glMainCond, &glMainMutex, 30*1000);
		pthread_mutex_unlock(&glMainMutex);

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
					 Sum += BufSize, fwrite(buf, 1, BufSize, wlog));

				Sum = fresize(wlog, Sum);
				fclose(wlog);
				fclose(rlog);
				NFREE(buf);
				if (!freopen(glLogFile, "a", stderr)) {
					LOG_ERROR("re-open error while truncating log", NULL);
				}
			}
		}
	}

	return NULL;
}


/*----------------------------------------------------------------------------*/
void MakeMacUnique(struct sMR *Device)
{
	int i;

	for (i = 0; i < MAX_RENDERERS; i++) {
		if (!glMRDevices[i].Running || Device == &glMRDevices[i]) continue;
		if (!memcmp(&glMRDevices[i].Config.mac, &Device->Config.mac, 6)) {
			u32_t hash = hash32(Device->UDN);

			LOG_INFO("[%p]: duplicated mac ... updating", Device);
			memset(&Device->Config.mac[0], 0xcc, 2);
			memcpy(&Device->Config.mac[0] + 2, &hash, 4);
		}
	}
}


/*----------------------------------------------------------------------------*/
static bool AddCastDevice(struct sMR *Device, char *Name, char *UDN, bool group, struct in_addr ip, u16_t port)
{
	unsigned long mac_size = 6;

	// read parameters from default then config file
	memcpy(&Device->Config, &glMRConfig, sizeof(tMRConfig));
	LoadMRConfig(glConfigID, UDN, &Device->Config);
	if (!Device->Config.Enabled) return false;

	// do not zero-out the structure as the mutex must be preserved
	strcpy(Device->UDN, UDN);
	Device->Magic		= MAGIC;
	Device->Running		= true;
	Device->State 		= STOPPED;
	Device->ExpectStop 	= false;
	Device->Volume 		= Device->Elapsed = Device->TrackPoll = 0;
	Device->CastCtx 	= NULL;
	Device->Raop 		= NULL;
	Device->RaopState	= RAOP_STOP;
	Device->Group 		= group;

	if (group) {
		Device->GroupMaster	= calloc(1, sizeof(struct sGroupMember));
		Device->GroupMaster->Host = ip;
		Device->GroupMaster->Port = port;
	} else Device->GroupMaster = NULL;

	if (!*Device->Config.Name) sprintf(Device->Config.Name, "%s+", Name);

	LOG_INFO("[%p]: adding renderer (%s)", Device, Name);

	if (!memcmp(Device->Config.mac, "\0\0\0\0\0\0", mac_size)) {
		if (group || SendARP(ip.s_addr, INADDR_ANY, Device->Config.mac, &mac_size)) {
			u32_t hash = hash32(UDN);

			LOG_ERROR("[%p]: creating MAC %x", Device, Device->Config.Name, hash);
			memcpy(Device->Config.mac + 2, &hash, 4);
		}
		memset(Device->Config.mac, 0xcc, 2);
	}

	// virtual players duplicate mac address
	MakeMacUnique(Device);

	Device->CastCtx = CreateCastDevice(Device, Device->Group, Device->Config.StopReceiver, ip, port, Device->Config.MediaVolume);

	pthread_create(&Device->Thread, NULL, &MRThread, Device);

	return true;
}


/*----------------------------------------------------------------------------*/
void FlushCastDevices(void)
{
	int i;

	for (i = 0; i < MAX_RENDERERS; i++) {
		struct sMR *p = &glMRDevices[i];
		if (p->Running) {
			raop_delete(p->Raop);
			RemoveCastDevice(p);
		 }
	}
}


/*----------------------------------------------------------------------------*/
void RemoveCastDevice(struct sMR *Device)
{
	pthread_mutex_lock(&Device->Mutex);
	Device->Running = false;
	pthread_mutex_unlock(&Device->Mutex);
	pthread_join(Device->Thread, NULL);

	clear_list((list_t**) &Device->GroupMaster, free);

	// device's thread has ended, so no risk of event generated by CC threads
	// to create some reaction, we can safely terminate CC part here
	DeleteCastDevice(Device->CastCtx);
}

/*----------------------------------------------------------------------------*/
static bool Start(void)
{
	int i;
	char hostname[_STR_LEN_];

	// mutexes must always be valid
	memset(&glMRDevices, 0, sizeof(glMRDevices));
	for (i = 0; i < MAX_RENDERERS; i++) pthread_mutex_init(&glMRDevices[i].Mutex, 0);

	InitSSL();

	glHost.s_addr = get_localhost(&glHostName);
	if (!strstr(glInterface, "?")) glHost.s_addr = inet_addr(glInterface);

	LOG_INFO("Binding to %s", inet_ntoa(glHost));

	snprintf(hostname, _STR_LEN_, "%s.local", glHostName);
	if ((glmDNSServer = mdnsd_start(glHost)) == NULL) return false;

	// initialize mDNS broadcast
	mdnsd_set_hostname(glmDNSServer, hostname, glHost);

	/* start the main thread */
	pthread_mutex_init(&glMainMutex, 0);
	pthread_cond_init(&glMainCond, 0);
	pthread_create(&glMainThread, NULL, &MainThread, NULL);

	/* start the mDNS devices discovery thread */
	glmDNSsearchHandle = init_mDNS(false, glHost);
	pthread_create(&glmDNSsearchThread, NULL, &mDNSsearchThread, NULL);

	return true;
}

static bool Stop(void)
{
	int i;

	glMainRunning = false;

	LOG_DEBUG("terminate search thread ...", NULL);
	// this forces an ongoing search to end
	close_mDNS(glmDNSsearchHandle);
	pthread_join(glmDNSsearchThread, NULL);

	LOG_DEBUG("flush renderers ...", NULL);
	FlushCastDevices();

	// stop broadcasting devices
	mdnsd_stop(glmDNSServer);

	LOG_DEBUG("terminate main thread ...", NULL);
	pthread_cond_signal(&glMainCond);
	pthread_join(glMainThread, NULL);
	pthread_mutex_destroy(&glMainMutex);
	pthread_cond_destroy(&glMainCond);
	for (i = 0; i < MAX_RENDERERS; i++) pthread_mutex_destroy(&glMRDevices[i].Mutex);

	LOG_DEBUG("terminate SSL ...", NULL);
	EndSSL();

	NFREE(glHostName);

	if (glConfigID) ixmlDocument_free(glConfigID);

#if WIN
	winsock_close();
#endif

	return true;
}

/*---------------------------------------------------------------------------*/
static void sighandler(int signum) {
	int i;

	if (!glGracefullShutdown) {
		for (i = 0; i < MAX_RENDERERS; i++) {
			struct sMR *p = &glMRDevices[i];
			if (p->Running && p->State == PLAYING) CastStop(p->CastCtx);
		}
		LOG_INFO("forced exit", NULL);
		exit(0);
	}

	Stop();
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
		if (strstr("bxdpifl", opt) && optind < argc - 1) {
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
		case 'f':
			glLogFile = optarg;
			break;
		case 'b':
			strcpy(glInterface, optarg);
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
			glDrift = true;
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
					if (!strcmp(l, "all") || !strcmp(l, "main")) main_loglevel = new;
					if (!strcmp(l, "all") || !strcmp(l, "util")) util_loglevel = new;
					if (!strcmp(l, "all") || !strcmp(l, "cast")) cast_loglevel = new;
					if (!strcmp(l, "all") || !strcmp(l, "raop")) raop_loglevel = new;
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

	LOG_ERROR("Starting aircast version: %s", VERSION);

	if (strtod("0.30", NULL) != 0.30) {
		LOG_ERROR("Wrong GLIBC version, use -static build", NULL);
		exit(1);
	}

	if (!glConfigID) {
		LOG_WARN("no config file, using defaults", NULL);
	}

	// just do device discovery and exit
	if (glDiscovery) {
		Start();
		sleep(DISCOVERY_TIME + 1);
		Stop();
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
			fprintf(pid_file, "%d", (int) getpid());
			fclose(pid_file);
		}
		else {
			LOG_ERROR("Cannot open PID file %s", glPidFile);
		}
	}

	if (!Start()) {
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

		if (!strcmp(resp, "castdbg"))	{
			char level[20];
			i = scanf("%s", level);
			cast_loglevel = debug2level(level);
		}

		if (!strcmp(resp, "raopdbg"))	{
			char level[20];
			i = scanf("%s", level);
			raop_loglevel = debug2level(level);
		}


		 if (!strcmp(resp, "save"))	{
			char name[128];
			i = scanf("%s", name);
			SaveConfig(name, glConfigID, true);
		}

		if (!strcmp(resp, "dump") || !strcmp(resp, "dumpall"))	{
			bool all = !strcmp(resp, "dumpall");

			for (i = 0; i < MAX_RENDERERS; i++) {
				struct sMR *p = &glMRDevices[i];
				bool Locked = pthread_mutex_trylock(&p->Mutex);

				if (!Locked) pthread_mutex_unlock(&p->Mutex);
				if (!p->Running && !all) continue;
				printf("%20.20s [r:%u] [l:%u] [s:%u]", p->Config.Name, p->Running,
					   Locked, p->State);
				if (p->Group)
					printf(" [m:%p, n:%p]\n", p->GroupMaster,
						   p->GroupMaster ? p->GroupMaster->Next : NULL);
				printf("\n");
			}
		}

	}

	LOG_INFO("stopping Cast devices ...", NULL);
	Stop();
	LOG_INFO("all done", NULL);

	return true;
}




