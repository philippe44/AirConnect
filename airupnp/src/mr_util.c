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
#include "ixml.h"
#include "airupnp.h"
#include "avt_util.h"
#include "upnpdebug.h"
#include "upnptools.h"
#include "util.h"
#include "mr_util.h"
#include "log_util.h"

extern log_level	util_loglevel;
static log_level 	*loglevel = &util_loglevel;

static IXML_Node*	_getAttributeNode(IXML_Node *node, char *SearchAttr);

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
/* 																			  */
/* XML utils															  */
/* 																			  */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static IXML_NodeList *XMLGetNthServiceList(IXML_Document *doc, unsigned int n, bool *contd)
{
	IXML_NodeList *ServiceList = NULL;
	IXML_NodeList *servlistnodelist = NULL;
	IXML_Node *servlistnode = NULL;
	*contd = false;

	/*  ixmlDocument_getElementsByTagName()
	 *  Returns a NodeList of all Elements that match the given
	 *  tag name in the order in which they were encountered in a preorder
	 *  traversal of the Document tree.
	 *
	 *  return (NodeList*) A pointer to a NodeList containing the
	 *                      matching items or NULL on an error. 	 */
	LOG_SDEBUG("GetNthServiceList called : n = %d", n);
	servlistnodelist = ixmlDocument_getElementsByTagName(doc, "serviceList");
	if (servlistnodelist &&
		ixmlNodeList_length(servlistnodelist) &&
		n < ixmlNodeList_length(servlistnodelist)) {
		/* Retrieves a Node from a NodeList} specified by a
		 *  numerical index.
		 *
		 *  return (Node*) A pointer to a Node or NULL if there was an
		 *                  error. */
		servlistnode = ixmlNodeList_item(servlistnodelist, n);
		if (servlistnode) {
			/* create as list of DOM nodes */
			ServiceList = ixmlElement_getElementsByTagName(
				(IXML_Element *)servlistnode, "service");
			*contd = true;
		} else
			LOG_WARN("ixmlNodeList_item(nodeList, n) returned NULL", NULL);
	}
	if (servlistnodelist)
		ixmlNodeList_free(servlistnodelist);

	return ServiceList;
}

/*----------------------------------------------------------------------------*/
int XMLFindAndParseService(IXML_Document *DescDoc, const char *location,
	const char *serviceTypeBase, char **serviceType, char **serviceId, char **eventURL, char **controlURL)
{
	unsigned int i;
	unsigned long length;
	int found = 0;
	int ret;
	unsigned int sindex = 0;
	char *tempServiceType = NULL;
	char *baseURL = NULL;
	const char *base = NULL;
	char *relcontrolURL = NULL;
	char *releventURL = NULL;
	IXML_NodeList *serviceList = NULL;
	IXML_Element *service = NULL;
	bool contd = true;

	baseURL = XMLGetFirstDocumentItem(DescDoc, "URLBase");
	if (baseURL) base = baseURL;
	else base = location;

	for (sindex = 0; contd; sindex++) {
		tempServiceType = NULL;
		relcontrolURL = NULL;
		releventURL = NULL;
		service = NULL;

		if ((serviceList = XMLGetNthServiceList(DescDoc , sindex, &contd)) == NULL) continue;
		length = ixmlNodeList_length(serviceList);
		for (i = 0; i < length; i++) {
			service = (IXML_Element *)ixmlNodeList_item(serviceList, i);
			tempServiceType = XMLGetFirstElementItem((IXML_Element *)service, "serviceType");
			LOG_SDEBUG("serviceType %s", tempServiceType);

			// remove version from service type
			*strrchr(tempServiceType, ':') = '\0';
			if (tempServiceType && strcmp(tempServiceType, serviceTypeBase) == 0) {
				NFREE(*serviceType);
				*serviceType = XMLGetFirstElementItem((IXML_Element *)service, "serviceType");
				NFREE(*serviceId);
				*serviceId = XMLGetFirstElementItem(service, "serviceId");
				LOG_SDEBUG("Service %s, serviceId: %s", *serviceType, *serviceId);
				relcontrolURL = XMLGetFirstElementItem(service, "controlURL");
				releventURL = XMLGetFirstElementItem(service, "eventSubURL");
				NFREE(*controlURL);
				*controlURL = (char*) malloc(strlen(base) + strlen(relcontrolURL) + 1);
				if (*controlURL) {
					ret = UpnpResolveURL(base, relcontrolURL, *controlURL);
					if (ret != UPNP_E_SUCCESS) LOG_ERROR("Error generating controlURL from %s + %s", base, relcontrolURL);
				}
				NFREE(*eventURL);
				*eventURL = (char*) malloc(strlen(base) + strlen(releventURL) + 1);
				if (*eventURL) {
					ret = UpnpResolveURL(base, releventURL, *eventURL);
					if (ret != UPNP_E_SUCCESS) LOG_ERROR("Error generating eventURL from %s + %s", base, releventURL);
				}
				free(relcontrolURL);
				free(releventURL);
				relcontrolURL = NULL;
				releventURL = NULL;
				found = 1;
				break;
			}
			free(tempServiceType);
			tempServiceType = NULL;
		}
		free(tempServiceType);
		tempServiceType = NULL;
		if (serviceList) ixmlNodeList_free(serviceList);
		serviceList = NULL;
	}

	free(baseURL);

	return found;
}


