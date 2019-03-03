/*
 *  AirConnect: Chromecast & UPnP to AirPlay
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

#include <stdarg.h>

#include "platform.h"

#if LINUX || OSX || FREEBSD || SUNOS
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netdb.h>
#include <ctype.h>
#if SUNOS
#include <sys/sockio.h>
#endif
#if FREEBSD
#include <ifaddrs.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#endif
#endif
#if WIN
#include <iphlpapi.h>
#endif
#if OSX
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <sys/time.h>
#endif

#include "pthread.h"
#include "util.h"
#include "log_util.h"

/*----------------------------------------------------------------------------*/
/* globals */
/*----------------------------------------------------------------------------*/

extern log_level	util_loglevel;

/*----------------------------------------------------------------------------*/
/* locals */
/*----------------------------------------------------------------------------*/
extern log_level 		util_loglevel;
static log_level 		*loglevel = &util_loglevel;
static pthread_mutex_t	wakeMutex;
static pthread_cond_t	wakeCond;

/*----------------------------------------------------------------------------*/
void InitUtils(void) {
	pthread_mutex_init(&wakeMutex, 0);
	pthread_cond_init(&wakeCond, 0);
}

/*----------------------------------------------------------------------------*/
void EndUtils(void) {
	pthread_mutex_destroy(&wakeMutex);
	pthread_cond_destroy(&wakeCond);
}


/*----------------------------------------------------------------------------*/
/* 																			  */
/* system-wide sleep & wakeup												  */
/* 																			  */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
void WakeableSleep(u32_t ms) {
	pthread_mutex_lock(&wakeMutex);
	if (ms) pthread_cond_reltimedwait(&wakeCond, &wakeMutex, ms);
	else pthread_cond_wait(&wakeCond, &wakeMutex);
	pthread_mutex_unlock(&wakeMutex);
}

/*----------------------------------------------------------------------------*/
void WakeAll(void) {
	pthread_mutex_lock(&wakeMutex);
	pthread_cond_broadcast(&wakeCond);
	pthread_mutex_unlock(&wakeMutex);
}


/*----------------------------------------------------------------------------*/
/* 																			  */
/* pthread utils															  */
/* 																			  */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
int pthread_cond_reltimedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, u32_t msWait)
{
	struct timespec ts;
	u32_t	nsec;
#if OSX || SUNOS
	struct timeval tv;
#endif

#if WIN
	struct _timeb SysTime;

	_ftime(&SysTime);
	ts.tv_sec = (long) SysTime.time;
	ts.tv_nsec = 1000000 * SysTime.millitm;
#elif LINUX || FREEBSD
	clock_gettime(CLOCK_REALTIME, &ts);
#elif OSX || SUNOS
	gettimeofday(&tv, NULL);
	ts.tv_sec = (long) tv.tv_sec;
	ts.tv_nsec = 1000L * tv.tv_usec;
#endif

	if (!msWait) return pthread_cond_wait(cond, mutex);

	nsec = ts.tv_nsec + (msWait % 1000) * 1000000;
	ts.tv_sec += msWait / 1000 + (nsec / 1000000000);
	ts.tv_nsec = nsec % 1000000000;

	return pthread_cond_timedwait(cond, mutex, &ts);
}

// mutex wait with timeout
#if LINUX || FREEBSD
int _mutex_timedlock(pthread_mutex_t *m, u32_t ms_wait)
{
	int rc = -1;
	struct timespec ts;

	if (!clock_gettime(CLOCK_REALTIME, &ts)) {
		ts.tv_nsec += (ms_wait % 1000) * 1000000;
		ts.tv_sec += ms_wait / 1000 + (ts.tv_nsec / 1000000000);
		ts.tv_nsec = ts.tv_nsec % 1000000000;
		rc = pthread_mutex_timedlock(m, &ts);
	}
	return rc;
}
#endif

#if OSX
int _mutex_timedlock(pthread_mutex_t *m, u32_t ms_wait)
{
	int rc;
	s32_t wait = (s32_t) ms_wait;

	/* Try to acquire the lock and, if we fail, sleep for 10ms. */
	while (((rc = pthread_mutex_trylock (m)) == EBUSY) && (wait > 0)) {
		wait -= 10;
		usleep(10000);
	}

	return rc;
}
#endif

/*----------------------------------------------------------------------------*/
/* 																			  */
/* QUEUE management															  */
/* 																			  */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
void QueueInit(tQueue *queue, bool mutex, void (*cleanup)(void*))
{
	queue->cleanup = cleanup;
	queue->list.item = NULL;
	if (mutex) {
		queue->mutex = malloc(sizeof(pthread_mutex_t));
		pthread_mutex_init(queue->mutex, NULL);
	}
	else queue->mutex = NULL;
}

/*----------------------------------------------------------------------------*/
void QueueInsert(tQueue *queue, void *item)
{
	struct sQueue_e *list;

	if (queue->mutex) pthread_mutex_lock(queue->mutex);
	list = &queue->list;

	while (list->item) list = list->next;
	list->item = item;
	list->next = malloc(sizeof(struct sQueue_e));
	list->next->item = NULL;

	if (queue->mutex) pthread_mutex_unlock(queue->mutex);
}


