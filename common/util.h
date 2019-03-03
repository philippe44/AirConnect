/*
 *  Misc utilities
 *
 *  (c) Adrian Smith 2012-2014, triode1@btinternet.com
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

#ifndef __UTIL_H
#define __UTIL_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "platform.h"
#include "pthread.h"
#ifdef _USE_XML_
#include "ixml.h"
#endif

#define NFREE(p) if (p) { free(p); p = NULL; }

typedef struct {
	pthread_mutex_t	*mutex;
	void (*cleanup)(void*);
	struct sQueue_e {
		struct sQueue_e *next;
		void 			*item;
	} list;
} tQueue;

typedef struct metadata_s {
	char *artist;
	char *album;
	char *title;
	char *genre;
	char *path;
	char *artwork;
	char *remote_title;
	u32_t track;
	u32_t duration;
	u32_t track_hash;
	u32_t sample_rate;
	u8_t  sample_size;
	u8_t  channels;
} metadata_t;


typedef struct list_s {
	struct list_s *next;
} list_t;

void 		InitUtils(void);
void		EndUtils(void);

void		WakeableSleep(u32_t ms);
void		WakeAll(void);

void 		QueueInit(tQueue *queue, bool mutex, void (*f)(void*));
void 		QueueInsert(tQueue *queue, void *item);
void 		*QueueExtract(tQueue *queue);
void 		QueueFlush(tQueue *queue);

list_t*		push_item(list_t *item, list_t **list);
list_t*		add_tail_item(list_t *item, list_t **list);
list_t*		add_ordered_item(list_t *item, list_t **list, int (*compare)(void *a, void *b));
list_t*		pop_item(list_t **list);
list_t*   	remove_item(list_t *item, list_t **list);
void 		clear_list(list_t **list, void (*free_func)(void *));

void 		free_metadata(struct metadata_s *metadata);
void 		dup_metadata(struct metadata_s *dst, struct metadata_s *src);

int			pthread_cond_reltimedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, u32_t msWait);

#ifdef _USE_XML_
const char 	*XMLGetLocalName(IXML_Document *doc, int Depth);
IXML_Node  	*XMLAddNode(IXML_Document *doc, IXML_Node *parent, char *name, char *fmt, ...);
IXML_Node  	*XMLUpdateNode(IXML_Document *doc, IXML_Node *parent, bool refresh, char *name, char *fmt, ...);
int 	   	XMLAddAttribute(IXML_Document *doc, IXML_Node *parent, char *name, char *fmt, ...);
char 	   	*XMLGetFirstDocumentItem(IXML_Document *doc, const char *item, bool strict);
char 		*XMLGetFirstElementItem(IXML_Element *element, const char *item);
bool 		XMLMatchDocumentItem(IXML_Document *doc, const char *item, const char *s);
#endif

u32_t 		gettime_ms(void);

char*		stristr(char *s1, char *s2);
#if WIN
char* 		strsep(char** stringp, const char* delim);
char 		*strndup(const char *s, size_t n);
int 		asprintf(char **strp, const char *fmt, ...);
#else
char 		*strlwr(char *str);
#endif
char* 		strextract(char *s1, char *beg, char *end);
u32_t 		hash32(char *str);
char*		ltrim(char *s);
char*		rtrim(char *s);
char*		trim(char *s);

bool 		get_interface(struct in_addr *addr);
in_addr_t 	get_localhost(char **name);
void 		get_mac(u8_t mac[]);
void 		winsock_init(void);
void 		winsock_close(void);
int 		shutdown_socket(int sd);
int 		bind_socket(short unsigned *port, int mode);
int 		conn_socket(unsigned short port);#if !WINint SendARP(in_addr_t src, in_addr_t dst, u8_t mac[], unsigned long *size);#endif
typedef struct {
	char *key;
	char *data;
} key_data_t;

bool 		http_parse(int sock, char *method, key_data_t *rkd, char **body, int *len);
char*		http_send(int sock, char *method, key_data_t *rkd);
int 		read_line(int fd, char *line, int maxlen, int timeout);
int 		send_response(int sock, char *response);

char*		kd_lookup(key_data_t *kd, char *key);
bool 		kd_add(key_data_t *kd, char *key, char *value);
char* 		kd_dump(key_data_t *kd);
void 		kd_free(key_data_t *kd);

u64_t 		gettime_ms64(void);

int 		_fprintf(FILE *file, ...);
#endif

