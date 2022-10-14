/*
 *  Chromecast parse utils
 *
 *  (c) Philippe, philippe_44@outlook.com
 *
 * See LICENSE
 *
 */


#include <stdlib.h>

#include "platform.h"
#include "cross_log.h"
#include "jansson.h"
#include "cast_parse.h"

extern log_level cast_loglevel;
//static log_level *loglevel = &cast_loglevel;

/*----------------------------------------------------------------------------*/
/* 																			  */
/* JSON parsing																  */
/* 																			  */
/*----------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------*/
const char *GetAppIdItem(json_t *root, char* appId, char *item)
{
	json_t *elm;

	if ((elm = json_object_get(root, "status")) == NULL) return NULL;
	if ((elm = json_object_get(elm, "applications")) == NULL) return NULL;
	for (int i = 0; i < json_array_size(elm); i++) {
		json_t *id, *data = json_array_get(elm, i);
		id = json_object_get(data, "appId");
		if (strcasecmp(json_string_value(id), appId)) continue;
		id = json_object_get(data, item);
		return json_string_value(id);
	}

	return NULL;
}


/*----------------------------------------------------------------------------*/
int GetMediaItem_I(json_t *root, int n, char *item)
{
	json_t *elm;

	if ((elm = json_object_get(root, "status")) == NULL) return 0;
	if ((elm = json_array_get(elm, n)) == NULL) return 0;
	if ((elm = json_object_get(elm, item)) == NULL) return 0;
	return json_integer_value(elm);
}


/*----------------------------------------------------------------------------*/
double GetMediaItem_F(json_t *root, int n, char *item)
{
	json_t *elm;

	if ((elm = json_object_get(root, "status")) == NULL) return 0;
	if ((elm = json_array_get(elm, n)) == NULL) return 0;
	if ((elm = json_object_get(elm, item)) == NULL) return 0;
	return json_number_value(elm);
}


/*----------------------------------------------------------------------------*/
bool GetMediaVolume(json_t *root, int n, double *volume, bool *muted)
{
	json_t *elm, *data;
	*volume = -1;
	*muted = false;

	if ((elm = json_object_get(root, "status")) == NULL) return false;
	if ((elm = json_object_get(elm, "volume")) == NULL) return false;

	if ((data = json_object_get(elm, "level")) != NULL) *volume = json_number_value(data);
	if ((data = json_object_get(elm, "muted")) != NULL) *muted = json_boolean_value(data);

	return true;
}


/*----------------------------------------------------------------------------*/
const char *GetMediaItem_S(json_t *root, int n, char *item)
{
	json_t *elm;
	const char *str;

	if ((elm = json_object_get(root, "status")) == NULL) return NULL;
	elm = json_array_get(elm, n);
	elm = json_object_get(elm, item);
	str = json_string_value(elm);
	return str;
}

/*----------------------------------------------------------------------------*/
const char *GetMediaInfoItem_S(json_t *root, int n, char *item)
{
	json_t *elm;
	const char *str;

	if ((elm = json_object_get(root, "status")) == NULL) return NULL;
	if ((elm = json_array_get(elm, n)) == NULL) return NULL;
	if ((elm = json_object_get(elm, "media")) == NULL) return NULL;
	elm = json_object_get(elm, item);
	str = json_string_value(elm);
	return str;
}






