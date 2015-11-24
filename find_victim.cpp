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

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <map>
#include <set>
#include <iostream>
#include <fstream>
#include <cstring>
#include <cstring>
#include <stdint.h>
#include <sys/types.h>
#include <sys/dir.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sstream>
#include <signal.h>
#include <syslog.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>

#include <libcgroup.h>

#include <cgroup_context.h>

#include <log.h>

typedef uint64_t memory_t;

void enumerate_tasks(char* cgpath, uid_t victim, std::vector<pid_t>& cached_task_list);


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

extern "C"
{
char is_oom(struct cgroup_context* cgc)
{
	char* path;
	asprintf(&path, "/%s/%s/memory.oom_control", cgc->cgroup_path, cgc->cgroup_name);
	std::ifstream oc(path, std::ifstream::in);
	char isoom = -1;
	while(oc.good())
	{
		std::string token;
		oc >> token;
		if(token == "under_oom")
		{
			oc >> isoom;
		}
	}
	oc.close();
	free(path);
	if(isoom == -1) abort();
	if(isoom) return(1);
	return(0);	
}
}

void sigkill_victim(pid_t pid)
{
	uid_t victim_uid;
	victim_uid = get_uid(pid);
	slog(LOG_ALERT,"killing UID:%u PID %d\n", victim_uid, pid);
	kill(pid, SIGKILL);

}
void kill_victim(struct cgroup_context* cgc, uid_t victim_uid)
{

	std::vector<pid_t> cached_task_list;
	//get PID list
	char* cgpath;
	asprintf(&cgpath, "/%s/%s/", cgc->cgroup_path, cgc->cgroup_name);	
	enumerate_tasks(cgpath, victim_uid, cached_task_list);
	free(cgpath);

	struct rlimit core_limit;
	core_limit.rlim_cur = 0;
	core_limit.rlim_max = 0;

	//Freeze all of user's processes
	for(std::vector<pid_t>::iterator i = cached_task_list.begin();
		i!= cached_task_list.end();
		i++)
	{
		cgroup_attach_task_pid(cgc->purgatory, *i);
	}

	char* root_freezer_path;
	char* root_memory_path;
	asprintf(&root_freezer_path, "/%s/tasks", cgc->freezer_path);
	asprintf(&root_memory_path, "/%s/tasks", cgc->cgroup_path);

	for(std::vector<pid_t>::iterator i = cached_task_list.begin();
		i!= cached_task_list.end();
		i++)
	{
		FILE* root_freezer = fopen(root_freezer_path,"w");
		FILE* root_memory = fopen(root_memory_path, "w");
		sigkill_victim(*i);
		fprintf(root_memory, "%d", *i);
		fclose(root_memory);
		fprintf(root_freezer, "%d", *i);
		fclose(root_freezer);
	}	

	free(root_memory_path);
	free(root_freezer_path);
}

void enumerate_tasks(char* cgpath, uid_t victim_uid, std::vector<pid_t>& cached_task_list)
{	
	pid_t pid;
	char* task_path;
	asprintf(&task_path, "/%s/tasks", cgpath);
	std::ifstream task_list(task_path,std::ifstream::in);
	while(task_list.good())
	{
		task_list >> pid;
		if(!(task_list.good())) //last read seems to be garbage
		{
			break;
		}
		uid_t uid = get_uid(pid);
		if(uid == victim_uid)
		{
			cached_task_list.push_back(pid);
		}
	}
	task_list.close();
	free(task_path);

	DIR* cgd = opendir(cgpath);
	if(cgd == NULL) slog(LOG_ALERT, "Error opening cgroup directory: %s\n", cgpath);
	struct dirent* de;
	struct stat stat_buf;
	int r;
	while((de=readdir(cgd))!=NULL)
	{
		char* tmp_path;
		asprintf(&tmp_path, "%s/%s/", cgpath, de->d_name);
		r = stat(tmp_path, &stat_buf);
		if(r!=0)
		{
			if(errno != ENOTDIR)
			{
		 	  slog(LOG_ALERT,
			   "enumerate_tasks(): stat() error code: %s on \"%s\"",
		  	   strerror(errno), tmp_path);
			}
		}
		else
		{
			if(S_ISDIR(stat_buf.st_mode) && de->d_name[0]!='.')
			{
				enumerate_tasks(tmp_path, victim_uid, cached_task_list);
			}
		}
		free(tmp_path);
	}
	closedir(cgd);
}

void enumerate_users(char* cgpath, std::map<uid_t, memory_t>& user_list)
{
	char* task_path;

	asprintf(&task_path, "/%s/tasks", cgpath);
	std::ifstream task_list(task_path,std::ifstream::in);
	pid_t pid;
	while(task_list.good())
	{
		task_list >> pid;
		if(!(task_list.good())) //last read seems to be garbage
		{
			break;
		}
		uid_t uid = get_uid(pid);
		memory_t rss = get_rss(pid);
		if(user_list.find(uid)==user_list.end())
		{
			user_list[uid] = rss;
		}
		else
		{
			user_list[uid] = user_list[uid] + rss;
		}
	}
	task_list.close();
	free(task_path);

	DIR* cgd = opendir(cgpath);
	if(cgd == NULL) slog(LOG_ALERT, "Error opening cgroup directory: %s\n", cgpath);
	struct dirent* de;
	struct stat stat_buf;
	int r;
	while((de=readdir(cgd))!=NULL)
	{
		char* tmp_path;
		asprintf(&tmp_path, "%s/%s/", cgpath, de->d_name);
		r = stat(tmp_path, &stat_buf);
		if(r!=0) {
			if(errno != ENOTDIR)
			{
			  slog(LOG_ALERT, 
			  "enumerate_users(): stat() error code: %s on \"%s\"",
			  strerror(errno), tmp_path);
			}
		}
		else
		{
			if(S_ISDIR(stat_buf.st_mode) && de->d_name[0]!='.')
			{
				enumerate_users(tmp_path, user_list);
			}
		}
		free(tmp_path);
	}
	closedir(cgd);
}

extern "C"
{
	int find_victim(struct cgroup_context* cgc)
{
	std::map<uid_t,memory_t> user_list;
	char* cgpath;
	asprintf(&cgpath, "/%s/%s/", cgc->cgroup_path, cgc->cgroup_name);
	enumerate_users(cgpath, user_list);	
	free(cgpath);
	
	if(user_list.size() < 1)
	{
		return(-1);
	}

	memory_t max = (user_list.begin())->second;
	uid_t max_uid = (user_list.begin())->first;
	for(std::map<uid_t,memory_t>::iterator i = user_list.begin();
		i!=user_list.end();
		i++)
	{
		if(i->second > max) 
			{
				max_uid = i->first;
				max = i->second;
			}
	}
	kill_victim(cgc, max_uid);
	return(0);
}
		
}
