/*
 * HairTunes - RAOP packet handler and slave-clocked replay engine
 * Copyright (c) James Laird 2011
 * All rights reserved.
 *
 * Modularisation: philippe_44@outlook.com, 2017
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <pthread.h>
#include <openssl/aes.h>
#include <math.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdint.h>
#include <fcntl.h>
#include <assert.h>

#include "platform.h"
#include "hairtunes.h"
#include "alac.h"
#include "FLAC/stream_encoder.h"
#include "log_util.h"
#include "util.h"

#define NTP2MS(ntp) ((((ntp) >> 10) * 1000L) >> 22)
#define MS2NTP(ms) (((((__u64) (ms)) << 22) / 1000) << 10)
#define NTP2TS(ntp, rate) ((((ntp) >> 16) * (rate)) >> 16)
#define TS2NTP(ts, rate)  (((((__u64) (ts)) << 16) / (rate)) << 16)
#define MS2TS(ms, rate) ((((__u64) (ms)) * (rate)) / 1000)
#define TS2MS(ts, rate) NTP2MS(TS2NTP(ts,rate))

#define GAP_THRES	8
#define GAP_COUNT	20

extern log_level 	raop_loglevel;
static log_level 	*loglevel = &raop_loglevel;

// #define __RTP_STORE

// default buffer size
#define BUFFER_FRAMES 1024
#define MAX_PACKET    2048
#define FLAC_BLOCK_SIZE 1024
#define MAX_FLAC_BYTES (FLAC_BLOCK_SIZE*4 + 1024)

#define RTP_SYNC	(0x01)
#define NTP_SYNC	(0x02)

#define RESEND_TO	200

enum { DATA, CONTROL, TIMING };
static char *mime_types[] = { "audio/flac", "audio/L16;rate=44100;channels=2", "audio/wav" };

typedef u16_t seq_t;
typedef struct audio_buffer_entry {   // decoded audio packets
	int ready;
	u32_t rtptime;
	u32_t last_resend;
	s16_t *data;
} abuf_t;

typedef struct hairtunes_s {
#ifdef __RTP_STORE
	FILE *rtpIN, *rtpOUT, *httpOUT;
#endif
	bool running;
	unsigned char aesiv[16];
	AES_KEY aes;
	int frame_size;
	int in_frames, out_frames;
	u32_t	resent_frames, silent_frames;
	struct in_addr host;
	struct sockaddr_in rtp_host;
	struct {
		unsigned short rport, lport;
		int sock;
	} rtp_sockets[3]; 					 // data, control, timing
	struct timing_s {
		bool drift;
		u64_t local, remote;
		u32_t count, gap_count;
		s64_t gap_sum, gap_adjust;
	} timing;
	struct {
		u32_t 	rtp, time;
		u8_t  	status;
		bool	first, required;
	} synchro;
	int latency;
	abuf_t audio_buffer[BUFFER_FRAMES];
	int http_listener;
	seq_t ab_read, ab_write;
	int skip;
	pthread_mutex_t ab_mutex;
	pthread_t http_thread, rtp_thread;
	FLAC__StreamEncoder *flac_codec;
	char flac_buffer[MAX_FLAC_BYTES];
	char *silence_frame;
	int delay, silence_count;
	int flac_len;
	bool flac_header;
	alac_file *alac_codec;
	int flush_seqno;
	bool playing, silence;
	codec_t codec;
	hairtunes_cb_t callback;
	void *owner;
} hairtunes_t;


#define BUFIDX(seqno) ((seq_t)(seqno) % BUFFER_FRAMES)
static void 	buffer_alloc(abuf_t *audio_buffer, int size);
static void 	buffer_release(abuf_t *audio_buffer);
static void 	buffer_reset(abuf_t *audio_buffer);
static void 	flac_init(hairtunes_t *ctx);
static bool 	rtp_request_resend(hairtunes_t *ctx, seq_t first, seq_t last);
static bool 	rtp_request_timing(hairtunes_t *ctx);
static void*	rtp_thread_func(void *arg);
static void*	http_thread_func(void *arg);
static bool 	handle_http(hairtunes_t *ctx, int sock);
static int	  	seq_order(seq_t a, seq_t b);
static FLAC__StreamEncoderWriteStatus 	flac_write_callback(const FLAC__StreamEncoder *encoder, const FLAC__byte buffer[], size_t bytes, unsigned samples, unsigned current_frame, void *client_data);

/*---------------------------------------------------------------------------*/
static void flac_init(hairtunes_t *ctx) {
	bool ok = true;

	ctx->flac_len = 0;
	ctx->flac_header = true;

	ok &= FLAC__stream_encoder_set_verify(ctx->flac_codec, false);
	ok &= FLAC__stream_encoder_set_compression_level(ctx->flac_codec, 5);
	ok &= FLAC__stream_encoder_set_channels(ctx->flac_codec, 2);
	ok &= FLAC__stream_encoder_set_bits_per_sample(ctx->flac_codec, 16);
	ok &= FLAC__stream_encoder_set_sample_rate(ctx->flac_codec, 44100);
	ok &= FLAC__stream_encoder_set_blocksize(ctx->flac_codec, FLAC_BLOCK_SIZE);
	ok &= FLAC__stream_encoder_set_streamable_subset(ctx->flac_codec, true);
	ok &= !FLAC__stream_encoder_init_stream(ctx->flac_codec, flac_write_callback, NULL, NULL, NULL, ctx);

	if (!ok) {
		LOG_ERROR("{%p]: Cannot set FLAC parameters", ctx);
	}
}

