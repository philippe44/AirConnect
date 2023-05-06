/*
 * Chromecast core protocol handler
 *
 *  (c) Philippe, philippe_44@outlook.com
 *
 * See LICENSE
 *
 */

#include <stdlib.h>
#include <stdarg.h>

#include "cross_log.h"
#include "cross_net.h"
#include "cross_thread.h"

#include "cast_parse.h"
#include "castcore.h"
#include "castitf.h"

#ifdef _WIN32
#define bswap32(n) _byteswap_ulong((n))
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define	bswap32(n) __builtin_bswap32((n))
#else 
#define bswap32(n) (n)
#endif

#define SELECT_SOCKET

/*----------------------------------------------------------------------------*/
/* locals */
/*----------------------------------------------------------------------------*/
static SSL_CTX *glSSLctx;
static void *CastSocketThread(void *args);
static void *CastPingThread(void *args);

extern log_level cast_loglevel;
static log_level *loglevel = &cast_loglevel;

//#define DEFAULT_RECEIVER	"CC1AD845"
#define DEFAULT_RECEIVER	"46C1A819"

/*----------------------------------------------------------------------------*/
static void CastExit(void) {
	if (glSSLctx) SSL_CTX_free(glSSLctx);
}

/*----------------------------------------------------------------------------*/
static bool read_bytes(pthread_mutex_t *Mutex, SSL *ssl, void *buffer, uint16_t bytes) {
	uint16_t read = 0;
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
		nb = SSL_read(ssl, (uint8_t*) buffer + read, bytes - read);
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
static bool write_bytes(pthread_mutex_t *Mutex, SSL *ssl, void *buffer, uint16_t bytes) {
	pthread_mutex_lock(Mutex);
	bool ret = SSL_write(ssl, buffer, bytes) > 0;
	pthread_mutex_unlock(Mutex);

	return ret;
}

/*----------------------------------------------------------------------------*/
 bool SendCastMessage(struct sCastCtx *Ctx, char *ns, char *dest, char *payload, ...) {
	CastMessage message = CastMessage_init_default;
	pb_ostream_t stream;
	uint8_t *buffer;
	uint16_t buffer_len = 4096;
	bool status;
	uint32_t len;
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
	len = bswap32(stream.bytes_written);

	status &= write_bytes(&Ctx->sslMutex, Ctx->ssl, &len, 4);
	status &= write_bytes(&Ctx->sslMutex, Ctx->ssl, buffer, stream.bytes_written);

	free(buffer);

	if (!strcasestr(message.payload_utf8, "PING")) {
		LOG_DEBUG("[%p]: Cast sending: %s", Ctx->ssl, message.payload_utf8);
	}

	return status;
}

/*----------------------------------------------------------------------------*/
static bool DecodeCastMessage(uint8_t *buffer, uint16_t len, CastMessage *msg) {
	CastMessage message = CastMessage_init_zero;
	pb_istream_t stream = pb_istream_from_buffer(buffer, len);

	bool status = pb_decode(&stream, CastMessage_fields, &message);
	memcpy(msg, &message, sizeof(CastMessage));
	return status;
}

/*----------------------------------------------------------------------------*/
static bool GetNextMessage(pthread_mutex_t *Mutex, SSL *ssl, CastMessage *message) {
	uint32_t len;
	uint8_t *buf;

	// the SSL might just have been closed by another thread
	if (!ssl || !read_bytes(Mutex, ssl, &len, 4)) return false;

	len = bswap32(len);
	if ((buf = malloc(len)) == NULL) return false;
	bool status = read_bytes(Mutex, ssl, buf, len);
	status &= DecodeCastMessage(buf, len, message);

	free(buf);
	return status;
}

/*----------------------------------------------------------------------------*/
json_t *GetTimedEvent(struct sCastCtx *Ctx, uint32_t msWait) {
	pthread_mutex_lock(&Ctx->eventMutex);
	pthread_cond_reltimedwait(&Ctx->eventCond, &Ctx->eventMutex, msWait);
	json_t* data = queue_extract(&Ctx->eventQueue);
	pthread_mutex_unlock(&Ctx->eventMutex);

	return data;
}

/*----------------------------------------------------------------------------*/
bool LaunchReceiver(tCastCtx *Ctx) {
	// try to reconnect if SSL connection is lost
	if (!CastConnect(Ctx)) {
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
				queue_insert(&Ctx->reqQueue, req);
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
bool CastConnect(struct sCastCtx *Ctx) {
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
	addr.sin_addr.s_addr = Ctx->ip.s_addr;
	addr.sin_port = htons(Ctx->port);

	err = tcp_connect_timeout(Ctx->sock, addr, 2*1000);

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
	crossthreads_wake();

	return true;
}

/*----------------------------------------------------------------------------*/
void CastDisconnect(struct sCastCtx *Ctx) {
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
	queue_flush(&Ctx->eventQueue);
	CastQueueFlush(&Ctx->reqQueue);

	SSL_shutdown(Ctx->ssl);
	SSL_clear(Ctx->ssl);
	closesocket(Ctx->sock);

	pthread_mutex_unlock(&Ctx->Mutex);
}

/*----------------------------------------------------------------------------*/
void SetMediaVolume(tCastCtx *Ctx, double Volume) {
	if (Volume > 1.0) Volume = 1.0;

	Ctx->waitId = Ctx->reqId++;

	SendCastMessage(Ctx, CAST_MEDIA, Ctx->transportId,
						"{\"type\":\"SET_VOLUME\",\"requestId\":%d,\"mediaSessionId\":%d,\"volume\":{\"level\":%0.4lf,\"muted\":false}}",
						Ctx->waitId, Ctx->mediaSessionId, Volume);
}

/*----------------------------------------------------------------------------*/
void *CreateCastDevice(void *owner, bool group, bool stopReceiver, struct in_addr ip, uint16_t port, double MediaVolume) {
	tCastCtx *Ctx = malloc(sizeof(tCastCtx));
	pthread_mutexattr_t mutexAttr;

	if (!glSSLctx) {
		const SSL_METHOD* method = SSLv23_client_method();
		glSSLctx = SSL_CTX_new(method);
		SSL_CTX_set_options(glSSLctx, SSL_OP_NO_SSLv2);
		atexit(CastExit);
	}

	Ctx->reqId 		= 1;
	Ctx->waitId 	= Ctx->waitMedia = Ctx->mediaSessionId = 0;
	Ctx->sessionId 	= Ctx->transportId = NULL;
	Ctx->owner 		= owner;
	Ctx->Status 	= CAST_DISCONNECTED;
	Ctx->ip 		= ip;
	Ctx->port		= port;
	Ctx->mediaVolume  = MediaVolume;
	Ctx->group 		= group;
	Ctx->stopReceiver = stopReceiver;
	Ctx->ssl  		= SSL_new(glSSLctx);

	queue_init(&Ctx->eventQueue, false, NULL);
	queue_init(&Ctx->reqQueue, false, NULL);
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
bool UpdateCastDevice(struct sCastCtx *Ctx, struct in_addr ip, uint16_t port) {
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
struct in_addr CastGetAddr(struct sCastCtx *Ctx) {
	return Ctx->ip;
}

/*----------------------------------------------------------------------------*/
void DeleteCastDevice(struct sCastCtx *Ctx) {
	pthread_mutex_lock(&Ctx->Mutex);
	Ctx->running = false;
	pthread_mutex_unlock(&Ctx->Mutex);

	CastDisconnect(Ctx);

	// wake up cast communication & ping threads
	crossthreads_wake();

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
void CastQueueFlush(cross_queue_t *Queue) {
	tReqItem *item;

	while ((item = queue_extract(Queue)) != NULL) {
		if (!strcasecmp(item->Type,"LOAD")) json_decref(item->data.msg);
		free(item);
	}
}

/*----------------------------------------------------------------------------*/
void ProcessQueue(tCastCtx *Ctx) {
	tReqItem *item;

	if ((item = queue_extract(&Ctx->reqQueue)) == NULL) return;

	if (!strcasecmp(item->Type, "LAUNCH")) {
		Ctx->waitId = Ctx->reqId++;
		Ctx->Status = CAST_LAUNCHING;

		LOG_INFO("[%p]: Launching receiver %d", Ctx->owner, Ctx->waitId);

		SendCastMessage(Ctx, CAST_RECEIVER, NULL, "{\"type\":\"LAUNCH\",\"requestId\":%d,\"appId\":\"%s\"}", Ctx->waitId, DEFAULT_RECEIVER);
	}

#if 0
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
#endif

	if (!strcasecmp(item->Type, "SET_VOLUME")) {

		if (item->data.volume) {
			SendCastMessage(Ctx, CAST_RECEIVER, NULL,
							"{\"type\":\"SET_VOLUME\",\"requestId\":%d,\"volume\":{\"level\":%0.4lf}}",
							Ctx->reqId++, item->data.volume);

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

			json_t* msg = json_pack("{ss,si,si}", "type", "PLAY", "requestId", Ctx->waitId,
												  "mediaSessionId", Ctx->mediaSessionId);

			json_t* customData = json_pack("{so}", "customData", item->data.customData);
			json_object_update(msg, customData);
			json_decref(customData);

			char* str = json_dumps(msg, JSON_ENCODE_ANY | JSON_INDENT(1));
			json_decref(item->data.customData);
			json_decref(msg);

			SendCastMessage(Ctx, CAST_MEDIA, Ctx->transportId, "%s", str);
			NFREE(str);
		}
		else {
			if (item->data.customData) json_decref(item->data.customData);
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
static void *CastPingThread(void *args) {
	tCastCtx *Ctx = (tCastCtx*) args;
	uint32_t last = gettime_ms();

	Ctx->running = true;

	while (Ctx->running) {
		uint32_t now = gettime_ms();

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

		crossthreads_sleep(1500);
	}

	// clear SSL error allocated memorry
	ERR_remove_thread_state(NULL);

	return NULL;
}

/*----------------------------------------------------------------------------*/
static void *CastSocketThread(void *args) {
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
			crossthreads_sleep(0);
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

			if (!strcasecmp(str, "CLOSE")) {
				// Connection closed by peer
				Ctx->Status = CAST_CONNECTED;
				Ctx->waitId = 0;
				ProcessQueue(Ctx);
				// VERSION_1_24
				if (Ctx->stopReceiver) {
					json_decref(root);
					forward = false;
				}
			} else if (!strcasecmp(str,"PING")) {
				// respond to device ping
				SendCastMessage(Ctx, CAST_BEAT, Message.source_id, "{\"type\":\"PONG\"}");
				json_decref(root);
				forward = false;
			} else if (!strcasecmp(str,"PONG")) {
				// receiving pong
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

			// expected request acknowledge (we know that str is still valid)
			if (Ctx->waitId && Ctx->waitId == requestId) {

				// reset waitId, might be set below
				Ctx->waitId = 0;

				if (!strcasecmp(str,"RECEIVER_STATUS") && Ctx->Status == CAST_LAUNCHING) {
					// receiver status before connection is fully established
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
				} else if (!strcasecmp(str,"MEDIA_STATUS") && Ctx->waitMedia == requestId) {
					// media status only acquired for expected id
					int id = GetMediaItem_I(root, 0, "mediaSessionId");

					if (id) {
						Ctx->waitMedia = 0;
						Ctx->mediaSessionId = id;
						LOG_INFO("[%p]: Media session id %d", Ctx->owner, Ctx->mediaSessionId);
						// set media volume when session is re-connected
						SetMediaVolume(Ctx, Ctx->mediaVolume);
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
			queue_insert(&Ctx->eventQueue, root);
			pthread_cond_signal(&Ctx->eventCond);
			pthread_mutex_unlock(&Ctx->eventMutex);
		}
	}

	// clear SSL error allocated memorry
	ERR_remove_thread_state(NULL);

	return NULL;
}
