/*
 *  UPnP Control util
 *
 *	(c) Philippe, philippe_44@outlook.comom
 *
 * see LICENSE
 *
 */

#pragma once

#include "ixml.h"

struct sMRConfig;
struct sMR;

typedef struct sAction {
	struct sMR *Device;
	void   *ActionNode;
	union {
		uint8_t Volume;
	} Param;
} tAction;

bool 	AVTSetURI(struct sMR *Device, char *URI, struct metadata_s *MetaData, char *ProtoInfo);
bool 	AVTSetNextURI(struct sMR *Device, char *URI, struct metadata_s *MetaData, char *ProtoInfo);
int 	AVTCallAction(struct sMR *Device, char *Var, void *Cookie);
bool 	AVTPlay(struct sMR *Device);
bool 	AVTSetPlayMode(struct sMR *Device);
bool 	AVTSeek(struct sMR *Device, unsigned Interval);
bool 	AVTBasic(struct sMR *Device, char *Action);
bool 	AVTStop(struct sMR *Device);
void	AVTActionFlush(cross_queue_t *Queue);
int 	CtrlSetVolume(struct sMR *Device, uint8_t Volume, void *Cookie);
int 	CtrlSetMute(struct sMR *Device, bool Mute, void *Cookie);
int 	CtrlGetVolume(struct sMR *Device);
int 	CtrlGetGroupVolume(struct sMR *Device);
char*	GetProtocolInfo(struct sMR *Device);