/*---------------------------------------------------------------------------*/
static FLAC__StreamEncoderWriteStatus flac_write_callback(const FLAC__StreamEncoder *encoder, const FLAC__byte buffer[], size_t bytes, unsigned samples, unsigned current_frame, void *client_data) {
	hairtunes_t *ctx = (hairtunes_t*) client_data;

	if (ctx->flac_len + bytes <= MAX_FLAC_BYTES) {
		memcpy(ctx->flac_buffer + ctx->flac_len, buffer, bytes);
		ctx->flac_len += bytes;
	} else {
		LOG_WARN("[%p]: flac coded buffer too big %u", ctx, bytes);
	}

	return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}


/*---------------------------------------------------------------------------*/
static alac_file* alac_init(int fmtp[32]) {
	alac_file *alac;
	int sample_size = fmtp[3];

	if (sample_size != 16) {
		LOG_ERROR("sample size must be 16 %d", sample_size);
		return false;
	}

	alac = create_alac(sample_size, 2);

	if (!alac) {
		LOG_ERROR("cannot create alac codec", NULL);
		return NULL;
	}

	alac->setinfo_max_samples_per_frame = fmtp[1];
	alac->setinfo_7a 				= fmtp[2];
	alac->setinfo_sample_size 		= sample_size;
	alac->setinfo_rice_historymult = fmtp[4];
	alac->setinfo_rice_initialhistory = fmtp[5];
	alac->setinfo_rice_kmodifier 	= fmtp[6];
	alac->setinfo_7f 				= fmtp[7];
	alac->setinfo_80 				= fmtp[8];
	alac->setinfo_82 			    = fmtp[9];
	alac->setinfo_86 				= fmtp[10];
	alac->setinfo_8a_rate			= fmtp[11];
	allocate_buffers(alac);

	return alac;
}

/*---------------------------------------------------------------------------*/
hairtunes_resp_t hairtunes_init(struct in_addr host, codec_t codec, bool sync, bool drift,
								char *latencies, char *aeskey, char *aesiv, char *fmtpstr,
								short unsigned pCtrlPort, short unsigned pTimingPort,
								void *owner, hairtunes_cb_t callback)
{
	int i = 0;
	char *arg, *p;
	int fmtp[12];
	bool rc = true;
	hairtunes_t *ctx = malloc(sizeof(hairtunes_t));
	hairtunes_resp_t resp = { 0, 0, 0, 0, NULL };

	if (!ctx) return resp;

	memset(ctx, 0, sizeof(hairtunes_t));
	ctx->host = host;
	ctx->rtp_host.sin_family = AF_INET;
	ctx->rtp_host.sin_addr.s_addr = INADDR_ANY;
	pthread_mutex_init(&ctx->ab_mutex, 0);
	ctx->flush_seqno = -1;
	ctx->codec = codec;
	ctx->flac_header = false;
	ctx->latency = atoi(latencies);
	if ((p = strchr(latencies, ':')) != NULL) ctx->delay = atoi(p + 1);
	ctx->callback = callback;
	ctx->owner = owner;
	ctx->synchro.required = sync;
	ctx->timing.drift = drift;

#ifdef __RTP_STORE
	ctx->rtpIN = fopen("airplay.rtpin", "wb");
	ctx->rtpOUT = fopen("airplay.rtpout", "wb");
	ctx->httpOUT = fopen("airplay.httpout", "wb");
#endif

	memcpy(ctx->aesiv, aesiv, 16);

	ctx->rtp_sockets[CONTROL].rport = pCtrlPort;
	ctx->rtp_sockets[TIMING].rport = pTimingPort;

	AES_set_decrypt_key((unsigned char*) aeskey, 128, &ctx->aes);

	memset(fmtp, 0, sizeof(fmtp));
	while ((arg = strsep(&fmtpstr, " \t")) != NULL) fmtp[i++] = atoi(arg);

	ctx->frame_size = fmtp[1]; // stereo samples
	ctx->silence_frame = (char*) calloc(ctx->frame_size, 4);

	// alac decoder
	ctx->alac_codec = alac_init(fmtp);
	rc &= ctx->alac_codec != NULL;

	// flac encoder
	if (ctx->codec == CODEC_FLAC) {
		ctx->flac_codec = FLAC__stream_encoder_new();
		rc &= ctx->flac_codec != NULL;
		LOG_INFO("[%p]: Using FLAC", ctx);
	}

	buffer_alloc(ctx->audio_buffer, ctx->frame_size*4);

	// create rtp ports
	for (i = 0; i < 3; i++) {
		ctx->rtp_sockets[i].sock = bind_socket(&ctx->rtp_sockets[i].lport, SOCK_DGRAM);
		rc &= ctx->rtp_sockets[i].sock > 0;
	}

	// create http port and start listening
	ctx->http_listener = bind_socket(&resp.hport, SOCK_STREAM);
	i = 128*1024;
	setsockopt(ctx->http_listener, SOL_SOCKET, SO_SNDBUF, &i, sizeof(i));
	rc &= ctx->http_listener > 0;
	rc &= listen(ctx->http_listener, 1) == 0;

	resp.cport = ctx->rtp_sockets[CONTROL].lport;
	resp.tport = ctx->rtp_sockets[TIMING].lport;
	resp.aport = ctx->rtp_sockets[DATA].lport;

	if (rc) {
		ctx->running = true;
		pthread_create(&ctx->rtp_thread, NULL, rtp_thread_func, (void *) ctx);
		pthread_create(&ctx->http_thread, NULL, http_thread_func, (void *) ctx);
	} else {
		hairtunes_end(ctx);
		ctx = NULL;
	}

	resp.ctx = ctx;

	return resp;
}

