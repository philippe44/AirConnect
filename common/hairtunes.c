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
#include "layer3.h"
#include "log_util.h"
#include "util.h"

#define NTP2MS(ntp) ((((ntp) >> 10) * 1000L) >> 22)
#define MS2NTP(ms) (((((u64_t) (ms)) << 22) / 1000) << 10)
#define NTP2TS(ntp, rate) ((((ntp) >> 16) * (rate)) >> 16)
#define TS2NTP(ts, rate)  (((((u64_t) (ts)) << 16) / (rate)) << 16)
#define MS2TS(ms, rate) ((((u64_t) (ms)) * (rate)) / 1000)
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
#define ENCODE_BUFFER_SIZE	(max(MAX_FLAC_BYTES, SHINE_MAX_SAMPLES*2*2*2))
#define TAIL_SIZE (2048*1024)

#define RTP_SYNC	(0x01)
#define NTP_SYNC	(0x02)

#define RESEND_TO	200

#define	ICY_INTERVAL 16384
#define ICY_LEN_MAX		(255*16+1)

enum { DATA, CONTROL, TIMING };
static char *mime_types[] = { "audio/mp3", "audio/flac", "audio/L16;rate=44100;channels=2", "audio/wav" };


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

typedef u16_t seq_t;
typedef struct audio_buffer_entry {   // decoded audio packets
	int ready;
	u32_t rtptime, last_resend;
	s16_t *data;
	int len;
} abuf_t;

typedef struct hairtunes_s {
#ifdef __RTP_STORE
	FILE *rtpIN, *rtpOUT, *httpOUT;
#endif
	bool running;
	unsigned char aesiv[16];
	AES_KEY aes;
	bool decrypt, range;
	int frame_size;
	int in_frames, out_frames;
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
	struct {
		u32_t time;
		seq_t seqno;
		u32_t rtptime;
	} record;
	int latency;			// rtp hold depth in samples
	int delay;              // http startup silence fill frames
	u32_t resent_frames;	// total recovered frames
	u32_t silent_frames;	// total silence frames
	u32_t silence_count;	// counter for startup silence frames
	u32_t filled_frames;    // silence frames in current silence episode
	bool http_fill;         // fill when missing or just wait
	int skip;				// number of frames to skip to keep sync alignement
	abuf_t audio_buffer[BUFFER_FRAMES];
	int http_listener;
	seq_t ab_read, ab_write;
	pthread_mutex_t ab_mutex;
	pthread_t http_thread, rtp_thread;
	struct {
		char 		buffer[ENCODE_BUFFER_SIZE];
		void 		*codec;
		encode_t 	config;
		int 		len;
		bool 		header;
	} encode;
	struct {
		size_t interval, remain;
		bool  updated;
	} icy;
	struct metadata_s metadata;
	char *silence_frame;
	alac_file *alac_codec;
	int flush_seqno;
	bool playing, silence, http_ready;
	hairtunes_cb_t callback;
	void *owner;
	char *http_tail;
	size_t http_count;
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
	FLAC__StreamEncoder *codec;

	ctx->encode.len = 0;
	ctx->encode.header = true;
	ctx->encode.codec = FLAC__stream_encoder_new();
	codec = (FLAC__StreamEncoder*) ctx->encode.codec;

	LOG_INFO("[%p]: Using FLAC-%u (%p)", ctx, ctx->encode.config.flac.level, ctx->encode.codec);

	ok &= FLAC__stream_encoder_set_verify(codec, false);
	ok &= FLAC__stream_encoder_set_compression_level(codec, ctx->encode.config.flac.level);
	ok &= FLAC__stream_encoder_set_channels(codec, 2);
	ok &= FLAC__stream_encoder_set_bits_per_sample(codec, 16);
	ok &= FLAC__stream_encoder_set_sample_rate(codec, 44100);
	ok &= FLAC__stream_encoder_set_blocksize(codec, FLAC_BLOCK_SIZE);
	ok &= FLAC__stream_encoder_set_streamable_subset(codec, true);
	ok &= !FLAC__stream_encoder_init_stream(codec, flac_write_callback, NULL, NULL, NULL, ctx);

	if (!ok) {
		LOG_ERROR("{%p]: Cannot set FLAC parameters", ctx);
	}
}