/*----------------------------------------------------------------------------*/
void *QueueExtract(tQueue *queue)
{
	void *item;
	struct sQueue_e *list;

	if (queue->mutex) pthread_mutex_lock(queue->mutex);

	list = &queue->list;
	item = list->item;

	if (item) {
		struct sQueue_e *next = list->next;
		if (next->item) {
			list->item = next->item;
			list->next = next->next;
		} else list->item = NULL;
		NFREE(next);
	}

	if (queue->mutex) pthread_mutex_unlock(queue->mutex);

	return item;
}


/*----------------------------------------------------------------------------*/
void QueueFlush(tQueue *queue)
{
	struct sQueue_e *list;

	if (queue->mutex) pthread_mutex_lock(queue->mutex);

	list = &queue->list;

	while (list->item) {
		struct sQueue_e *next = list->next;
		if (queue->cleanup)	(*(queue->cleanup))(list->item);
		if (list != &queue->list) { NFREE(list); }
		list = next;
	}

	if (list != &queue->list) { NFREE(list); }
	queue->list.item = NULL;

	if (queue->mutex) {
		pthread_mutex_unlock(queue->mutex);
		pthread_mutex_destroy(queue->mutex);
		free(queue->mutex);
	}
}


/*----------------------------------------------------------------------------*/
/* 																			  */
/* LIST management															  */
/* 																			  */
/*----------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
list_t *push_item(list_t *item, list_t **list) {
  if (*list) item->next = *list;
  else item->next = NULL;

  *list = item;

  return item;
}


/*---------------------------------------------------------------------------*/
list_t *add_tail_item(list_t *item, list_t **list) {
  if (*list) {
	struct list_s *p = *list;
	while (p->next) p = p->next;
	item->next = p->next;
	p->next = item;
  } else {
	item->next = NULL;
	*list = item;
  }

  return item;
}


/*---------------------------------------------------------------------------*/
list_t *add_ordered_item(list_t *item, list_t **list, int (*compare)(void *a, void *b)) {
  if (*list) {
	struct list_s *p = *list;
	while (p->next && compare(p->next, item) <= 0) p = p->next;
	item->next = p->next;
	p->next = item;
  } else {
	item->next = NULL;
	*list = item;
  }

  return item;
}


/*---------------------------------------------------------------------------*/
list_t *pop_item(list_t **list) {
  if (*list) {
	list_t *item = *list;
	*list = item->next;
	return item;
  } else return NULL;
}


/*---------------------------------------------------------------------------*/
list_t *remove_item(list_t *item, list_t **list) {
  if (item != *list) {
	struct list_s *p = *list;
	while (p && p->next != item) p = p->next;
	if (p) p->next = item->next;
	item->next = NULL;
  } else *list = (*list)->next;

  return item;
}


/*---------------------------------------------------------------------------*/
void clear_list(list_t **list, void (*free_func)(void *)) {
  if (!*list) return;
  while (*list) {
	struct list_s *next = (*list)->next;
	if (free_func) (*free_func)(*list);
	else free(*list);
	*list = next;
  }
  *list = NULL;
}

/*----------------------------------------------------------------------------*/
/* 																			  */
/* NETWORKING utils															  */
/* 																			  */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
// mac address
#if LINUX
// search first 4 interfaces returned by IFCONF
void get_mac(u8_t mac[]) {
	struct ifconf ifc;
	struct ifreq *ifr, *ifend;
	struct ifreq ifreq;
	struct ifreq ifs[4];

	mac[0] = mac[1] = mac[2] = mac[3] = mac[4] = mac[5] = 0;

	int s = socket(AF_INET, SOCK_DGRAM, 0);

	ifc.ifc_len = sizeof(ifs);
	ifc.ifc_req = ifs;

	if (ioctl(s, SIOCGIFCONF, &ifc) == 0) {
		ifend = ifs + (ifc.ifc_len / sizeof(struct ifreq));

		for (ifr = ifc.ifc_req; ifr < ifend; ifr++) {
			if (ifr->ifr_addr.sa_family == AF_INET) {

				strncpy(ifreq.ifr_name, ifr->ifr_name, sizeof(ifreq.ifr_name));
				if (ioctl (s, SIOCGIFHWADDR, &ifreq) == 0) {
					memcpy(mac, ifreq.ifr_hwaddr.sa_data, 6);
					if (mac[0]+mac[1]+mac[2] != 0) {
						break;
					}
				}
			}
		}
	}

	close(s);
}
#elif OSX || FREEBSD
void get_mac(u8_t mac[]) {
	struct ifaddrs *addrs, *ptr;
	const struct sockaddr_dl *dlAddr;
	const unsigned char *base;

	mac[0] = mac[1] = mac[2] = mac[3] = mac[4] = mac[5] = 0;

	if (getifaddrs(&addrs) == 0) {
		ptr = addrs;
		while (ptr) {
			if (ptr->ifa_addr->sa_family == AF_LINK && ((const struct sockaddr_dl *) ptr->ifa_addr)->sdl_type == IFT_ETHER) {
				dlAddr = (const struct sockaddr_dl *)ptr->ifa_addr;
				base = (const unsigned char*) &dlAddr->sdl_data[dlAddr->sdl_nlen];
				memcpy(mac, base, min(dlAddr->sdl_alen, 6));
				break;
			}
			ptr = ptr->ifa_next;
		}
		freeifaddrs(addrs);
	}
}
#elif WIN
#pragma comment(lib, "IPHLPAPI.lib")
void get_mac(u8_t mac[]) {
	IP_ADAPTER_INFO AdapterInfo[16];
	DWORD dwBufLen = sizeof(AdapterInfo);
	DWORD dwStatus = GetAdaptersInfo(AdapterInfo, &dwBufLen);

	mac[0] = mac[1] = mac[2] = mac[3] = mac[4] = mac[5] = 0;

	if (GetAdaptersInfo(AdapterInfo, &dwBufLen) == ERROR_SUCCESS) {
		memcpy(mac, AdapterInfo[0].Address, 6);
	}
}
#endif


