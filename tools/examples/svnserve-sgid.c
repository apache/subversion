/*
 * Wrapper to run the svnserve process setgid.
 * The idea is to avoid the problem that some interpreters like bash
 * invoked by svnserve in hook scripts will reset the effective gid to
 * the real gid, nuking the effect of an ordinary setgid svnserve binary.
 * Sadly, to set the real gid portably, you need to be root, if only
 * for a moment.
 * Also smashes the environment to something known, so that games
 * can't be played to try to break the security of the hook scripts,
 * by setting IFS, PATH, and similar means.
 */
/*
 * Written by Perry Metzger, and placed into the public domain.
 */

#include <stdio.h>
#include <unistd.h>

#define REAL_PATH "/usr/bin/svnserve.real"

char *newenv[] = { "PATH=/bin:/usr/bin", "SHELL=/bin/sh", NULL };

int
main(int argc, char **argv)
{
	if (setgid(getegid()) == -1) {
		perror("setgid(getegid())");
		return 1;
	}

	if (seteuid(getuid()) == -1) {
		perror("seteuid(getuid())");
		return 1;
	}

	execve(REAL_PATH, argv, newenv);
	perror("attempting to exec " REAL_PATH " failed");
	return 1;
}
