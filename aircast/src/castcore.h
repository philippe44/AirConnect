/*
 *  Chromecast protocol handler
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

#ifndef __CASTCORE_H
#define __CASTCORE_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "aircast.h"

#include "openssl/crypto.h"
#include "openssl/x509.h"
#include "openssl/pem.h"
#include "openssl/ssl.h"
#include "openssl/err.h"

#include <fcntl.h>

#include <pb_encode.h>
#include <pb_decode.h>
#include "jansson.h"
#include "castmessage.pb.h"

#define CAST_BEAT "urn:x-cast:com.google.cast.tp.heartbeat"
#define CAST_RECEIVER "urn:x-cast:com.google.cast.receiver"
#define CAST_CONNECTION "urn:x-cast:com.google.cast.tp.connection"
#define CAST_MEDIA "urn:x-cast:com.google.cast.media"

typedef int sockfd;

typedef struct sCastCtx {
	bool			running;
	enum { CAST_DISCONNECTED, CAST_CONNECTING, CAST_CONNECTED, CAST_AUTOLAUNCH, CAST_LAUNCHING, CAST_LAUNCHED } Status;
	void			*owner;
	SSL 			*ssl;
	sockfd 			sock;
	int				reqId, waitId, waitMedia;
	pthread_t 		Thread, PingThread;
	pthread_mutex_t	Mutex, eventMutex, sslMutex;
	pthread_cond_t	eventCond;
	char 			*sessionId, *transportId;
	int				mediaSessionId;
	enum { CAST_WAIT, CAST_WAIT_MEDIA } State;
	struct in_addr	ip;
	u16_t			port;
	tQueue			eventQueue, reqQueue;
	double 			MediaVolume;
	uint32_t			lastPong;
	bool			group;
	bool			stopReceiver;
} tCastCtx;

typedef struct {
	char Type[32] ;
	union {
		json_t *msg;
		double Volume;
	} data;
} tReqItem;

bool 	SendCastMessage(struct sCastCtx *Ctx, char *ns, char *dest, char *payload, ...);
bool 	LaunchReceiver(tCastCtx *Ctx);
void 	SetVolume(tCastCtx *Ctx, double Volume);
void 	CastQueueFlush(tQueue *Queue);
bool	CastConnect(struct sCastCtx *Ctx);
void 	CastDisconnect(struct sCastCtx *Ctx);

#endif