/*---------------------------------------------------------------------------*/
static FLAC__StreamEncoderWriteStatus flac_write_callback(const FLAC__StreamEncoder *encoder, const FLAC__byte buffer[], size_t bytes, unsigned samples, unsigned current_frame, void *client_data) {
	hairtunes_t *ctx = (hairtunes_t*) client_data;

	if (ctx->encode.len + bytes <= MAX_FLAC_BYTES) {
		memcpy(ctx->encode.buffer + ctx->encode.len, buffer, bytes);
		ctx->encode.len += bytes;
	} else {
		LOG_WARN("[%p]: flac coded buffer too big %u", ctx, bytes);
	}

	return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

/*---------------------------------------------------------------------------*/
static void mp3_init(hairtunes_t *ctx) {
	shine_config_t config;

	ctx->encode.len = 0;

	shine_set_config_mpeg_defaults(&config.mpeg);
	config.wave.samplerate = 44100;
	config.wave.channels = 2;
	config.mpeg.bitr = ctx->encode.config.mp3.bitrate ? ctx->encode.config.mp3.bitrate : 128;
	config.mpeg.mode = STEREO;

	ctx->encode.codec = shine_initialise(&config);
	LOG_INFO("[%p]: Using shine MP3-%u (%p)", ctx, ctx->encode.config.mp3.bitrate, ctx->encode.codec);
}

/*---------------------------------------------------------------------------*/
static void encoder_close(hairtunes_t *ctx) {
	if (!ctx->encode.codec) return;

	if (ctx->encode.config.codec == CODEC_FLAC) {
		FLAC__stream_encoder_finish(ctx->encode.codec);
		FLAC__stream_encoder_delete(ctx->encode.codec);
	} else if (ctx->encode.config.codec == CODEC_MP3) {
		int len;
		shine_flush(ctx->encode.codec, &len);
		shine_close(ctx->encode.codec);
	}

	ctx->encode.codec = NULL;
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
hairtunes_resp_t hairtunes_init(struct in_addr host, encode_t codec,
								bool sync, bool drift, bool range, char *latencies,
								char *aeskey, char *aesiv, char *fmtpstr,
								short unsigned pCtrlPort, short unsigned pTimingPort,
								void *owner, hairtunes_cb_t callback)
{
	int i = 0;
	char *arg, *p;
	int fmtp[12];
	bool rc = true;
	hairtunes_t *ctx = calloc(1, sizeof(hairtunes_t));
	hairtunes_resp_t resp = { 0, 0, 0, 0, NULL };

	if (!ctx) return resp;

	ctx->http_tail = malloc(TAIL_SIZE);
	if (!ctx->http_tail) {
		free(ctx);
		return resp;
	}
	ctx->host = host;
	ctx->decrypt = false;
	ctx->rtp_host.sin_family = AF_INET;
	ctx->rtp_host.sin_addr.s_addr = INADDR_ANY;
	pthread_mutex_init(&ctx->ab_mutex, 0);
	ctx->flush_seqno = -1;
	ctx->encode.config = codec;
	ctx->encode.header = false;
	ctx->latency = atoi(latencies);
	ctx->latency = (ctx->latency * 44100) / 1000;
	if (strstr(latencies, ":f")) ctx->http_fill = true;
	ctx->callback = callback;
	ctx->owner = owner;
	ctx->synchro.required = sync;
	ctx->timing.drift = drift;
	ctx->range = range;
	ctx->http_ready = false;

	// write pointer = last written, read pointer = next to read so fill = w-r+1
	ctx->ab_read = ctx->ab_write + 1;

#ifdef __RTP_STORE
	ctx->rtpIN = fopen("airplay.rtpin", "wb");
	ctx->rtpOUT = fopen("airplay.rtpout", "wb");
	ctx->httpOUT = fopen("airplay.httpout", "wb");
#endif

	ctx->rtp_sockets[CONTROL].rport = pCtrlPort;
	ctx->rtp_sockets[TIMING].rport = pTimingPort;

	if (aesiv && aeskey) {
		memcpy(ctx->aesiv, aesiv, 16);
		AES_set_decrypt_key((unsigned char*) aeskey, 128, &ctx->aes);
		ctx->decrypt = true;
	}

	memset(fmtp, 0, sizeof(fmtp));
	while ((arg = strsep(&fmtpstr, " \t")) != NULL) fmtp[i++] = atoi(arg);

	ctx->frame_size = fmtp[1];
	ctx->silence_frame = (char*) calloc(ctx->frame_size, 4);
	if ((p = strchr(latencies, ':')) != NULL) {
		ctx->delay = atoi(p + 1);
		ctx->delay = (ctx->delay * 44100) / (ctx->frame_size * 1000);
	}

	// alac decoder
	ctx->alac_codec = alac_init(fmtp);
	rc &= ctx->alac_codec != NULL;

	buffer_alloc(ctx->audio_buffer, ctx->frame_size*4);

	// create rtp ports
	for (i = 0; i < 3; i++) {
		ctx->rtp_sockets[i].sock = bind_socket(&ctx->rtp_sockets[i].lport, SOCK_DGRAM);
		rc &= ctx->rtp_sockets[i].sock > 0;
	}

	// create http port and start listening
	ctx->http_listener = bind_socket(&resp.hport, SOCK_STREAM);
	i = 128*1024;
	setsockopt(ctx->http_listener, SOL_SOCKET, SO_SNDBUF, (void*) &i, sizeof(i));
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
void hairtunes_metadata(struct hairtunes_s *ctx, metadata_t *metadata) {
	pthread_mutex_lock(&ctx->ab_mutex);
	free_metadata(&ctx->metadata);
	dup_metadata(&ctx->metadata, metadata);
	ctx->icy.updated = true;
	pthread_mutex_unlock(&ctx->ab_mutex);
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
	if (ctx->encode.codec) {
		if (ctx->encode.config.codec == CODEC_FLAC) {
			FLAC__stream_encoder_finish(ctx->encode.codec);
			FLAC__stream_encoder_delete(ctx->encode.codec);
		} else if (ctx->encode.config.codec == CODEC_MP3) {
			shine_close(ctx->encode.codec);
		}
	}

	buffer_release(ctx->audio_buffer);
	free(ctx->silence_frame);
	free(ctx->http_tail);
	free_metadata(&ctx->metadata);
	free(ctx);

#ifdef __RTP_STORE
	fclose(ctx->rtpIN);
	fclose(ctx->rtpOUT);
	fclose(ctx->httpOUT);
#endif
}

/*---------------------------------------------------------------------------*/
bool hairtunes_flush(hairtunes_t *ctx, unsigned short seqno, unsigned int rtptime)
{
	bool rc = true;
	u32_t now = gettime_ms();

	if (now < ctx->record.time + 250 || (ctx->record.seqno == seqno && ctx->record.rtptime == rtptime)) {
		rc = false;
		LOG_ERROR("[%p]: FLUSH ignored as same as RECORD (%hu - %u)", ctx, seqno, rtptime);
	} else {
		pthread_mutex_lock(&ctx->ab_mutex);
		buffer_reset(ctx->audio_buffer);
		ctx->playing = false;
		ctx->flush_seqno = seqno;
		ctx->synchro.first = false;
		ctx->http_ready = false;
		encoder_close(ctx);
		pthread_mutex_unlock(&ctx->ab_mutex);
	}

	LOG_INFO("[%p]: flush %hu %u", ctx, seqno, rtptime);

	return rc;
}

/*---------------------------------------------------------------------------*/
void hairtunes_record(hairtunes_t *ctx, unsigned short seqno, unsigned rtptime)
{
	ctx->record.seqno = seqno;
	ctx->record.rtptime = rtptime;
	ctx->record.time = gettime_ms();

	LOG_INFO("[%p]: record %hu %u", ctx, seqno, rtptime);
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
static void alac_decode(hairtunes_t *ctx, s16_t *dest, char *buf, int len, int *outsize) {
	unsigned char packet[MAX_PACKET];
	unsigned char iv[16];
	int aeslen;
	assert(len<=MAX_PACKET);

	if (ctx->decrypt) {
		aeslen = len & ~0xf;
		memcpy(iv, ctx->aesiv, sizeof(iv));
		AES_cbc_encrypt((unsigned char*)buf, packet, aeslen, &ctx->aes, iv, AES_DECRYPT);
		memcpy(packet+aeslen, buf+aeslen, len-aeslen);
		decode_frame(ctx->alac_codec, packet, dest, outsize);
	} else decode_frame(ctx->alac_codec, (unsigned char*) buf, dest, outsize);
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
			ctx->http_count = 0;
			if (ctx->encode.config.codec == CODEC_FLAC) flac_init(ctx);
			else if (ctx->encode.config.codec == CODEC_MP3) mp3_init(ctx);
			else if (ctx->encode.config.codec == CODEC_WAV) ctx->encode.header = true;
		} else {
			pthread_mutex_unlock(&ctx->ab_mutex);
			return;
		}
	}

	if (seqno == ctx->ab_write+1) {
		// expected packet
		abuf = ctx->audio_buffer + BUFIDX(seqno);
		ctx->ab_write = seqno;
		LOG_SDEBUG("packet expected seqno:%hu rtptime:%u (W:%hu R:%hu)", seqno, rtptime, ctx->ab_write, ctx->ab_read);
	} else if (seq_order(ctx->ab_write, seqno)) {
		// newer than expected
		if (seqno - ctx->ab_write - 1 > ctx->latency / ctx->frame_size) {
			// only get rtp latency-1 frames back (last one is seqno)
			LOG_WARN("[%p] too many missing frames %hu", ctx, seqno - ctx->ab_write - 1);
			ctx->ab_write = seqno - ctx->latency / ctx->frame_size;
		}
		if (seqno - ctx->ab_read + 1 > ctx->delay) {
			// if ab_read is lagging more than http latency, advance it
			LOG_WARN("[%p] on hold for too long %hu", ctx, seqno - ctx->ab_read + 1);
			ctx->ab_read = seqno - ctx->delay + 1;
		}
		if (rtp_request_resend(ctx, ctx->ab_write + 1, seqno-1)) {
			seq_t i;
			u32_t now = gettime_ms();
			for (i = ctx->ab_write + 1; i <= seqno-1; i++) {
				ctx->audio_buffer[BUFIDX(i)].rtptime = rtptime - (seqno-i)*ctx->frame_size;
				ctx->audio_buffer[BUFIDX(i)].last_resend = now;
			}
		}
		LOG_DEBUG("[%p]: packet newer seqno:%hu rtptime:%u (W:%hu R:%hu)", ctx, seqno, rtptime, ctx->ab_write, ctx->ab_read);
		abuf = ctx->audio_buffer + BUFIDX(seqno);
		ctx->ab_write = seqno;
	} else if (seq_order(ctx->ab_read, seqno + 1)) {
		// recovered packet, not yet sent
		abuf = ctx->audio_buffer + BUFIDX(seqno);
		LOG_DEBUG("[%p]: packet recovered seqno:%hu rtptime:%u (W:%hu R:%hu)", ctx, seqno, rtptime, ctx->ab_write, ctx->ab_read);
	} else {
		// too late
		LOG_DEBUG("[%p]: packet too late seqno:%hu rtptime:%u (W:%hu R:%hu)", ctx, seqno, rtptime, ctx->ab_write, ctx->ab_read);
	}

	if (!(ctx->in_frames++ & 0x1ff)) {
		LOG_INFO("[%p]: fill [level:%hd] [W:%hu R:%hu]", ctx, (seq_t) (ctx->ab_write - ctx->ab_read + 1), ctx->ab_write, ctx->ab_read);
	}

	if (abuf) {
		alac_decode(ctx, abuf->data, data, len, &abuf->len);
		abuf->ready = 1;
		// this is the local rtptime when this frame is expected to play
		abuf->rtptime = rtptime;
#ifdef __RTP_STORE
		fwrite(data, len, 1, ctx->rtpIN);
		fwrite(abuf->data, abuf->len, 1, ctx->rtpOUT);
#endif
		if (ctx->silence && memcmp(abuf->data, ctx->silence_frame, abuf->len)) {
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

				pthread_mutex_lock(&ctx->ab_mutex);

				// re-align timestamp and expected local playback time
				if (!ctx->latency) ctx->latency = rtp_now - rtp_now_latency;
				ctx->synchro.rtp = rtp_now - ctx->latency;
				ctx->synchro.time = ctx->timing.local + (u32_t) NTP2MS(remote - ctx->timing.remote);

				// now we are synced on RTP frames
				ctx->synchro.status |= RTP_SYNC;

				// 1st sync packet received (signals a restart of playback)
				if (packet[0] & 0x10) {
					ctx->synchro.first = true;
					LOG_INFO("[%p]: 1st sync packet received", ctx);
				}

				pthread_mutex_unlock(&ctx->ab_mutex);

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
				u32_t roundtrip   = gettime_ms() - reference;

				// better discard sync packets when roundtrip is suspicious
				if (roundtrip > 100) {
					LOG_WARN("[%p]: discarding NTP roundtrip of %u ms", ctx, roundtrip);
					break;
				}

				/*
				  The expected elapsed remote time should be exactly the same as
				  elapsed local time between the two request, corrected by the
				  drifting
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
						LOG_INFO("[%p]: Sending packets too fast %Ld [W:%hu R:%hu]", ctx, ctx->timing.gap_sum, ctx->ab_write, ctx->ab_read);
						ctx->ab_read--;
						ctx->audio_buffer[BUFIDX(ctx->ab_read)].ready = 1;
						ctx->timing.gap_sum -= GAP_THRES;
						ctx->timing.gap_adjust -= GAP_THRES;
					/*
					 if expected time is less than remote, then our time is
					 running slower and we are transmitting frames too slowly,
					 so we'll overflow frames buffer, need to remove one
					*/
					} else if (ctx->timing.gap_sum < -GAP_THRES && ctx->timing.gap_count++ > GAP_COUNT) {
						if (seq_order(ctx->ab_read, ctx->ab_write + 1)) {
							ctx->audio_buffer[BUFIDX(ctx->ab_read)].ready = 0;
							ctx->ab_read++;
						} else ctx->skip++;
						ctx->timing.gap_sum += GAP_THRES;
						ctx->timing.gap_adjust += GAP_THRES;
						LOG_INFO("[%p]: Sending packets too slow %Ld (skip: %d)  [W:%hu R:%hu]", ctx, ctx->timing.gap_sum, ctx->skip, ctx->ab_write, ctx->ab_read);
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
static short *_buffer_get_frame(hairtunes_t *ctx, int *len) {
	short buf_fill;
	abuf_t *curframe = 0;
	int i;
	u32_t now, playtime;

	if (!ctx->playing) return NULL;

	// send silence if required to create enough buffering
	if (ctx->silence_count && ctx->silence_count--)	{
		*len = ctx->frame_size * 4;
		return (short*) ctx->silence_frame;
	}

	// skip frames if we are running late and skip could not be done in SYNC
	while (ctx->skip && seq_order(ctx->ab_read, ctx->ab_write + 1)) {
		ctx->audio_buffer[BUFIDX(ctx->ab_read)].ready = 0;
		ctx->ab_read++;
		ctx->skip--;
		LOG_INFO("[%p]: Sending packets too slow (skip: %d) [W:%hu R:%hu]", ctx, ctx->skip, ctx->ab_write, ctx->ab_read);
	}

	buf_fill = ctx->ab_write - ctx->ab_read + 1;

	if (buf_fill >= BUFFER_FRAMES) {
		LOG_ERROR("[%p]: Buffer overrun %hu", ctx, buf_fill);
		ctx->ab_read = ctx->ab_write - (BUFFER_FRAMES - 64);
		buf_fill = ctx->ab_write - ctx->ab_read + 1;
	}

	now = gettime_ms();
	curframe = ctx->audio_buffer + BUFIDX(ctx->ab_read);

	// use next frame when buffer is empty or silence continues to be sent
	if (!buf_fill) curframe->rtptime = ctx->audio_buffer[BUFIDX(ctx->ab_read - 1)].rtptime + ctx->frame_size;

	playtime = ctx->synchro.time + (((s32_t)(curframe->rtptime - ctx->synchro.rtp))*1000)/44100;

	LOG_SDEBUG("playtime %u %d [W:%hu R:%hu] %d", playtime, playtime - now, ctx->ab_write, ctx->ab_read, curframe->ready);

	// wait if not ready but have time, otherwise send silence
	if ((!buf_fill && !ctx->http_fill) || ctx->synchro.status != (RTP_SYNC | NTP_SYNC) || (now < playtime && !curframe->ready)) {
		LOG_SDEBUG("[%p]: waiting (fill:%hd, W:%hu R:%hu) now:%u, playtime:%u, wait:%d", ctx, buf_fill, ctx->ab_write, ctx->ab_read, now, playtime, playtime - now);
		// look for "blocking" frames at the top of the queue and try to catch-up
		for (i = 0; i < min(16, buf_fill); i++) {
			abuf_t *frame = ctx->audio_buffer + BUFIDX(ctx->ab_read + i);
			if (!frame->ready && now - frame->last_resend > RESEND_TO) {
				rtp_request_resend(ctx, ctx->ab_read + i, ctx->ab_read + i);
				frame->last_resend = now;
			}
		}
		return NULL;
	}

	// when silence is inserted at the top, need to move write pointer
	if (!buf_fill) {
		if (!ctx->filled_frames) {
			LOG_WARN("[%p]: start silence (late %d ms) [W:%hu R:%hu]", ctx, now - playtime, ctx->ab_write, ctx->ab_read);
		}
		ctx->ab_write++;
		ctx->filled_frames++;
	} else ctx->filled_frames = 0;

	if (!(ctx->out_frames++ & 0x1ff)) {
		LOG_INFO("[%p]: drain [level:%hd gap:%d] [W:%hu R:%hu] [R:%u S:%u F:%u]",
					ctx, buf_fill-1, playtime - now, ctx->ab_write, ctx->ab_read,
					ctx->resent_frames, ctx->silent_frames, ctx->filled_frames);
	}

	// each missing packet will be requested up to (latency_frames / 16) times
	for (i = 16; seq_order(ctx->ab_read + i, ctx->ab_write); i += 16) {
		abuf_t *frame = ctx->audio_buffer + BUFIDX(ctx->ab_read + i);
		if (!frame->ready && now - frame->last_resend > RESEND_TO) {
			rtp_request_resend(ctx, ctx->ab_read + i, ctx->ab_read + i);
			frame->last_resend = now;
		}
	}

	if (!curframe->ready) {
		LOG_DEBUG("[%p]: created zero frame (W:%hu R:%hu)", ctx, ctx->ab_write, ctx->ab_read);
		memset(curframe->data, 0, ctx->frame_size*4);
		curframe->len = ctx->frame_size * 4;
		ctx->silent_frames++;
	} else {
		LOG_SDEBUG("[%p]: prepared frame (fill:%hd, W:%hu R:%hu)", ctx, buf_fill-1, ctx->ab_write, ctx->ab_read);
	}

	*len = curframe->len;
	curframe->ready = 0;
	ctx->ab_read++;

	return curframe->data;
}

/*---------------------------------------------------------------------------*/
#ifdef CHUNKED
int send_data(int sock, void *data, int len, int flags) {
	char *chunk;
	int bytes = len;

	asprintf(&chunk, "%x\r\n", len);
	send(sock, chunk, strlen(chunk), flags);
	free(chunk);
	while (bytes) {
		int sent = send(sock, data + len - bytes, bytes, flags);
		if (sent < 0) {
			LOG_ERROR("Error sending data %u", len);
			return -1;
		}
		bytes -= sent;
	}
	send(sock, "\r\n", 2, flags);

	return sent;
}
#else
#define send_data send
#endif


/*---------------------------------------------------------------------------*/
static void *http_thread_func(void *arg) {
	s16_t *inbuf;
	int frame_count = 0;
	FLAC__int32 *flac_samples = NULL;
	hairtunes_t *ctx = (hairtunes_t*) arg;
	int sock = -1;
	struct timeval timeout = { 0, 0 };

	if (ctx->encode.config.codec == CODEC_FLAC && ((flac_samples = malloc(2 * ctx->frame_size * sizeof(FLAC__int32))) == NULL)) {
		LOG_ERROR("[%p]: Cannot allocate FLAC sample buffer %u", ctx, ctx->frame_size);
	}

	while (ctx->running) {
		ssize_t sent;
		fd_set rfds;
		int n;
		bool res = true;
		int size = 0;

		if (sock == -1) {
			struct timeval timeout = {0, 50*1000};

			FD_ZERO(&rfds);
			FD_SET(ctx->http_listener, &rfds);

			if (select(ctx->http_listener + 1, &rfds, NULL, NULL, &timeout) > 0) {
				sock = accept(ctx->http_listener, NULL, NULL);
			}

			if (sock != -1 && ctx->running) {
				int on = 1;
				setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *) &on, sizeof(on));

				ctx->silence_count = ctx->delay;
				pthread_mutex_lock(&ctx->ab_mutex);

				if (ctx->playing) {
					short buf_fill = ctx->ab_write - ctx->ab_read + 1;
					if (buf_fill > 0) ctx->silence_count -= min(ctx->silence_count, buf_fill);
					else ctx->silence_count = 0;
				}

				pthread_mutex_unlock(&ctx->ab_mutex);

				LOG_INFO("[%p]: got HTTP connection %u (silent frames %d)", ctx, sock, ctx->silence_count);
			} else continue;
		}

		FD_ZERO(&rfds);
		FD_SET(sock, &rfds);

		n = select(sock + 1, &rfds, NULL, NULL, &timeout);

		pthread_mutex_lock(&ctx->ab_mutex);

		if (n > 0) {
			res = handle_http(ctx, sock);
			ctx->http_ready = res;
		}

		// terminate connection if required by HTTP peer
		if (n < 0 || !res) {
			closesocket(sock);
			LOG_INFO("HTTP close %u", sock);
			sock = -1;
			ctx->http_ready = false;
		}

		// wait for session to be ready before sending (no need for mutex)
		if (ctx->http_ready && (inbuf = _buffer_get_frame(ctx, &size)) != NULL) {
			int len;

			if (ctx->encode.config.codec == CODEC_FLAC) {
				// send streaminfo at beginning
				if (ctx->encode.header && ctx->encode.len) {
#ifdef __RTP_STORE
					fwrite(ctx->encode.buffer, ctx->encode.len, 1, ctx->httpOUT);
#endif
					memcpy(ctx->http_tail, ctx->encode.buffer, ctx->encode.len);
					ctx->http_count = ctx->encode.len;

					// should be fast enough and can't release mutex anyway
					send_data(sock, (void*) ctx->encode.buffer, ctx->encode.len, 0);
					ctx->encode.len = 0;
					ctx->encode.header = false;
				}

				// now send body
				for (len = 0; len < 2*size/4; len++) flac_samples[len] = inbuf[len];
				FLAC__stream_encoder_process_interleaved(ctx->encode.codec, flac_samples, size/4);
				inbuf = (void*) ctx->encode.buffer;
				len = ctx->encode.len;
				ctx->encode.len = 0;
			} else if (ctx->encode.config.codec == CODEC_MP3) {
				int block = shine_samples_per_pass(ctx->encode.codec) * 4;
				memcpy(ctx->encode.buffer + ctx->encode.len, inbuf, size);
				ctx->encode.len += size;
				if (ctx->encode.len >= block) {
					inbuf = (s16_t*) shine_encode_buffer_interleaved(ctx->encode.codec, (s16_t*) ctx->encode.buffer, &len);
					ctx->encode.len -= block;
					memcpy(ctx->encode.buffer, ctx->encode.buffer + block, ctx->encode.len);
				} else len = 0;
			} else {
				if (ctx->encode.config.codec == CODEC_PCM) {
					s16_t *p = inbuf;
					for (len = size*2/4; len > 0; len--,p++) *p = (u8_t) *p << 8 | (u8_t) (*p >> 8);
				} else if (ctx->encode.header) {
#ifdef __RTP_STORE
					fwrite(&wave_header, sizeof(wave_header), 1, ctx->httpOUT);
#endif
					ctx->http_count = sizeof(wave_header);
					memcpy(ctx->http_tail, &wave_header, sizeof(wave_header));
					send_data(sock, (void*) &wave_header, sizeof(wave_header), 0);
					ctx->encode.header = false;
				}
				len = size;
			}

			if (len) {
				u32_t space, gap = gettime_ms();
				int offset;

#ifdef __RTP_STORE
				fwrite(inbuf, len, 1, ctx->httpOUT);
#endif
				// store data for a potential re-send
				space = min(len, TAIL_SIZE - ctx->http_count % TAIL_SIZE);
				memcpy(ctx->http_tail + (ctx->http_count % TAIL_SIZE), inbuf, space);
				memcpy(ctx->http_tail, inbuf + space, len - space);
				ctx->http_count += len;

				// check if ICY sending is active (len < ICY_INTERVAL)
				if (ctx->icy.interval && len > ctx->icy.remain) {
					int len_16 = 0;
					char buffer[ICY_LEN_MAX];

					if (ctx->icy.updated) {
						char *format;

						// there is room for 1 extra byte at the beginning for length
						if (ctx->metadata.artwork) format = "NStreamTitle='%s%s%s';StreamURL='%s';";
						else format = "NStreamTitle='%s%s%s';";
						len_16 = sprintf(buffer, format, ctx->metadata.artist,
										 *ctx->metadata.artist ? " - " : "",
										 ctx->metadata.title, ctx->metadata.artwork) - 1;
						LOG_INFO("[%p]: ICY update %s", ctx, buffer + 1);
						len_16 = (len_16 + 15) / 16;
						ctx->icy.updated = false;
					}

					buffer[0] = len_16;

					// release mutex here as send might take a while
					pthread_mutex_unlock(&ctx->ab_mutex);

					// send remaining data first
					offset = ctx->icy.remain;
					send_data(sock, (void*) inbuf, offset, 0);
					len -= offset;

					// then send icy data
					send_data(sock, (void*) buffer, len_16 * 16 + 1, 0);
					ctx->icy.remain = ctx->icy.interval;

					LOG_SDEBUG("[%p]: ICY checked %u", ctx, ctx->icy.remain);
				} else {
					offset = 0;
					pthread_mutex_unlock(&ctx->ab_mutex);
				}

				LOG_SDEBUG("[%p]: HTTP sent frame count:%u bytes:%u (W:%hu R:%hu)", ctx, frame_count++, len+offset, ctx->ab_write, ctx->ab_read);
				sent = send_data(sock, (u8_t*) inbuf + offset , len, 0);

				// update remaining count with desired length
				if (ctx->icy.interval) ctx->icy.remain -= len;

				gap = gettime_ms() - gap;

				if (gap > 50) {
					LOG_ERROR("[%p]: spent %u ms in send for %u bytes (sent %zd)!", ctx, gap, len, sent);
				}

				if (sent != len) {
					LOG_WARN("[%p]: HTTP send() unexpected response: %li (data=%i): %s", ctx, (long int) sent, len, strerror(errno));
				}
			} else pthread_mutex_unlock(&ctx->ab_mutex);

			// packet just sent, don't wait in case we have more so sent (catch-up mode)
			timeout.tv_usec = 0;
		} else {
			// nothing to send, so probably can wait 2 frame
			timeout.tv_usec = (2*ctx->frame_size*1000000)/44100;
			pthread_mutex_unlock(&ctx->ab_mutex);
		}
	}

	if (sock != -1) shutdown_socket(sock);

	if (ctx->encode.config.codec == CODEC_FLAC && flac_samples) free(flac_samples);

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


/*----------------------------------------------------------------------------*/
static bool handle_http(hairtunes_t *ctx, int sock)
{
	char *body = NULL, method[16] = "", *str, *head = NULL;
	key_data_t headers[64], resp[16] = { { NULL, NULL } };
	size_t offset = 0;
	int len;

	if (!http_parse(sock, method, headers, &body, &len)) return false;

	LOG_INFO("[%p]: received %s", ctx, method);

	kd_add(resp, "Server", "HairTunes");
	kd_add(resp, "Content-Type", mime_types[ctx->encode.config.codec]);

	// is there a range request (chromecast non-compliance to HTTP !!!)
	if (ctx->range && (str = kd_lookup(headers, "Range")) != NULL) {
#if WIN
		sscanf(str, "bytes=%u", &offset);
#else
		sscanf(str, "bytes=%zu", &offset);
#endif
		if (offset) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
			asprintf(&str, "bytes %zu-%zu/*", offset, ctx->http_count);
#pragma GCC diagnostic pop            }
			head = "HTTP/1.1 206 Partial Content";
			kd_add(resp, "Content-Range", str);
			free(str);
		}
	}

	// check if add ICY metadata is needed (only on live stream)
	if (ctx->encode.config.codec == CODEC_MP3 && ctx->encode.config.mp3.icy &&
		((str = kd_lookup(headers, "Icy-MetaData")) != NULL) && atoi(str)) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
		asprintf(&str, "%u", ICY_INTERVAL);
#pragma GCC diagnostic pop
		kd_add(resp, "icy-metaint", str);
		ctx->icy.interval = ctx->icy.remain = ICY_INTERVAL;
		free(str);
	} else ctx->icy.interval = 0;

#ifdef CHUNKED
	mirror_header(headers, resp, "Connection");
	mirror_header(headers, resp, "transferMode.dlna.org");
	kd_add(resp, "Transfer-Encoding", "chunked");
	str = http_send(sock, head ? head : "HTTP/1.1 200 OK", resp);
#else
	kd_add(resp, "Connection", "close");
	str = http_send(sock, head ? head : "HTTP/1.0 200 OK", resp);
#endif

	LOG_INFO("[%p]: responding:\n%s", ctx, str);

	NFREE(body);
	NFREE(str);
	kd_free(resp);
	kd_free(headers);

	// need to re-send the range
	if (offset) {
		size_t count = 0;

		LOG_INFO("[%p] re-sending offset %zu/%zu", ctx, offset, ctx->http_count);
		ctx->silence_count = 0;
		while (count != ctx->http_count - offset) {
			size_t bytes = ctx->icy.interval ? ctx->icy.remain : ICY_INTERVAL;
			int sent;

			bytes = min(bytes, ctx->http_count - offset - count);
			sent = send_data(sock, ctx->http_tail + ((offset + count) % TAIL_SIZE), bytes, 0);

			if (sent < 0) {
				LOG_ERROR("[%p]: error re-sending range %u", ctx, offset);
				break;
			}

			count += sent;

			// send ICY data if needed
			if (ctx->icy.interval) {
				ctx->icy.remain -= sent;
				if (!ctx->icy.remain) {
					send_data(sock, "\1", 1, 0);
					ctx->icy.remain = ctx->icy.interval;
				}
			}
		}
	}

	return true;
}

