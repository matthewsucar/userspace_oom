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

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/eventfd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/epoll.h>
#include <stdint.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <getopt.h>
#include <syslog.h>
#include <execinfo.h>

#include <libcgroup.h>

#include <cgroup_context.h>

#include <log.h>
#include <classifier.h>

void exit_handler(int);
void crash_handler(int);
void child_reap(int sig);

static volatile sig_atomic_t exit_flag;
static volatile sig_atomic_t restart_flag;
static jmp_buf exit_stack;
static pid_t classifier_pid;

void start_oomkiller(struct cgroup_context* cgc);
void stop_oomkiller(struct cgroup_context* cgc);
int find_victim(struct cgroup_context* cgc);
void kill_victim(struct cgroup_context* cgc, uid_t victim_uid);
char is_oom(struct cgroup_context* cgc);

int main(int argc, char** argv)
{
	static struct option longopts[] = {
		{ "daemonize", no_argument, NULL, 'd' },
		{ "cgroup", required_argument, NULL, 'g' },
		{ "pidfile", required_argument, NULL, 'p'},
		{ "restart_on_crash", no_argument, NULL, 'r'},
		{ "verbose", no_argument, NULL, 'v'}, 
		{ NULL, 0, NULL, 0}
	};

	int cl;
	char* event_command;
	char* event_control_path;	
	char* oom_control_path;
	char* pidfile = NULL;
	uint64_t efdcounter;
	struct sigaction sa;
	int flag;
	assert(argc > 1);
	exit_flag = 0;
	restart_flag = 0;
	char daemon_flag = 0;
	char restart_on_crash_flg = 0;
	struct cgroup_context cgc;
	char verbose_log = 0;
	cgc.cgroup_name = NULL;

	int ch;
	while((ch = getopt_long(argc, argv, "rvdg:p:", longopts, NULL)) != -1)
	{
		switch(ch)
		{
			case 'd':
				daemon_flag = 1;
				break;
			case 'g':
				asprintf(&cgc.cgroup_name, "%s", optarg);
				break;
			case 'p':
				asprintf(&pidfile, "%s", optarg);
				break;
			case 'r':
				restart_on_crash_flg = 1;
				break;
			case 'v':
				verbose_log = 1;
				break;
			default:
				break;
		}
	}
	//setup classifier sub-daemon first
	//must happen here due to some non-reentrant behavior. 
	//(FIXME someday)
	//ensure we don't make zombies
	classifier_pid = 0;
	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = &child_reap;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
	sigaction(SIGCHLD, &sa, 0);
	start_classifier("/run/tc_classifyd");

//TODO		
	if(cgc.cgroup_name == NULL)
	{
		slog(LOG_ALERT, "FATAL: No cgroup specified, exiting");
		abort();
	}
	if(daemon_flag)
	{
		if(daemon(0,0) == -1)
		{
			slog(LOG_ALERT, "FATAL: failed to daemonize!");
			abort();
		}
		if(pidfile)
		{
			pid_t pid = getpid();
			FILE* f = fopen(pidfile, "w");
			if(!f)
			{
				slog(LOG_ALERT, "FATAL: Failed to write to pidfile");
				abort();
			}
			fprintf(f, "%d", pid);
			fclose(f);
			free(pidfile);
			pidfile = NULL;
		}
	}
	cgc.efd = eventfd(0,0);
	assert(cgc.efd != -1);

	cgroup_init();
	cgroup_get_subsys_mount_point("memory", &((cgc.cgroup_path)));
	cgroup_get_subsys_mount_point("freezer", &((cgc.freezer_path)));

	cgc.purgatory = cgroup_new_cgroup("purgatory");
	cgroup_add_controller(cgc.purgatory, "freezer");
	cgroup_create_cgroup(cgc.purgatory,1);

	char* purgatory_freeze_path;
	FILE* freezer_fd;
	asprintf(&purgatory_freeze_path, "/%s/purgatory/freezer.state", cgc.freezer_path);
	freezer_fd = fopen(purgatory_freeze_path,"w");
	fprintf(freezer_fd, "FROZEN");
	fclose(freezer_fd);
	free(purgatory_freeze_path);

	asprintf(&event_control_path, "/%s/%s/cgroup.event_control",
			cgc.cgroup_path, cgc.cgroup_name);
	cgc.ecfd = open(event_control_path, O_WRONLY);
	if(cgc.ecfd < 0)
	{
		slog(LOG_ALERT, 
			"FATAL: failed to open cgroup event control: %s\n",
			 event_control_path);
		perror("cgroup.event_control");
	}

	asprintf(&oom_control_path, "/%s/%s/memory.oom_control",
			cgc.cgroup_path, cgc.cgroup_name);
	cgc.oomfd = open(oom_control_path, O_RDWR);
	if(!(cgc.oomfd >=0))
	{
		slog(LOG_ALERT,
			"FATAL: Failed to open oom_control");
		abort();
	}

	cl = asprintf(&event_command, "%d %d", cgc.efd, cgc.oomfd);
	write(cgc.ecfd, event_command, cl);
	free(event_control_path);
	free(event_command);
	free(oom_control_path);
	setjmp(exit_stack);

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NOMASK;
	sa.sa_handler = exit_handler;
	sigaction(SIGINT, &sa, NULL);
	if(restart_on_crash_flg) //optionally make an effort to handle crashes
	{
		sa.sa_handler = crash_handler;
		sigaction(SIGSEGV, &sa, NULL);
		sigaction(SIGBUS, &sa, NULL);
		sigaction(SIGPIPE, &sa, NULL);
		sigaction(SIGABRT, &sa, NULL);
	}

	if(restart_flag < 2) //try to handle recursive faults
	{
		stop_oomkiller(&cgc);
		while(!exit_flag)
		{
			read(cgc.efd, &efdcounter, sizeof(uint64_t));
			flag = 0; //stop killing if the task list is empty (shouldn't happen)
			if(verbose_log)
				log_process_table(); //dump process list to syslog
			while(is_oom(&cgc) && flag >= 0)
			{
				flag = find_victim(&cgc);
				usleep(100); //give processes a chance to die
			}
		}
		cgroup_delete_cgroup(cgc.purgatory, 0);
		start_oomkiller(&cgc);
		close(cgc.oomfd);
		close(cgc.ecfd);
	}
	if(restart_flag)
	{
		char* args[argc+1];
		int i;
		for(i=0;i<argc;i++)
		{
			asprintf(&(args[i]), "%s", argv[i]);
		}
		args[argc] = NULL;

		execv(argv[0], args);
	}
}

