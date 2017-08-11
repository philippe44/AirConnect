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
#include "log_util.h"

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

/*----------------------------------------------------------------------------*/
raop_ctx_t *raop_create(struct in_addr host, struct mdnsd *svr, char *name,
						char *model, unsigned char mac[6], bool use_flac,
						int latency, void *owner, raop_cb_t callback) {
	struct raop_ctx_s *ctx = malloc(sizeof(struct raop_ctx_s));
	struct sockaddr_in addr;
	socklen_t nlen = sizeof(struct sockaddr);
	char *id = malloc(strlen(name) + 12 + 1 + 1);
	int i;
	char *txt[] = { NULL, "tp=UDP", "sm=false", "sv=false", "ek=1",
					"et=0,1", "md=0,1,2", "cn=0,1", "ch=2",
					"ss=16", "sr=44100", "vn=3", "txtvers=1",
					NULL };

	if (!ctx) return NULL;

	// set model
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
	asprintf(&(txt[0]), "am=%s", model);
#pragma GCC diagnostic pop

	// make sure we have a clean context
	memset(ctx, 0, sizeof(raop_ctx_t));

	ctx->sock = socket(AF_INET, SOCK_STREAM, 0);
	ctx->callback = callback;
	ctx->use_flac = use_flac;
	ctx->latency = latency;
	ctx->owner = owner;
	ctx->volume_stamp = gettime_ms() - 1000;
	S_ADDR(ctx->active_remote.host) = INADDR_ANY;

	if (!ctx->sock) {
		LOG_ERROR("Cannot create listening socket", NULL);
		return NULL;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_addr.s_addr = host.s_addr;
	addr.sin_family = AF_INET;
	addr.sin_port = 0;

	if (bind(ctx->sock, (struct sockaddr *) &addr, sizeof(addr)) < 0 || listen(ctx->sock, 1)) {
		LOG_ERROR("Cannot bind or listen RTSP listener: %s", strerror(errno));
		closesocket(ctx->sock);
		return NULL;
	}

	getsockname(ctx->sock, (struct sockaddr *) &addr, &nlen);
	ctx->port = ntohs(addr.sin_port);
	ctx->host = host;

	memcpy(ctx->mac, mac, 6);
	for (i = 0; i < 6; i++) sprintf(id + i*2, "%02X", mac[i]);
	sprintf(id + 12, "@%s", name);

	ctx->svc = mdnsd_register_svc(svr, id, "_raop._tcp.local", ctx->port, NULL, (const char**) txt);
	mdns_service_destroy(ctx->svc);

#ifdef _FIXME_MDNS_DEREGISTER_
	ctx->_fixme_id = strdup(id);
	ctx->_fixme_model = strdup(model);
#endif
	free(txt[0]);
	free(id);

	ctx->running = true;
	pthread_create(&ctx->thread, NULL, &rtsp_thread, ctx);

	return ctx;
}


#ifdef _FIXME_MDNS_DEREGISTER_
/*----------------------------------------------------------------------------*/
void raop_fixme_register(struct raop_ctx_s *ctx, struct mdnsd *svr) {
	char *txt[] = { NULL, "tp=UDP", "sm=false", "sv=false", "ek=1",
					"et=0,1", "md=0,1,2", "cn=0,1", "ch=2",
					"ss=16", "sr=44100", "vn=3", "txtvers=1",
					NULL };

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
	asprintf(&(txt[0]), "am=%s", ctx->_fixme_model);
#pragma GCC diagnostic pop
	ctx->svc = mdnsd_register_svc(svr, ctx->_fixme_id, "_raop._tcp.local", ctx->port, NULL, (const char**) txt);
	mdns_service_destroy(ctx->svc);
	free(txt[0]);
}
#endif


/*----------------------------------------------------------------------------*/
void  raop_delete(struct raop_ctx_s *ctx) {
	ctx->running = false;

	hairtunes_end(ctx->ht);

#if WIN
	shutdown(ctx->sock, SD_BOTH);
#else
	shutdown(ctx->sock, SHUT_RDWR);
#endif
	closesocket(ctx->sock);

	pthread_join(ctx->thread, NULL);
	pthread_join(ctx->search_thread, NULL);

	NFREE(ctx->rtsp.aeskey);
	NFREE(ctx->rtsp.aesiv);

#ifdef _FIXME_MDNS_DEREGISTER_
	free(ctx->_fixme_id);
   	free(ctx->_fixme_model);
#endif

	if (ctx) free(ctx);
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
			if (ctx->volume_stamp + 1000 - gettime_ms() > 0x7fffffff) {
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

	if (!command) return;

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
		LOG_INFO("[%p]: sending airplay remote\n%sand received\n%s", ctx, buf, resp);

		NFREE(method);
		NFREE(buf);
		free(command);
		kd_free(headers);
	}

	closesocket(sock);
}

/*----------------------------------------------------------------------------*/
static void *rtsp_thread(void *arg) {
	raop_ctx_t *ctx = (raop_ctx_t*) arg;
	int  sock = -1;

	while (ctx->running) {
		fd_set rfds;
		struct timeval timeout = {0, 50*1000};
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

	return NULL;
}


/*----------------------------------------------------------------------------*/
static bool handle_rtsp(raop_ctx_t *ctx, int sock)
{
	char *buf = NULL, *body = NULL, method[16] = "";
	key_data_t headers[64], resp[16] = { {NULL, NULL} };
	int len;
	bool success = true;

	if (!http_parse(sock, method, headers, &body, &len)) return false;

	LOG_INFO("[%p]: received %s", ctx, method);

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

		ctx->active_remote.search = true;
		pthread_create(&ctx->search_thread, NULL, &search_remote, ctx);

	} else if (!strcmp(method, "SETUP") && ((buf = kd_lookup(headers, "Transport")) != NULL)) {
		char *p;
		hairtunes_resp_t ht;
		short unsigned tport, cport;

		if ((p = stristr(buf, "timing_port")) != NULL) sscanf(p, "%*[^=]=%hu", &tport);
		if ((p = stristr(buf, "control_port")) != NULL) sscanf(p, "%*[^=]=%hu", &cport);

		ht = hairtunes_init(ctx->peer, ctx->use_flac, false, ctx->latency,
							ctx->rtsp.aeskey, ctx->rtsp.aesiv,
							ctx->rtsp.fmtp, cport, tport, ctx, hairtunes_cb);

		ctx->hport = ht.hport;
		ctx->ht = ht.ctx;

		if (cport * tport * ht.cport * ht.tport * ht.aport * ht.hport && ht.ctx) {
			char *transport;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
			asprintf(&transport, "RTP/AVP/UDP;unicast;mode=record;control_port=%u;timing_port=%u;server_port=%u", ht.cport, ht.tport, ht.aport);
#pragma GCC diagnostic pop
			LOG_INFO("[%p]: http=(%hu) audio=(%hu:%hu), timing=(%hu:%hu), control=(%hu:%hu)", ctx, ht.hport, 0, ht.aport, tport, ht.tport, cport, ht.cport);
			kd_add(resp, "Transport", transport);
			kd_add(resp, "Session", "DEADBEEF");
			free(transport);
		} else {
			success = false;
			LOG_INFO("[%p]: cannot start session, missing ports", ctx);
		}

	} else if (!strcmp(method, "RECORD")) {

		if (ctx->latency) {
			char latency[5];
			snprintf(latency, 5, "%u", (ctx->latency * 44100) / 1000);
			kd_add(resp, "Audio-Latency", latency);
		}

		ctx->callback(ctx->owner, RAOP_STREAM, &ctx->hport);

	}  else if (!strcmp(method, "FLUSH")) {
		unsigned short seqno = 0;
		char *p;

		buf = kd_lookup(headers, "RTP-Info");
		if ((p = stristr(buf, "seq")) != NULL) sscanf(p, "%*[^=]=%hu", &seqno);

		// only send FLUSH if useful (discards frames above buffer head and top)
		if (hairtunes_flush(ctx->ht, seqno, 0))
			ctx->callback(ctx->owner, RAOP_FLUSH, &ctx->hport);

	}  else if (!strcmp(method, "TEARDOWN")) {

		hairtunes_end(ctx->ht);

		ctx->ht = NULL;
		ctx->hport = -1;

		// need to make sure no search is on-going
		ctx->active_remote.search = false;
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

	}

	// don't need to free "buf" because kd_lookup return a pointer, not a strdup

	kd_add(resp, "Audio-Jack-Status", "connected; type=analog");
	kd_add(resp, "CSeq", kd_lookup(headers, "CSeq"));

	if (success) buf = http_send(sock, "RTSP/1.0 200 OK", resp);
	else buf = http_send(sock, "RTSP/1.0 500 ERROR", NULL);

	LOG_INFO("[%p]: responding:\n%s", ctx, buf ? buf : "<void>");

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
static void* search_remote(void *args) {
	raop_ctx_t *ctx = (raop_ctx_t*) args;
	DiscoveredList DiscDevices;
	int mDNSId;
	int i;

	mDNSId = init_mDNS(false, ctx->host);

	while (ctx->active_remote.search) {
		query_mDNS(mDNSId, "_dacp._tcp.local", &DiscDevices, 5);

		// see if we have found an active remote for our ID
		for (i = 0; i < DiscDevices.count; i++) {
			if (stristr(DiscDevices.items[i].name, ctx->active_remote.DACPid)) {
				ctx->active_remote.host = DiscDevices.items[i].addr;
				ctx->active_remote.port = DiscDevices.items[i].port;
				ctx->active_remote.search = false;
				LOG_INFO("[%p]: found ActiveRemote for %s at %s:%u", ctx, ctx->active_remote.DACPid,
								inet_ntoa(ctx->active_remote.host), ctx->active_remote.port);
				break;
			}
		}

		free_discovered_list(&DiscDevices);
	}

	close_mDNS(mDNSId);

	return NULL;
}


/*----------------------------------------------------------------------------*/
static char *rsa_apply(unsigned char *input, int inlen, int *outlen, int mode)
{
	unsigned char *out;
	static RSA *rsa = NULL;
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

	if (!rsa) {
		BIO *bmem = BIO_new_mem_buf(super_secret_key, -1);
		rsa = PEM_read_bio_RSAPrivateKey(bmem, NULL, NULL, NULL);
		BIO_free(bmem);
	}

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




