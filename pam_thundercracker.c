#define _GNU_SOURCE
#define PAM_SM_SESSION
#include <security/pam_modules.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <syslog.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <pwd.h>

PAM_EXTERN int pam_sm_open_session(pam_handle_t* pamh, int flags,
								int argc, const char** argv)
{
	openlog("pam_thundercracker", LOG_PID|LOG_NDELAY|LOG_NOWAIT, LOG_AUTH);
	char* username = NULL;
	int ret = pam_get_item(pamh, PAM_USER, (void**) &username);
	if(ret != PAM_SUCCESS)
	{
		syslog(LOG_ERR, "Error getting user: %s", pam_strerror(pamh, ret));
		return(PAM_SUCCESS);
	}
	struct passwd* pws = getpwnam(username);
	if(!pws)
	{
		syslog(LOG_ERR, "bad username from PAM");
		return(PAM_SUCCESS);
	}
	if(pws->pw_uid < 1000)
	{
		syslog(LOG_ERR, "ignoring special user");
		return(PAM_SUCCESS); //ignore special users
	}
	char path[] = "/run/tc_classifyd";
	struct msghdr msg;
	struct iovec iov;
	int32_t data = (int32_t) getpid();
	union 
	{
		struct cmsghdr mh;
		char control[CMSG_SPACE(sizeof(struct ucred))];
	} control_data;
	struct ucred* uc;
	int s;
	struct sockaddr_un address;
	if((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
	{
		syslog(LOG_ERR, "Error opening socket: %s", strerror(errno));
		return(PAM_SESSION_ERR);
	}
	memset(&address, 0, sizeof(struct sockaddr_un));
	address.sun_family = AF_UNIX;
	strncpy(address.sun_path, path, sizeof(struct sockaddr_un)-sizeof(sa_family_t));	
	if(connect(s, (struct sockaddr*) &address, sizeof(struct sockaddr_un)) == -1)
	{
		syslog(LOG_ERR, "Error connecting socket: %s", strerror(errno));
		close(s);
		return(PAM_SESSION_ERR); //fail safe for now
	}
	memset(&msg, 0, sizeof(struct msghdr));
	memset(&iov, 0, sizeof(struct iovec));
	iov.iov_base = &data;
	iov.iov_len = sizeof(int32_t);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;	
	//extra stuff to drop root
	msg.msg_control = control_data.control;
	msg.msg_controllen = sizeof(control_data.control);
	struct cmsghdr* msghdrp = CMSG_FIRSTHDR(&msg);
	msghdrp->cmsg_len = CMSG_LEN(sizeof(struct ucred));
	msghdrp->cmsg_level = SOL_SOCKET;
	msghdrp->cmsg_type = SCM_CREDENTIALS;
	uc = (struct ucred*) CMSG_DATA(msghdrp);
	uc->pid = getpid();
	uc->uid = pws->pw_uid;
	uc->gid = pws->pw_gid;

	sendmsg(s, &msg, 0);
	close(s);

	closelog();

  /** 
   * Applications are inheriting the pam_thundercracker syslog name
   * reopen with null name to force default name per glibc
   */
	openlog(NULL, 0, 0);
	closelog();

	return(PAM_SUCCESS);
}

PAM_EXTERN int pam_sm_close_session(pam_handle_t* pamh, int flags,
									int argc, const char** argv)
{
	openlog("pam_thundercracker", LOG_PID|LOG_NDELAY|LOG_NOWAIT, LOG_AUTH);
	closelog();

	openlog(NULL, 0, 0);
	closelog();

	return(PAM_SUCCESS);
}
