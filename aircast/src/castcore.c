/*
 * Chromecast protocol handler
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

#include <stdarg.h>


#include "log_util.h"
#include "util.h"
#include "cast_parse.h"
#include "castcore.h"
#include "castitf.h"


#define SELECT_SOCKET

/*----------------------------------------------------------------------------*/
/* locals */
/*----------------------------------------------------------------------------*/
static SSL_CTX *glSSLctx;
static void *CastSocketThread(void *args);
static void *CastPingThread(void *args);

extern log_level cast_loglevel;
static log_level *loglevel = &cast_loglevel;

#define DEFAULT_RECEIVER	"CC1AD845"

/*----------------------------------------------------------------------------*/
#if OSX
static void set_nosigpipe(sockfd s) {
	int set = 1;
	setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (void *)&set, sizeof(int));
}
#else
#define set_nosigpipe(s)
#endif


/*----------------------------------------------------------------------------*/
static void set_nonblock(sockfd s) {
#if WIN
	u_long iMode = 1;
	ioctlsocket(s, FIONBIO, &iMode);
#else
	int flags = fcntl(s, F_GETFL,0);
	fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
}


/*----------------------------------------------------------------------------*/
static void set_block(sockfd s) {
#if WIN
	u_long iMode = 0;
	ioctlsocket(s, FIONBIO, &iMode);
#else
	int flags = fcntl(s, F_GETFL,0);
	fcntl(s, F_SETFL, flags & (~O_NONBLOCK));
#endif
}


