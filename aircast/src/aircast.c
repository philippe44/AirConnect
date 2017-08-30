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

#define VERSION "v0.0.2.4"" ("__DATE__" @ "__TIME__")"

/*
TODO :
- for no pause, the solution will be to send the elapsed time to LMS through CLI so that it does take care of the seek
- samplerate management will have to be reviewed when decode will be used
*/

/*----------------------------------------------------------------------------*/
/* globals initialized */
/*----------------------------------------------------------------------------*/

#if LINUX || FREEBSD
bool				glDaemonize = false;
#endif
bool				glInteractive = true;
s32_t				glLogLimit = -1;
static char			*glPidFile = NULL;
static char			*glSaveConfigFile = NULL;
bool				glAutoSaveConfigFile = false;
bool				glGracefullShutdown = true;
char 				glInterface[16] = "?";
void				*glConfigID = NULL;
char				glConfigName[_STR_LEN_] = "./config.xml";
static bool			glDiscovery = false;
u32_t				glScanInterval = SCAN_INTERVAL;
u32_t				glScanTimeout = SCAN_TIMEOUT;
struct mdnsd*		glmDNSServer = NULL;

log_level	main_loglevel = lINFO;
log_level	raop_loglevel = lINFO;
log_level	util_loglevel = lWARN;
log_level	cast_loglevel = lINFO;