/*---------------------------------------------------------------------------*/
void hairtunes_end(hairtunes_t *ctx)
{
	int i;

	if (!ctx) return;

	if (ctx->running) {
		ctx->running = false;
		pthread_join(ctx->rtp_thread, NULL);
		pthread_join(ctx->http_thread, NULL);
	}

	shutdown_socket(ctx->http_listener);
	for (i = 0; i < 3; i++) shutdown_socket(ctx->rtp_sockets[i].sock);

	delete_alac(ctx->alac_codec);
	if (ctx->flac_codec) {
		FLAC__stream_encoder_finish(ctx->flac_codec);
		FLAC__stream_encoder_delete(ctx->flac_codec);
	}

	buffer_release(ctx->audio_buffer);
	free(ctx->silence_frame);
	free(ctx);

#ifdef __RTP_STORE
	fclose(ctx->rtpIN);
	fclose(ctx->rtpOUT);
	fclose(ctx->httpOUT);
#endif
}

/*---------------------------------------------------------------------------*/
bool hairtunes_flush(hairtunes_t *ctx, unsigned short seqno, unsigned int rtpframe)
{
	bool rc = true;

	pthread_mutex_lock(&ctx->ab_mutex);

	if (seq_order(seqno, ctx->ab_read) || seqno == ctx->ab_read) {
		rc = false;
		LOG_ERROR("[%p]: FLUSH ignored as seqno (%hu) <= ab_read (%hu)", ctx, seqno, ctx->ab_read);
	} else {
		buffer_reset(ctx->audio_buffer);
		ctx->playing = false;
		ctx->flush_seqno = seqno;
		ctx->synchro.first = false;

		if (ctx->codec == CODEC_FLAC) {
			FLAC__stream_encoder_finish(ctx->flac_codec);
		}
	}

	pthread_mutex_unlock(&ctx->ab_mutex);

	return rc;
}

/*---------------------------------------------------------------------------*/
static void buffer_alloc(abuf_t *audio_buffer, int size) {
	int i;
	for (i = 0; i < BUFFER_FRAMES; i++) {
		audio_buffer[i].data = malloc(size);
		audio_buffer[i].ready = 0;
	}
}

/*---------------------------------------------------------------------------*/
static void buffer_release(abuf_t *audio_buffer) {
	int i;
	for (i = 0; i < BUFFER_FRAMES; i++) {
		free(audio_buffer[i].data);
	}
}

/*---------------------------------------------------------------------------*/
static void buffer_reset(abuf_t *audio_buffer) {
	int i;
	for (i = 0; i < BUFFER_FRAMES; i++) audio_buffer[i].ready = 0;
}

/*---------------------------------------------------------------------------*/
// the sequence numbers will wrap pretty often.
// this returns true if the second arg is after the first
static int seq_order(seq_t a, seq_t b) {
	s16_t d = b - a;
	return d > 0;
}

/*---------------------------------------------------------------------------*/
static void alac_decode(hairtunes_t *ctx, s16_t *dest, char *buf, int len) {
	unsigned char packet[MAX_PACKET];
	unsigned char iv[16];
	int aeslen;
	int outsize;
	assert(len<=MAX_PACKET);

	aeslen = len & ~0xf;
	memcpy(iv, ctx->aesiv, sizeof(iv));
	AES_cbc_encrypt((unsigned char*)buf, packet, aeslen, &ctx->aes, iv, AES_DECRYPT);
	memcpy(packet+aeslen, buf+aeslen, len-aeslen);

	decode_frame(ctx->alac_codec, packet, dest, &outsize);

	assert(outsize == ctx->frame_size*4);
}