/*----------------------------------------------------------------------------*/
char *XMLGetChangeItem(IXML_Document *doc, char *Tag, char *SearchAttr, char *SearchVal, char *RetAttr)
{
	IXML_Node *node;
	IXML_Document *ItemDoc;
	IXML_Element *LastChange;
	IXML_NodeList *List;
	char *buf, *ret = NULL;
	u32_t i;

	LastChange = ixmlDocument_getElementById(doc, "LastChange");
	if (!LastChange) return NULL;

	node = ixmlNode_getFirstChild((IXML_Node*) LastChange);
	if (!node) return NULL;

	buf = (char*) ixmlNode_getNodeValue(node);
	if (!buf) return NULL;

	ItemDoc = ixmlParseBuffer(buf);
	if (!ItemDoc) return NULL;

	List = ixmlDocument_getElementsByTagName(ItemDoc, Tag);
	if (!List) {
		ixmlDocument_free(ItemDoc);
		return NULL;
	}

	for (i = 0; i < ixmlNodeList_length(List); i++) {
		IXML_Node *node = ixmlNodeList_item(List, i);
		IXML_Node *attr = _getAttributeNode(node, SearchAttr);

		if (!attr) continue;

		if (!strcasecmp(ixmlNode_getNodeValue(attr), SearchVal)) {
			if ((node = ixmlNode_getNextSibling(attr)) == NULL)
				if ((node = ixmlNode_getPreviousSibling(attr)) == NULL) continue;
			if (!strcasecmp(ixmlNode_getNodeName(node), "val")) {
				ret = strdup(ixmlNode_getNodeValue(node));
				break;
			}
		}
	}

	ixmlNodeList_free(List);
	ixmlDocument_free(ItemDoc);

	return ret;
}


/*----------------------------------------------------------------------------*/
static IXML_Node *_getAttributeNode(IXML_Node *node, char *SearchAttr)
{
	IXML_Node *ret;
	IXML_NamedNodeMap *map = ixmlNode_getAttributes(node);
	int i;

	/*
	supposed to act like but case insensitive
	ixmlElement_getAttributeNode((IXML_Element*) node, SearchAttr);
	*/

	for (i = 0; i < ixmlNamedNodeMap_getLength(map); i++) {
		ret = ixmlNamedNodeMap_item(map, i);
		if (strcasecmp(ixmlNode_getNodeName(ret), SearchAttr)) ret = NULL;
		else break;
	}

	ixmlNamedNodeMap_free(map);

	return ret;
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









