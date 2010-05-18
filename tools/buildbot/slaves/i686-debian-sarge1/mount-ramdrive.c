
#include <stdio.h>
#include <unistd.h>


int main()
{
  const char *cmd = "/bin/mount -t tmpfs -o size=50M tmpfs `subversion/tests/cmdline/svn-test-work";

  setuid(0);

  system(cmd);

}
