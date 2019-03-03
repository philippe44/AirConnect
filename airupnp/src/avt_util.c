/*
 *  UPnP control utils
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


#include <stdlib.h>
#include <math.h>

#include "platform.h"
#include "upnptools.h"
#include "upnp.h"
#include "airupnp.h"
#include "util.h"
#include "avt_util.h"

/*
WARNING
 - ALL THESE FUNCTION MUST BE CALLED WITH MUTEX LOCKED
*/

extern log_level	upnp_loglevel;
static log_level 	*loglevel = &upnp_loglevel;

static char *CreateDIDL(char *URI, char *ProtInfo, struct metadata_s *MetaData, struct sMRConfig *Config);

/*----------------------------------------------------------------------------*/
bool SubmitTransportAction(struct sMR *Device, IXML_Document *ActionNode)
{
	struct sService *Service = &Device->Service[AVT_SRV_IDX];
	int rc = 0;

	if (!Device->WaitCookie) {
		Device->WaitCookie = Device->seqN++;
		rc = UpnpSendActionAsync(glControlPointHandle, Service->ControlURL, Service->Type,
								 NULL, ActionNode, ActionHandler, Device->WaitCookie);

		if (rc != UPNP_E_SUCCESS) {
			LOG_ERROR("[%p]: Error in UpnpSendActionAsync -- %d", Device, rc);
		}

		ixmlDocument_free(ActionNode);
	}
	else {
		tAction *Action = malloc(sizeof(tAction));
		Action->Device = Device;
		Action->ActionNode = ActionNode;
		QueueInsert(&Device->ActionQueue, Action);
	}

	return (rc == 0);
}


/*----------------------------------------------------------------------------*/
void AVTActionFlush(tQueue *Queue)
{
	tAction *Action;

	while ((Action = QueueExtract(Queue)) != NULL) {
		free(Action);
	}
}

/*----------------------------------------------------------------------------*/
bool AVTSetURI(struct sMR *Device, char *URI, struct metadata_s *MetaData, char *ProtoInfo)
{
	IXML_Document *ActionNode = NULL;
	struct sService *Service = &Device->Service[AVT_SRV_IDX];
	char *DIDLData;

	DIDLData = CreateDIDL(URI, ProtoInfo, MetaData, &Device->Config);
	LOG_DEBUG("[%p]: DIDL header: %s", Device, DIDLData);

	LOG_INFO("[%p]: uPNP setURI %s (cookie %p)", Device, URI, Device->seqN);

	if ((ActionNode = UpnpMakeAction("SetAVTransportURI", Service->Type, 0, NULL)) == NULL) return false;
	UpnpAddToAction(&ActionNode, "SetAVTransportURI", Service->Type, "InstanceID", "0");
	UpnpAddToAction(&ActionNode, "SetAVTransportURI", Service->Type, "CurrentURI", URI);
	UpnpAddToAction(&ActionNode, "SetAVTransportURI", Service->Type, "CurrentURIMetaData", DIDLData);
	free(DIDLData);

	return SubmitTransportAction(Device, ActionNode);
}

/*----------------------------------------------------------------------------*/
bool AVTSetNextURI(struct sMR *Device, char *URI, struct metadata_s *MetaData, char *ProtoInfo)
{
	IXML_Document *ActionNode = NULL;
	struct sService *Service = &Device->Service[AVT_SRV_IDX];
	char *DIDLData;

	DIDLData = CreateDIDL(URI, ProtoInfo, MetaData, &Device->Config);
	LOG_DEBUG("[%p]: DIDL header: %s", Device, DIDLData);

	LOG_INFO("[%p]: uPNP setNextURI %s (cookie %p)", Device, URI, Device->seqN);

	if ((ActionNode = UpnpMakeAction("SetNextAVTransportURI", Service->Type, 0, NULL)) == NULL) return false;
	UpnpAddToAction(&ActionNode, "SetNextAVTransportURI", Service->Type, "InstanceID", "0");
	UpnpAddToAction(&ActionNode, "SetNextAVTransportURI", Service->Type, "NextURI", URI);
	UpnpAddToAction(&ActionNode, "SetNextAVTransportURI", Service->Type, "NextURIMetaData", DIDLData);
	free(DIDLData);

	return SubmitTransportAction(Device, ActionNode);
}

