/*
 *  AirUPnP - AirPlay to UPnP Bridge
 *
 *	(c) Philippe, philippe_44@outlook.com
 *
 *  See LICENSE
 *
 */

#pragma once

#include "airupnp.h"

void 		FlushMRDevices(void);
void 		DelMRDevice(struct sMR *p);
struct sMR *GetMaster(struct sMR *Device, char **Name);
int 		CalcGroupVolume(struct sMR *Master);
bool		CheckAndLock(struct sMR *Device);
double		GetLocalGroupVolume(struct sMR *Member, int *count);

struct sMR*  SID2Device(const UpnpString *SID);
struct sMR*  CURL2Device(const UpnpString *CtrlURL);
struct sMR*  PURL2Device(const UpnpString *URL);
struct sMR*  UDN2Device(const char *SID);

struct sService* EventURL2Service(const UpnpString *URL, struct sService *s);

int  XMLFindAndParseService(IXML_Document* DescDoc, const char* location, const char* serviceTypeBase, char** serviceType, 
                            char** serviceId, char** eventURL, char** controlURL, char** serviceURL);
bool  XMLFindAction(const char* base, char* service, char* action);
char* XMLGetChangeItem(IXML_Document *doc, char *Tag, char *SearchAttr, char *SearchVal, char *RetAttr);

char* uPNPEvent2String(Upnp_EventType S);