/*----------------------------------------------------------------------------*/
static int connect_timeout(sockfd sock, const struct sockaddr *addr, socklen_t addrlen, int timeout) {
	fd_set w, e;
	struct timeval tval;

	if (connect(sock, addr, addrlen) < 0) {
#if !WIN
		if (last_error() != EINPROGRESS) {
#else
		if (last_error() != WSAEWOULDBLOCK) {
#endif
			return -1;
		}
	}

	FD_ZERO(&w);
	FD_SET(sock, &w);
	e = w;
	tval.tv_sec = timeout / 1000;
	tval.tv_usec = (timeout - tval.tv_sec * 1000) * 1000;

	// only return 0 if w set and sock error is zero, otherwise return error code
	if (select(sock + 1, NULL, &w, &e, timeout ? &tval : NULL) == 1 && FD_ISSET(sock, &w)) {
		int	error = 0;
		socklen_t len = sizeof(error);
		getsockopt(sock, SOL_SOCKET, SO_ERROR, (void *)&error, &len);
		return error;
	}

	return -1;
}


/*----------------------------------------------------------------------------*/
void InitSSL(void)
{
	const SSL_METHOD *method;

	// initialize SSL stuff
	// SSL_load_error_strings();
	SSL_library_init();

	// build the SSL objects...
	method = SSLv23_client_method();

	glSSLctx = SSL_CTX_new(method);
	SSL_CTX_set_options(glSSLctx, SSL_OP_NO_SSLv2);
}


/*----------------------------------------------------------------------------*/
void EndSSL(void)
{
	SSL_CTX_free(glSSLctx);
}


/*----------------------------------------------------------------------------*/
void swap32(u32_t *n)
{
#if SL_LITTLE_ENDIAN
	u32_t buf = *n;
	*n = 	(((u8_t) (buf >> 24))) +
		(((u8_t) (buf >> 16)) << 8) +
		(((u8_t) (buf >> 8)) << 16) +
		(((u8_t) (buf)) << 24);
#else
#endif
}


/*----------------------------------------------------------------------------*/
bool read_bytes(pthread_mutex_t *Mutex, SSL *ssl, void *buffer, u16_t bytes)
{
	u16_t read = 0;
	sockfd sock = SSL_get_fd(ssl);

	if (sock == -1) return false;

	while (bytes - read) {
		int nb;
#ifdef SELECT_SOCKET
		fd_set rfds;
		struct timeval timeout = { 0, 100000 };
		FD_ZERO(&rfds);
		FD_SET(sock, &rfds);

		if (!SSL_pending(ssl)) {
			if (select(sock + 1, &rfds, NULL, NULL, &timeout) == -1) {
				LOG_WARN("[s-%p]: socket closed", ssl);
				return false;
			}

			if (!FD_ISSET(sock, &rfds)) continue;
		}
#endif
		ERR_clear_error();
		pthread_mutex_lock(Mutex);
		nb = SSL_read(ssl, (u8_t*) buffer + read, bytes - read);
		pthread_mutex_unlock(Mutex);
		if (nb <= 0) {
			LOG_WARN("[s-%p]: SSL error code %d (err:%d)", ssl, SSL_get_error(ssl, nb), ERR_get_error());
			return false;
		}
		read += nb;
	}

	return true;
}


/*----------------------------------------------------------------------------*/
bool write_bytes(pthread_mutex_t *Mutex, SSL *ssl, void *buffer, u16_t bytes)
{
	bool ret;

	pthread_mutex_lock(Mutex);
	ret = SSL_write(ssl, buffer, bytes) > 0;
	pthread_mutex_unlock(Mutex);

	return ret;
}


/*----------------------------------------------------------------------------*/
bool SendCastMessage(struct sCastCtx *Ctx, char *ns, char *dest, char *payload, ...)
{
	CastMessage message = CastMessage_init_default;
	pb_ostream_t stream;
	u8_t *buffer;
	u16_t buffer_len = 4096;
	bool status;
	u32_t len;
	va_list args;

	if (!Ctx->ssl) return false;

	va_start(args, payload);

	if (dest) strcpy(message.destination_id, dest);
	strcpy(message.namespace, ns);
	len = vsprintf(message.payload_utf8, payload, args);
	message.has_payload_utf8 = true;
	if ((buffer = malloc(buffer_len)) == NULL) return false;
	stream = pb_ostream_from_buffer(buffer, buffer_len);
	status = pb_encode(&stream, CastMessage_fields, &message);
	len = stream.bytes_written;
	swap32(&len);

	status &= write_bytes(&Ctx->sslMutex, Ctx->ssl, &len, 4);
	status &= write_bytes(&Ctx->sslMutex, Ctx->ssl, buffer, stream.bytes_written);

	free(buffer);

	if (!stristr(message.payload_utf8, "PING")) {
		LOG_DEBUG("[%p]: Cast sending: %s", Ctx->ssl, message.payload_utf8);
	}

	return status;
}


/*----------------------------------------------------------------------------*/
bool DecodeCastMessage(u8_t *buffer, u16_t len, CastMessage *msg)
{
	bool status;
	CastMessage message = CastMessage_init_zero;
	pb_istream_t stream = pb_istream_from_buffer(buffer, len);

	status = pb_decode(&stream, CastMessage_fields, &message);
	memcpy(msg, &message, sizeof(CastMessage));
	return status;
}


/*----------------------------------------------------------------------------*/
bool GetNextMessage(pthread_mutex_t *Mutex, SSL *ssl, CastMessage *message)
{
	bool status;
	u32_t len;
	u8_t *buf;

	// the SSL might just have been closed by another thread
	if (!ssl || !read_bytes(Mutex, ssl, &len, 4)) return false;

	swap32(&len);
	if ((buf = malloc(len))== NULL) return false;
	status = read_bytes(Mutex, ssl, buf, len);
	status &= DecodeCastMessage(buf, len, message);
	free(buf);
	return status;
}



/*----------------------------------------------------------------------------*/
json_t *GetTimedEvent(void *p, u32_t msWait)
{
	json_t *data;
	tCastCtx *Ctx = (tCastCtx*) p;

	pthread_mutex_lock(&Ctx->eventMutex);
	pthread_cond_reltimedwait(&Ctx->eventCond, &Ctx->eventMutex, msWait);
	data = QueueExtract(&Ctx->eventQueue);
	pthread_mutex_unlock(&Ctx->eventMutex);

	return data;
}


/*----------------------------------------------------------------------------*/
bool LaunchReceiver(tCastCtx *Ctx)
{
	// try to reconnect if SSL connection is lost
	if (!CastConnect(Ctx)) {
		pthread_mutex_unlock(&Ctx->Mutex);
		return false;
	}

	pthread_mutex_lock(&Ctx->Mutex);

	switch (Ctx->Status) {
		case CAST_LAUNCHED:
			break;
		case CAST_CONNECTING:
			Ctx->Status = CAST_AUTOLAUNCH;
			break;
		case CAST_CONNECTED:
			if (!Ctx->waitId) {
				Ctx->Status = CAST_LAUNCHING;
				Ctx->waitId = Ctx->reqId++;
				SendCastMessage(Ctx, CAST_RECEIVER, NULL, "{\"type\":\"LAUNCH\",\"requestId\":%d,\"appId\":\"%s\"}", Ctx->waitId, DEFAULT_RECEIVER);
				LOG_INFO("[%p]: Launching receiver %d", Ctx->owner, Ctx->waitId);
			} else {
				tReqItem *req = malloc(sizeof(tReqItem));
				strcpy(req->Type, "LAUNCH");
				QueueInsert(&Ctx->reqQueue, req);
				LOG_INFO("[%p]: Queuing %s", Ctx->owner, req->Type);
			 }
			break;
		default:
			LOG_INFO("[%p]: unhandled state %d", Ctx->owner, Ctx->Status);
			break;
	}

	pthread_mutex_unlock(&Ctx->Mutex);

	return true;
}


/*----------------------------------------------------------------------------*/
bool CastConnect(struct sCastCtx *Ctx)
{
	int err;
	struct sockaddr_in addr;

	pthread_mutex_lock(&Ctx->Mutex);

	if (Ctx->Status != CAST_DISCONNECTED) {
		pthread_mutex_unlock(&Ctx->Mutex);
		return true;
	}

	Ctx->sock = socket(AF_INET, SOCK_STREAM, 0);
	set_nonblock(Ctx->sock);
	set_nosigpipe(Ctx->sock);

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = S_ADDR(Ctx->ip);
	addr.sin_port = htons(Ctx->port);

	err = connect_timeout(Ctx->sock, (struct sockaddr *) &addr, sizeof(addr), 2*1000);

	if (err) {
		closesocket(Ctx->sock);
		LOG_ERROR("[%p]: Cannot open socket connection (%d)", Ctx->owner, err);
		pthread_mutex_unlock(&Ctx->Mutex);
		return false;
	}

	set_block(Ctx->sock);
	SSL_set_fd(Ctx->ssl, Ctx->sock);

	if (SSL_connect(Ctx->ssl)) {
		LOG_INFO("[%p]: SSL connection opened [%p]", Ctx->owner, Ctx->ssl);
	}
	else {
		err = SSL_get_error(Ctx->ssl,err);
		LOG_ERROR("[%p]: Cannot open SSL connection (%d)", Ctx->owner, err);
		closesocket(Ctx->sock);
		pthread_mutex_unlock(&Ctx->Mutex);
		return false;
	}

	Ctx->Status = CAST_CONNECTING;
	Ctx->lastPong = gettime_ms();
	SendCastMessage(Ctx, CAST_CONNECTION, NULL, "{\"type\":\"CONNECT\"}");
	pthread_mutex_unlock(&Ctx->Mutex);

	// wake up everybody who can be waiting
	WakeAll();

	return true;
}


/*----------------------------------------------------------------------------*/
void CastDisconnect(struct sCastCtx *Ctx)
{
	pthread_mutex_lock(&Ctx->Mutex);

	// powered off already
	if (Ctx->Status == CAST_DISCONNECTED) {
		pthread_mutex_unlock(&Ctx->Mutex);
		return;
	}

	Ctx->reqId = 1;
	Ctx->waitId = Ctx->waitMedia = Ctx->mediaSessionId = 0;
	Ctx->Status = CAST_DISCONNECTED;
	NFREE(Ctx->sessionId);
	NFREE(Ctx->transportId);
	QueueFlush(&Ctx->eventQueue);
	CastQueueFlush(&Ctx->reqQueue);

	SSL_shutdown(Ctx->ssl);
	closesocket(Ctx->sock);

	pthread_mutex_unlock(&Ctx->Mutex);
}


/*----------------------------------------------------------------------------*/
void SetMediaVolume(tCastCtx *Ctx, double Volume)
{
	if (Volume > 1.0) Volume = 1.0;

	Ctx->waitId = Ctx->reqId++;

	SendCastMessage(Ctx, CAST_MEDIA, Ctx->transportId,
						"{\"type\":\"SET_VOLUME\",\"requestId\":%d,\"mediaSessionId\":%d,\"volume\":{\"level\":%0.4lf,\"muted\":false}}",
						Ctx->waitId, Ctx->mediaSessionId, Volume);
}



/*----------------------------------------------------------------------------*/

void *CreateCastDevice(void *owner, bool group, bool stopReceiver, struct in_addr ip, u16_t port, double MediaVolume)
{
	tCastCtx *Ctx = malloc(sizeof(tCastCtx));
	pthread_mutexattr_t mutexAttr;

	Ctx->reqId 		= 1;
	Ctx->waitId 	= Ctx->waitMedia = Ctx->mediaSessionId = 0;
	Ctx->sessionId 	= Ctx->transportId = NULL;
	Ctx->owner 		= owner;
	Ctx->ssl 		= NULL;
	Ctx->Status 	= CAST_DISCONNECTED;
	Ctx->ip 		= ip;
	Ctx->port		= port;
	Ctx->MediaVolume  = MediaVolume;
	Ctx->group 		= group;
	Ctx->stopReceiver = stopReceiver;
	Ctx->ssl  		= SSL_new(glSSLctx);

	QueueInit(&Ctx->eventQueue, false, NULL);
	QueueInit(&Ctx->reqQueue, false, NULL);
	pthread_mutexattr_init(&mutexAttr);
	pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&Ctx->Mutex, &mutexAttr);
	pthread_mutexattr_destroy(&mutexAttr);
	pthread_mutex_init(&Ctx->eventMutex, 0);
	pthread_mutex_init(&Ctx->sslMutex, 0);
	pthread_cond_init(&Ctx->eventCond, 0);

	pthread_create(&Ctx->Thread, NULL, &CastSocketThread, Ctx);
	pthread_create(&Ctx->PingThread, NULL, &CastPingThread, Ctx);

	return Ctx;
}


