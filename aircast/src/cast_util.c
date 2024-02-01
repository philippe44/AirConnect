/*
 *  Chromecast misc utils
 *
 *  (c) Philippe, philippe_44@outlook.com
 *
 * See LICENSE
 *
 */

#include <stdlib.h>

#include "platform.h"
#include "metadata.h"
#include "cross_log.h"
#include "castcore.h"
#include "cast_util.h"
#include "castitf.h"

extern log_level cast_loglevel;
static log_level *loglevel = &cast_loglevel;

/*----------------------------------------------------------------------------*/
static json_t* BuildMetaData(struct metadata_s* MetaData) {
	if (!MetaData) return NULL;

	json_t* json = json_pack("{si,ss,ss,ss,ss,si}",
		"metadataType", 3,
		"albumName", MetaData->album, "title", MetaData->title,
		"albumArtist", MetaData->artist, "artist", MetaData->artist,
		"trackNumber", MetaData->track);

	if (MetaData->artwork) {
		json_t* artwork = json_pack("{s[{ss}]}", "images", "url", MetaData->artwork);
		json_object_update(json, artwork);
		json_decref(artwork);
	}

	return json_pack("{so}", "metadata", json);
}


/*----------------------------------------------------------------------------*/
bool CastIsConnected(struct sCastCtx *Ctx) {
	if (!Ctx) return false;

	pthread_mutex_lock(&Ctx->Mutex);
	bool status = Ctx->Status >= CAST_CONNECTED;
	pthread_mutex_unlock(&Ctx->Mutex);
	return status;
}

/*----------------------------------------------------------------------------*/
bool CastIsMediaSession(struct sCastCtx *Ctx) {
	if (!Ctx) return false;

	pthread_mutex_lock(&Ctx->Mutex);
	bool status = Ctx->mediaSessionId != 0;
	pthread_mutex_unlock(&Ctx->Mutex);

	return status;
}

/*----------------------------------------------------------------------------*/
void CastGetStatus(struct sCastCtx* Ctx) {
	// SSL context might not be set yet
	if (!Ctx) return;

	pthread_mutex_lock(&Ctx->Mutex);

	json_t* msg = json_pack("{ss,si}", "type", "GET_STATUS", "requestId", Ctx->reqId++);

	char* str = json_dumps(msg, JSON_ENCODE_ANY | JSON_INDENT(1));
	json_decref(msg);

	SendCastMessage(Ctx, CAST_RECEIVER, NULL, "%s", str);
	NFREE(str);

	pthread_mutex_unlock(&Ctx->Mutex);
}

/*----------------------------------------------------------------------------*/
void CastGetMediaStatus(struct sCastCtx *Ctx) {
	// SSL context might not be set yet
	if (!Ctx) return;

	pthread_mutex_lock(&Ctx->Mutex);

	if (Ctx->mediaSessionId) {
		json_t* msg = json_pack("{ss,si,si}", "type", "GET_STATUS",
								"mediaSessionId", Ctx->mediaSessionId,
								"requestId", Ctx->reqId++); 
		
		char* str = json_dumps(msg, JSON_ENCODE_ANY | JSON_INDENT(1));
		json_decref(msg);

		SendCastMessage(Ctx, CAST_MEDIA, Ctx->transportId, "%s", str);
		NFREE(str);
    }

	pthread_mutex_unlock(&Ctx->Mutex);
}