/*----------------------------------------------------------------------------*/
int AVTCallAction(struct sMR *Device, char *Action, void *Cookie)
{
	IXML_Document *ActionNode = NULL;
	struct sService *Service = &Device->Service[AVT_SRV_IDX];
	int rc;

	LOG_SDEBUG("[%p]: uPNP %s (cookie %p)", Device, Action, Cookie);

	if ((ActionNode = UpnpMakeAction(Action, Service->Type, 0, NULL)) == NULL) return false;
	UpnpAddToAction(&ActionNode, Action, Service->Type, "InstanceID", "0");

	rc = UpnpSendActionAsync(glControlPointHandle, Service->ControlURL, Service->Type, NULL,
							 ActionNode, ActionHandler, Cookie);

	if (rc != UPNP_E_SUCCESS) LOG_ERROR("[%p]: Error in UpnpSendActionAsync -- %d", Device, rc);
	ixmlDocument_free(ActionNode);

	return rc;
}


/*----------------------------------------------------------------------------*/
bool AVTPlay(struct sMR *Device)
{
	struct sService *Service = &Device->Service[AVT_SRV_IDX];
	IXML_Document *ActionNode = NULL;

	LOG_INFO("[%p]: uPNP play (cookie %p)", Device, Device->seqN);

	if ((ActionNode =  UpnpMakeAction("Play", Service->Type, 0, NULL)) == NULL) return false;
	UpnpAddToAction(&ActionNode, "Play", Service->Type, "InstanceID", "0");
	UpnpAddToAction(&ActionNode, "Play", Service->Type, "Speed", "1");

	return SubmitTransportAction(Device, ActionNode);
}


/*----------------------------------------------------------------------------*/
bool AVTSetPlayMode(struct sMR *Device)
{
	struct sService *Service = &Device->Service[AVT_SRV_IDX];
	IXML_Document *ActionNode = NULL;

	LOG_INFO("[%p]: uPNP set play mode (cookie %p)", Device, Device->seqN);
	if ((ActionNode =  UpnpMakeAction("SetPlayMode", Service->Type, 0, NULL)) == NULL) return false;;
	UpnpAddToAction(&ActionNode, "SetPlayMode", Service->Type, "InstanceID", "0");
	UpnpAddToAction(&ActionNode, "SetPlayMode", Service->Type, "NewPlayMode", "NORMAL");

	return SubmitTransportAction(Device, ActionNode);
}


/*----------------------------------------------------------------------------*/
bool AVTSeek(struct sMR *Device, unsigned Interval)
{
	struct sService *Service = &Device->Service[AVT_SRV_IDX];
	IXML_Document *ActionNode = NULL;
	char	params[128];

	LOG_INFO("[%p]: uPNP seek (%ds) (cookie %p)", Device, Interval, Device->seqN);

	if ((ActionNode =  UpnpMakeAction("Seek", Service->Type, 0, NULL)) == NULL) return false;
	UpnpAddToAction(&ActionNode, "Seek", Service->Type, "InstanceID", "0");
	sprintf(params, "%d", (int) (Interval / 1000 + 0.5));
	UpnpAddToAction(&ActionNode, "Seek", Service->Type, "Unit", params);
	UpnpAddToAction(&ActionNode, "Seek", Service->Type, "Target", "REL_TIME");

	return SubmitTransportAction(Device, ActionNode);
}


/*----------------------------------------------------------------------------*/
bool AVTBasic(struct sMR *Device, char *Action)
{
	struct sService *Service = &Device->Service[AVT_SRV_IDX];
	IXML_Document *ActionNode = NULL;

	LOG_INFO("[%p]: uPNP %s (cookie %p)", Device, Action, Device->seqN);

	if ((ActionNode = UpnpMakeAction(Action, Service->Type, 0, NULL)) == NULL) return false;
	UpnpAddToAction(&ActionNode, Action, Service->Type, "InstanceID", "0");

	return SubmitTransportAction(Device, ActionNode);
}


