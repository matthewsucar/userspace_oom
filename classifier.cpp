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
#include <sys/stat.h>
#include <sys/un.h>
#include <syslog.h>
#include <errno.h>

#ifdef USE_SYSTEMD
#include <dbus/dbus.h>
#include <uuid/uuid.h>
#endif

#include <log.h>
#include <proc_utils.h>
#include <classifier.h>

int validate_pid(pid_t pid, uid_t request_uid, pid_t requesting_pid)
{
	uid_t process_uid = get_uid(pid); //get target process owner from /proc
	if(request_uid == 0) //let root do whatever
		return(1);
	if(requesting_pid == pid && request_uid >= 1000)
	{
		//setuid exec jailing self
		//otherwise kernel wouldn't allow this case
		return(1);  
	}
	if(process_uid < 1000)
		return(0); //leave system processes alone
	if(process_uid != request_uid && request_uid != 0)
		return(0); //allow root to change other people's stuff (only)
	if(process_uid == request_uid)
		return(1); //people can change their own
	return(0);
}

#ifdef USE_SYSTEMD

static void append_variant(DBusMessageIter *itr, int type, void* value)
{
	DBusMessageIter valueitr;
	char signature[2];
	signature[0] = type;
	signature[1] = 0;

	dbus_message_iter_open_container(itr, DBUS_TYPE_VARIANT,
		signature, &valueitr);
	dbus_message_iter_append_basic(&valueitr, type, value);
	dbus_message_iter_close_container(itr, &valueitr);
}

static int classify(pid_t pid)
{
#ifdef DBUS_RESTART
	dbus_threads_init_default();
#endif
	DBusConnection* con = NULL;
	DBusError dberr;
	char* mode = "fail"; //"replace"? //"fail"?
	char* pids_s = "PIDs";
	char* slice = "hpcjob.slice";
	char* slicepr = "Slice";
	char* taskstr = "TasksMax";
	uint64_t num_tasks_max = 16384;
	dbus_error_init(&dberr);
	uint32_t mypid = (uint32_t) pid;
	con = dbus_bus_get_private(DBUS_BUS_SYSTEM, &dberr);
	dbus_connection_set_exit_on_disconnect(con, 0);
	uuid_t uuid;
	char uuid_s[37];
	char* scope = NULL;
	char success = 0;
	DBusMessage* scope_create_query = NULL;
	DBusMessageIter propary_i, propstruct_i, msgi, auxi, pidlisti, pidaryi;
	DBusMessage* reply = NULL;

	if(dbus_error_is_set(&dberr)) {
		slog(LOG_ERR, "%s", dberr.message);
		dbus_connection_close(con);
		dbus_connection_unref(con);
#ifdef DBUS_RESTART
		dbus_shutdown();
#endif
		return(1);
	}
	while(!success)
	{
		uuid_generate(uuid);
		uuid_unparse(uuid, uuid_s);
		uuid_s[36]='\0';
		asprintf(&scope, "hpcjob-%s.scope", uuid_s);
		scope_create_query = dbus_message_new_method_call(
			"org.freedesktop.systemd1",
			"/org/freedesktop/systemd1",
			"org.freedesktop.systemd1.Manager",
			"StartTransientUnit");
		dbus_message_iter_init_append(scope_create_query, &msgi);
		dbus_message_iter_append_basic(&msgi, DBUS_TYPE_STRING, &scope);
		dbus_message_iter_append_basic(&msgi, DBUS_TYPE_STRING, &mode);

		//properties
		dbus_message_iter_open_container(&msgi, DBUS_TYPE_ARRAY, "(sv)", &propary_i);

		//tasks max
		dbus_message_iter_open_container(&propary_i, DBUS_TYPE_STRUCT, NULL, &propstruct_i);
		dbus_message_iter_append_basic(&propstruct_i, DBUS_TYPE_STRING, &taskstr);
		append_variant(&propstruct_i, DBUS_TYPE_UINT64, &num_tasks_max);
		dbus_message_iter_close_container(&propary_i, &propstruct_i);

			//slice
		dbus_message_iter_open_container(&propary_i, DBUS_TYPE_STRUCT, NULL, &propstruct_i);
		dbus_message_iter_append_basic(&propstruct_i, DBUS_TYPE_STRING, &slicepr);
		append_variant(&propstruct_i, DBUS_TYPE_STRING, &slice);
		dbus_message_iter_close_container(&propary_i, &propstruct_i);
		
			//pid list struct
		dbus_message_iter_open_container(&propary_i, DBUS_TYPE_STRUCT, NULL, &propstruct_i);
		dbus_message_iter_append_basic(&propstruct_i, DBUS_TYPE_STRING, &pids_s);
			//actual list of pids
		dbus_message_iter_open_container(&propstruct_i, DBUS_TYPE_VARIANT, "au", &pidlisti);
		dbus_message_iter_open_container(&pidlisti, DBUS_TYPE_ARRAY, "u", &pidaryi);
		dbus_message_iter_append_basic(&pidaryi, DBUS_TYPE_UINT32, &mypid);
		dbus_message_iter_close_container(&pidlisti, &pidaryi);
		dbus_message_iter_close_container(&propstruct_i, &pidlisti);

		dbus_message_iter_close_container(&propary_i, &propstruct_i);	

		dbus_message_iter_close_container(&msgi, &propary_i);

		//aux
		dbus_message_iter_open_container(&msgi, DBUS_TYPE_ARRAY, "(sa(sv))", &auxi);
		dbus_message_iter_close_container(&msgi, &auxi);

		reply = dbus_connection_send_with_reply_and_block(con,
			scope_create_query, 1000, &dberr);
		if(dbus_error_is_set(&dberr)) {
			slog(LOG_ERR, "errname: %s", dberr.name);
			slog(LOG_ERR, "errmsg: %s", dberr.message);
			if(dbus_error_has_name(&dberr, "org.freedesktop.systemd1.UnitExists"))
			{
				slog(LOG_ERR, "NOT_AN_ERROR!: scope conflict: %s", scope);
				dbus_error_free(&dberr);
				if(scope)
				{
					free(scope);
					scope = NULL;
				}
				if(reply)
				{
					dbus_message_unref(reply);
					reply = NULL;
				}
				if(scope_create_query)
				{
					dbus_message_unref(scope_create_query);
					scope_create_query = NULL;
				}
			}
			else
			{
				//dbus_connection_unref(con);
				if(reply)
				{
					dbus_message_unref(reply);
					reply = NULL;
				}
				if(scope_create_query)
				{
					dbus_message_unref(scope_create_query);
					scope_create_query = NULL;
				}
				dbus_connection_close(con);
				dbus_connection_unref(con);
#ifdef DBUS_RESTART			
				dbus_shutdown();
#endif				
				if(scope)
				{
					free(scope);
					scope = NULL;
				}
				slog(LOG_ERR, "unknw error");
				return(1);
			}
		}
		else
		{
			success = 1;
		}
	}
	if(reply)
		dbus_message_unref(reply);
	if(scope_create_query)
		dbus_message_unref(scope_create_query);
	if(scope)
		free(scope);
	dbus_connection_close(con);
	dbus_connection_unref(con);
#ifdef DBUS_RESTART	
	dbus_shutdown();
#endif
}
#else
static int classify(pid_t pid)
{
	slog(LOG_ALERT, "NOT IMPLEMENTED YET");
}
#endif

