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

#ifndef __CAST_UTIL_H
#define __CAST_UTIL_H

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

#endif

