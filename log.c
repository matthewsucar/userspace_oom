/*
 * Copyright (c) 2015, University Corporation for Atmospheric Research
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

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>
#include <syslog.h>
#ifndef _BSD_SOURCE
#define _BSD_SOURCE //for struct dirent.d_type definitions
#endif
#include <dirent.h>

#include <log.h>

#include <config.h>
#ifndef DAEMON_NAME
#define DAEMON_NAME "userspace-oomkiller"
#endif

void slog(int priority, const char* format, ...)
{
	va_list args;
	static char log_open = 0;
	va_start(args, format);
	if(!log_open)
	{
		log_open = 1;
		openlog(DAEMON_NAME, LOG_NDELAY|LOG_PERROR|LOG_PID, LOG_DAEMON);
	}
	vsyslog(priority, format, args);
	va_end(args);
}

void log_pid(char* name)
{
	char* path;
	FILE* fd;
	asprintf(&path, "/proc/%s/stat", name);
	fd = fopen(path, "r");
	if(fd==0) return; //process probably died while we were inspecting it
	char buf[4097];
	size_t ret;
	while(1)
	{
		ret=fread(buf, 1, 4096, fd);
		buf[ret] = '\0';
		slog(LOG_INFO, "%s", buf);
		if(ret < 4096) break;
	}
	fclose(fd);
	free(path);	
}

void log_process_table()
{
	DIR* proc = opendir("/proc");
	struct dirent* e;
	size_t len;
	char is_pid;
	size_t i;
	while((e=readdir(proc))!=NULL)
	{
		if(e->d_type != DT_DIR) continue;
		len = strlen(e->d_name);
		is_pid = 1; 
		for(i=0;i<len;i++)
		{
			if(!isdigit((e->d_name)[i]))
				is_pid = 0;
		}
		if(is_pid)
			log_pid(e->d_name);
	}
	closedir(proc);
}