/*
 *  AirConnect: Chromecast & UPnP to AirPlay
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

#include <stdio.h>

#include "platform.h"

#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/engine.h>

#include "mdns.h"
#include "mdnsd.h"
#include "mdnssd-itf.h"
#include "util.h"
#include "base64.h"
#include "raopcore.h"
#include "hairtunes.h"
#include "dmap_parser.h"
#include "log_util.h"

typedef struct raop_ctx_s {
	struct mdns_service *svc;
	struct mdnsd *svr;
	struct in_addr host;	// IP of bridge
	short unsigned port;    // RTSP port for AirPlay
	int sock;               // socket of the above
	short unsigned hport; 	// HTTP port of audio server where CC can "GET" audio
	struct in_addr peer;	// IP of the iDevice (airplay sender)
	char *latencies;
	bool running;
	encode_t encode;
	bool drift;
	pthread_t thread, search_thread;
	unsigned char mac[6];
	unsigned int volume_stamp;
	struct {
		char *aesiv, *aeskey;
		char *fmtp;
	} rtsp;
	struct hairtunes_s *ht;
	raop_cb_t	callback;
	struct {
		char				DACPid[32], id[32];
		struct in_addr		host;
		u16_t				port;
		struct mDNShandle_s *handle;
	} active_remote;
	void *owner;
} raop_ctx_t;

extern log_level	raop_loglevel;
static log_level 	*loglevel = &raop_loglevel;

static void*	rtsp_thread(void *arg);
static bool 	handle_rtsp(raop_ctx_t *ctx, int sock);

static char*	rsa_apply(unsigned char *input, int inlen, int *outlen, int mode);
static int  	base64_pad(char *src, char **padded);
static void 	hairtunes_cb(void *owner, hairtunes_event_t event);
static void* 	search_remote(void *args);

extern char private_key[];
enum { RSA_MODE_KEY, RSA_MODE_AUTH };

static void on_dmap_string(void *ctx, const char *code, const char *name, const char *buf, size_t len);

/*----------------------------------------------------------------------------*/
struct raop_ctx_s *raop_create(struct in_addr host, struct mdnsd *svr, char *name,
						char *model, unsigned char mac[6], char *codec, bool metadata,
						bool drift,	char *latencies, void *owner, raop_cb_t callback) {
	struct raop_ctx_s *ctx = malloc(sizeof(struct raop_ctx_s));
	struct sockaddr_in addr;
	socklen_t nlen = sizeof(struct sockaddr);
	char *id;
	int i;
	char *txt[] = { NULL, "tp=UDP", "sm=false", "sv=false", "ek=1",
					"et=0,1", "md=0,1,2", "cn=0,1", "ch=2",
					"ss=16", "sr=44100", "vn=3", "txtvers=1",
					NULL };

	if (!ctx) return NULL;

	// make sure we have a clean context
	memset(ctx, 0, sizeof(raop_ctx_t));

	ctx->sock = socket(AF_INET, SOCK_STREAM, 0);
	ctx->callback = callback;
	ctx->latencies = latencies;
	ctx->owner = owner;
	ctx->volume_stamp = gettime_ms() - 1000;
	ctx->drift = drift;
	if (!strcasecmp(codec, "pcm")) ctx->encode.codec = CODEC_PCM;
	else if (!strcasecmp(codec, "wav")) ctx->encode.codec = CODEC_WAV;
	else if (stristr(codec, "mp3")) {
		ctx->encode.codec = CODEC_MP3;
		ctx->encode.mp3.icy = metadata;
		if (strchr(codec, ':')) ctx->encode.mp3.bitrate = atoi(strchr(codec, ':') + 1);
	} else {
		ctx->encode.codec = CODEC_FLAC;
		if (strchr(codec, ':')) ctx->encode.flac.level = atoi(strchr(codec, ':') + 1);
	}

	if (ctx->sock == -1) {
		LOG_ERROR("Cannot create listening socket", NULL);
		free(ctx);
		return NULL;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_addr.s_addr = host.s_addr;
	addr.sin_family = AF_INET;
	addr.sin_port = 0;

	if (bind(ctx->sock, (struct sockaddr *) &addr, sizeof(addr)) < 0 || listen(ctx->sock, 1)) {
		LOG_ERROR("Cannot bind or listen RTSP listener: %s", strerror(errno));
		free(ctx);
		closesocket(ctx->sock);
		return NULL;
	}

	getsockname(ctx->sock, (struct sockaddr *) &addr, &nlen);
	ctx->port = ntohs(addr.sin_port);
	ctx->host = host;

	// set model
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
	asprintf(&(txt[0]), "am=%s", model);
#pragma GCC diagnostic pop
	id = malloc(strlen(name) + 12 + 1 + 1);

	memcpy(ctx->mac, mac, 6);
	for (i = 0; i < 6; i++) sprintf(id + i*2, "%02X", mac[i]);
	// mDNS instance name length cannot be more than 63
	sprintf(id + 12, "@%s", name);
	// Windows snprintf does not add NULL if string is larger than n ...
	if (strlen(id) > 63) id[63] = '\0';

	ctx->svr = svr;
	ctx->svc = mdnsd_register_svc(svr, id, "_raop._tcp.local", ctx->port, NULL, (const char**) txt);

	free(txt[0]);
	free(id);

	ctx->running = true;
	pthread_create(&ctx->thread, NULL, &rtsp_thread, ctx);

	return ctx;
}


/*----------------------------------------------------------------------------*/
void raop_delete(struct raop_ctx_s *ctx) {
	int sock;
	struct sockaddr addr;
	socklen_t nlen = sizeof(struct sockaddr);

	if (!ctx) return;

	ctx->running = false;

	// wake-up thread by connecting socket, needed for freeBSD
	sock = socket(AF_INET, SOCK_STREAM, 0);
	getsockname(ctx->sock, (struct sockaddr *) &addr, &nlen);
	connect(sock, (struct sockaddr*) &addr, sizeof(addr));
	closesocket(sock);

	pthread_join(ctx->thread, NULL);

	hairtunes_end(ctx->ht);

#if WIN
	shutdown(ctx->sock, SD_BOTH);
#else
	shutdown(ctx->sock, SHUT_RDWR);
#endif
	closesocket(ctx->sock);

	// terminate search, but do not reclaim memory of pthread if never launched
	if (ctx->active_remote.handle) {
		close_mDNS(ctx->active_remote.handle);
		pthread_join(ctx->search_thread, NULL);
	}

	NFREE(ctx->rtsp.aeskey);
	NFREE(ctx->rtsp.aesiv);
	NFREE(ctx->rtsp.fmtp);

	mdns_service_remove(ctx->svr, ctx->svc);

	free(ctx);
}


/*----------------------------------------------------------------------------*/
void  raop_notify(struct raop_ctx_s *ctx, raop_event_t event, void *param) {
	struct sockaddr_in addr;
	int sock;
	char *command = NULL;

	switch(event) {
		case RAOP_PAUSE:
			command = strdup("pause");
			break;
		case RAOP_PLAY:
			command = strdup("play");
			break;
		case RAOP_STOP:
			command = strdup("stop");
			break;
		case RAOP_VOLUME: {
			// feedback that is less than aecond old is an echo, ignore it
			if ((ctx->volume_stamp + 1000) - gettime_ms() > 1000) {
				double Volume = *((double*) param);

				Volume = Volume ? (Volume - 1) * 30 : -144;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
				asprintf(&command,"setproperty?dmcp.device-volume=%0.4lf", Volume);
#pragma GCC diagnostic pop
			}
			break;
		}
		default:
			break;
	}

	// no command to send to remote or no remote found yet
	if (!command || !ctx->active_remote.port) {
		NFREE(command);
		return;
	}

	sock = socket(AF_INET, SOCK_STREAM, 0);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = S_ADDR(ctx->active_remote.host);
	addr.sin_port = htons(ctx->active_remote.port);

	if (!connect(sock, (struct sockaddr*) &addr, sizeof(addr))) {
		char *method, *buf, resp[512] = "";
		int len;
		key_data_t headers[4] = { {NULL, NULL} };

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
		asprintf(&method, "GET /ctrl-int/1/%s HTTP/1.0", command);
#pragma GCC diagnostic pop
		kd_add(headers, "Active-Remote", ctx->active_remote.id);
		kd_add(headers, "Connection", "close");

		buf = http_send(sock, method, headers);
		len = recv(sock, resp, 512, 0);
		if (len > 0) resp[len-1] = '\0';
		LOG_INFO("[%p]: sending airplay remote\n%s<== received ==>\n%s", ctx, buf, resp);

		NFREE(method);
		NFREE(buf);
		kd_free(headers);
	}

	free(command);

	closesocket(sock);
}

/*----------------------------------------------------------------------------*/
static void *rtsp_thread(void *arg) {
	raop_ctx_t *ctx = (raop_ctx_t*) arg;
	int  sock = -1;

	while (ctx->running) {
		fd_set rfds;
		struct timeval timeout = {0, 100*1000};
		int n;
		bool res = false;

		if (sock == -1) {
			struct sockaddr_in peer;
			socklen_t addrlen = sizeof(struct sockaddr_in);

			sock = accept(ctx->sock, (struct sockaddr*) &peer, &addrlen);
			ctx->peer.s_addr = peer.sin_addr.s_addr;

			if (sock != -1 && ctx->running) {
				LOG_INFO("got RTSP connection %u", sock);
			} else continue;
		}

		FD_ZERO(&rfds);
		FD_SET(sock, &rfds);

		n = select(sock + 1, &rfds, NULL, NULL, &timeout);

		if (!n) continue;

		if (n > 0) res = handle_rtsp(ctx, sock);

		if (n < 0 || !res) {
			closesocket(sock);
			LOG_INFO("RTSP close %u", sock);
			sock = -1;
		}
	}

	if (sock != -1) closesocket(sock);

	return NULL;
}


/*----------------------------------------------------------------------------*/
static bool handle_rtsp(raop_ctx_t *ctx, int sock)
{
	char *buf = NULL, *body = NULL, method[16] = "";
	key_data_t headers[64], resp[16] = { {NULL, NULL} };
	int len;
	bool success = true;

	if (!http_parse(sock, method, headers, &body, &len)) {
		NFREE(body);
		kd_free(headers);
		return false;
	}

	if (strcmp(method, "OPTIONS")) {
		LOG_INFO("[%p]: received %s", ctx, method);
	}

	if ((buf = kd_lookup(headers, "Apple-Challenge")) != NULL) {
		int n;
		char *buf_pad, *p, *data_b64 = NULL, data[32];

		LOG_INFO("[%p]: challenge %s", ctx, buf);

		// need to pad the base64 string as apple device don't
		base64_pad(buf, &buf_pad);

		p = data + min(base64_decode(buf_pad, data), 32-10);
		p = (char*) memcpy(p, &S_ADDR(ctx->host), 4) + 4;
		p = (char*) memcpy(p, ctx->mac, 6) + 6;
		memset(p, 0, 32 - (p - data));
		p = rsa_apply((unsigned char*) data, 32, &n, RSA_MODE_AUTH);
		n = base64_encode(p, n, &data_b64);

		// remove padding as well (seems to be optional now)
		for (n = strlen(data_b64) - 1; n > 0 && data_b64[n] == '='; data_b64[n--] = '\0');

		kd_add(resp, "Apple-Response", data_b64);

		NFREE(p);
		NFREE(buf_pad);
		NFREE(data_b64);
	}

	if (!strcmp(method, "OPTIONS")) {

		kd_add(resp, "Public", "ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER");

	} else if (!strcmp(method, "ANNOUNCE")) {
		char *padded, *p;

		NFREE(ctx->rtsp.aeskey);
		NFREE(ctx->rtsp.aesiv);
		NFREE(ctx->rtsp.fmtp);

		if ((p = stristr(body, "rsaaeskey")) != NULL) {
			unsigned char *aeskey;
			int len, outlen;

			p = strextract(p, ":", "\r\n");
			base64_pad(p, &padded);
			aeskey = malloc(strlen(padded));
			len = base64_decode(padded, aeskey);
			ctx->rtsp.aeskey = rsa_apply(aeskey, len, &outlen, RSA_MODE_KEY);

			NFREE(p);
			NFREE(aeskey);
			NFREE(padded);
		}

		if ((p = stristr(body, "aesiv")) != NULL) {
			p = strextract(p, ":", "\r\n");
			base64_pad(p, &padded);
			ctx->rtsp.aesiv = malloc(strlen(padded));
			base64_decode(padded, ctx->rtsp.aesiv);

			NFREE(p);
			NFREE(padded);
		}

		if ((p = stristr(body, "fmtp")) != NULL) {
			p = strextract(p, ":", "\r\n");
			ctx->rtsp.fmtp = strdup(p);
			NFREE(p);
		}

		// on announce, search remote
		if ((buf = kd_lookup(headers, "DACP-ID")) != NULL) strcpy(ctx->active_remote.DACPid, buf);
		if ((buf = kd_lookup(headers, "Active-Remote")) != NULL) strcpy(ctx->active_remote.id, buf);

		ctx->active_remote.handle = init_mDNS(false, ctx->host);
		pthread_create(&ctx->search_thread, NULL, &search_remote, ctx);

	} else if (!strcmp(method, "SETUP") && ((buf = kd_lookup(headers, "Transport")) != NULL)) {
		char *p;
		hairtunes_resp_t ht;
		short unsigned tport = 0, cport = 0;

		if ((p = stristr(buf, "timing_port")) != NULL) sscanf(p, "%*[^=]=%hu", &tport);
		if ((p = stristr(buf, "control_port")) != NULL) sscanf(p, "%*[^=]=%hu", &cport);

		ht = hairtunes_init(ctx->peer, ctx->encode, false, ctx->drift, true, ctx->latencies,
							ctx->rtsp.aeskey, ctx->rtsp.aesiv, ctx->rtsp.fmtp,
							cport, tport, ctx, hairtunes_cb);

		ctx->hport = ht.hport;
		ctx->ht = ht.ctx;

		if (cport * tport * ht.cport * ht.tport * ht.aport * ht.hport && ht.ctx) {
			char *transport;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
			asprintf(&transport, "RTP/AVP/UDP;unicast;mode=record;control_port=%u;timing_port=%u;server_port=%u", ht.cport, ht.tport, ht.aport);
#pragma GCC diagnostic pop
			LOG_DEBUG("[%p]: http=(%hu) audio=(%hu:%hu), timing=(%hu:%hu), control=(%hu:%hu)", ctx, ht.hport, 0, ht.aport, tport, ht.tport, cport, ht.cport);
			kd_add(resp, "Transport", transport);
			kd_add(resp, "Session", "DEADBEEF");
			free(transport);
		} else {
			success = false;
			LOG_INFO("[%p]: cannot start session, missing ports", ctx);
		}

	} else if (!strcmp(method, "RECORD")) {
		unsigned short seqno = 0;
		unsigned rtptime = 0;
		char *p;

		if (atoi(ctx->latencies)) {
			char latency[6];
			snprintf(latency, 6, "%u", (atoi(ctx->latencies) * 44100) / 1000);
			kd_add(resp, "Audio-Latency", latency);
		}

		buf = kd_lookup(headers, "RTP-Info");
		if ((p = stristr(buf, "seq")) != NULL) sscanf(p, "%*[^=]=%hu", &seqno);
		if ((p = stristr(buf, "rtptime")) != NULL) sscanf(p, "%*[^=]=%u", &rtptime);

		if (ctx->ht) hairtunes_record(ctx->ht, seqno, rtptime);

		ctx->callback(ctx->owner, RAOP_STREAM, &ctx->hport);

	}  else if (!strcmp(method, "FLUSH")) {
		unsigned short seqno = 0;
		unsigned rtptime = 0;
		char *p;

		buf = kd_lookup(headers, "RTP-Info");
		if ((p = stristr(buf, "seq")) != NULL) sscanf(p, "%*[^=]=%hu", &seqno);
		if ((p = stristr(buf, "rtptime")) != NULL) sscanf(p, "%*[^=]=%u", &rtptime);

		// only send FLUSH if useful (discards frames above buffer head and top)
		if (ctx->ht && hairtunes_flush(ctx->ht, seqno, rtptime))
			ctx->callback(ctx->owner, RAOP_FLUSH, &ctx->hport);

	}  else if (!strcmp(method, "TEARDOWN")) {

		hairtunes_end(ctx->ht);

		ctx->ht = NULL;
		ctx->hport = -1;

		// need to make sure no search is on-going and reclaim pthread memory
		if (ctx->active_remote.handle) close_mDNS(ctx->active_remote.handle);
		pthread_join(ctx->search_thread, NULL);
		memset(&ctx->active_remote, 0, sizeof(ctx->active_remote));

		NFREE(ctx->rtsp.aeskey);
		NFREE(ctx->rtsp.aesiv);
		NFREE(ctx->rtsp.fmtp);

		ctx->callback(ctx->owner, RAOP_STOP, &ctx->hport);

	} if (!strcmp(method, "SET_PARAMETER")) {
		char *p;

		if ((p = stristr(body, "volume")) != NULL) {
			double volume;

			ctx->volume_stamp = gettime_ms();
			sscanf(p, "%*[^:]:%lf", &volume);
			LOG_INFO("[%p]: SET PARAMETER volume %lf", ctx, volume);
			volume = (volume == -144.0) ? 0 : (1 + volume / 30);
			ctx->callback(ctx->owner, RAOP_VOLUME, &volume);
		}

		if (((p = kd_lookup(headers, "Content-Type")) != NULL) && !strcasecmp(p, "application/x-dmap-tagged")) {
			struct metadata_s metadata;
			dmap_settings settings = {
				NULL, NULL, NULL, NULL,	NULL, NULL,	NULL, on_dmap_string, NULL,
				NULL
			};

			settings.ctx = &metadata;
			memset(&metadata, 0, sizeof(struct metadata_s));
			if (!dmap_parse(&settings, body, len)) {
				hairtunes_metadata(ctx->ht, &metadata);
				LOG_INFO("[%p]: received metadata\n\tartist: %s\n\talbum:  %s\n\ttitle:  %s",
						 ctx, metadata.artist, metadata.album, metadata.title);
				free_metadata(&metadata);
			}
		}
	}

	// don't need to free "buf" because kd_lookup return a pointer, not a strdup

	kd_add(resp, "Audio-Jack-Status", "connected; type=analog");
	kd_add(resp, "CSeq", kd_lookup(headers, "CSeq"));

	if (success) buf = http_send(sock, "RTSP/1.0 200 OK", resp);
	else buf = http_send(sock, "RTSP/1.0 500 ERROR", NULL);

	if (strcmp(method, "OPTIONS")) {
		LOG_INFO("[%p]: responding:\n%s", ctx, buf ? buf : "<void>");
	}

	NFREE(body);
	NFREE(buf);
	kd_free(resp);
	kd_free(headers);

	return true;
}

/*----------------------------------------------------------------------------*/
static void hairtunes_cb(void *owner, hairtunes_event_t event)
{
	raop_ctx_t *ctx = (raop_ctx_t*) owner;

	switch(event) {
		case HAIRTUNES_PLAY:
			ctx->callback(ctx->owner, RAOP_PLAY, &ctx->hport);
			break;
		default:
			LOG_ERROR("[%p]: unknown hairtunes event", ctx, event);
			break;
	}
}


/*----------------------------------------------------------------------------*/
bool search_remote_cb(mDNSservice_t *slist, void *cookie, bool *stop) {
	mDNSservice_t *s;
	raop_ctx_t *ctx = (raop_ctx_t*) cookie;

	// see if we have found an active remote for our ID
	for (s = slist; s; s = s->next) {
		if (stristr(s->name, ctx->active_remote.DACPid)) {
			ctx->active_remote.host = s->addr;
			ctx->active_remote.port = s->port;
			LOG_INFO("[%p]: found ActiveRemote for %s at %s:%u", ctx, ctx->active_remote.DACPid,
								inet_ntoa(ctx->active_remote.host), ctx->active_remote.port);
			*stop = true;
			break;
		}
	}

	// let caller clear list
	return false;
}


/*----------------------------------------------------------------------------*/
static void* search_remote(void *args) {
	raop_ctx_t *ctx = (raop_ctx_t*) args;

	query_mDNS(ctx->active_remote.handle, "_dacp._tcp.local", 0, 0, &search_remote_cb, (void*) ctx);

	return NULL;
}


/*----------------------------------------------------------------------------*/
static char *rsa_apply(unsigned char *input, int inlen, int *outlen, int mode)
{
	unsigned char *out;
	RSA *rsa;
	static char super_secret_key[] =
	"-----BEGIN RSA PRIVATE KEY-----\n"
	"MIIEpQIBAAKCAQEA59dE8qLieItsH1WgjrcFRKj6eUWqi+bGLOX1HL3U3GhC/j0Qg90u3sG/1CUt\n"
	"wC5vOYvfDmFI6oSFXi5ELabWJmT2dKHzBJKa3k9ok+8t9ucRqMd6DZHJ2YCCLlDRKSKv6kDqnw4U\n"
	"wPdpOMXziC/AMj3Z/lUVX1G7WSHCAWKf1zNS1eLvqr+boEjXuBOitnZ/bDzPHrTOZz0Dew0uowxf\n"
	"/+sG+NCK3eQJVxqcaJ/vEHKIVd2M+5qL71yJQ+87X6oV3eaYvt3zWZYD6z5vYTcrtij2VZ9Zmni/\n"
	"UAaHqn9JdsBWLUEpVviYnhimNVvYFZeCXg/IdTQ+x4IRdiXNv5hEewIDAQABAoIBAQDl8Axy9XfW\n"
	"BLmkzkEiqoSwF0PsmVrPzH9KsnwLGH+QZlvjWd8SWYGN7u1507HvhF5N3drJoVU3O14nDY4TFQAa\n"
	"LlJ9VM35AApXaLyY1ERrN7u9ALKd2LUwYhM7Km539O4yUFYikE2nIPscEsA5ltpxOgUGCY7b7ez5\n"
	"NtD6nL1ZKauw7aNXmVAvmJTcuPxWmoktF3gDJKK2wxZuNGcJE0uFQEG4Z3BrWP7yoNuSK3dii2jm\n"
	"lpPHr0O/KnPQtzI3eguhe0TwUem/eYSdyzMyVx/YpwkzwtYL3sR5k0o9rKQLtvLzfAqdBxBurciz\n"
	"aaA/L0HIgAmOit1GJA2saMxTVPNhAoGBAPfgv1oeZxgxmotiCcMXFEQEWflzhWYTsXrhUIuz5jFu\n"
	"a39GLS99ZEErhLdrwj8rDDViRVJ5skOp9zFvlYAHs0xh92ji1E7V/ysnKBfsMrPkk5KSKPrnjndM\n"
	"oPdevWnVkgJ5jxFuNgxkOLMuG9i53B4yMvDTCRiIPMQ++N2iLDaRAoGBAO9v//mU8eVkQaoANf0Z\n"
	"oMjW8CN4xwWA2cSEIHkd9AfFkftuv8oyLDCG3ZAf0vrhrrtkrfa7ef+AUb69DNggq4mHQAYBp7L+\n"
	"k5DKzJrKuO0r+R0YbY9pZD1+/g9dVt91d6LQNepUE/yY2PP5CNoFmjedpLHMOPFdVgqDzDFxU8hL\n"
	"AoGBANDrr7xAJbqBjHVwIzQ4To9pb4BNeqDndk5Qe7fT3+/H1njGaC0/rXE0Qb7q5ySgnsCb3DvA\n"
	"cJyRM9SJ7OKlGt0FMSdJD5KG0XPIpAVNwgpXXH5MDJg09KHeh0kXo+QA6viFBi21y340NonnEfdf\n"
	"54PX4ZGS/Xac1UK+pLkBB+zRAoGAf0AY3H3qKS2lMEI4bzEFoHeK3G895pDaK3TFBVmD7fV0Zhov\n"
	"17fegFPMwOII8MisYm9ZfT2Z0s5Ro3s5rkt+nvLAdfC/PYPKzTLalpGSwomSNYJcB9HNMlmhkGzc\n"
	"1JnLYT4iyUyx6pcZBmCd8bD0iwY/FzcgNDaUmbX9+XDvRA0CgYEAkE7pIPlE71qvfJQgoA9em0gI\n"
	"LAuE4Pu13aKiJnfft7hIjbK+5kyb3TysZvoyDnb3HOKvInK7vXbKuU4ISgxB2bB3HcYzQMGsz1qJ\n"
	"2gG0N5hvJpzwwhbhXqFKA4zaaSrw622wDniAK5MlIE0tIAKKP4yxNGjoD2QYjhBGuhvkWKY=\n"
	"-----END RSA PRIVATE KEY-----";

	BIO *bmem = BIO_new_mem_buf(super_secret_key, -1);
	rsa = PEM_read_bio_RSAPrivateKey(bmem, NULL, NULL, NULL);
	BIO_free(bmem);

	out = malloc(RSA_size(rsa));
	switch (mode) {
		case RSA_MODE_AUTH:
			*outlen = RSA_private_encrypt(inlen, input, out, rsa,
										  RSA_PKCS1_PADDING);
			break;
		case RSA_MODE_KEY:
			*outlen = RSA_private_decrypt(inlen, input, out, rsa,
										  RSA_PKCS1_OAEP_PADDING);
			break;
	}

	RSA_free(rsa);

	return (char*) out;
}

/*----------------------------------------------------------------------------*/
static int  base64_pad(char *src, char **padded)
{
	int n;

	n = strlen(src) + strlen(src) % 4;
	*padded = malloc(n + 1);
	memset(*padded, '=', n);
	memcpy(*padded, src, strlen(src));
	(*padded)[n] = '\0';

	return strlen(*padded);
}

static void on_dmap_string(void *ctx, const char *code, const char *name, const char *buf, size_t len) {
	struct metadata_s *metadata = (struct metadata_s *) ctx;

	if (!strcasecmp(code, "asar")) metadata->artist = strndup(buf, len);
	else if (!strcasecmp(code, "asal")) metadata->album = strndup(buf, len);
	else if (!strcasecmp(code, "minm")) metadata->title = strndup(buf, len);
}