/*---------------------------------------------------------------------------*/
static void buffer_put_packet(hairtunes_t *ctx, seq_t seqno, unsigned rtptime, bool first, char *data, int len) {
	abuf_t *abuf = NULL;

	pthread_mutex_lock(&ctx->ab_mutex);

	if (!ctx->playing) {
		if ((ctx->flush_seqno == -1 || seq_order(ctx->flush_seqno, seqno)) &&
		   ((ctx->synchro.required && ctx->synchro.first) || !ctx->synchro.required)) {
			ctx->ab_write = seqno-1;
			ctx->ab_read = seqno;
			ctx->skip = 0;
			ctx->flush_seqno = -1;
			ctx->playing = true;
			ctx->silence = true;
			ctx->synchro.first = false;
			ctx->resent_frames = ctx->silent_frames = 0;
			if (ctx->codec == CODEC_FLAC) flac_init(ctx);
		} else {
			pthread_mutex_unlock(&ctx->ab_mutex);
			return;
		}
	}

	if (!(ctx->in_frames++ & 0x1ff)) {
		LOG_INFO("[%p]: fill status [level:%hu] [W:%hu R:%hu]", ctx, (seq_t) (ctx->ab_write - ctx->ab_read), ctx->ab_write, ctx->ab_read);
	}

	if (seqno == (seq_t)(ctx->ab_write+1)) {                  // expected packet
		abuf = ctx->audio_buffer + BUFIDX(seqno);
		ctx->ab_write = seqno;
		LOG_SDEBUG("packet expected seqno:%hu rtptime:%u (W:%hu R:%hu)", seqno, rtptime, ctx->ab_write, ctx->ab_read);
	} else if (seq_order(ctx->ab_write, seqno)) {    // newer than expected
		if (rtp_request_resend(ctx, ctx->ab_write+1, seqno-1)) {
			seq_t i;
			u32_t now = gettime_ms();
			for (i = ctx->ab_write+1; i <= seqno-1; i++) {
				ctx->audio_buffer[BUFIDX(i)].rtptime = rtptime - (seqno-i)*ctx->frame_size;
				ctx->audio_buffer[BUFIDX(i)].last_resend = now;
			}
		}
		LOG_DEBUG("[%p]: packet newer seqno:%hu rtptime:%u (W:%hu R:%hu)", ctx, seqno, rtptime, ctx->ab_write, ctx->ab_read);
		abuf = ctx->audio_buffer + BUFIDX(seqno);
		ctx->ab_write = seqno;
	} else if (seqno == ctx->ab_read || seq_order(ctx->ab_read, seqno)) {     // late but not yet played
		abuf = ctx->audio_buffer + BUFIDX(seqno);
		LOG_DEBUG("[%p]: packet recovered seqno:%hu rtptime:%u (W:%hu R:%hu)", ctx, seqno, rtptime, ctx->ab_write, ctx->ab_read);
	} else {    // too late.
		LOG_DEBUG("[%p]: packet too late seqno:%hu rtptime:%u (W:%hu R:%hu)", ctx, seqno, rtptime, ctx->ab_write, ctx->ab_read);
	}

	if (abuf) {
		alac_decode(ctx, abuf->data, data, len);
		abuf->ready = 1;
		// this is the local time when this frame is expected to play
		abuf->rtptime = rtptime;
#ifdef __RTP_STORE
		fwrite(data, len, 1, ctx->rtpIN);
		fwrite(abuf->data, ctx->frame_size*4, 1, ctx->rtpOUT);
#endif
		if (ctx->silence && memcmp(abuf->data, ctx->silence_frame, ctx->frame_size*4)) {
			ctx->callback(ctx->owner, HAIRTUNES_PLAY);
			ctx->silence = false;
		}
	}

	pthread_mutex_unlock(&ctx->ab_mutex);
}

