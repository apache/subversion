
#include <stdio.h>
#include <unistd.h>


int main()
{
  const char *cmd = "/bin/umount `/usr/bin/dirname $0`/build/subversion/tests/cmdline/svn-test-work";

  setuid(0);

  return system(cmd);

}