/*----------------------------------------------------------------------------*/
bool AVTStop(struct sMR *Device)
{
	struct sService *Service = &Device->Service[AVT_SRV_IDX];
	IXML_Document *ActionNode = NULL;
	int rc;

	LOG_INFO("[%p]: uPNP stop (cookie %p)", Device, Device->seqN);

	if ((ActionNode = UpnpMakeAction("Stop", Service->Type, 0, NULL)) == NULL) return false;
	UpnpAddToAction(&ActionNode, "Stop", Service->Type, "InstanceID", "0");
	AVTActionFlush(&Device->ActionQueue);

	Device->WaitCookie = Device->seqN++;
	rc = UpnpSendActionAsync(glControlPointHandle, Service->ControlURL, Service->Type,
							 NULL, ActionNode, ActionHandler, Device->WaitCookie);

	ixmlDocument_free(ActionNode);

	if (rc != UPNP_E_SUCCESS) {
		LOG_ERROR("[%p]: Error in UpnpSendActionAsync -- %d", Device, rc);
	}

	return (rc == 0);
}


/*----------------------------------------------------------------------------*/
int CtrlSetVolume(struct sMR *Device, u8_t Volume, void *Cookie)
{
	IXML_Document *ActionNode = NULL;
	struct sService *Service;
	char params[8], *cmd;
	int rc;

	if (*Device->Service[GRP_REND_SRV_IDX].ControlURL) {
		Service = &Device->Service[GRP_REND_SRV_IDX];
		cmd = "SetGroupVolume";
	} else {
		Service = &Device->Service[REND_SRV_IDX];
		cmd = "SetVolume";
	}

	LOG_INFO("[%p]: uPNP volume %d (cookie %p)", Device, Volume, Cookie);

	ActionNode =  UpnpMakeAction(cmd, Service->Type, 0, NULL);
	UpnpAddToAction(&ActionNode, cmd, Service->Type, "InstanceID", "0");
	if (!*Device->Service[GRP_REND_SRV_IDX].ControlURL)
		UpnpAddToAction(&ActionNode, cmd, Service->Type, "Channel", "Master");
	sprintf(params, "%d", (int) Volume);
	UpnpAddToAction(&ActionNode, cmd, Service->Type, "DesiredVolume", params);

	rc = UpnpSendActionAsync(glControlPointHandle, Service->ControlURL, Service->Type, NULL,
							 ActionNode, ActionHandler, Cookie);

	if (ActionNode) ixmlDocument_free(ActionNode);

	if (rc != UPNP_E_SUCCESS) {
		LOG_ERROR("[%p]: Error in UpnpSendActionAsync -- %d", Device, rc);
	}

	return rc;
}


/*----------------------------------------------------------------------------*/
int CtrlSetMute(struct sMR *Device, bool Mute, void *Cookie)
{
	IXML_Document *ActionNode = NULL;
	struct sService *Service = &Device->Service[REND_SRV_IDX];
	int rc;

	LOG_INFO("[%p]: uPNP mute %d (cookie %p)", Device, Mute, Cookie);
	ActionNode =  UpnpMakeAction("SetMute", Service->Type, 0, NULL);
	UpnpAddToAction(&ActionNode, "SetMute", Service->Type, "InstanceID", "0");
	UpnpAddToAction(&ActionNode, "SetMute", Service->Type, "Channel", "Master");
	UpnpAddToAction(&ActionNode, "SetMute", Service->Type, "DesiredMute", Mute ? "1" : "0");

	rc = UpnpSendActionAsync(glControlPointHandle, Service->ControlURL, Service->Type, NULL,
							 ActionNode, ActionHandler, Cookie);

	if (ActionNode) ixmlDocument_free(ActionNode);

	if (rc != UPNP_E_SUCCESS) {
		LOG_ERROR("[%p]: Error in UpnpSendActionAsync -- %d", Device, rc);
	}

	return rc;
}


/*----------------------------------------------------------------------------*/
int GetGroupVolume(struct sMR *Device)
{
	IXML_Document *ActionNode, *Response = NULL;
	struct sService *Service = &Device->Service[GRP_REND_SRV_IDX];
	char *Item;
	int Volume = -1;

	if (!*Service->ControlURL) return Volume;

	ActionNode = UpnpMakeAction("GetGroupVolume", Service->Type, 0, NULL);
	UpnpAddToAction(&ActionNode, "GetGroupVolume", Service->Type, "InstanceID", "0");
	UpnpSendAction(glControlPointHandle, Service->ControlURL, Service->Type,
								 NULL, ActionNode, &Response);

	if (ActionNode) ixmlDocument_free(ActionNode);

	Item = XMLGetFirstDocumentItem(Response, "CurrentVolume", true);
	if (Response) ixmlDocument_free(Response);

	if (Item) {
		Volume = atoi(Item);
		free(Item);
	}

	return Volume;
}


