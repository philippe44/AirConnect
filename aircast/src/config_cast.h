/*
 *  AirCast: config management
 *
 * (c) Philippe, philippe_44@outlook.com
 *
 * See LICENSE
 *
 */

#pragma once

void	  	SaveConfig(char *name, void *ref, bool full);
void*		LoadConfig(char *name, struct sMRConfig *Conf);
void*		FindMRConfig(void *ref, char *UDN);
void*		LoadMRConfig(void *ref, char *UDN, struct sMRConfig *Conf);