/*---------------------------------------------------------------------------*/
static void *rtp_thread_func(void *arg) {
	fd_set fds;
	int i, sock = -1;
	int count = 0;
	bool ntp_sent;
	hairtunes_t *ctx = (hairtunes_t*) arg;

	for (i = 0; i < 3; i++) {
		if (ctx->rtp_sockets[i].sock > sock) sock = ctx->rtp_sockets[i].sock;
		// send synchro requets 3 times
		ntp_sent = rtp_request_timing(ctx);
	}

	while (ctx->running) {
		ssize_t plen;
		char type, packet[MAX_PACKET];
		socklen_t rtp_client_len = sizeof(struct sockaddr_storage);
		int idx = 0;
		char *pktp = packet;
		struct timeval timeout = {0, 50*1000};

		FD_ZERO(&fds);
		for (i = 0; i < 3; i++)	{ FD_SET(ctx->rtp_sockets[i].sock, &fds); }

		if (select(sock + 1, &fds, NULL, NULL, &timeout) <= 0) continue;

		for (i = 0; i < 3; i++)
			if (FD_ISSET(ctx->rtp_sockets[i].sock, &fds)) idx = i;

		plen = recvfrom(ctx->rtp_sockets[idx].sock, packet, sizeof(packet), 0, (struct sockaddr*) &ctx->rtp_host, &rtp_client_len);

		if (!ntp_sent) {
			LOG_WARN("[%p]: NTP request not send yet", ctx);
			ntp_sent = rtp_request_timing(ctx);
		}

		if (plen < 0) continue;
		assert(plen <= MAX_PACKET);

		type = packet[1] & ~0x80;
		pktp = packet;

		switch (type) {
			seq_t seqno;
			unsigned rtptime;

			// re-sent packet
			case 0x56: {
				pktp += 4;
				plen -= 4;
			}

			// data packet
			case 0x60: {
				seqno = ntohs(*(u16_t*)(pktp+2));
				rtptime = ntohl(*(u32_t*)(pktp+4));

				// adjust pointer and length
				pktp += 12;
				plen -= 12;

				LOG_SDEBUG("[%p]: seqno:%hu rtp:%u (type: %x, first: %u)", ctx, seqno, rtptime, type, packet[1] & 0x80);

				// check if packet contains enough content to be reasonable
				if (plen < 16) break;

				if ((packet[1] & 0x80) && (type != 0x56)) {
					LOG_INFO("[%p]: 1st audio packet received", ctx);
				}

				buffer_put_packet(ctx, seqno, rtptime, packet[1] & 0x80, pktp, plen);

				break;
			}

			// sync packet
			case 0x54: {
				u32_t rtp_now_latency = ntohl(*(u32_t*)(pktp+4));
				u64_t remote = (((u64_t) ntohl(*(u32_t*)(pktp+8))) << 32) + ntohl(*(u32_t*)(pktp+12));
				u32_t rtp_now = ntohl(*(u32_t*)(pktp+16));

				// re-align timestamp and expected local playback time (mutex not needed)
				ctx->synchro.rtp = (ctx->latency) ? rtp_now - (ctx->latency*44100)/1000 : rtp_now_latency;
				ctx->synchro.time = ctx->timing.local + (u32_t) NTP2MS(remote - ctx->timing.remote);

				// now we are synced on RTP frames
				ctx->synchro.status |= RTP_SYNC;

				// 1st sync packet received (signals a restart of playback)
				if (packet[0] & 0x10) {
					ctx->synchro.first = true;
					LOG_INFO("[%p]: 1st sync packet received", ctx);
				}

				LOG_DEBUG("[%p]: sync packet rtp_latency:%u rtp:%u remote ntp:%Lx, local time %u (now:%u)",
						  ctx, rtp_now_latency, rtp_now, remote, ctx->synchro.time, gettime_ms());

				if (!count--) {
					rtp_request_timing(ctx);
					count = 3;
				}

				break;
			}

			// NTP timing packet
			case 0x53: {
				u64_t expected;
				s64_t delta = 0;
				u32_t reference   = ntohl(*(u32_t*)(pktp+12)); // only low 32 bits in our case
				u64_t remote 	  =(((u64_t) ntohl(*(u32_t*)(pktp+16))) << 32) + ntohl(*(u32_t*)(pktp+20));

				/*
				 This expected time is more than it should be due to the
				 network transit time server => client, but the timing.remote
				 also has the same error, assuming client => server is the same
				 so the delta calculated below is correct
				*/
				expected = ctx->timing.remote + MS2NTP(reference - ctx->timing.local);

				ctx->timing.remote = remote;
				ctx->timing.local = reference;
				ctx->timing.count++;

				if (!ctx->timing.drift && (ctx->synchro.status & NTP_SYNC)) {
					delta = NTP2MS((s64_t) expected - (s64_t) ctx->timing.remote);
					ctx->timing.gap_sum += delta;

					pthread_mutex_lock(&ctx->ab_mutex);

					/*
					  if expected time is more than remote, then our time is
					  running faster and we are transmitting frames too quickly,
					  so we'll run out of frames, need to add one
					*/
					if (ctx->timing.gap_sum > GAP_THRES && ctx->timing.gap_count++ > GAP_COUNT) {
						LOG_INFO("[%p]: Sending packets too fast %Ld", ctx, ctx->timing.gap_sum);
						ctx->ab_read--;
						ctx->timing.gap_sum -= GAP_THRES;
						ctx->timing.gap_adjust -= GAP_THRES;
					/*
					  if expected time is less than remote, then our time is
					  running slower and we are transmitting frames too slowly,
					  so we'll overflow frames buffer, need to remove one
					*/
					} else if (ctx->timing.gap_sum < -GAP_THRES && ctx->timing.gap_count++ > GAP_COUNT) {
						if (ctx->ab_read != ctx->ab_write) ctx->ab_read++;
						else ctx->skip++;
						ctx->timing.gap_sum += GAP_THRES;
						ctx->timing.gap_adjust += GAP_THRES;
						LOG_INFO("[%p]: Sending packets too slow %Ld (skip: %d)", ctx, ctx->timing.gap_sum, ctx->skip);
					}

					if (llabs(ctx->timing.gap_sum) < 8) ctx->timing.gap_count = 0;

					pthread_mutex_unlock(&ctx->ab_mutex);
				}

				// now we are synced on NTP (mutex not needed)
				ctx->synchro.status |= NTP_SYNC;

				LOG_DEBUG("[%p]: Timing references local:%Lu, remote:%Lx (delta:%Ld, sum:%Ld, adjust:%Ld, gaps:%d)",
						  ctx, ctx->timing.local, ctx->timing.remote, delta, ctx->timing.gap_sum, ctx->timing.gap_adjust, ctx->timing.gap_count);

				break;
			}
		}
	}

	LOG_INFO("[%p]: terminating", ctx);

	return NULL;
}