/*----------------------------------------------------------------------------*/
char *GetProtocolInfo(struct sMR *Device)
{
	IXML_Document *ActionNode, *Response = NULL;
	struct sService *Service = &Device->Service[CNX_MGR_IDX];
	char *ProtocolInfo = NULL;

	LOG_DEBUG("[%p]: uPNP GetProtocolInfo", Device);
	ActionNode =  UpnpMakeAction("GetProtocolInfo", Service->Type, 0, NULL);

	UpnpSendAction(glControlPointHandle, Service->ControlURL, Service->Type, NULL,
							 ActionNode, &Response);

	if (ActionNode) ixmlDocument_free(ActionNode);

	if (Response) {
		ProtocolInfo = XMLGetFirstDocumentItem(Response, "Sink", false);
		ixmlDocument_free(Response);
		LOG_DEBUG("[%p]: ProtocolInfo %s", Device, ProtocolInfo);
	}

	return ProtocolInfo;
}


/*----------------------------------------------------------------------------*/
char *CreateDIDL(char *URI, char *ProtoInfo, struct metadata_s *MetaData, struct sMRConfig *Config)
{
	char *s;

	IXML_Document *doc = ixmlDocument_createDocument();
	IXML_Node	 *node, *root;

	root = XMLAddNode(doc, NULL, "DIDL-Lite", NULL);
	XMLAddAttribute(doc, root, "xmlns:dc", "http://purl.org/dc/elements/1.1/");
	XMLAddAttribute(doc, root, "xmlns:upnp", "urn:schemas-upnp-org:metadata-1-0/upnp/");
	XMLAddAttribute(doc, root, "xmlns", "urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/");
	XMLAddAttribute(doc, root, "xmlns:dlna", "urn:schemas-dlna-org:metadata-1-0/");

	node = XMLAddNode(doc, root, "item", NULL);
	XMLAddAttribute(doc, node, "id", "1");
	XMLAddAttribute(doc, node, "parentID", "0");
	XMLAddAttribute(doc, node, "restricted", "1");

	if (MetaData->duration) {
		div_t duration 	= div(MetaData->duration, 1000);

		if (Config->SendMetaData) {
			XMLAddNode(doc, node, "dc:title", MetaData->title);
			XMLAddNode(doc, node, "dc:creator", MetaData->artist);
			XMLAddNode(doc, node, "upnp:genre", MetaData->genre);
			XMLAddNode(doc, node, "upnp:artist", MetaData->artist);
			XMLAddNode(doc, node, "upnp:album", MetaData->album);
			XMLAddNode(doc, node, "upnp:originalTrackNumber", "%d", MetaData->track);
			if (MetaData->artwork) XMLAddNode(doc, node, "upnp:albumArtURI", "%s", MetaData->artwork);
		}

		XMLAddNode(doc, node, "upnp:class", "object.item.audioItem.musicTrack");
		node = XMLAddNode(doc, node, "res", URI);
		XMLAddAttribute(doc, node, "duration", "%1d:%02d:%02d.%03d",
						duration.quot/3600, (duration.quot % 3600) / 60,
						duration.quot % 60, duration.rem);
	}
	else {
		if (Config->SendMetaData) {
			XMLAddNode(doc, node, "dc:title", MetaData->remote_title);
			XMLAddNode(doc, node, "dc:creator", "");
			XMLAddNode(doc, node, "upnp:album", "");
			XMLAddNode(doc, node, "upnp:channelName", MetaData->remote_title);
			XMLAddNode(doc, node, "upnp:channelNr", "%d", MetaData->track);
			if (MetaData->artwork) XMLAddNode(doc, node, "upnp:albumArtURI", "%s", MetaData->artwork);
		}

		XMLAddNode(doc, node, "upnp:class", "object.item.audioItem.audioBroadcast");
		node = XMLAddNode(doc, node, "res", URI);
	}

	XMLAddAttribute(doc, node, "protocolInfo", ProtoInfo);

	// set optional parameters if we have them all (only happens with pcm)
	if (MetaData->sample_rate && MetaData->sample_size && MetaData->channels) {
		XMLAddAttribute(doc, node, "sampleFrequency", "%u", MetaData->sample_rate);
		XMLAddAttribute(doc, node, "bitsPerSample", "%hhu", MetaData->sample_size);
		XMLAddAttribute(doc, node, "nrAudioChannels", "%hhu", MetaData->channels);
		if (MetaData->duration)
			XMLAddAttribute(doc, node, "size", "%u", (u32_t) ((MetaData->sample_rate *
							MetaData->sample_size / 8 * MetaData->channels *
							(u64_t) MetaData->duration) / 1000));
	}

	s = ixmlNodetoString((IXML_Node*) doc);

	ixmlDocument_free(doc);

	return s;
}


