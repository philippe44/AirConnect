#ifndef _HAIRTUNES_H_
#define _HAIRTUNES_H_

typedef struct {
	unsigned short cport, tport, aport, hport;
	struct hairtunes_s *ctx;
} hairtunes_resp_t;

typedef enum { HAIRTUNES_PLAY } hairtunes_event_t;
typedef enum { CODEC_FLAC = 0, CODEC_PCM = 1, CODEC_WAV = 2} codec_t;

typedef	void		(*hairtunes_cb_t)(void *owner, hairtunes_event_t event);

hairtunes_resp_t 	hairtunes_init(struct in_addr host, codec_t codec, bool sync, bool drift,
							char *latencies, char *aeskey, char *aesiv, char *fmtpstr,
							short unsigned pCtrlPort, short unsigned pTimingPort,
							void *owner, hairtunes_cb_t callback);
void			 	hairtunes_end(struct hairtunes_s *ctx);
bool 				hairtunes_flush(struct hairtunes_s *ctx, unsigned short seqno, unsigned rtpframe);

#endif