extern "C" int start_classifier(char* path)
//int main(int argc, char** argv)
{
	//char path[] = "/run/tc_classifyd";
	pid_t pid = fork();
	int s = (-1);
	int cs = (-1);
	int tru = 1;
	size_t bytesr;
	struct msghdr msg;
	struct iovec iov;
	int32_t data; //pid of process to work on

	union 
	{
		struct cmsghdr mh;
		char control[CMSG_SPACE(sizeof(struct ucred))];
	} control_data;

	if(pid != 0)
	{
		return(pid);
	}
	fcloseall();
	struct sockaddr_un local, remote;
	socklen_t sz = sizeof(remote);
	if((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
	{
		slog(LOG_ALERT, "Error opening socket: %s", strerror(errno));
		abort();
	}
	memset(&local, 0, sizeof(struct sockaddr_un));
	memset(&remote, 0, sizeof(struct sockaddr_un));

	local.sun_family = AF_UNIX;
	strncpy(local.sun_path, path, sizeof(struct sockaddr_un)-sizeof(sa_family_t));
	unlink(path);
	size_t len=strlen(local.sun_path)+sizeof(local.sun_family);
	if(bind(s, (struct sockaddr*) &local, len) == -1)
	{
		slog(LOG_ALERT, "Error binding socket: %s", strerror(errno));
		abort();
	}
	if(listen(s, 1024) == -1)
	{
		slog(LOG_ALERT, "Error listening: %s", strerror(errno));
		abort();
	}
	if(chmod(path, 0777) == -1)
	{
		slog(LOG_ALERT, "Error setting permissions: %s", strerror(errno));
	}
	while(1)
	{
		cs = accept(s, (struct sockaddr*) &remote, &sz);
		if(cs == -1)
		{
			slog(LOG_ALERT, "accept error: %s", strerror(errno));
			continue;
		}
		if(setsockopt(cs, SOL_SOCKET, SO_PASSCRED, &tru, sizeof(tru)) == -1)
		{
			slog(LOG_ALERT, "error setting sockopt: %s", strerror(errno));
			close(cs);
			memset(&remote, 0, sizeof(struct sockaddr_un));
			continue;
		}
		memset(&msg, 0, sizeof(struct msghdr));
		memset(&iov, 0, sizeof(struct iovec));
		memset(&control_data, 0, sizeof(control_data));
		control_data.mh.cmsg_len = CMSG_LEN(sizeof(struct ucred));
		control_data.mh.cmsg_level = SOL_SOCKET;
		control_data.mh.cmsg_type = SCM_CREDENTIALS;
		msg.msg_control = control_data.control;
		msg.msg_controllen = sizeof(control_data.control);
		iov.iov_base = &data;
		iov.iov_len = sizeof(int32_t);
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		bytesr = recvmsg(cs, &msg, 0);
		struct cmsghdr* cmhdr = CMSG_FIRSTHDR(&msg);
		if(cmhdr == NULL || cmhdr->cmsg_len != CMSG_LEN(sizeof(struct ucred)) ||
			cmhdr->cmsg_level != SOL_SOCKET || cmhdr->cmsg_type != SCM_CREDENTIALS)
		{
			slog(LOG_ALERT, "error getting credentials");
			close(cs);
			continue;
		}
		struct ucred* ucredrx = (struct ucred*) CMSG_DATA(cmhdr);
		slog(LOG_ALERT,
			"cgroup change request for pid: %d by pid: %d, user: %d, group: %d",
			data, ucredrx->pid, ucredrx->uid, ucredrx->gid);
		if(validate_pid(data, ucredrx->uid, ucredrx->pid))
		{
			classify((pid_t)data);
		}
		else
		{
			slog(LOG_ALERT,
				"shenanigans! cgroup change request denied for pid: %d from user %d",
				data, ucredrx->uid);
		}
		close(cs);
	}
}
