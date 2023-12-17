/*
 *  Metadata instance
 *
 *  (c) Philippe, philippe_44@outlook.com
 *
 *  See LICENSE
 *
 *
 */

#pragma once

#include <stdint.h>

typedef struct metadata_s {
	char* artist;
	char* album;
	char* title;
	char* remote_title;
	char* artwork;
	char *genre;
	uint32_t track, disc;
	uint32_t duration, live_duration;
	uint32_t sample_rate;
	uint8_t  sample_size;
	uint8_t  channels;
} metadata_t;