/*----------------------------------------------------------------------------*/
#if LINUX
int SendARP(in_addr_t src, in_addr_t dst, u8_t mac[], unsigned long *size) {
	int                 s;
	struct arpreq       areq;
	struct sockaddr_in *sin;

	/* Get an internet domain socket. */
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		return -1;
	}

	/* Make the ARP request. */
	memset(&areq, 0, sizeof(areq));
	sin = (struct sockaddr_in *) &areq.arp_pa;
	sin->sin_family = AF_INET;

	sin->sin_addr.s_addr = src;
	sin = (struct sockaddr_in *) &areq.arp_ha;
	sin->sin_family = ARPHRD_ETHER;

	strncpy(areq.arp_dev, "eth0", 15);

	if (ioctl(s, SIOCGARP, (caddr_t) &areq) == -1) {
		return -1;
	}

	memcpy(mac, &(areq.arp_ha.sa_data), *size);
	return 0;
}
#elif OSX
int SendARP(in_addr_t src, in_addr_t dst, u8_t mac[], unsigned long *size)
{
	int mib[6];
	size_t needed;
	char *lim, *buf, *next;
	struct rt_msghdr *rtm;
	struct sockaddr_inarp *sin;
	struct sockaddr_dl *sdl;
	int found_entry = -1;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = AF_INET;
	mib[4] = NET_RT_FLAGS;
	mib[5] = RTF_LLINFO;

	if (sysctl(mib, 6, NULL, &needed, NULL, 0) < 0)
		return (found_entry);

	if ((buf = malloc(needed)) == NULL)
		return (found_entry);

	if (sysctl(mib, 6, buf, &needed, NULL, 0) < 0)
		return (found_entry);

	lim = buf + needed;
	for (next = buf; next < lim; next += rtm->rtm_msglen)
	{
		rtm = (struct rt_msghdr *)next;
		sin = (struct sockaddr_inarp *)(rtm + 1);
		sdl = (struct sockaddr_dl *)(sin + 1);

		if (src)
		{
			if (src != sin->sin_addr.s_addr)
				continue;
		}

		if (sdl->sdl_alen)
		{
			found_entry = 0;
			memcpy(mac,  LLADDR(sdl), sdl->sdl_alen);
		}
	}

	free(buf);
	return (found_entry);
}
#elif !WIN
int SendARP(in_addr_t src, in_addr_t dst, u8_t mac[], unsigned long *size)
{
	LOG_ERROR("No SendARP build for this platform", NULL);
	return 1;
}
#endif

#if LINUX || OSX || BSD || SUNOS
bool get_interface(struct in_addr *addr)
{
	struct ifreq *ifreq;
	struct ifconf ifconf;
	char buf[512];
	unsigned i, nb;
	int fd;
	bool valid = false;

	fd = socket(AF_INET, SOCK_DGRAM, 0);

	ifconf.ifc_len = sizeof(buf);
	ifconf.ifc_buf = buf;

	if (ioctl(fd, SIOCGIFCONF, &ifconf)!=0) return false;

	ifreq = ifconf.ifc_req;
	nb = ifconf.ifc_len / sizeof(struct ifreq);

	for (i = 0; i < nb; i++) {
		ioctl(fd, SIOCGIFFLAGS, &ifreq[i]);
		//!(ifreq[i].ifr_flags & IFF_POINTTOPOINT);
		if ((ifreq[i].ifr_flags & IFF_UP) &&
			!(ifreq[i].ifr_flags & IFF_LOOPBACK) &&
			ifreq[i].ifr_flags & IFF_MULTICAST) {
				*addr = ((struct sockaddr_in *) &(ifreq[i].ifr_addr))->sin_addr;
				valid = true;
				break;
		 }
	}

	close(fd);
	return valid;
}
#endif