/*---------------------------------------------------------------------------*/
static bool rtp_request_timing(hairtunes_t *ctx) {
	unsigned char req[32];
	u32_t now = gettime_ms();
	int i;
	struct sockaddr_in host;

	LOG_DEBUG("[%p]: timing request now:%u (port: %hu)", ctx, now, ctx->rtp_sockets[TIMING].rport);

	req[0] = 0x80;
	req[1] = 0x52|0x80;
	*(u16_t*)(req+2) = htons(7);
	*(u32_t*)(req+4) = htonl(0);  // dummy
	for (i = 0; i < 16; i++) req[i+8] = 0;
	*(u32_t*)(req+24) = 0;
	*(u32_t*)(req+28) = htonl(now); // this is not a real NTP, but a 32 ms counter in the low part of the NTP

	if (ctx->host.s_addr != INADDR_ANY) {
		host.sin_family = AF_INET;
		host.sin_addr =	ctx->host;
	} else host = ctx->rtp_host;

	// no address from sender, need to wait for 1st packet to be received
	if (host.sin_addr.s_addr == INADDR_ANY) return false;

	host.sin_port = htons(ctx->rtp_sockets[TIMING].rport);

	if (sizeof(req) != sendto(ctx->rtp_sockets[TIMING].sock, req, sizeof(req), 0, (struct sockaddr*) &host, sizeof(host))) {
		LOG_WARN("[%p]: SENDTO failed (%s)", ctx, strerror(errno));
	}

	return true;
}

/*---------------------------------------------------------------------------*/
static bool rtp_request_resend(hairtunes_t *ctx, seq_t first, seq_t last) {
	unsigned char req[8];    // *not* a standard RTCP NACK

	// do not request silly ranges (happens in case of network large blackouts)
	if (seq_order(last, first) || last - first > BUFFER_FRAMES / 2) return false;

	ctx->resent_frames += last - first + 1;

	LOG_DEBUG("resend request [W:%hu R:%hu first=%hu last=%hu]", ctx->ab_write, ctx->ab_read, first, last);

	req[0] = 0x80;
	req[1] = 0x55|0x80;  // Apple 'resend'
	*(u16_t*)(req+2) = htons(1);  // our seqnum
	*(u16_t*)(req+4) = htons(first);  // missed seqnum
	*(u16_t*)(req+6) = htons(last-first+1);  // count

	ctx->rtp_host.sin_port = htons(ctx->rtp_sockets[CONTROL].rport);

	if (sizeof(req) != sendto(ctx->rtp_sockets[CONTROL].sock, req, sizeof(req), 0, (struct sockaddr*) &ctx->rtp_host, sizeof(ctx->rtp_host))) {
		LOG_WARN("[%p]: SENDTO failed (%s)", ctx, strerror(errno));
	}

	return true;
}


