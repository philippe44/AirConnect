/*
 *  Metadata instance
 *
 *  (c) Philippe, philippe_44@outlook.com
 *
 *  See LICENSE
 *
 *
 */

#include <stdlib.h>
#include <string.h>
#include "metadata.h"

#define __STRDUP__(s) ((s) ? strdup(s) : NULL)
#define __FREE__(s) do { \
	if (s) free(s);      \
	s = NULL;            \
} while (0)	

void metadata_init(struct metadata_s* data) {
	memset(data, 0, sizeof(*data));
}
	
void metadata_free(struct metadata_s* const data) {
	if (!data) return;
	__FREE__(data->artist);
	__FREE__(data->album);
	__FREE__(data->title);
	__FREE__(data->remote_title);
	__FREE__(data->artwork);
	__FREE__(data->genre);
}
	
struct metadata_s* metadata_clone(struct metadata_s* const data) {
	struct metadata_s *clone = malloc(sizeof(*data));
	memcpy(clone, data, sizeof(*data));
	clone->artist  = __STRDUP__(data->artist);
	clone->album   = __STRDUP__(data->album);
	clone->title   = __STRDUP__(data->title);
	clone->remote_title = __STRDUP__(data->remote_title);
	clone->artwork = __STRDUP__(data->artwork);
	clone->genre = __STRDUP__(data->genre);
	return clone;
}
