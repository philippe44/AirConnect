#ifndef _HAIRTUNES_H_
#define _HAIRTUNES_H_

#include "util.h"

typedef struct {
	unsigned short cport, tport, aport, hport;
	struct hairtunes_s *ctx;
} hairtunes_resp_t;

typedef struct {
	enum { CODEC_MP3 = 0, CODEC_FLAC, CODEC_PCM, CODEC_WAV} codec;
	union {
		struct {
			int bitrate;
			bool icy;
		} mp3;
		struct {
			int level;
		} flac;
	};
} encode_t;

typedef enum { HAIRTUNES_PLAY } hairtunes_event_t;

typedef	void		(*hairtunes_cb_t)(void *owner, hairtunes_event_t event);

hairtunes_resp_t 	hairtunes_init(struct in_addr host, encode_t codec,
							bool sync, bool drift, bool range, char *latencies,
							char *aeskey, char *aesiv, char *fmtpstr,
							short unsigned pCtrlPort, short unsigned pTimingPort,
							void *owner, hairtunes_cb_t callback);
void			 	hairtunes_end(struct hairtunes_s *ctx);
bool 				hairtunes_flush(struct hairtunes_s *ctx, unsigned short seqno, unsigned rtptime);
void 				hairtunes_record(struct hairtunes_s *ctx, unsigned short seqno, unsigned rtptime);
void 				hairtunes_metadata(struct hairtunes_s *ctx, struct metadata_s *metadata);

#endif
