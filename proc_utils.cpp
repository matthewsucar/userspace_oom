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


#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <syslog.h>

#include <log.h>
#include <proc_utils.h>

uid_t get_uid(pid_t pid)
{
	char* status_path;
	asprintf(&status_path, "/proc/%d/status", pid);
	std::ifstream pid_status(status_path);
	std::string line;
	uid_t uid;
	char found = 0;
	while(pid_status.good())
	{
		std::getline(pid_status, line);
		size_t pos = line.find("Uid:",0);
		std::string token;
		if(pos != std::string::npos)
		{
			std::stringstream ls(line,std::ios_base::in);
			ls >> token; //header
			ls >> uid;
			found = 1;
			break;
		}
	}	
	if(!found) {
		slog(LOG_ERR,"Error mapping UID for PID %d\n", pid);
	}
	pid_status.close();
	free(status_path);
	return(uid);
}

memory_t get_rss(pid_t pid)
{
	char* status_path;
	asprintf(&status_path, "/proc/%d/status", pid);
	std::ifstream pid_status(status_path);
	std::string line;
	uint64_t rss;
	char found = 0;

	while(pid_status.good())
	{
		std::getline(pid_status, line);
		size_t pos = line.find("VmRSS:",0);
		std::string token;
		if(pos != std::string::npos)
		{
			std::stringstream ls(line,std::ios_base::in);
			ls >> token; //header
			ls >> rss;
			//FIXME assumes always kB, which is currently correct
			//but could change
			found = 1;
			break;
		}
	}
	//if(!found) //ignore this case for now - due to races, it's fairly common 
	//{
	//	//note: this can happen for kthreads
	//	slog(LOG_WARNING,"Error finding RSS for PID %d\n", pid);
	//} 
	pid_status.close();
	free(status_path);
	return(rss);
}