/* typical DIDL header
"<DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\">"
	"<item id=\"{2148F1D5-1BE6-47C3-81AF-615A960E3704}.0.4\" restricted=\"0\" parentID=\"4\">"
		"<dc:title>Make You Feel My Love</dc:title>"
		"<dc:creator>Adele</dc:creator>"
		"<res size=\"2990984\" duration=\"0:03:32.000\" bitrate=\"14101\" protocolInfo=\"http-get:*:audio/mpeg:DLNA.ORG_PN=MP3;DLNA.ORG_OP=01;DLNA.ORG_FLAGS=01500000000000000000000000000000\" sampleFrequency=\"44100\" bitsPerSample=\"16\" nrAudioChannels=\"2\" microsoft:codec=\"{00000055-0000-0010-8000-00AA00389B71}\" xmlns:microsoft=\"urn:schemas-microsoft-com:WMPNSS-1-0/\">http://192.168.2.10:10243/WMPNSSv4/4053149364/0_ezIxNDhGMUQ1LTFCRTYtNDdDMy04MUFGLTYxNUE5NjBFMzcwNH0uMC40.mp3</res>"
		"<res duration=\"0:03:32.000\" bitrate=\"176400\" protocolInfo=\"http-get:*:audio/L16;rate=44100;channels=2:DLNA.ORG_PN=LPCM;DLNA.ORG_OP=10;DLNA.ORG_CI=1;DLNA.ORG_FLAGS=01500000000000000000000000000000\" sampleFrequency=\"44100\" bitsPerSample=\"16\" nrAudioChannels=\"2\" microsoft:codec=\"{00000001-0000-0010-8000-00AA00389B71}\" xmlns:microsoft=\"urn:schemas-microsoft-com:WMPNSS-1-0/\">http://192.168.2.10:10243/WMPNSSv4/4053149364/ezIxNDhGMUQ1LTFCRTYtNDdDMy04MUFGLTYxNUE5NjBFMzcwNH0uMC40?formatID=20</res>"
		"<res duration=\"0:03:32.000\" bitrate=\"88200\" protocolInfo=\"http-get:*:audio/L16;rate=44100;channels=1:DLNA.ORG_PN=LPCM;DLNA.ORG_OP=10;DLNA.ORG_CI=1;DLNA.ORG_FLAGS=01500000000000000000000000000000\" sampleFrequency=\"44100\" bitsPerSample=\"16\" nrAudioChannels=\"1\" microsoft:codec=\"{00000001-0000-0010-8000-00AA00389B71}\" xmlns:microsoft=\"urn:schemas-microsoft-com:WMPNSS-1-0/\">http://192.168.2.10:10243/WMPNSSv4/4053149364/ezIxNDhGMUQ1LTFCRTYtNDdDMy04MUFGLTYxNUE5NjBFMzcwNH0uMC40?formatID=18</res>"
		"<res duration=\"0:03:32.000\" bitrate=\"16000\" protocolInfo=\"http-get:*:audio/mpeg:DLNA.ORG_PN=MP3;DLNA.ORG_OP=10;DLNA.ORG_CI=1;DLNA.ORG_FLAGS=01500000000000000000000000000000\" sampleFrequency=\"44100\" nrAudioChannels=\"1\" microsoft:codec=\"{00000055-0000-0010-8000-00AA00389B71}\" xmlns:microsoft=\"urn:schemas-microsoft-com:WMPNSS-1-0/\">http://192.168.2.10:10243/WMPNSSv4/4053149364/ezIxNDhGMUQ1LTFCRTYtNDdDMy04MUFGLTYxNUE5NjBFMzcwNH0uMC40.mp3?formatID=24</res>"
		"<res duration=\"0:03:32.000\" bitrate=\"16000\" protocolInfo=\"http-get:*:audio/x-ms-wma:DLNA.ORG_PN=WMABASE;DLNA.ORG_OP=10;DLNA.ORG_CI=1;DLNA.ORG_FLAGS=01500000000000000000000000000000\" sampleFrequency=\"44100\" nrAudioChannels=\"2\" microsoft:codec=\"{00000161-0000-0010-8000-00AA00389B71}\" xmlns:microsoft=\"urn:schemas-microsoft-com:WMPNSS-1-0/\">http://192.168.2.10:10243/WMPNSSv4/4053149364/ezIxNDhGMUQ1LTFCRTYtNDdDMy04MUFGLTYxNUE5NjBFMzcwNH0uMC40.wma?formatID=42</res>"
		"<res duration=\"0:03:32.000\" bitrate=\"6000\" protocolInfo=\"http-get:*:audio/x-ms-wma:DLNA.ORG_PN=WMABASE;DLNA.ORG_OP=10;DLNA.ORG_CI=1;DLNA.ORG_FLAGS=01500000000000000000000000000000\" sampleFrequency=\"44100\" nrAudioChannels=\"1\" microsoft:codec=\"{00000161-0000-0010-8000-00AA00389B71}\" xmlns:microsoft=\"urn:schemas-microsoft-com:WMPNSS-1-0/\">http://192.168.2.10:10243/WMPNSSv4/4053149364/ezIxNDhGMUQ1LTFCRTYtNDdDMy04MUFGLTYxNUE5NjBFMzcwNH0uMC40.wma?formatID=50</res>"
		"<res duration=\"0:03:32.000\" bitrate=\"8000\" protocolInfo=\"http-get:*:audio/x-ms-wma:DLNA.ORG_PN=WMABASE;DLNA.ORG_OP=10;DLNA.ORG_CI=1;DLNA.ORG_FLAGS=01500000000000000000000000000000\" sampleFrequency=\"44100\" nrAudioChannels=\"2\" microsoft:codec=\"{00000161-0000-0010-8000-00AA00389B71}\" xmlns:microsoft=\"urn:schemas-microsoft-com:WMPNSS-1-0/\">http://192.168.2.10:10243/WMPNSSv4/4053149364/ezIxNDhGMUQ1LTFCRTYtNDdDMy04MUFGLTYxNUE5NjBFMzcwNH0uMC40.wma?formatID=54</res>"
		"<upnp:class>object.item.audioItem.musicTrack</upnp:class>"
		"<upnp:genre>[Unknown Genre]</upnp:genre>"
		"<upnp:artist role=\"AlbumArtist\">Adele</upnp:artist>"
		"<upnp:artist role=\"Performer\">Adele</upnp:artist>"
		"<upnp:author role=\"Composer\">[Unknown Composer]</upnp:author>"
		"<upnp:album>19</upnp:album>"
		"<upnp:originalTrackNumber>9</upnp:originalTrackNumber>"
		"<dc:date>2008-01-02</dc:date>"
		"<upnp:actor>Adele</upnp:actor>"
		"<desc id=\"artist\" nameSpace=\"urn:schemas-microsoft-com:WMPNSS-1-0/\" xmlns:microsoft=\"urn:schemas-microsoft-com:WMPNSS-1-0/\">"
			"<microsoft:artistAlbumArtist>Adele</microsoft:artistAlbumArtist>"
			"<microsoft:artistPerformer>Adele</microsoft:artistPerformer>"
		"</desc>"
		"<desc id=\"author\" nameSpace=\"urn:schemas-microsoft-com:WMPNSS-1-0/\" xmlns:microsoft=\"urn:schemas-microsoft-com:WMPNSS-1-0/\">"
			"<microsoft:authorComposer>[Unknown Composer]</microsoft:authorComposer>"
		"</desc>"
		"<desc id=\"Year\" nameSpace=\"urn:schemas-microsoft-com:WMPNSS-1-0/\" xmlns:microsoft=\"urn:schemas-microsoft-com:WMPNSS-1-0/\">"
			"<microsoft:year>2008</microsoft:year>"
		"</desc>"
		"<desc id=\"UserRating\" nameSpace=\"urn:schemas-microsoft-com:WMPNSS-1-0/\" xmlns:microsoft=\"urn:schemas-microsoft-com:WMPNSS-1-0/\">"
			"<microsoft:userEffectiveRatingInStars>3</microsoft:userEffectiveRatingInStars>"
			"<microsoft:userEffectiveRating>50</microsoft:userEffectiveRating>"
		"</desc>"
   "</item>"
"</DIDL-Lite>"
*/