/*----------------------------------------------------------------------------*/
#define LOAD_FLUSH
bool CastLoad(struct sCastCtx *Ctx, char *URI, char *ContentType, const char *Name, struct metadata_s *MetaData, uint64_t StartTime) {
	json_t *msg, *customData;
	char* str;

	if (!LaunchReceiver(Ctx)) {
		LOG_ERROR("[%p]: Cannot connect Cast receiver", Ctx->owner);
		return false;
	}

	msg = json_pack("{ss,ss,ss}", "contentId", URI, "streamType", (MetaData && !MetaData->duration) ? "LIVE" : "BUFFERED", 
						          "contentType", ContentType);

	if (MetaData && MetaData->duration) {
		json_t* duration = json_pack("{sf}", "duration", (double)MetaData->duration / 1000);
		json_object_update(msg, duration);
		json_decref(duration);
	}

	if (StartTime) customData = json_pack("{s{sssI}}", "customData", "deviceName", Name, "startTime", StartTime);
	else customData = json_pack("{s{ss}}", "customData", "deviceName", Name);
	json_object_update(msg, customData);
	json_decref(customData);

	json_t* jsonMetaData = BuildMetaData(MetaData);

	if (jsonMetaData) {
		json_object_update(msg, jsonMetaData);
		json_decref(jsonMetaData);
	}

	pthread_mutex_lock(&Ctx->Mutex);

#ifdef LOAD_FLUSH
	if (Ctx->Status == CAST_LAUNCHED && (!Ctx->waitId || Ctx->waitMedia)) {

		/*
		For some reason a LOAD request is pending (maybe not enough data have
		been buffered yet, so LOAD has not been acknowledged, a stop might
		be stuck in the queue and the source does not send any more data, so
		this is a deadlock (see usage with iOS 10.x). Best is to have LOAD
		request flushing the queue then
		*/
		if (Ctx->waitMedia) CastQueueFlush(&Ctx->reqQueue);
#else
	if (Ctx->Status == CAST_LAUNCHED && !Ctx->waitId) {
#endif

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

		LOG_INFO("[%p]: Immediate LOAD (id:%u)", Ctx->owner, Ctx->waitId);
	} else {
		// otherwise queue it for later
		tReqItem *req = malloc(sizeof(tReqItem));
#ifndef LOAD_FLUSH
		// if waiting for a media, need to unlock queue and take precedence
		Ctx->waitMedia = 0;
#endif
		strcpy(req->Type, "LOAD");
		req->data.msg = msg;
		queue_insert(&Ctx->reqQueue, req);
		LOG_INFO("[%p]: Queuing %s", Ctx->owner, req->Type);
	}

	pthread_mutex_unlock(&Ctx->Mutex);


	return true;
}

/*----------------------------------------------------------------------------*/
void CastSimple(struct sCastCtx *Ctx, char *Type) {
	// lock on wait for a Cast response
	pthread_mutex_lock(&Ctx->Mutex);

	if (Ctx->Status == CAST_LAUNCHED && !Ctx->waitId) {
		// no media session, nothing to do
		if (Ctx->mediaSessionId) {
			Ctx->waitId = Ctx->reqId++;

			SendCastMessage(Ctx, CAST_MEDIA, Ctx->transportId,
							"{\"type\":\"%s\",\"requestId\":%d,\"mediaSessionId\":%d}",
							Type, Ctx->waitId, Ctx->mediaSessionId);

			LOG_INFO("[%p]: Immediate %s (id:%u)", Ctx->owner, Type, Ctx->waitId);

		} else {
			LOG_WARN("[%p]: %s req w/o a session", Ctx->owner, Type);
	   }

	} else {
		tReqItem *req = malloc(sizeof(tReqItem));
		strcpy(req->Type, Type);
		queue_insert(&Ctx->reqQueue, req);
		LOG_INFO("[%p]: Queuing %s", Ctx->owner, req->Type);
	}

	pthread_mutex_unlock(&Ctx->Mutex);
}

/*----------------------------------------------------------------------------*/
void CastPlay(struct sCastCtx* Ctx, struct metadata_s* MetaData) {
	// lock on wait for a Cast response
	pthread_mutex_lock(&Ctx->Mutex);

	json_t* customData;
	if (MetaData && MetaData->live_duration != -1) customData = json_pack("{si}", "liveDuration", MetaData->live_duration);
	else customData = json_object();

	json_t* item = BuildMetaData(MetaData);

	if (item) {
		json_object_update(customData, item);
		json_decref(item);
	}

	if (Ctx->Status == CAST_LAUNCHED && !Ctx->waitId) {
		// no media session, nothing to do
		if (Ctx->mediaSessionId) {
			Ctx->waitId = Ctx->reqId++;

			json_t* msg = json_pack("{ss,si,si}", "type", "PLAY", "requestId", Ctx->waitId,
												  "mediaSessionId", Ctx->mediaSessionId);

			item = json_pack("{so}", "customData", customData);
			json_object_update(msg, item);
			json_decref(item);

			char* str = json_dumps(msg, JSON_ENCODE_ANY | JSON_INDENT(1));
			json_decref(msg);

			SendCastMessage(Ctx, CAST_MEDIA, Ctx->transportId, "%s", str);
			NFREE(str);

			LOG_INFO("[%p]: Immediate PLAY (id:%u)", Ctx->owner, Ctx->waitId);

		} else {
			json_decref(customData);
			LOG_WARN("[%p]: PLAY req w/o a session", Ctx->owner);
		}

	} else {
		tReqItem* req = malloc(sizeof(tReqItem));
		req->data.customData = customData;
		strcpy(req->Type, "PLAY");
		queue_insert(&Ctx->reqQueue, req);
		LOG_INFO("[%p]: Queuing %s", Ctx->owner, req->Type);
	}

	pthread_mutex_unlock(&Ctx->Mutex);
}

/*----------------------------------------------------------------------------*/
void CastStop(struct sCastCtx *Ctx) {
	// lock on wait for a Cast response
	pthread_mutex_lock(&Ctx->Mutex);

	CastQueueFlush(&Ctx->reqQueue);

	// if a session is active, stop can be sent-immediately
	if (Ctx->mediaSessionId) {

		Ctx->waitId = Ctx->reqId++;

		// version 1.24
		if (Ctx->stopReceiver) {
			SendCastMessage(Ctx, CAST_RECEIVER, NULL,
						"{\"type\":\"STOP\",\"requestId\":%d}", Ctx->waitId);
			Ctx->Status = CAST_CONNECTED;

		} else {
			SendCastMessage(Ctx, CAST_MEDIA, Ctx->transportId,
							"{\"type\":\"STOP\",\"requestId\":%d,\"mediaSessionId\":%d}",
							Ctx->waitId, Ctx->mediaSessionId);
		}

		Ctx->mediaSessionId = 0;
		LOG_INFO("[%p]: Immediate STOP (id:%u)", Ctx->owner, Ctx->waitId);

	// waiting for a session, need to queue the stop
	} else if (Ctx->waitMedia) {

		tReqItem *req = malloc(sizeof(tReqItem));
		strcpy(req->Type, "STOP");
		queue_insert(&Ctx->reqQueue, req);
		LOG_INFO("[%p]: Queuing %s", Ctx->owner, req->Type);

	// launching happening, just go back to CONNECT mode
	} else if (Ctx->Status == CAST_LAUNCHING) {
		Ctx->Status = CAST_CONNECTED;
		LOG_WARN("[%p]: Stop while still launching receiver", Ctx->owner);
	// a random stop
	} else {
		LOG_WARN("[%p]: Stop w/o session or connect", Ctx->owner);
	}

	pthread_mutex_unlock(&Ctx->Mutex);
}

/*----------------------------------------------------------------------------*/
void CastPowerOff(struct sCastCtx *Ctx) {
	CastRelease(Ctx);
	CastDisconnect(Ctx);
}

/*----------------------------------------------------------------------------*/
bool CastPowerOn(struct sCastCtx *Ctx) {
	return CastConnect(Ctx);
}

/*----------------------------------------------------------------------------*/
void CastRelease(struct sCastCtx *Ctx) {
	pthread_mutex_lock(&Ctx->Mutex);
	if (Ctx->Status != CAST_DISCONNECTED) {
		SendCastMessage(Ctx, CAST_RECEIVER, NULL,
						"{\"type\":\"STOP\",\"requestId\":%d}", Ctx->reqId++);
		Ctx->Status = CAST_CONNECTED;
	}
	pthread_mutex_unlock(&Ctx->Mutex);
}


/*----------------------------------------------------------------------------*/
void CastSetDeviceVolume(struct sCastCtx *Ctx, double Volume, bool Queue) {
	pthread_mutex_lock(&Ctx->Mutex);
	
	if (Volume > 1.0) Volume = 1.0;

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

		LOG_DEBUG("[%p]: Immediate VOLUME (id:%u)", Ctx->owner, Ctx->reqId);

		Ctx->reqId++;
	} else {
		// otherwise queue it for later
		tReqItem *req = malloc(sizeof(tReqItem));
		strcpy(req->Type, "SET_VOLUME");
		req->data.volume = Volume;
		queue_insert(&Ctx->reqQueue, req);
		LOG_INFO("[%p]: Queuing %s", Ctx->owner, req->Type);
	}

	pthread_mutex_unlock(&Ctx->Mutex);
}

/*----------------------------------------------------------------------------*/
int CastSeek(char *ControlURL, unsigned Interval) {
	return 0;
}