#if WIN
bool get_interface(struct in_addr *addr)
{
	INTERFACE_INFO ifList[20];
	unsigned bytes;
	int i, nb;
	bool valid = false;
	int fd;

	memset(addr, 0, sizeof(struct in_addr));
	fd = socket(AF_INET, SOCK_DGRAM, 0);

	if (WSAIoctl(fd, SIO_GET_INTERFACE_LIST, 0, 0, (void*) &ifList, sizeof(ifList), (void*) &bytes, 0, 0) == SOCKET_ERROR) return false;

	nb = bytes / sizeof(INTERFACE_INFO);
	for (i = 0; i < nb; i++) {
		if ((ifList[i].iiFlags & IFF_UP) &&
			!(ifList[i].iiFlags & IFF_POINTTOPOINT) &&
			!(ifList[i].iiFlags & IFF_LOOPBACK) &&
			(ifList[i].iiFlags & IFF_MULTICAST)) {
				*addr = ((struct sockaddr_in *) &(ifList[i].iiAddress))->sin_addr;
				valid = true;
			break;
		}
	}

	close(fd);
	return valid;
}
#endif



/*---------------------------------------------------------------------------*/
#define MAX_INTERFACES 256
#define DEFAULT_INTERFACE 1
#if !defined(WIN32)
#define INVALID_SOCKET (-1)
#endif
in_addr_t get_localhost(char **name)
{
#ifdef WIN32
	char buf[256];
	struct hostent *h = NULL;
	struct sockaddr_in LocalAddr;

	memset(&LocalAddr, 0, sizeof(LocalAddr));

	gethostname(buf, 256);
	h = gethostbyname(buf);

	if (name) *name = strdup(buf);

	if (h != NULL) {
		memcpy(&LocalAddr.sin_addr, h->h_addr_list[0], 4);
		return LocalAddr.sin_addr.s_addr;
	}
	else return INADDR_ANY;
#elif defined (__APPLE__) || defined(__FreeBSD__)
	struct ifaddrs *ifap, *ifa;

	if (name) {
		*name = malloc(256);
		gethostname(*name, 256);
	}

	if (getifaddrs(&ifap) != 0) return INADDR_ANY;

	/* cycle through available interfaces */
	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		/* Skip loopback, point-to-point and down interfaces,
		 * except don't skip down interfaces
		 * if we're trying to get a list of configurable interfaces. */
		if ((ifa->ifa_flags & IFF_LOOPBACK) ||
			(!( ifa->ifa_flags & IFF_UP))) {
			continue;
		}
		if (ifa->ifa_addr->sa_family == AF_INET) {
			/* We don't want the loopback interface. */
			if (((struct sockaddr_in *)(ifa->ifa_addr))->sin_addr.s_addr ==
				htonl(INADDR_LOOPBACK)) {
				continue;
			}
			return ((struct sockaddr_in *)(ifa->ifa_addr))->sin_addr.s_addr;
			break;
		}
	}
	freeifaddrs(ifap);

	return INADDR_ANY;
#elif defined(linux) || defined(sun)
	char szBuffer[MAX_INTERFACES * sizeof (struct ifreq)];
	struct ifconf ifConf;
	struct ifreq ifReq;
	int nResult;
	long unsigned int i;
	int LocalSock;
	struct sockaddr_in LocalAddr;
	int j = 0;

	if (name) {
		*name = malloc(256);
		gethostname(*name, 256);
	}

	/* purify */
	memset(&ifConf,  0, sizeof(ifConf));
	memset(&ifReq,   0, sizeof(ifReq));
	memset(szBuffer, 0, sizeof(szBuffer));
	memset(&LocalAddr, 0, sizeof(LocalAddr));

	/* Create an unbound datagram socket to do the SIOCGIFADDR ioctl on.  */
	LocalSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (LocalSock == INVALID_SOCKET) return false;
	/* Get the interface configuration information... */
	ifConf.ifc_len = (int)sizeof szBuffer;
	ifConf.ifc_ifcu.ifcu_buf = (caddr_t) szBuffer;
	nResult = ioctl(LocalSock, SIOCGIFCONF, &ifConf);
	if (nResult < 0) {
		close(LocalSock);
		return INADDR_ANY;
	}

	/* Cycle through the list of interfaces looking for IP addresses. */
	for (i = 0lu; i < (long unsigned int)ifConf.ifc_len && j < DEFAULT_INTERFACE; ) {
		struct ifreq *pifReq =
			(struct ifreq *)((caddr_t)ifConf.ifc_req + i);
		i += sizeof *pifReq;
		/* See if this is the sort of interface we want to deal with. */
		memset(ifReq.ifr_name, 0, sizeof(ifReq.ifr_name));
		strncpy(ifReq.ifr_name, pifReq->ifr_name,
			sizeof(ifReq.ifr_name) - 1);
		/* Skip loopback, point-to-point and down interfaces,
		 * except don't skip down interfaces
		 * if we're trying to get a list of configurable interfaces. */
		ioctl(LocalSock, SIOCGIFFLAGS, &ifReq);
		if ((ifReq.ifr_flags & IFF_LOOPBACK) ||
			(!(ifReq.ifr_flags & IFF_UP))) {
			continue;
		}
		if (pifReq->ifr_addr.sa_family == AF_INET) {
			/* Get a pointer to the address...*/
			memcpy(&LocalAddr, &pifReq->ifr_addr,
				sizeof pifReq->ifr_addr);
			/* We don't want the loopback interface. */
			if (LocalAddr.sin_addr.s_addr ==
				htonl(INADDR_LOOPBACK)) {
				continue;
			}
		}
		/* increment j if we found an address which is not loopback
		 * and is up */
		j++;
	}
	close(LocalSock);

	return LocalAddr.sin_addr.s_addr;
