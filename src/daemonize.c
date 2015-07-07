/********************************************************
 * Filename: daemonize.c
 * Author: daedalus
 * Email: 
 * Description: 
 *
 *******************************************************/
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <getopt.h>
#include <unistd.h>
#include <syslog.h>
#include <stdarg.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "daemonize.h"

char* const* program_args;
const char* pidf;
pid_t pid;
int _stdout;

void die(int n, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf(": %s", strerror(errno));
	exit(n);
}

int setuser(const char* user)
{
	struct passwd* usr = getpwnam(user);
	if(!usr)
		dprintf(_stdout, "- as user %s\n", user);
	else{
		if (setuid(usr->pw_uid) != -1) 
			dprintf(_stdout, "+ as user %s (%d)\n", user, usr->pw_uid);
		else 
			dprintf(_stdout, "- as user %s (%d): %s\n", usr->pw_name, usr->pw_uid, strerror(errno));
	}
	return 0;
}

int setgroup(const char* group)
{
	struct group* grp = getgrnam(group);
	if(!grp)
		dprintf(_stdout, "- as group %s\n", group);
	else{
		if(setgid(grp->gr_gid) != -1)
			dprintf(_stdout, "+ as group %s (%d)\n", grp->gr_name, grp->gr_gid);
		else
			dprintf(_stdout, "- as group %s (%d): %s\n", grp->gr_name, grp->gr_gid, strerror(errno));
	}
	return 0;
}

int setdir(const char* dir)
{
	if(chdir(dir) != -1)
		dprintf(_stdout, "+ in directory %s\n", dir);
	else
		dprintf(_stdout, "- in directory %s: %s\n", dir, strerror(errno));
	return 0;
}

int setstdout(const char* file)
{
	int fd = open(file, O_RDWR | O_TRUNC | O_CREAT, 0666);
	if (fd != -1) {
		dup2(fd, STDOUT_FILENO);
		close(fd);
		dprintf(_stdout, "+ redirecting stdout to %s\n", file);
	} else {
		dprintf(_stdout, "- redirecting stdout to %s: %s\n", file, strerror(errno));
	}
	return 0;
}

int setstderr(const char* file)
{
	int fd = open(file, O_RDWR | O_TRUNC | O_CREAT, 0666);
	if (fd != -1) {
		dup2(fd, STDERR_FILENO);
		close(fd);
		dprintf(_stdout, "+ redirecting stderr to %s\n", file);
	} else {
		dprintf(_stdout, "- redirecting stderr to %s: %s\n", file, strerror(errno));
	}
	return 0;
}

int setpidfile(const char* file)
{
	int fd = open(file, O_RDWR | O_TRUNC | O_CREAT, 0666);
	if (fd != -1) {
		close(fd);
		dprintf(_stdout, "+ using pidfile %s\n", file);
		pidf = file;
	} else {
		dprintf(_stdout, "- using pidfile %s\n", file);
		pidf = NULL;
	}
	return 0;
}

int setmask(const char* mask)
{
	int msk;
	sscanf(mask, "%o", &msk);
	umask(msk);
	dprintf(_stdout, "+ using file mask %o\n", msk);
	return 0;
}

int setchroot(const char* root)
{
	if(chroot(root) != -1)
		dprintf(_stdout, "+ chroot into %s\n", root);
	else
		dprintf(_stdout, "- chroot into %s: %s\n", root, strerror(errno));

	return 0;
}

void execute()
{
	pid = fork();
	switch(pid) {
		case -1:
			return;
			break;
		case 0:
			/* child */
			if(setsid() == -1){
				exit(1);
			}
			close(_stdout);
			execvp(program_args[0], program_args);
			exit(1);
			break;
		default:
			if(pidf) {
				int fd = open(pidf, O_RDWR | O_TRUNC | O_CREAT, 0666);
				dprintf(fd, "%d", pid);
				close(fd);
			}
			break;
	}
}

