/*
 *  Chromecast protocol handler 
 *  (c) Philippe 2016-2017, philippe_44@outlook.com
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

#ifndef __CASTITF_H
#define __CASTITF_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <pb_encode.h>
#include <pb_decode.h>
#include "jansson.h"
#include "castmessage.pb.h"

void InitSSL(void);
void EndSSL(void);

struct sCastCtx;

json_t*	GetTimedEvent(void *p, u32_t msWait);
void*	CreateCastDevice(void *owner, bool group, bool stopReceiver, struct in_addr ip, u16_t port, double MediaVolume);
bool 	UpdateCastDevice(struct sCastCtx *Ctx, struct in_addr ip, u16_t port);
void 	DeleteCastDevice(struct sCastCtx *Ctx);
bool	CastIsConnected(struct sCastCtx *Ctx);
bool 	CastIsMediaSession(struct sCastCtx *Ctx);
struct in_addr GetAddr(struct sCastCtx *Ctx);

#endif
