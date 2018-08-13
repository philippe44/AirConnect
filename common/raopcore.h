/*
 *  AirCast: Chromecast to AirPlay
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

#ifndef __RAOPCORE_H
#define __RAOPCORE_H

#include "pthread.h"
#include "mdnsd.h"

typedef enum { RAOP_STREAM, RAOP_PLAY, RAOP_FLUSH, RAOP_PAUSE, RAOP_STOP, RAOP_VOLUME } raop_event_t ;
typedef void (*raop_cb_t)(void *owner, raop_event_t event, void *param);

struct raop_ctx_s*   raop_create(struct in_addr host, struct mdnsd *svr, char *name,
						  char *model, unsigned char mac[6], char *codec, bool metadata,
						  bool drift, char *latencies, void *owner, raop_cb_t callback);
void  		  raop_delete(struct raop_ctx_s *ctx);
void		  raop_notify(struct raop_ctx_s *ctx, raop_event_t event, void *param);

#endif