/*----------------------------------------------------------------------------*/
bool UpdateCastDevice(struct sCastCtx *Ctx, struct in_addr ip, u16_t port)
{
	if (Ctx->port != port || Ctx->ip.s_addr != ip.s_addr) {
		LOG_INFO("[%p]: changed ip:port %s:%d", Ctx, inet_ntoa(ip), port);
		pthread_mutex_lock(&Ctx->Mutex);
		Ctx->ip	= ip;
		Ctx->port = port;
		pthread_mutex_unlock(&Ctx->Mutex);
		CastDisconnect(Ctx);
		return true;
	}
	return false;
}


/*----------------------------------------------------------------------------*/
struct in_addr GetAddr(struct sCastCtx *Ctx)
{
	return Ctx->ip;
}


/*----------------------------------------------------------------------------*/
void DeleteCastDevice(struct sCastCtx *Ctx)
{
	pthread_mutex_lock(&Ctx->Mutex);
	Ctx->running = false;
	pthread_mutex_unlock(&Ctx->Mutex);

	CastDisconnect(Ctx);

	// wake up cast communication & ping threads
	WakeAll();

	pthread_join(Ctx->PingThread, NULL);
	pthread_join(Ctx->Thread, NULL);

	// wake-up threads locked on GetTimedEvent
	pthread_mutex_lock(&Ctx->eventMutex);
	pthread_cond_signal(&Ctx->eventCond);
	pthread_mutex_unlock(&Ctx->eventMutex);

	// cleanup mutexes & conds
	pthread_cond_destroy(&Ctx->eventCond);
	pthread_mutex_destroy(&Ctx->eventMutex);
	pthread_mutex_destroy(&Ctx->sslMutex);

	LOG_INFO("[%p]: Cast device stopped", Ctx->owner);
	SSL_free(Ctx->ssl);
	free(Ctx);
}


