/*
 *  Chromecast control utils
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


#include <stdlib.h>
#include <math.h>

#include "platform.h"
#include "log_util.h"
#include "util.h"
#include "castcore.h"
#include "cast_util.h"

extern log_level cast_loglevel;
static log_level *loglevel = &cast_loglevel;

/*----------------------------------------------------------------------------*/
bool CastIsConnected(void *p)
{
	tCastCtx *Ctx = (tCastCtx*) p;
	bool status;

	pthread_mutex_lock(&Ctx->Mutex);
	status = (Ctx->ssl != NULL);
	pthread_mutex_unlock(&Ctx->Mutex);
	return status;
}


/*----------------------------------------------------------------------------*/
bool CastIsMediaSession(void *p)
{
	tCastCtx *Ctx = (tCastCtx*) p;
	bool status;

	if (!Ctx) return false;

	pthread_mutex_lock(&Ctx->Mutex);
	status = Ctx->mediaSessionId != 0;
	pthread_mutex_unlock(&Ctx->Mutex);

	return status;
}


/*----------------------------------------------------------------------------*/
void CastGetStatus(struct sCastCtx *Ctx)
{
	// SSL context might not be set yet
	if (!Ctx) return;

	pthread_mutex_lock(&Ctx->Mutex);
	SendCastMessage(Ctx, CAST_RECEIVER, NULL, "{\"type\":\"GET_STATUS\",\"requestId\":%d}", Ctx->reqId++);
	pthread_mutex_unlock(&Ctx->Mutex);
}


/*----------------------------------------------------------------------------*/
void CastGetMediaStatus(struct sCastCtx *Ctx)
{
	// SSL context might not be set yet
	if (!Ctx) return;

	pthread_mutex_lock(&Ctx->Mutex);
	if (Ctx->mediaSessionId) {
		SendCastMessage(Ctx, CAST_MEDIA, Ctx->transportId,
						"{\"type\":\"GET_STATUS\",\"requestId\":%d,\"mediaSessionId\":%d}",
						Ctx->reqId++, Ctx->mediaSessionId);
    }
	pthread_mutex_unlock(&Ctx->Mutex);
}


/*----------------------------------------------------------------------------*/
bool CastLoad(struct sCastCtx *Ctx, char *URI, char *ContentType, struct metadata_s *MetaData)
{
	json_t *msg;
	char* str;

	if (!LaunchReceiver(Ctx)) {
		LOG_ERROR("[%p]: Cannot connect Cast receiver", Ctx->owner);
		return false;
	}

	msg = json_pack("{ss,ss,ss}", "contentId", URI, "streamType", "BUFFERED",
					"contentType", ContentType);

	if (MetaData) {

		json_t *metadata = json_pack("{si,ss,ss,ss,ss,si}",
							"metadataType", 3,
							"albumName", MetaData->album, "title", MetaData->title,
							"albumArtist", MetaData->artist, "artist", MetaData->artist,
							"trackNumber", MetaData->track);

		if (MetaData->artwork) {
			json_t *artwork = json_pack("{s[{ss}]}", "images", "url", MetaData->artwork);
			json_object_update(metadata, artwork);
			json_decref(artwork);
		}

		metadata = json_pack("{s,o}", "metadata", metadata);
		json_object_update(msg, metadata);
		json_decref(metadata);
	}

	pthread_mutex_lock(&Ctx->Mutex);

	// if receiver launched and no STOP pending (precaution) just send message
	if (Ctx->Status == CAST_LAUNCHED && !Ctx->waitId) {
		Ctx->waitId = Ctx->reqId++;
		Ctx->waitMedia = Ctx->waitId;
		Ctx->mediaSessionId = 0;

		msg = json_pack("{ss,si,ss,sf,sb,so}", "type", "LOAD",
						"requestId", Ctx->waitId, "sessionId", Ctx->sessionId,
						"currentTime", 0.0, "autoplay", 0,
						"media", msg);

		str = json_dumps(msg, JSON_ENCODE_ANY | JSON_INDENT(1));
		SendCastMessage(Ctx, CAST_MEDIA, Ctx->transportId, "%s", str);
		json_decref(msg);
		NFREE(str);
	}
	// otherwise queue it for later
	else {
		tReqItem *req = malloc(sizeof(tReqItem));
		strcpy(req->Type, "LOAD");
		req->data.msg = msg;
		QueueInsert(&Ctx->reqQueue, req);
		LOG_INFO("[%p]: Queuing %s", Ctx->owner, req->Type);
	}

	pthread_mutex_unlock(&Ctx->Mutex);

	return true;
}


/*----------------------------------------------------------------------------*/
void CastSimple(struct sCastCtx *Ctx, char *Type)
{
	// lock on wait for a Cast response
	pthread_mutex_lock(&Ctx->Mutex);
	if (Ctx->Status == CAST_LAUNCHED && !Ctx->waitId) {
		// no media session, nothing to do
		if (Ctx->mediaSessionId) {
			Ctx->waitId = Ctx->reqId++;

			SendCastMessage(Ctx, CAST_MEDIA, Ctx->transportId,
							"{\"type\":\"%s\",\"requestId\":%d,\"mediaSessionId\":%d}",
							Type, Ctx->waitId, Ctx->mediaSessionId);

		}
		else {
			LOG_WARN("[%p]: %s req w/o a session", Type, Ctx->owner);
	   }
	}
	else {
		tReqItem *req = malloc(sizeof(tReqItem));
		strcpy(req->Type, Type);
		QueueInsert(&Ctx->reqQueue, req);
		LOG_INFO("[%p]: Queuing %s", Ctx->owner, req->Type);
	}

	pthread_mutex_unlock(&Ctx->Mutex);
}


