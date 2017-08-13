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

#ifndef __MR_UTIL_H
#define __MR_UTIL_H

#include "airupnp.h"

void 			FlushMRDevices(void);
void 			DelMRDevice(struct sMR *p);

struct sMR* 	SID2Device(Upnp_SID Sid);
struct sMR* 	CURL2Device(char *CtrlURL);
struct sMR* 	UDN2Device(char *SID);

void 			MakeMacUnique(struct sMR *Device);
in_addr_t 		ExtractIP(const char *URL);

int 	   	XMLFindAndParseService(IXML_Document *DescDoc, const char *location,
							const char *serviceTypeBase, char **serviceId,
							char **serviceType, char **eventURL, char **controlURL);
char 	   	*XMLGetChangeItem(IXML_Document *doc, char *Tag, char *SearchAttr, char *SearchVal, char *RetAttr);

char*			uPNPEvent2String(Upnp_EventType S);

#endif