#else
	// missing platform here ...
	return INADDR_ANY;
#endif
}


/*----------------------------------------------------------------------------*/
#if WIN
void winsock_init(void) {
	WSADATA wsaData;
	WORD wVersionRequested = MAKEWORD(2, 2);
	int WSerr = WSAStartup(wVersionRequested, &wsaData);
	if (WSerr != 0) {
		LOG_ERROR("Bad winsock version", NULL);
		exit(1);
	}
}

/*----------------------------------------------------------------------------*/
void winsock_close(void) {
	WSACleanup();
}
#endif


/*----------------------------------------------------------------------------*/
int shutdown_socket(int sd)
{
	if (sd <= 0) return -1;

#ifdef WIN32
	shutdown(sd, SD_BOTH);
#else
	shutdown(sd, SHUT_RDWR);
#endif

	LOG_DEBUG("closed socket %d", sd);

	return closesocket(sd);
}


/*----------------------------------------------------------------------------*/
int bind_socket(unsigned short *port, int mode)
{
	int sock;
	socklen_t len = sizeof(struct sockaddr);
	struct sockaddr_in addr;

	if ((sock = socket(AF_INET, mode, 0)) < 0) {
		LOG_ERROR("cannot create socket %d", sock);
		return sock;
	}

	/*  Populate socket address structure  */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port        = htons(*port);
#ifdef SIN_LEN
	si.sin_len = sizeof(si);
#endif

	if (bind(sock, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
		closesocket(sock);
		LOG_ERROR("cannot bind socket %d", sock);
		return -1;
	}

	if (!*port) {
		getsockname(sock, (struct sockaddr *) &addr, &len);
		*port = ntohs(addr.sin_port);
	}

	LOG_DEBUG("socket binding %d on port %d", sock, *port);

	return sock;
}


/*----------------------------------------------------------------------------*/
int conn_socket(unsigned short port)
{
	struct sockaddr_in addr;
	int sd;

	sd = socket(AF_INET, SOCK_STREAM, 0);

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(port);

	if (sd < 0 || connect(sd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		close(sd);
		return -1;
	}

	LOG_DEBUG("created socket %d", sd);

	return sd;
}



/*----------------------------------------------------------------------------*/
/* 																			  */
/* SYSTEM utils															 	  */
/* 																			  */
/*----------------------------------------------------------------------------*/

#if WIN
/*----------------------------------------------------------------------------*/
void *dlopen(const char *filename, int flag) {
	SetLastError(0);
	return LoadLibrary((LPCTSTR)filename);
}

/*----------------------------------------------------------------------------*/
void *dlsym(void *handle, const char *symbol) {
	SetLastError(0);
	return (void *)GetProcAddress(handle, symbol);
}

/*----------------------------------------------------------------------------*/
char *dlerror(void) {
	static char ret[32];
	int last = GetLastError();
	if (last) {
		sprintf(ret, "code: %i", last);
		SetLastError(0);
		return ret;
	}
	return NULL;
}
#endif


/*----------------------------------------------------------------------------*/
#if LINUX || FREEBSD
void touch_memory(u8_t *buf, size_t size) {
	u8_t *ptr;
	for (ptr = buf; ptr < buf + size; ptr += sysconf(_SC_PAGESIZE)) {
		*ptr = 0;
	}
}
#endif


/*----------------------------------------------------------------------------*/
#if LINUX || FREEBSD || OSX
char *GetTempPath(u16_t size, char *path)
{
	strncpy(path, P_tmpdir, size);
	if (!strlen(path)) strncpy(path, "/var/tmp", size);
	path[size - 1] = '\0';
	return path;
}
#endif

/*----------------------------------------------------------------------------*/
// cmdline parsing
char *next_param(char *src, char c) {
	static char *str = NULL;
	char *ptr, *ret;
	if (src) str = src;
	if (str && (ptr = strchr(str, c))) {
		ret = str;
		*ptr = '\0';
		str = ptr + 1;
	} else {
		ret = str;
		str = NULL;
	}

	return ret && ret[0] ? ret : NULL;
}

/*----------------------------------------------------------------------------*/
/* 																			  */
/* clock																 	  */
/* 																			  */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
u32_t gettime_ms(void) {
#if WIN
	return GetTickCount();
#else
#if LINUX || FREEBSD
	struct timespec ts;
	if (!clock_gettime(CLOCK_MONOTONIC, &ts)) {
		return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
	}
#endif
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}


/*----------------------------------------------------------------------------*/
u64_t gettime_ms64(void) {
#if WIN
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	return (((u64_t) ft.dwHighDateTime) << 32 | ft.dwLowDateTime) / 10000;
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (u64_t) (tv.tv_sec + 0x83AA7E80) * 1000 + tv.tv_usec / 1000;
#endif
}


/*----------------------------------------------------------------------------*/
/* 																			  */
/* STDLIB extensions													 	  */
/* 																			  */
/*----------------------------------------------------------------------------*/

#if LINUX || OSX || FREEBSD || SUNOS
/*---------------------------------------------------------------------------*/
char *strlwr(char *str)
{
 char *p = str;
 while (*p) {
	*p = tolower(*p);
	p++;
 }
 return str;
}
#endif


/*---------------------------------------------------------------------------*/
char *stristr(char *s1, char *s2)
{
 char *s1_lwr, *s2_lwr, *p;

 if (!s1 || !s2) return NULL;

 s1_lwr = strlwr(strdup(s1));
 s2_lwr = strlwr(strdup(s2));
 p = strstr(s1_lwr, s2_lwr);

 if (p) p = s1 + (p - s1_lwr);
 free(s1_lwr);
 free(s2_lwr);
 return p;
}

#if WIN
/*---------------------------------------------------------------------------*/
char* strsep(char** stringp, const char* delim)
{
  char* start = *stringp;
  char* p;

  p = (start != NULL) ? strpbrk(start, delim) : NULL;

  if (p == NULL)  {
	*stringp = NULL;
  } else {
	*p = '\0';
	*stringp = p + 1;
  }

  return start;
}

/*---------------------------------------------------------------------------*/
char *strndup(const char *s, size_t n) {
	char *p = malloc(n + 1);
	strncpy(p, s, n);
	p[n] = '\0';

	return p;
}
#endif


/*----------------------------------------------------------------------------*/
char* strextract(char *s1, char *beg, char *end)
{
	char *p1, *p2, *res;

	p1 = stristr(s1, beg);
	if (!p1) return NULL;

	p1 += strlen(beg);
	p2 = stristr(p1, end);
	if (!p2) return strdup(p1);

	res = malloc(p2 - p1 + 1);
	memcpy(res, p1, p2 - p1);
	res[p2 - p1] = '\0';

	return res;
}


#if WIN
/*----------------------------------------------------------------------------*/
int asprintf(char **strp, const char *fmt, ...)
{
	va_list args, cp;
	int len, ret = 0;

	va_start(args, fmt);
	len = vsnprintf(NULL, 0, fmt, args);
	*strp = malloc(len + 1);

	if (*strp) ret = vsprintf(*strp, fmt, args);

	va_end(args);

	return ret;
}
#endif

/*---------------------------------------------------------------------------*/
u32_t hash32(char *str)
{
	u32_t hash = 5381;
	s32_t c;

	if (!str) return 0;

	while ((c = *str++) != 0)
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

	return hash;
}

/*---------------------------------------------------------------------------*/
char *ltrim(char *s)
{
	while(isspace(*s)) s++;
	return s;
}

/*---------------------------------------------------------------------------*/
char *rtrim(char *s)
{
	char* back = s + strlen(s);
	while(isspace(*--back));
	*(back+1) = '\0';
	return s;
}

/*---------------------------------------------------------------------------*/
char *trim(char *s)
{
	return rtrim(ltrim(s));
}


/*----------------------------------------------------------------------------*/
/* 																			  */
/* HTTP management														 	  */
/* 																			  */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
bool http_parse(int sock, char *method, key_data_t *rkd, char **body, int *len)
{
	char line[256], *dp;
	unsigned j;
	int i, timeout = 100;

	rkd[0].key = NULL;

	if ((i = read_line(sock, line, sizeof(line), timeout)) <= 0) {
		if (i < 0) {
			LOG_ERROR("cannot read method", NULL);
		}
		return false;
	}

	if (!sscanf(line, "%s", method)) {
		LOG_ERROR("missing method", NULL);
		return false;
	}

	i = *len = 0;

	while (read_line(sock, line, sizeof(line), timeout) > 0) {

		LOG_SDEBUG("sock: %u, received %s", line);

		// line folding should be deprecated
		if (i && rkd[i].key && (line[0] == ' ' || line[0] == '\t')) {
			for(j = 0; j < strlen(line); j++) if (line[j] != ' ' && line[j] != '\t') break;
			rkd[i].data = realloc(rkd[i].data, strlen(rkd[i].data) + strlen(line + j) + 1);
			strcat(rkd[i].data, line + j);
			continue;
		}

		dp = strstr(line,":");

		if (!dp){
			LOG_ERROR("Request failed, bad header", NULL);
			kd_free(rkd);
			return false;
		}

		*dp = 0;
		rkd[i].key = strdup(line);
		rkd[i].data = strdup(ltrim(dp + 1));

		if (!strcasecmp(rkd[i].key, "Content-Length")) *len = atol(rkd[i].data);

		i++;
		rkd[i].key = NULL;
	}

	if (*len) {
		int size = 0;

		*body = malloc(*len + 1);
		while (*body && size < *len) {
			int bytes = recv(sock, *body + size, *len - size, 0);
			if (bytes <= 0) break;
			size += bytes;
		}

		(*body)[*len] = '\0';

		if (!*body || size != *len) {
			LOG_ERROR("content length receive error %d %d", *len, size);
		}
	}

	return true;
}


/*----------------------------------------------------------------------------*/
int read_line(int fd, char *line, int maxlen, int timeout)
{
	int i,rval;
	int count=0;
	struct pollfd pfds;
	char ch;

	*line = 0;
	pfds.fd = fd;
	pfds.events = POLLIN;

	for(i = 0; i < maxlen; i++){
		if (poll(&pfds, 1, timeout)) rval=recv(fd, &ch, 1, 0);
		else return 0;

		if (rval == -1) {
			if (errno == EAGAIN) return 0;
			LOG_ERROR("fd: %d read error: %s", fd, strerror(errno));
			return -1;
		}

		if (rval == 0) {
			LOG_INFO("disconnected on the other end %u", fd);
			return 0;
		}

		if (ch == '\n') {
			*line=0;
			return count;
		}

		if (ch=='\r') continue;

		*line++=ch;
		count++;
		if (count >= maxlen-1) break;
	}

	*line = 0;
	return count;
}


/*----------------------------------------------------------------------------*/
char *http_send(int sock, char *method, key_data_t *rkd)
{
	unsigned sent, len;
	char *resp = kd_dump(rkd);
	char *data = malloc(strlen(method) + 2 + strlen(resp) + 2 + 1);

	len = sprintf(data, "%s\r\n%s\r\n", method, resp);
	NFREE(resp);

	sent = send(sock, data, len, 0);

	if (sent != len) {
		LOG_ERROR("HTTP send() error:%s %u (strlen=%u)", data, sent, len);
		NFREE(data);
	}

	return data;
}


/*----------------------------------------------------------------------------*/
char *kd_lookup(key_data_t *kd, char *key)
{
	int i = 0;
	while (kd && kd[i].key){
		if (!strcasecmp(kd[i].key, key)) return kd[i].data;
		i++;
	}
	return NULL;
}


/*----------------------------------------------------------------------------*/
bool kd_add(key_data_t *kd, char *key, char *data)
{
	int i = 0;
	while (kd && kd[i].key) i++;

	kd[i].key = strdup(key);
	kd[i].data = strdup(data);
	kd[i+1].key = NULL;

	return NULL;
}


/*----------------------------------------------------------------------------*/
void kd_free(key_data_t *kd)
{
	int i = 0;
	while (kd && kd[i].key){
		free(kd[i].key);
		if (kd[i].data) free(kd[i].data);
		i++;
	}

	kd[0].key = NULL;
}


/*----------------------------------------------------------------------------*/
char *kd_dump(key_data_t *kd)
{
	int i = 0;
	int pos = 0, size = 0;
	char *str = NULL;

	if (!kd || !kd[0].key) return strdup("\r\n");

	while (kd && kd[i].key) {
		char *buf;
		int len;

		len = asprintf(&buf, "%s: %s\r\n", kd[i].key, kd[i].data);

		while (pos + len >= size) {
			void *p = realloc(str, size + 1024);
			size += 1024;
			if (!p) {
				free(str);
				return NULL;
			}
			str = p;
		}

		memcpy(str + pos, buf, len);

		pos += len;
		free(buf);
		i++;
	}

	str[pos] = '\0';

	return str;
}


/*----------------------------------------------------------------------------*/
/* 																			  */
/* XML utils															  */
/* 																			  */
/*----------------------------------------------------------------------------*/

#ifdef _USE_XML_
/*----------------------------------------------------------------------------*/
IXML_Node *XMLAddNode(IXML_Document *doc, IXML_Node *parent, char *name, char *fmt, ...)
{
	IXML_Node *node, *elm;
	char buf[256];
	va_list args;

	elm = (IXML_Node*) ixmlDocument_createElement(doc, name);
	if (parent) ixmlNode_appendChild(parent, elm);
	else ixmlNode_appendChild((IXML_Node*) doc, elm);

	if (fmt) {
		va_start(args, fmt);

		vsprintf(buf, fmt, args);
		node = ixmlDocument_createTextNode(doc, buf);
		ixmlNode_appendChild(elm, node);

		va_end(args);
	}

	return elm;
}


/*----------------------------------------------------------------------------*/
IXML_Node *XMLUpdateNode(IXML_Document *doc, IXML_Node *parent, bool refresh, char *name, char *fmt, ...)
{
	char buf[256];
	va_list args;
	IXML_Node *node = (IXML_Node*) ixmlDocument_getElementById((IXML_Document*) parent, name);

	va_start(args, fmt);
	vsprintf(buf, fmt, args);

	if (!node) XMLAddNode(doc, parent, name, buf);
	else if (refresh) {
		node = ixmlNode_getFirstChild(node);
		ixmlNode_setNodeValue(node, buf);
	}

	va_end(args);

	return node;
}


/*----------------------------------------------------------------------------*/
char *XMLGetFirstDocumentItem(IXML_Document *doc, const char *item, bool strict)
{
	IXML_NodeList *nodeList = NULL;
	IXML_Node *textNode = NULL;
	IXML_Node *tmpNode = NULL;
	char *ret = NULL;
	int i;

	nodeList = ixmlDocument_getElementsByTagName(doc, (char *)item);

	for (i = 0; nodeList && i < (int) ixmlNodeList_length(nodeList); i++) {
		tmpNode = ixmlNodeList_item(nodeList, i);

		if (tmpNode) {
			textNode = ixmlNode_getFirstChild(tmpNode);
			if (textNode) {
				ret = strdup(ixmlNode_getNodeValue(textNode));
				if (ret) break;
				LOG_WARN("ixmlNode_getNodeValue returned NULL", NULL);
			} else {
				LOG_WARN("(BUG) ixmlNode_getFirstChild(tmpNode) returned NULL", NULL);
			}
		} else {
			LOG_WARN("ixmlNodeList_item(nodeList, %d) returned NULL", i, NULL);
		}

		if (strict) break;
	}

	if (nodeList) {
		ixmlNodeList_free(nodeList);
    } else {
		LOG_SDEBUG("Error finding %s in XML Node", item);
    }

	return ret;
}


/*----------------------------------------------------------------------------*/
bool XMLMatchDocumentItem(IXML_Document *doc, const char *item, const char *s)
{
	IXML_NodeList *nodeList = NULL;
	IXML_Node *textNode = NULL;
	IXML_Node *tmpNode = NULL;
	int i;
	bool ret = false;

	nodeList = ixmlDocument_getElementsByTagName(doc, (char *)item);

	for (i = 0; nodeList && i < (int) ixmlNodeList_length(nodeList); i++) {
		tmpNode = ixmlNodeList_item(nodeList, i);
		if (!tmpNode) continue;
		textNode = ixmlNode_getFirstChild(tmpNode);
		if (!textNode) continue;
		if (!strcmp(ixmlNode_getNodeValue(textNode), s)) {
			ret = true;
			break;
		}
	}

	if (nodeList) ixmlNodeList_free(nodeList);

	return ret;
}


/*----------------------------------------------------------------------------*/
char *XMLGetFirstElementItem(IXML_Element *element, const char *item)
{
	IXML_NodeList *nodeList = NULL;
	IXML_Node *textNode = NULL;
	IXML_Node *tmpNode = NULL;
	char *ret = NULL;

	nodeList = ixmlElement_getElementsByTagName(element, (char *)item);
	if (nodeList == NULL) {
		LOG_WARN("Error finding %s in XML Node", item);
		return NULL;
	}
	tmpNode = ixmlNodeList_item(nodeList, 0);
	if (!tmpNode) {
		LOG_WARN("Error finding %s value in XML Node", item);
		ixmlNodeList_free(nodeList);
		return NULL;
	}
	textNode = ixmlNode_getFirstChild(tmpNode);
	ret = strdup(ixmlNode_getNodeValue(textNode));
	if (!ret) {
		LOG_ERROR("Error allocating memory for %s in XML Node",item);
		ixmlNodeList_free(nodeList);
		return NULL;
	}
	ixmlNodeList_free(nodeList);

	return ret;
}


/*----------------------------------------------------------------------------*/
int XMLAddAttribute(IXML_Document *doc, IXML_Node *parent, char *name, char *fmt, ...)
{
	char buf[256];
	int ret;
	va_list args;

	va_start(args, fmt);
	vsnprintf(buf, 256, fmt, args);
	ret = ixmlElement_setAttribute((IXML_Element*) parent, name, buf);
	va_end(args);

	return ret;
}


/*----------------------------------------------------------------------------*/
const char *XMLGetLocalName(IXML_Document *doc, int Depth)
{
	IXML_Node *node = (IXML_Node*) doc;

	while (Depth--) {
		node = ixmlNode_getFirstChild(node);
		if (!node) return NULL;
	}

	return ixmlNode_getLocalName(node);
}
#endif


/*--------------------------------------------------------------------------*/
void free_metadata(struct metadata_s *metadata)
{
	NFREE(metadata->artist);
	NFREE(metadata->album);
	NFREE(metadata->title);
	NFREE(metadata->genre);
	NFREE(metadata->path);
	NFREE(metadata->artwork);
	NFREE(metadata->remote_title);
}


/*--------------------------------------------------------------------------*/
void dup_metadata(struct metadata_s *dst, struct metadata_s *src)
{
	free_metadata(dst);
	if (src->artist) dst->artist = strdup(src->artist);
	if (src->album) dst->album = strdup(src->album);
	if (src->title) dst->title = strdup(src->title);
	if (src->genre) dst->genre = strdup(src->genre);
	if (src->path) dst->path = strdup(src->path);
	if (src->artwork) dst->artwork = strdup(src->artwork);
	if (src->remote_title) dst->remote_title = strdup(src->remote_title);
	dst->duration = src->duration;
	dst->track = src->track;
	dst->track_hash = src->track_hash;

}


/*----------------------------------------------------------------------------*/

int _fprintf(FILE *file, ...)
{
	va_list args;
	char *fmt;
	int n;

	va_start(args, file);
	fmt = va_arg(args, char*);

	n = vfprintf(file, fmt, args);
	va_end(args);
	return n;
}









