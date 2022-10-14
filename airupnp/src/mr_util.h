/*
 *  Squeeze2upnp - LMS to uPNP gateway
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

void 		MakeMacUnique(struct sMR *Device);
in_addr_t	ExtractIP(const char *URL);

int 	   	 XMLFindAndParseService(IXML_Document *DescDoc, const char *location,
							const char *serviceTypeBase, char **serviceId,
							char **serviceType, char **eventURL, char **controlURL);
char 	   	 *XMLGetChangeItem(IXML_Document *doc, char *Tag, char *SearchAttr, char *SearchVal, char *RetAttr);

char*		 uPNPEvent2String(Upnp_EventType S);