tMRConfig			glMRConfig = {
							true,	// enabled
							false,	// stop_receiver
							"",
							true,
							true,
							3,      // remove_count
							true, 	// use_flac
							0.5,	// media volume (0..1)
							{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
							0,		// rtp_latency (0 = use client's request)
					};

/*----------------------------------------------------------------------------*/
/* globals */
/*----------------------------------------------------------------------------*/
static pthread_t 	glMainThread;
struct sMR			glMRDevices[MAX_RENDERERS];
char				*glLogFile;
static int 			gl_mDNSId;
static struct in_addr glHost;
char				*glHostName = NULL;


/*----------------------------------------------------------------------------*/
/* consts or pseudo-const*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* locals */
/*----------------------------------------------------------------------------*/

static log_level 	*loglevel = &main_loglevel;
pthread_t			glUpdateMRThread;
static bool			glMainRunning = true;

static char usage[] =
			VERSION "\n"
		   "See -t for license terms\n"
		   "Usage: [options]\n"
		   "  -b <address>\t\tnetwork address to bind to\n"
		   "  -x <config file>\tread config from file (default is ./config.xml)\n"
		   "  -i <config file>\tdiscover players, save <config file> and exit\n"
		   "  -I \t\t\tauto save config at every network scan\n"
   		   "  -l <rtp>\t\tset RTP latency (ms)\n"
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
static void *UpdateMRThread(void *args);
static bool  AddCastDevice(struct sMR *Device, char *Name, char *UDN, bool Group, struct in_addr ip, u16_t port);
void 		 RemoveCastDevice(struct sMR *Device);

void callback(void *owner, raop_event_t event, void *param)
{
	struct sMR *device = (struct sMR*) owner;

	// need to use a mutex as PLAY comes from another thread than the others
	pthread_mutex_lock(&device->Mutex);

	switch (event) {
		case RAOP_STREAM:
			// a PLAY will come later, so we'll do the load at that time
			LOG_INFO("[%p]: Stream", device);
			device->RaopState = event;
			break;
		case RAOP_STOP:
			LOG_INFO("[%p]: Stop", device);
			if (device->RaopState == RAOP_PLAY) {
				CastStop(device->CastCtx);
				device->ExpectStop = true;
			}
			device->RaopState = event;
			break;
		case RAOP_FLUSH:
			LOG_INFO("[%p]: Flush", device);
			CastStop(device->CastCtx);
			device->ExpectStop = true;
			device->RaopState = event;
			break;
		case RAOP_PLAY:
			LOG_INFO("[%p]: Play", device);
			if (device->RaopState != RAOP_PLAY) {
				char *uri;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
				asprintf(&uri, "http://%s:%u/stream", inet_ntoa(glHost), *((short unsigned*) param));
#pragma GCC diagnostic pop
				CastLoad(device->CastCtx, uri, "audio/flac", NULL);
				free(uri);
			}
			CastPlay(device->CastCtx);
			CastSetDeviceVolume(device->CastCtx, device->Volume, true);
			device->RaopState = event;
			break;
		case RAOP_VOLUME: {
			device->Volume = *((double*) param);
			CastSetDeviceVolume(device->CastCtx, device->Volume, false);
			LOG_INFO("[%p]: Volume[0..1] %0.4lf", device, device->Volume);
			break;
		}
		default:
			break;
	}

	pthread_mutex_unlock(&device->Mutex);
}


/*----------------------------------------------------------------------------*/
#define TRACK_POLL  (1000)
#define MAX_ACTION_ERRORS (5)
static void *MRThread(void *args)
{
	int elapsed;
	unsigned last;
	struct sMR *p = (struct sMR*) args;
	json_t *data;
	double Volume = -1;

	last = gettime_ms();

	while (p->Running) {
		data = GetTimedEvent(p->CastCtx, 500);
		elapsed = gettime_ms() - last;
		pthread_mutex_lock(&p->Mutex);

		LOG_SDEBUG("Cast thread timer %d", elapsed);

		// a message has been received
		if (data) {
			json_t *val = json_object_get(data, "type");
			const char *type = json_string_value(val);

			// a mediaSessionId has been acquired
			if (type && !strcasecmp(type, "MEDIA_STATUS")) {
				const char *state = GetMediaItem_S(data, 0, "playerState");

				if (state && (!strcasecmp(state, "PLAYING") || !strcasecmp(state, "BUFFERING")) && p->State != PLAYING) {
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
			if (Volume != -1 && fabs(Volume - p->Volume) >= 0.01)
			{
				LOG_INFO("[%p]: Volume local change %0.4lf", p, Volume);
				raop_notify(p->Raop, RAOP_VOLUME, &Volume);
				Volume = -1;
			}

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
static bool RefreshTO(char *UDN)
{
	int i;

	for (i = 0; i < MAX_RENDERERS; i++) {
		if (glMRDevices[i].InUse && !strcmp(glMRDevices[i].UDN, UDN)) {
			glMRDevices[i].TimeOut = false;
			glMRDevices[i].MissingCount = glMRDevices[i].Config.RemoveCount;
			return true;
		}
	}
	return false;
}


/*----------------------------------------------------------------------------*/
char *GetmDNSAttribute(struct mDNSItem_s *p, char *name)
{
	int j;

	for (j = 0; j < p->attr_count; j++)
		if (!strcasecmp(p->attr[j].name, name))
			return strdup(p->attr[j].value);

	return NULL;
}


/*----------------------------------------------------------------------------*/
static void *UpdateMRThread(void *args)
{
	struct sMR *Device = NULL;
	int i, TimeStamp;
	DiscoveredList DiscDevices;

	LOG_DEBUG("Begin Cast devices update", NULL);
	TimeStamp = gettime_ms();

	if (!glMainRunning) {
		LOG_DEBUG("Aborting ...", NULL);
		return NULL;
	}

	query_mDNS(gl_mDNSId, "_googlecast._tcp.local", &DiscDevices, glScanTimeout);

	for (i = 0; i < DiscDevices.count; i++) {
		char *UDN = NULL, *Name = NULL;
		int j;
		struct mDNSItem_s *p = &DiscDevices.items[i];

		// is the mDNS record usable
		if ((UDN = GetmDNSAttribute(p, "id")) == NULL) continue;

		if (!RefreshTO(UDN)) {
			char *Model;
			bool Group;

			// new device so search a free spot.
			for (j = 0; j < MAX_RENDERERS && glMRDevices[j].InUse; j++);

			// no more room !
			if (j == MAX_RENDERERS) {
				LOG_ERROR("Too many Cast devices", NULL);
				NFREE(UDN);
				break;
			}
			else Device = &glMRDevices[j];

			Name = GetmDNSAttribute(p, "fn");
			if (!Name) Name = strdup(p->hostname);

			// if model is a group, must ignore a few things
			Model = GetmDNSAttribute(p, "md");
			if (Model && !stristr(Model, "Group")) Group = false;
			else Group = true;
			NFREE(Model);

			if (AddCastDevice(Device, Name, UDN, Group, p->addr, p->port) && !glSaveConfigFile) {
				Device->Raop = raop_create(glHost, glmDNSServer, Name, "aircast", Device->Config.mac,
						                    Device->Config.UseFlac, Device->Config.Latency,
											Device, callback);
				if (!Device->Raop) {
					LOG_ERROR("[%p]: cannot create RAOP instance (%s)", Device, Device->FriendlyName);
					RemoveCastDevice(Device);
				}
			}
		}
		else for (j = 0; j < MAX_RENDERERS; j++) {
			if (glMRDevices[j].InUse && !strcmp(glMRDevices[j].UDN, UDN)) {
				UpdateCastDevice(glMRDevices[j].CastCtx, p->addr, p->port);
				break;
			}
		}

		NFREE(UDN);
		NFREE(Name);
	}

	free_discovered_list(&DiscDevices);

	// then walk through the list of devices to remove missing ones
	for (i = 0; i < MAX_RENDERERS; i++) {
		Device = &glMRDevices[i];
		if (!Device->InUse || !Device->Config.RemoveCount) continue;
		if (Device->TimeOut && Device->MissingCount) Device->MissingCount--;
		if (CastIsConnected(Device->CastCtx) || Device->MissingCount) continue;

		LOG_INFO("[%p]: removing renderer (%s)", Device, Device->FriendlyName);

		raop_delete(Device->Raop);
		RemoveCastDevice(Device);
	}

	glDiscovery = true;

	if (glAutoSaveConfigFile && !glSaveConfigFile) {
		LOG_DEBUG("Updating configuration %s", glConfigName);
		SaveConfig(glConfigName, glConfigID, false);
	}

	LOG_DEBUG("End Cast devices update %d", gettime_ms() - TimeStamp);

	return NULL;
}

/*----------------------------------------------------------------------------*/
static void *MainThread(void *args)
{
	unsigned last = gettime_ms();
	int ScanPoll = glScanInterval*1000 + 1;

	while (glMainRunning) {
		int i;
		int elapsed = gettime_ms() - last;

		// reset timeout and re-scan devices
		ScanPoll += elapsed;
		if (glScanInterval && ScanPoll > glScanInterval*1000) {
			pthread_attr_t attr;
			ScanPoll = 0;

			for (i = 0; i < MAX_RENDERERS; i++) {
				glMRDevices[i].TimeOut = true;
				glDiscovery = false;
			}

			pthread_attr_init(&attr);
			pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN + 32*1024);
			pthread_create(&glUpdateMRThread, &attr, &UpdateMRThread, NULL);
			pthread_detach(glUpdateMRThread);
			pthread_attr_destroy(&attr);
		}

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

		last = gettime_ms();
		sleep(1);
	}
	return NULL;
}


/*----------------------------------------------------------------------------*/
void MakeMacUnique(struct sMR *Device)
{
	int i;

	for (i = 0; i < MAX_RENDERERS; i++) {
		if (!glMRDevices[i].InUse || Device == &glMRDevices[i]) continue;
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
	pthread_attr_t attr;

	// read parameters from default then config file
	memset(Device, 0, sizeof(struct sMR));
	memcpy(&Device->Config, &glMRConfig, sizeof(tMRConfig));
	LoadMRConfig(glConfigID, UDN, &Device->Config);
	if (!Device->Config.Enabled) return false;

	pthread_mutex_init(&Device->Mutex, 0);
	strcpy(Device->UDN, UDN);
	Device->Magic = MAGIC;
	Device->TimeOut = false;
	Device->MissingCount = Device->Config.RemoveCount;
	Device->Running = true;
	Device->InUse = true;
	Device->State = STOPPED;
	Device->ExpectStop = false;
	Device->Group = group;
	strcpy(Device->FriendlyName, Name);

	LOG_INFO("[%p]: adding renderer (%s)", Device, Name);

	if (!memcmp(Device->Config.mac, "\0\0\0\0\0\0", mac_size)) {
		if (group || SendARP(ip.s_addr, INADDR_ANY, Device->Config.mac, &mac_size)) {
			u32_t hash = hash32(UDN);

			LOG_ERROR("[%p]: creating MAC %x", Device, Device->FriendlyName, hash);
			memcpy(Device->Config.mac + 2, &hash, 4);
		}
		memset(Device->Config.mac, 0xcc, 2);
	}

	// virtual players duplicate mac address
	MakeMacUnique(Device);

	Device->CastCtx = CreateCastDevice(Device, Device->Group, Device->Config.StopReceiver, ip, port, Device->Config.MediaVolume);

	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN + 32*1024);
	pthread_create(&Device->Thread, &attr, &MRThread, Device);
	pthread_attr_destroy(&attr);

	return true;
}


/*----------------------------------------------------------------------------*/
void FlushCastDevices(void)
{
	int i;

	for (i = 0; i < MAX_RENDERERS; i++) {
		struct sMR *p = &glMRDevices[i];
		if (p->InUse) {
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
	Device->InUse = false;
	pthread_mutex_unlock(&Device->Mutex);
	pthread_join(Device->Thread, NULL);

	DeleteCastDevice(Device->CastCtx);

	pthread_mutex_destroy(&Device->Mutex);
	memset(Device, 0, sizeof(struct sMR));
}

/*----------------------------------------------------------------------------*/
static bool Start(void)
{
	char hostname[_STR_LEN_];

#if WIN
	winsock_init();
#endif

	if (glScanInterval) {
		if (glScanInterval < SCAN_INTERVAL) glScanInterval = SCAN_INTERVAL;
		if (glScanTimeout < SCAN_TIMEOUT) glScanTimeout = SCAN_TIMEOUT;
		if (glScanTimeout > glScanInterval - SCAN_TIMEOUT) glScanTimeout = glScanInterval - SCAN_TIMEOUT;
	}

	memset(&glMRDevices, 0, sizeof(glMRDevices));

	InitSSL();

	// initialize mDNS query
	glHost.s_addr = get_localhost(&glHostName);
	if (!strstr(glInterface, "?")) glHost.s_addr = inet_addr(glInterface);

	LOG_INFO("Binding to %s", inet_ntoa(glHost));

	gl_mDNSId = init_mDNS(false, glHost);

	snprintf(hostname, _STR_LEN_, "%s.local", glHostName);
	if ((glmDNSServer = mdnsd_start(glHost)) == NULL) return false;

	mdnsd_set_hostname(glmDNSServer, hostname, glHost);

	/* start the main thread */
	pthread_create(&glMainThread, NULL, &MainThread, NULL);
	return true;
}

static bool Stop(void)
{
	LOG_DEBUG("flush renderers ...", NULL);
	FlushCastDevices();

	// this forces an ongoing search to end
	close_mDNS(gl_mDNSId);

	// stop broadcasting devices
	mdnsd_stop(glmDNSServer);

	LOG_DEBUG("terminate main thread ...", NULL);
	pthread_join(glMainThread, NULL);

	LOG_DEBUG("terminate SSL ...", NULL);
	EndSSL();

	NFREE(glHostName);

#if WIN
	winsock_close();
#endif

	return true;
}

/*---------------------------------------------------------------------------*/
static void sighandler(int signum) {
	int i;

	glMainRunning = false;

	if (!glGracefullShutdown) {
		for (i = 0; i < MAX_RENDERERS; i++) {
			struct sMR *p = &glMRDevices[i];
			if (p->InUse && p->State == PLAYING) CastStop(p->CastCtx);
		}
		LOG_INFO("forced exit", NULL);
		exit(EXIT_SUCCESS);
	}

	Stop();
	exit(EXIT_SUCCESS);
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
		} else if (strstr("tzZIk", opt)) {
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
			glSaveConfigFile = optarg;
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
		case 'l':
			glMRConfig.Latency = atoi(optarg);
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

#if LINUX || FREEBSD
	if (glDaemonize && !glSaveConfigFile) {
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

	if (!Start()) strcpy(resp, "exit");

	if (glSaveConfigFile) {
		while (!glDiscovery) sleep(1);
		SaveConfig(glSaveConfigFile, glConfigID, true);
	}

	while (strcmp(resp, "exit") && !glSaveConfigFile) {

#if LINUX || FREEBSD
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
	}

	if (glConfigID) ixmlDocument_free(glConfigID);
	glMainRunning = false;
	LOG_INFO("stopping Cast devices ...", NULL);
	Stop();
	LOG_INFO("all done", NULL);

	return true;
}




