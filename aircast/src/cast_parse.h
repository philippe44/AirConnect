/*
 *  Chromecast parse utils
 *
 *  (c) Philippe 2016-2017, philippe_44@outlook.com
 *
 * See LICENSE
 *
 */

#pragma once

#include <stdbool.h>
#include "jansson.h"

int 		GetMediaItem_I(json_t *root, int n, char *item);
double 		GetMediaItem_F(json_t *root, int n, char *item);
const char* GetMediaItem_S(json_t *root, int n, char *item);
const char* GetAppIdItem(json_t *root, char* appId, char *item);
const char* GetMediaInfoItem_S(json_t *root, int n, char *item);
bool 		GetMediaVolume(json_t *root, int n, double *volume, bool *muted);


