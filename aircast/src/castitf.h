/*
 * Chromecast internal interface
 * 
 * (c) Philippe 2016-2017, philippe_44@outlook.com
 * 
 * See LICENSE
 *
 */

#pragma once

#include <stdint.h>

#include "jansson.h"

struct sCastCtx;

json_t*	GetTimedEvent(struct sCastCtx *Ctx, uint32_t msWait);
void*	CreateCastDevice(void *owner, bool group, bool stopReceiver, struct in_addr ip, uint16_t port, double MediaVolume);
bool 	UpdateCastDevice(struct sCastCtx *Ctx, struct in_addr ip, uint16_t port);
void 	DeleteCastDevice(struct sCastCtx *Ctx);
bool	CastIsConnected(struct sCastCtx *Ctx);
bool 	CastIsMediaSession(struct sCastCtx *Ctx);
struct in_addr GetAddr(struct sCastCtx *Ctx);

