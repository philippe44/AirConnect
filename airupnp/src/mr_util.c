/*
 *  AirUPnP - AirPlay to uPNP gateway
 *
 *	(c) Philippe 2015-2017, philippe_44@outlook.com
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

#include "platform.h"
#include "airupnp.h"
#include "avt_util.h"
#include "upnpdebug.h"
#include "upnptools.h"
#include "mr_util.h"
#include "log_util.h"

extern log_level	util_loglevel;
static log_level 	*loglevel = &util_loglevel;


/*----------------------------------------------------------------------------*/
void FlushMRDevices(void)
{
	int i;

	for (i = 0; i < MAX_RENDERERS; i++) {
		struct sMR *p = &glMRDevices[i];
		if (p->InUse) {
			// critical to stop the device otherwise libupnp mean wait forever
			if (p->RaopState == RAOP_PLAY) AVTStop(p);
			raop_delete(p->Raop);
			DelMRDevice(p);
		}
	}
}


/*----------------------------------------------------------------------------*/
void DelMRDevice(struct sMR *p)
{
	pthread_mutex_lock(&p->Mutex);
	p->Running = false;
	pthread_mutex_unlock(&p->Mutex);
	pthread_join(p->Thread, NULL);

	pthread_mutex_lock(&p->Mutex);
	p->InUse = false;

	AVTActionFlush(&p->ActionQueue);
	free_metadata(&p->MetaData);
	NFREE(p->CurrentURI);

	pthread_mutex_unlock(&p->Mutex);
	pthread_mutex_destroy(&p->Mutex);

	memset(p, 0, sizeof(struct sMR));
}


 /*----------------------------------------------------------------------------*/
void MakeMacUnique(struct sMR *Device)
{
	int i;

	for (i = 0; i < MAX_RENDERERS; i++) {
		if (!glMRDevices[i].InUse || Device == &glMRDevices[i]) continue;
		if (!memcmp(&glMRDevices[i].Config.mac, &Device->Config.mac, 6)) {
			u32_t hash = hash32(Device->UDN);

			LOG_INFO("[%p]: duplicated mac ... updating", Device);
			memset(&Device->Config.mac[0], 0xcc, 2);
			memcpy(&Device->Config.mac[0] + 2, &hash, 4);
		}
	}
}


/*----------------------------------------------------------------------------*/
struct sMR* SID2Device(char *SID)
{
	int i, j;

	for (i = 0; i < MAX_RENDERERS; i++) {
		if (!glMRDevices[i].InUse) continue;
		for (j = 0; j < NB_SRV; j++) {
			if (!strcmp(glMRDevices[i].Service[j].SID, SID)) {
				return &glMRDevices[i];
			}
		}
	}

	return NULL;
}


/*----------------------------------------------------------------------------*/
struct sMR* UDN2Device(char *UDN)
{
	int i;

	for (i = 0; i < MAX_RENDERERS; i++) {
		if (!glMRDevices[i].InUse) continue;
		if (!strcmp(glMRDevices[i].UDN, UDN)) {
			return &glMRDevices[i];
		}
	}

	return NULL;
}


/*----------------------------------------------------------------------------*/
struct sMR* CURL2Device(char *CtrlURL)
{
	int i, j;

	for (i = 0; i < MAX_RENDERERS; i++) {
		if (!glMRDevices[i].InUse) continue;
		for (j = 0; j < NB_SRV; j++) {
			if (!strcmp(glMRDevices[i].Service[j].ControlURL, CtrlURL)) {
				return &glMRDevices[i];
			}
		}
	}

	return NULL;
}


/*----------------------------------------------------------------------------*/
in_addr_t ExtractIP(const char *URL)
{
	char *p1, ip[32];

	sscanf(URL, "http://%31s", ip);

	ip[31] = '\0';
	p1 = strchr(ip, ':');
	if (p1) *p1 = '\0';

	return inet_addr(ip);;
}


/*----------------------------------------------------------------------------*/
char *uPNPEvent2String(Upnp_EventType S)
{
	switch (S) {
	/* Discovery */
	case UPNP_DISCOVERY_ADVERTISEMENT_ALIVE:
		return "UPNP_DISCOVERY_ADVERTISEMENT_ALIVE";
	case UPNP_DISCOVERY_ADVERTISEMENT_BYEBYE:
		return "UPNP_DISCOVERY_ADVERTISEMENT_BYEBYE";
	case UPNP_DISCOVERY_SEARCH_RESULT:
		return "UPNP_DISCOVERY_SEARCH_RESULT";
	case UPNP_DISCOVERY_SEARCH_TIMEOUT:
		return "UPNP_DISCOVERY_SEARCH_TIMEOUT";
	/* SOAP */
	case UPNP_CONTROL_ACTION_REQUEST:
		return "UPNP_CONTROL_ACTION_REQUEST";
	case UPNP_CONTROL_ACTION_COMPLETE:
		return "UPNP_CONTROL_ACTION_COMPLETE";
	case UPNP_CONTROL_GET_VAR_REQUEST:
		return "UPNP_CONTROL_GET_VAR_REQUEST";
	case UPNP_CONTROL_GET_VAR_COMPLETE:
		return "UPNP_CONTROL_GET_VAR_COMPLETE";
	case UPNP_EVENT_SUBSCRIPTION_REQUEST:
		return "UPNP_EVENT_SUBSCRIPTION_REQUEST";
	case UPNP_EVENT_RECEIVED:
		return "UPNP_EVENT_RECEIVED";
	case UPNP_EVENT_RENEWAL_COMPLETE:
		return "UPNP_EVENT_RENEWAL_COMPLETE";
	case UPNP_EVENT_SUBSCRIBE_COMPLETE:
		return "UPNP_EVENT_SUBSCRIBE_COMPLETE";
	case UPNP_EVENT_UNSUBSCRIBE_COMPLETE:
		return "UPNP_EVENT_UNSUBSCRIBE_COMPLETE";
	case UPNP_EVENT_AUTORENEWAL_FAILED:
		return "UPNP_EVENT_AUTORENEWAL_FAILED";
	case UPNP_EVENT_SUBSCRIPTION_EXPIRED:
		return "UPNP_EVENT_SUBSCRIPTION_EXPIRED";
	}

	return "";
}









