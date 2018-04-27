#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <strings.h>

int main(int argc, char **argv)
{
  const char *cmd;
  const char *name = strrchr(argv[0], '/');

  if (name)
    ++name;
  else
    name = argv[0];

  if (!strcmp(name, "mount-tmpfs"))
    {
      cmd = "/usr/sbin/mount -F tmpfs -o size=768m tmpfs /export/home/wandisco/buildbot/slave/svn-sparc-solaris/obj/subversion/tests/";
    }
  else if (!strcmp(name, "umount-tmpfs"))
    {
      cmd = "/usr/sbin/umount /export/home/wandisco/buildbot/slave/svn-sparc-solaris/obj/subversion/tests/";
    }
  else
    {
      fprintf(stderr, "command not recognised\n");
      return -1;
    }

  if (setuid(0))
    {
      fprintf(stderr, "setuid failed\n");
      return -1;
    }

  return system(cmd);
}