/*---------------------------------------------------------------------------*/
// get the next frame, when available. return 0 if underrun/stream reset.
static short *buffer_get_frame(hairtunes_t *ctx) {
	short buf_fill;
	abuf_t *curframe = 0;
	int i;
	u32_t now, playtime;

	// no real need for mutex here
	if (!ctx->playing) return NULL;

	// send silence if required to create enough buffering
	if (ctx->silence_count && ctx->silence_count--)	return (short*) ctx->silence_frame;

	pthread_mutex_lock(&ctx->ab_mutex);

	// skip frames if we are running late and skip could not be done in SYNC
	while (ctx->skip && ctx->ab_read != ctx->ab_write) {
		ctx->ab_read++;
		ctx->skip--;
	}

	buf_fill = ctx->ab_write - ctx->ab_read;

	if (buf_fill >= BUFFER_FRAMES) {
		LOG_ERROR("[%p]: Buffer overrun %hu", ctx, buf_fill);
		ctx->ab_read = ctx->ab_write - (BUFFER_FRAMES - 64);
	}

	now = gettime_ms();
	curframe = ctx->audio_buffer + BUFIDX(ctx->ab_read);

	/*
	  Last RTP sync might have happen recently and buffer frames have an RTP
	  older than sync.rtp, so difference will be negative, need to treat that
	  as a signed number. This works even in case of 32 bits rollover
	*/
	playtime = ctx->synchro.time + (((s32_t)(curframe->rtptime - ctx->synchro.rtp))*1000)/44100;

	if (!ctx->playing || !buf_fill || ctx->synchro.status != (RTP_SYNC | NTP_SYNC) || (now < playtime && !curframe->ready)) {
		LOG_SDEBUG("[%p]: waiting (fill:%hu, W:%hu R:%hu) now:%u, playtime:%u, wait:%d", ctx, buf_fill, ctx->ab_write, ctx->ab_read, now, playtime, playtime - now);
		// look for "blocking" frames at the top of the queue and try to catch-up
		for (i = 0; i < min(16, buf_fill); i++) {
			abuf_t *frame = ctx->audio_buffer + BUFIDX(ctx->ab_read + i);
			if (!frame->ready && frame->last_resend + RESEND_TO - now > 0x7fffffff) {
				rtp_request_resend(ctx, ctx->ab_read + i, ctx->ab_read + i);
				frame->last_resend = now;
			}
		}
		pthread_mutex_unlock(&ctx->ab_mutex);
		return NULL;
	}

	if (!(ctx->out_frames++ & 0x1ff)) {
		LOG_INFO("[%p]: drain status [level:%hu] [W:%hu R:%hu] [R:%u S:%u]",
					ctx, buf_fill, ctx->ab_write, ctx->ab_read, ctx->resent_frames, ctx->silent_frames);
	}

	// each missing packet will be requested up to (latency_frames / 16) times
	for (i = 16; seq_order(ctx->ab_read + i, ctx->ab_write); i += 16) {
		abuf_t *frame = ctx->audio_buffer + BUFIDX(ctx->ab_read + i);
		if (!frame->ready && frame->last_resend + RESEND_TO - now > 0x7fffffff) {
			rtp_request_resend(ctx, ctx->ab_read + i, ctx->ab_read + i);
			frame->last_resend = now;
		}
	}

	if (!curframe->ready) {
		LOG_INFO("[%p]: created zero frame (fill:%hu,  W:%hu R:%hu)", ctx, buf_fill - 1, ctx->ab_write, ctx->ab_read);
		memset(curframe->data, 0, ctx->frame_size*4);
		ctx->silent_frames++;
	} else {
		LOG_SDEBUG("[%p]: prepared frame (fill:%hu, W:%hu R:%hu)", ctx, buf_fill - 1, ctx->ab_write, ctx->ab_read);
	}

	curframe->ready = 0;
	ctx->ab_read++;

	pthread_mutex_unlock(&ctx->ab_mutex);

	return curframe->data;
}


/*---------------------------------------------------------------------------*/
#ifdef CHUNKED
int send_data(int sock, void *data, int len, int flags) {
	char *chunk;
	int sent;

	asprintf(&chunk, "%x\r\n", len);
	send(sock, chunk, strlen(chunk), flags);
	free(chunk);
	sent = send(sock, data, len, flags);
	send(sock, "\r\n", 2, flags);

	return sent;
}
#else
#define send_data send
#endif