//NOTE: will only work if memory.use_hierarchy=0 in root cgroup
//(can be 1 in nested groups)
void stop_oomkiller(struct cgroup_context* cgc)
{
	char* command;
	int len;
	len = asprintf(&command, "1\n");
	write(cgc->oomfd, command, len);
	free(command);
}
void start_oomkiller(struct cgroup_context* cgc)
{
	char* command;
	int len;
	len = asprintf(&command, "0\n");
	write(cgc->oomfd, command, len);
	free(command);
}

void exit_handler(int signal)
{
	exit_flag = 1;
	if(classifier_pid != 0)
	{
		kill(classifier_pid, SIGKILL);
	}
	longjmp(exit_stack, 0);
}

void crash_handler(int singal) //this will leak file descriptors
{						//but should work a few times
	exit_flag = 1;
	restart_flag++;
	slog(LOG_ALERT, "Something Bad Happened, trying to restart\n");
	void* frames[100];
	size_t size;
	char** strings;
	int i;
	size = backtrace(frames, 100);
	strings = backtrace_symbols(frames, size);
	for(i=0;i<size;i++)
	{
		slog(LOG_ALERT, "%s", strings[i]);
	}
	longjmp(exit_stack, 0);
}

void child_reap(int sig)
{
        int oerrno = errno;
        while(waitpid((pid_t)(-1), 0, WNOHANG) > 0) {}
        errno = oerrno;
}