/*----------------------------------------------------------------------------*/
void CastQueueFlush(tQueue *Queue)
{
	tReqItem *item;

	while ((item = QueueExtract(Queue)) != NULL) {
		if (!strcasecmp(item->Type,"LOAD")) json_decref(item->data.msg);
		free(item);
	}
}


/*----------------------------------------------------------------------------*/
void ProcessQueue(tCastCtx *Ctx) {
	tReqItem *item;

	if ((item = QueueExtract(&Ctx->reqQueue)) == NULL) return;

	if (!strcasecmp(item->Type, "LAUNCH")) {
		Ctx->waitId = Ctx->reqId++;
		Ctx->Status = CAST_LAUNCHING;

		LOG_INFO("[%p]: Launching receiver %d", Ctx->owner, Ctx->waitId);

		SendCastMessage(Ctx, CAST_RECEIVER, NULL, "{\"type\":\"LAUNCH\",\"requestId\":%d,\"appId\":\"%s\"}", Ctx->waitId, DEFAULT_RECEIVER);
	}

	if (!strcasecmp(item->Type, "GET_MEDIA_STATUS") && Ctx->mediaSessionId) {
		Ctx->waitId = Ctx->reqId++;

		LOG_INFO("[%p]: Processing GET_MEDIA_STATUS (id:%u)", Ctx->owner, Ctx->waitId);

		SendCastMessage(Ctx, CAST_MEDIA, Ctx->transportId,
			"{\"type\":\"GET_STATUS\",\"requestId\":%d,\"mediaSessionId\":%d}",
			Ctx->waitId, Ctx->mediaSessionId);
	}

	if (!strcasecmp(item->Type, "GET_STATUS")) {
		Ctx->waitId = Ctx->reqId++;

		LOG_INFO("[%p]: Processing GET_STATUS (id:%u)", Ctx->owner, Ctx->waitId);

		SendCastMessage(Ctx, CAST_RECEIVER, NULL, "{\"type\":\"GET_STATUS\",\"requestId\":%d}", Ctx->waitId);
	}

	if (!strcasecmp(item->Type, "SET_VOLUME")) {

		if (item->data.Volume) {
			SendCastMessage(Ctx, CAST_RECEIVER, NULL,
							"{\"type\":\"SET_VOLUME\",\"requestId\":%d,\"volume\":{\"level\":%0.4lf}}",
							Ctx->reqId++, item->data.Volume);

			SendCastMessage(Ctx, CAST_RECEIVER, NULL,
							"{\"type\":\"SET_VOLUME\",\"requestId\":%d,\"volume\":{\"muted\":false}}",
							Ctx->reqId);
		}
		else {
			SendCastMessage(Ctx, CAST_RECEIVER, NULL,
							"{\"type\":\"SET_VOLUME\",\"requestId\":%d,\"volume\":{\"muted\":true}}",
							Ctx->reqId);
		}

		Ctx->waitId = Ctx->reqId++;

		LOG_INFO("[%p]: Processing VOLUME (id:%u)", Ctx->owner, Ctx->waitId);
	}

	if (!strcasecmp(item->Type, "PLAY") || !strcasecmp(item->Type, "PAUSE")) {
		if (Ctx->mediaSessionId) {
			Ctx->waitId = Ctx->reqId++;

			LOG_INFO("[%p]: Processing %s (id:%u)", Ctx->owner, item->Type, Ctx->waitId);

			SendCastMessage(Ctx, CAST_MEDIA, Ctx->transportId,
							"{\"type\":\"%s\",\"requestId\":%d,\"mediaSessionId\":%d}",
							item->Type, Ctx->waitId, Ctx->mediaSessionId);
		}
		else {
			LOG_WARN("[%p]: PLAY un-queued but no media session", Ctx->owner);
		}
	}

	if (!strcasecmp(item->Type, "LOAD")) {
		json_t *msg = item->data.msg;
		char *str;

		Ctx->waitId = Ctx->reqId++;
		Ctx->waitMedia = Ctx->waitId;
		Ctx->mediaSessionId = 0;

		LOG_INFO("[%p]: Processing LOAD (id:%u)", Ctx->owner, Ctx->waitId);

		msg = json_pack("{ss,si,ss,sf,sb,so}", "type", "LOAD",
						"requestId", Ctx->waitId, "sessionId", Ctx->sessionId,
						"currentTime", 0.0, "autoplay", 0,
						"media", msg);

		str = json_dumps(msg, JSON_ENCODE_ANY | JSON_INDENT(1));
		SendCastMessage(Ctx, CAST_MEDIA, Ctx->transportId, "%s", str);
		NFREE(str);
		
		json_decref(msg);
   }

	if (!strcasecmp(item->Type, "STOP")) {

		Ctx->waitId = Ctx->reqId++;

		// version 1.24
		if (Ctx->stopReceiver) {
			SendCastMessage(Ctx, CAST_RECEIVER, NULL,
						"{\"type\":\"STOP\",\"requestId\":%d}", Ctx->waitId);
			Ctx->Status = CAST_CONNECTED;

		}
		else if (Ctx->mediaSessionId) {
			SendCastMessage(Ctx, CAST_MEDIA, Ctx->transportId,
							"{\"type\":\"STOP\",\"requestId\":%d,\"mediaSessionId\":%d}",
							Ctx->waitId, Ctx->mediaSessionId);
		}

		Ctx->mediaSessionId = 0;
	}

   free(item);
}


