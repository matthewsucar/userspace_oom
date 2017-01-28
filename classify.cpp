/*
 * Copyright (c) 2017, University Corporation for Atmospheric Research
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, 
 * this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <syslog.h>
#include <errno.h>


int main(int argc, char** argv)
{
	char path[] = "/run/tc_classifyd";
	struct msghdr msg;
	struct iovec iov;
	int32_t data = (int32_t) strtol(argv[1], (char**) NULL, 10);
	union 
	{
		struct cmsghdr mh;
		char control[CMSG_SPACE(sizeof(struct ucred))];
	} control_data;
	struct ucred* ucredrx;
	int s;
	struct sockaddr_un address;
	if((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
	{
		fprintf(stderr, "Error opening socket: %s", strerror(errno));
		abort();
	}
	memset(&address, 0, sizeof(struct sockaddr_un));
	address.sun_family = AF_UNIX;
	strncpy(address.sun_path, path, sizeof(struct sockaddr_un)-sizeof(sa_family_t));	
	if(connect(s, (struct sockaddr*) &address, sizeof(struct sockaddr_un)) == -1)
	{
		fprintf(stderr, "Error connecting socket: %s", strerror(errno));
		close(s);
		abort();
	}
	memset(&msg, 0, sizeof(struct msghdr));
	memset(&iov, 0, sizeof(struct iovec));
	iov.iov_base = &data;
	iov.iov_len = sizeof(int32_t);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;	
	sendmsg(s, &msg, 0);
	close(s);
	return(0);
}