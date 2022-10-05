/*****************************************************************************
 * cross-platform functions
 *  (c) Philippe, philippe_44@outlook.com: AirPlay V2 + simple library
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/
#include <stdio.h>
#include <stdarg.h>

#include "platform.h"

#if WIN
/*----------------------------------------------------------------------------*/
int asprintf(char **strp, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	int len = vasprintf(strp, fmt, args);
	va_end(args);

	return len;
}

/*----------------------------------------------------------------------------*/
int vasprintf(char** strp, const char* fmt, va_list args)
{
	int len = vsnprintf(NULL, 0, fmt, args);
	*strp = malloc(len + 1);

	if (*strp) len = vsprintf(*strp, fmt, args);
	else len = 0;

	return len;
}
#endif

#if WIN
int poll(struct pollfd *fds, unsigned long numfds, int timeout) {
	if (numfds > 1) {
		// WSAPoll is broken till Windows 10, see NOTe on https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsapoll
		return WSAPoll(fds, numfds, timeout);
	} else {
		fd_set r, w;
		struct timeval tv;

		FD_ZERO(&r);
		FD_ZERO(&w);

		if (fds[0].events & POLLIN) FD_SET(fds[0].fd, &r);
		if (fds[0].events & POLLOUT) FD_SET(fds[0].fd, &w);

		tv.tv_sec = timeout / 1000;
		tv.tv_usec = 1000 * (timeout % 1000);

		int ret = select(fds[0].fd + 1, &r, &w, NULL, &tv);

		if (ret < 0) return ret;

		fds[0].revents = 0;
		if (FD_ISSET(fds[0].fd, &r)) fds[0].revents |= POLLIN;
		if (FD_ISSET(fds[0].fd, &w)) fds[0].revents |= POLLOUT;

		return ret;
	}
}
#endif