/*---------------------------------------------------------------------------*/
static void *http_thread_func(void *arg) {
	signed short *inbuf;
	int frame_count = 0;
	FLAC__int32 *flac_samples = NULL;
	hairtunes_t *ctx = (hairtunes_t*) arg;
	int sock = -1;
	bool http_ready = false;
	struct timeval timeout = { 0, 0 };

	if (ctx->codec == CODEC_FLAC && ((flac_samples = malloc(2 * ctx->frame_size * sizeof(FLAC__int32))) == NULL)) {
		LOG_ERROR("[%p]: Cannot allocate FLAC sample buffer %u", ctx, ctx->frame_size);
	}

	while (ctx->running) {
		ssize_t sent;
		fd_set rfds;
		int n;
		bool res = true;

		if (sock == -1) {
			struct timeval timeout = {0, 50*1000};

			FD_ZERO(&rfds);
			FD_SET(ctx->http_listener, &rfds);

			if (select(ctx->http_listener + 1, &rfds, NULL, NULL, &timeout) > 0) {
				sock = accept(ctx->http_listener, NULL, NULL);
			}

			if (sock != -1 && ctx->running) {
				ctx->silence_count = (ctx->delay * 44100) / (ctx->frame_size * 1000);
				LOG_INFO("[%p]: got HTTP connection %u (silent frames %d)", ctx, sock, ctx->silence_count);
			} else continue;
		}

		FD_ZERO(&rfds);
		FD_SET(sock, &rfds);

		n = select(sock + 1, &rfds, NULL, NULL, &timeout);

		if (n > 0) {
			res = handle_http(ctx, sock);
			http_ready = res;
		}

		// terminate connection if required by HTTP peer
		if (n < 0 || !res) {
			closesocket(sock);
			LOG_INFO("HTTP close %u", sock);
			sock = -1;
			http_ready = false;
		}

		// wait for session to be ready before sending
		if (http_ready && (inbuf = buffer_get_frame(ctx)) != NULL) {
			int len;

			if (ctx->codec == CODEC_FLAC) {
				// send streaminfo at beginning
				if (ctx->flac_header && ctx->flac_len) {
					send_data(sock, (void*) ctx->flac_buffer, ctx->flac_len, 0);
					ctx->flac_len = 0;
					ctx->flac_header = false;
				}

				// now send body
				for (len = 0; len < 2*ctx->frame_size; len++) flac_samples[len] = inbuf[len];
				FLAC__stream_encoder_process_interleaved(ctx->flac_codec, flac_samples, ctx->frame_size);
				inbuf = (void*) ctx->flac_buffer;
				len = ctx->flac_len;
				ctx->flac_len = 0;
			} else {
				if (ctx->codec == CODEC_PCM) {
					short *p = inbuf;
					for (len = ctx->frame_size*2; len > 0; len--,p++) *p = (u8_t) *p << 8 | (u8_t) (*p >> 8);
				}
				len = ctx->frame_size*4;
			}

			if (len) {
				u32_t gap = gettime_ms();

#ifdef __RTP_STORE
				fwrite(inbuf, len, 1, ctx->httpOUT);
#endif
				LOG_SDEBUG("[%p]: HTTP sent frame count:%u bytes:%u (W:%hu R:%hu)", ctx, frame_count++, len, ctx->ab_write, ctx->ab_read);
				sent = send_data(sock, (void*) inbuf, len, 0);
				gap = gettime_ms() - gap;

				if (gap > 50) {
					LOG_ERROR("[%p]: spent %u ms in send!", ctx, gap);
				}

				if (sent != len) {
					LOG_WARN("[%p]: HTTP send() unexpected response: %li (data=%i): %s", ctx, (long int) sent, len, strerror(errno));
				}
			}

			// packet just sent, don't wait in case we have more so sent (catch-up mode)
			timeout.tv_usec = 0;
		} else {

			// nothing to send, so probably can wait a nominal amount, i.e 2/3 of length of a frame
			timeout.tv_usec = (ctx->frame_size*1000000)/44100;
		}
	}

	if (sock != -1) shutdown_socket(sock);

	if (ctx->codec == CODEC_FLAC && flac_samples) free(flac_samples);

	LOG_INFO("[%p]: terminating", ctx);

	return NULL;
}


#ifdef CHUNKED
/*----------------------------------------------------------------------------*/
static void mirror_header(key_data_t *src, key_data_t *rsp, char *key) {
	char *data;

	data = kd_lookup(src, key);
	if (data) kd_add(rsp, key, data);
}
#endif


static struct wave_header_s {
	u8_t 	chunk_id[4];
	u8_t	chunk_size[4];
	u8_t	format[4];
	u8_t	subchunk1_id[4];
	u8_t	subchunk1_size[4];
	u8_t	audio_format[2];
	u8_t	channels[2];
	u8_t	sample_rate[4];
	u8_t    byte_rate[4];
	u8_t	block_align[2];
	u8_t	bits_per_sample[2];
	u8_t	subchunk2_id[4];
	u8_t	subchunk2_size[4];
} wave_header = {
		{ 'R', 'I', 'F', 'F' },
		{ 0x24, 0xff, 0xff, 0xff },
		{ 'W', 'A', 'V', 'E' },
		{ 'f','m','t',' ' },
		{ 16, 0, 0, 0 },
		{ 1, 0 },
		{ 2, 0 },
		{ 0x44, 0xac, 0x00, 0x00  },
		{ 0x10, 0xb1, 0x02, 0x00 },
		{ 4, 0 },
		{ 16, 0 },
		{ 'd', 'a', 't', 'a' },
		{ 0x00, 0xff, 0xff, 0xff },
	};

/*----------------------------------------------------------------------------*/
static bool handle_http(hairtunes_t *ctx, int sock)
{
	char *body = NULL, method[16] = "", *str;
	key_data_t headers[64], resp[16] = { { NULL, NULL } };
	int len;

	if (!http_parse(sock, method, headers, &body, &len)) return false;

	LOG_INFO("[%p]: received %s", ctx, method);

	kd_add(resp, "Server", "HairTunes");
	kd_add(resp, "Content-Type", mime_types[ctx->codec]);

#ifdef CHUNKED
	mirror_header(headers, resp, "Connection");
	mirror_header(headers, resp, "TransferMode.DLNA.ORG");
	kd_add(resp, "Transfer-Encoding", "chunked");

	str = http_send(sock, "HTTP/1.1 200 OK", resp);
#else
	kd_add(resp, "Connection", "close");

	str = http_send(sock, "HTTP/1.0 200 OK", resp);
#endif

	LOG_INFO("[%p]: responding:\n%s", ctx, str);

	NFREE(body);
	NFREE(str);
	kd_free(resp);
	kd_free(headers);

	if (ctx->codec == CODEC_WAV) send_data(sock, (void*) &wave_header, sizeof(wave_header), 0);

	return true;
}