/*----------------------------------------------------------------------------*/
static void *CastPingThread(void *args)
{
	tCastCtx *Ctx = (tCastCtx*) args;
	u32_t last = gettime_ms();

	Ctx->running = true;

	while (Ctx->running) {
		u32_t now = gettime_ms();

		if (now - last > 3000 && Ctx->Status != CAST_DISCONNECTED) {
			pthread_mutex_lock(&Ctx->Mutex);

			// ping SSL connection
			if (Ctx->ssl) {
				SendCastMessage(Ctx, CAST_BEAT, NULL, "{\"type\":\"PING\"}");
				if (now - Ctx->lastPong > 15000) {
					LOG_INFO("[%p]: No response to ping", Ctx);
					CastDisconnect(Ctx);
				}
			}

			// then ping RECEIVER connection
			if (Ctx->Status == CAST_LAUNCHED) SendCastMessage(Ctx, CAST_BEAT, Ctx->transportId, "{\"type\":\"PING\"}");

			pthread_mutex_unlock(&Ctx->Mutex);
			last = now;
		}

		WakeableSleep(1500);
	}

	// clear SSL error allocated memorry
	ERR_remove_state(0);

	return NULL;
}


/*----------------------------------------------------------------------------*/
static void *CastSocketThread(void *args)
{
	tCastCtx *Ctx = (tCastCtx*) args;
	CastMessage Message;
	json_t *root, *val;
	json_error_t  error;

	Ctx->running = true;

	while (Ctx->running) {
		int requestId = 0;
		bool forward = true;
		const char *str = NULL;

		// allow "virtual" power off
		if (Ctx->Status == CAST_DISCONNECTED) {
			WakeableSleep(0);
			continue;
		}

		// this SSL access is not mutex protected, but it should be fine
		if (!GetNextMessage(&Ctx->sslMutex, Ctx->ssl, &Message)) {
			LOG_WARN("[%p]: SSL connection closed", Ctx);
			CastDisconnect(Ctx);
			continue;
		}

		root = json_loads(Message.payload_utf8, 0, &error);
		LOG_SDEBUG("[%p]: %s", Ctx->owner, json_dumps(root, JSON_ENCODE_ANY | JSON_INDENT(1)));

		val = json_object_get(root, "requestId");
		if (json_is_integer(val)) requestId = json_integer_value(val);

		val = json_object_get(root, "type");

		if (json_is_string(val)) {
			pthread_mutex_lock(&Ctx->Mutex);
			str = json_string_value(val);

			if (!strcasecmp(str, "MEDIA_STATUS")) {
				LOG_DEBUG("[%p]: type:%s (id:%d) %s", Ctx->owner, str, requestId, GetMediaItem_S(root, 0, "playerState"));
			}
			else if (strcasecmp(str, "PONG") || *loglevel == lSDEBUG) {
				LOG_DEBUG("[%p]: type:%s (id:%d)", Ctx->owner, str, requestId);
			}

			LOG_SDEBUG("(s:%s) (d:%s)\n%s", Message.source_id, Message.destination_id, Message.payload_utf8);

			// Connection closed by peer
			if (!strcasecmp(str, "CLOSE")) {
				Ctx->Status = CAST_CONNECTED;
				Ctx->waitId = 0;
				ProcessQueue(Ctx);
				// VERSION_1_24
				if (Ctx->stopReceiver) {
					json_decref(root);
					forward = false;
				}
			}

			// respond to device ping
			if (!strcasecmp(str,"PING")) {
				SendCastMessage(Ctx, CAST_BEAT, Message.source_id, "{\"type\":\"PONG\"}");
				json_decref(root);
				forward = false;
			}

			// receiving pong
			if (!strcasecmp(str,"PONG")) {
				Ctx->lastPong = gettime_ms();
				// connection established, start receiver was requested
				if (Ctx->Status == CAST_AUTOLAUNCH) {
					Ctx->Status = CAST_LAUNCHING;
					Ctx->waitId = Ctx->reqId++;
					SendCastMessage(Ctx, CAST_RECEIVER, NULL, "{\"type\":\"LAUNCH\",\"requestId\":%d,\"appId\":\"%s\"}", Ctx->waitId, DEFAULT_RECEIVER);
					LOG_INFO("[%p]: Launching receiver %d", Ctx->owner, Ctx->waitId);
				} else if (Ctx->Status == CAST_CONNECTING) Ctx->Status = CAST_CONNECTED;

				json_decref(root);
				forward = false;
			}

			LOG_SDEBUG("[%p]: recvID %u (waitID %u)", Ctx, requestId, Ctx->waitId);

			// expected request acknowledge
			if (Ctx->waitId && Ctx->waitId == requestId) {

				// reset waitId, might be set below
				Ctx->waitId = 0;

				// receiver status before connection is fully established
				if (!strcasecmp(str,"RECEIVER_STATUS") && Ctx->Status == CAST_LAUNCHING) {
					const char *str;

					NFREE(Ctx->sessionId);
					str = GetAppIdItem(root, DEFAULT_RECEIVER, "sessionId");
					if (str) Ctx->sessionId = strdup(str);
					NFREE(Ctx->transportId);
					str = GetAppIdItem(root, DEFAULT_RECEIVER, "transportId");
					if (str) Ctx->transportId = strdup(str);

					if (Ctx->sessionId && Ctx->transportId) {
						Ctx->Status = CAST_LAUNCHED;
						LOG_INFO("[%p]: Receiver launched", Ctx->owner);
						SendCastMessage(Ctx, CAST_CONNECTION, Ctx->transportId,
									"{\"type\":\"CONNECT\",\"origin\":{}}");
					}

					json_decref(root);
					forward = false;
				}

				// media status only acquired for expected id
				if (!strcasecmp(str,"MEDIA_STATUS") && Ctx->waitMedia == requestId) {
					int id = GetMediaItem_I(root, 0, "mediaSessionId");

					if (id) {
						Ctx->waitMedia = 0;
						Ctx->mediaSessionId = id;
						LOG_INFO("[%p]: Media session id %d", Ctx->owner, Ctx->mediaSessionId);
						// set media volume when session is re-connected
						SetMediaVolume(Ctx, Ctx->MediaVolume);
					} else {
						LOG_ERROR("[%p]: waitMedia match but no session %u", Ctx->owner, Ctx->waitMedia);
					}

					// Don't need to forward this, no valuable info
					json_decref(root);
					forward = false;
				}

				// must be done at the end, once all parameters have been acquired
				if (!Ctx->waitId && Ctx->Status == CAST_LAUNCHED) ProcessQueue(Ctx);
			}

			pthread_mutex_unlock(&Ctx->Mutex);
		}

		// queue event and signal handler
		if (forward) {
			pthread_mutex_lock(&Ctx->eventMutex);
			QueueInsert(&Ctx->eventQueue, root);
			pthread_cond_signal(&Ctx->eventCond);
			pthread_mutex_unlock(&Ctx->eventMutex);
		}
	}

	// clear SSL error allocated memorry
	ERR_remove_state(0);

	return NULL;
}





