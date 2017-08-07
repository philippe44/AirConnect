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

#ifndef __AIRPLAY_H
#define __AIRPLAY_H

#include "pthread.h"
#include "mdnsd.h"

typedef enum { RAOP_STREAM, RAOP_PLAY, RAOP_FLUSH, RAOP_PAUSE, RAOP_STOP, RAOP_VOLUME } raop_event_t ;
typedef void (*raop_cb_t)(void *owner, raop_event_t event, void *param);
typedef struct raop_ctx_s {
	struct mdns_service *svc;
	struct in_addr host;	// IP of bridge
	short unsigned port;    // RTSP port for AirPlay
	int sock;               // socket of the above
	short unsigned hport; 	// HTTP port of audio server where CC can "GET" audio
	struct in_addr peer;	// IP of the iDevice (airplay sender)
	int latency;
	bool running;
	bool use_flac;
	pthread_t thread, search_thread;
	unsigned char mac[6];
	unsigned int volume_stamp;
	struct {
		char *aesiv, *aeskey;
		char *fmtp;
	} rtsp;
	struct hairtunes_s *ht;
	raop_cb_t	callback;
	struct {
		char			DACPid[32], id[32];
		struct in_addr	host;
		u16_t			port;
		bool 			search;
	} active_remote;
	void *owner;
#ifdef _FIXME_MDNS_DEREGISTER_
	char *_fixme_id;
#endif
} raop_ctx_t;


raop_ctx_t*   raop_create(struct in_addr host, struct mdnsd *svr, char *name,
						  unsigned char mac[6], bool use_flac, int latency,
						  void *owner, raop_cb_t callback);
void  		  raop_delete(struct raop_ctx_s *ctx);
void		  raop_notify(struct raop_ctx_s *ctx, raop_event_t event, void *param);
#ifdef _FIXME_MDNS_DEREGISTER_
void		  raop_fixme_register(struct raop_ctx_s *ctx, struct mdnsd *svr);
#endif

#endif
