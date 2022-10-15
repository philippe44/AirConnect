/*
 *  Chromecast control utils
 *
 *  (c) Philippe, philippe_44@outlook.com
 *
 * See LICENSE
 *
 *
 */

# pragma once

#include "metadata.h"

typedef enum { CAST_PLAY, CAST_PAUSE, CAST_STOP } tCastAction;

struct sq_metadata_s;
struct sMRConfig;
struct sCastCtx;

void	CastGetStatus(struct sCastCtx *Ctx);
void	CastGetMediaStatus(struct sCastCtx *Ctx);

void 	CastPowerOff(struct sCastCtx *Ctx);
void 	CastPowerOn(struct sCastCtx *Ctx);
void 	CastRelease(struct sCastCtx *Ctx);

void 	CastStop(struct sCastCtx *Ctx);
#define CastPlay(Ctx)	CastSimple(Ctx, "PLAY")
#define CastPause(Ctx)	CastSimple(Ctx, "PAUSE")
void 	CastSimple(struct sCastCtx *Ctx, char *Type);
bool	CastLoad(struct sCastCtx *Ctx, char *URI, char *ContentType, struct metadata_s *MetaData);
void 	CastSetDeviceVolume(struct sCastCtx *p, double Volume, bool Queue);