/*----------------------------------------------------------------------------*/
void CastStop(struct sCastCtx *Ctx)
{
	// lock on wait for a Cast response
	pthread_mutex_lock(&Ctx->Mutex);
	CastQueueFlush(&Ctx->reqQueue);
	if (Ctx->Status == CAST_LAUNCHED && Ctx->mediaSessionId) {
		Ctx->waitId = Ctx->reqId++;

		// VERSION_1_24
		if (Ctx->stopReceiver) {
			SendCastMessage(Ctx, CAST_RECEIVER, NULL,
						"{\"type\":\"STOP\",\"requestId\":%d,\"sessionId\":%d}", Ctx->waitId, Ctx->mediaSessionId);
			Ctx->Status = CAST_CONNECTED;

		}
		else {
			SendCastMessage(Ctx, CAST_MEDIA, Ctx->transportId,
							"{\"type\":\"STOP\",\"requestId\":%d,\"mediaSessionId\":%d}",
							Ctx->waitId, Ctx->mediaSessionId);
		}

		Ctx->mediaSessionId = 0;
	}
	else {
		if (Ctx->Status == CAST_LAUNCHING) {
			Ctx->Status = CAST_CONNECTED;
			LOG_WARN("[%p]: Stop while still launching receiver", Ctx->owner);
		} else {
			LOG_WARN("[%p]: Stop w/o session or connect", Ctx->owner);
		}
	}

	pthread_mutex_unlock(&Ctx->Mutex);
}


/*----------------------------------------------------------------------------*/
void CastPowerOff(struct sCastCtx *Ctx)
{
	CastDisconnect(Ctx);
}


/*----------------------------------------------------------------------------*/
void CastPowerOn(struct sCastCtx *Ctx)
{
	CastConnect(Ctx);
}


/*----------------------------------------------------------------------------*/
#if 1
void CastSetDeviceVolume(struct sCastCtx *Ctx, double Volume, bool Queue)
{
	if (Ctx->group) Volume = Volume * Ctx->MediaVolume;

	if (Volume > 1.0) Volume = 1.0;

	pthread_mutex_lock(&Ctx->Mutex);

	if (Ctx->Status == CAST_LAUNCHED && (!Ctx->waitId || !Queue)) {

		if (Volume) {
			SendCastMessage(Ctx, CAST_RECEIVER, NULL,
						"{\"type\":\"SET_VOLUME\",\"requestId\":%d,\"volume\":{\"level\":%0.4lf}}",
						Ctx->reqId++, Volume);

			SendCastMessage(Ctx, CAST_RECEIVER, NULL,
						"{\"type\":\"SET_VOLUME\",\"requestId\":%d,\"volume\":{\"muted\":false}}",
						Ctx->reqId);

		} else {
			SendCastMessage(Ctx, CAST_RECEIVER, NULL,
						"{\"type\":\"SET_VOLUME\",\"requestId\":%d,\"volume\":{\"muted\":true}}",
						Ctx->reqId);
	  }

		// Only set waitId if this is NOT queue bypass
		if (Queue) Ctx->waitId = Ctx->reqId;

		Ctx->reqId++;

	}
	// otherwise queue it for later
	else {
		tReqItem *req = malloc(sizeof(tReqItem));
		strcpy(req->Type, "SET_VOLUME");
		req->data.Volume = Volume;
		QueueInsert(&Ctx->reqQueue, req);
		LOG_INFO("[%p]: Queuing %s", Ctx->owner, req->Type);
	}

	pthread_mutex_unlock(&Ctx->Mutex);
}
/*----------------------------------------------------------------------------*/
#else
void CastSetDeviceVolume(struct sCastCtx *Ctx, double Volume, bool Queue)
{
	if (Ctx->group) Volume = Volume * Ctx->MediaVolume;

	if (Volume > 1.0) Volume = 1.0;

	pthread_mutex_lock(&Ctx->Mutex);

	if (Volume) {
		SendCastMessage(Ctx, CAST_RECEIVER, NULL,
						"{\"type\":\"SET_VOLUME\",\"requestId\":%d,\"volume\":{\"level\":%ff}}",
						Ctx->reqId++, Volume);

		SendCastMessage(Ctx, CAST_RECEIVER, NULL,
						"{\"type\":\"SET_VOLUME\",\"requestId\":%d,\"volume\":{\"muted\":false}}",
						Ctx->reqId++);
	}
	else {
		SendCastMessage(Ctx, CAST_RECEIVER, NULL,
						"{\"type\":\"SET_VOLUME\",\"requestId\":%d,\"volume\":{\"muted\":true}}",
						Ctx->reqId++);
	}

	pthread_mutex_unlock(&Ctx->Mutex);
}
#endif


/*----------------------------------------------------------------------------*/
int CastSeek(char *ControlURL, unsigned Interval)
{
	int rc = 0;

	return rc;
}