void baby_sit()
{
	dprintf(_stdout, "+ babysitting...\n");
	pid_t _pid = fork();
	switch(_pid){
		case -1:
			break;
		case 0:
			close(_stdout);
			openlog("daemonizer", LOG_PID, LOG_DAEMON);
			setsid();
			int status = -1;
			int sitting = 1;
			execute();
			while(sitting){
				pid_t cpid = waitpid(pid, &status, 0);
				syslog(LOG_INFO, "%s WIFEXITED: %d ; WIFSIGNALED: %d ; WEXITSTATUS: %d ; WTERMSIG: %d", program_args[0],
						WIFEXITED(status), WIFSIGNALED(status), 
						WEXITSTATUS(status), WTERMSIG(status));
				if( WIFEXITED(status) && WEXITSTATUS(status) == 0)
					break;
				execute();
			}
			break;
		default:
			exit(0);
	}
}

void print_help()
{
	printf("Daemonize:\n"
			"	-u		--user		[current]	run as user\n"
			"	-g		--group		[current]	run as group\n"
			"	-d		--chdir		[current]	run in directory\n"
			"	-p		--pid		[none]		write pid to file\n"
			"	-o		--stdout	[current]	redirect stdout\n"
			"	-e		--stderr	[current]	redirect stderr\n"
			"	-m		--mask		[current]	set file mask\n"
			"	-r		--chroot	[none]		set root directory\n"
			"	-h		--help				print this help message\n"
			"	-b		--babysit	[no]		restart the program if it dies. syslog death.\n"
			"	-M		--monitor	[no]		monitor the programs resources (implicit babysit)\n"
			"			--null				redirect stdout and stderr to /dev/null.\n");
}

int main(int argc, char** argv)
{
	int option_index = 0;
	/* long options */
	static struct option long_opts[] = {
		{"user", required_argument, 0, 'u'},
		{"group", required_argument, 0, 'g'},
		{"chdir", required_argument, 0, 'd'},
		{"pid", required_argument, 0, 'p'},
		{"stdout", required_argument, 0, 'o'},
		{"stderr", required_argument, 0, 'e'},
		{"mask", required_argument, 0, 'm'},
		{"chroot", required_argument, 0, 'r'},
		{"help", no_argument, 0, 'h'},
		{"null", no_argument, 0, 0},
		{0, 0, 0, 0}
	};
	if(argc == 1){
		print_help();
		exit(0);
	}
	setenv("POSIXLY_CORRECT", "YES", 1); /* stop reading arguments after first non-arg found" */
	dup2(STDOUT_FILENO, 100);			 /* maintain a way to write to stdout */
	_stdout = 100;
	opterr = 0;
	optind = 0;
	int babysit = 0;
	/* Parse arguments */
	while(1) {
		int c = getopt_long(argc, argv, "u:g:d:p:o:e:m:r:hbM", long_opts, &option_index);
		if (c == -1)
			break;
		switch(c) {
			case 0:
				setstdout("/dev/null");
				setstderr("/dev/null");
				break;
			case 'h':
				print_help();
				exit(0);
				break;
			case 'u':
				setuser(optarg);
				break;
			case 'g':
				setgroup(optarg);
				break;
			case 'd':
				setdir(optarg);
				break;
			case 'p':
				setpidfile(optarg);
				break;
			case 'o':
				setstdout(optarg);
				break;
			case 'e':
				setstderr(optarg);
				break;
			case 'm':
				setmask(optarg);
				break;
			case 'r':
				setchroot(optarg);
				break;
			case 'b':
				babysit = 1;
				break;
			case 'M':
				babysit = 1;
				dprintf(_stdout, "+ monitoring is not yet implemented\n");
				break;
			case '?':
				dprintf(_stdout, "unkown argument %s\n", optarg);
				exit(1);
				break;
			case ':':
				dprintf(_stdout, "missing argument %s\n", optarg);
				break;
			default:
				printf("error\n");
				break;
		}
	}
	program_args = &argv[optind];
	if (babysit) {
		baby_sit();
		return 0;
	}
	pid = fork();
	switch(pid) {
		case -1:
			dprintf(_stdout, "-forking: %s\n", strerror(errno));
			return 0;
			break;
		case 0:
			/* child */
			if(setsid() == -1){
				dprintf(_stdout, "- setsid: %s", strerror(errno));
				exit(1);
			}
			close(_stdout);
			execvp(program_args[0], program_args);
			exit(1);
			break;
		default:
			dprintf(_stdout, "+ forking\n");
			if(pidf) {
				int fd = open(pidf, O_RDWR | O_TRUNC | O_CREAT, 0666);
				dprintf(fd, "%d", pid);
				close(fd);
			}
			break;
	}
	return 0;
}

