# ---------------------------------------------------------------------------
#
# Copyright (c) 2005, Greg Stein
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.
#
# ---------------------------------------------------------------------------
#
# This software lives at:
#    http://gstein.googlecode.com/svn/trunk/python/daemonize.py
#

import os
import signal
import sys
import time


# possible return values from Daemon.daemonize()
DAEMON_RUNNING = 'The daemon is running'
DAEMON_NOT_RUNNING = 'The daemon is not running'
DAEMON_COMPLETE = 'The daemon has completed its operations'
DAEMON_STARTED = 'The daemon has been started'


class Daemon(object):

  def __init__(self, logfile, pidfile):
    self.logfile = logfile
    self.pidfile = pidfile

  def foreground(self):
    "Run in the foreground."
    ### we should probably create a pidfile. other systems may try to detect
    ### the pidfile to see if this "daemon" is running.
    self.setup()
    self.run()
    ### remove the pidfile

  def daemonize_exit(self):
    try:
      result = self.daemonize()
    except (ChildFailed, DaemonFailed) as e:
      # duplicate the exit code
      sys.exit(e.code)
    except (ChildTerminatedAbnormally, ChildForkFailed,
            DaemonTerminatedAbnormally, DaemonForkFailed) as e:
      sys.stderr.write('ERROR: %s\n' % e)
      sys.exit(1)
    except ChildResumedIncorrectly:
      sys.stderr.write('ERROR: continued after receiving unknown signal.\n')
      sys.exit(1)

    if result == DAEMON_STARTED or result == DAEMON_COMPLETE:
      sys.exit(0)
    elif result == DAEMON_NOT_RUNNING:
      sys.stderr.write('ERROR: the daemon exited with a success code '
                       'without signalling its startup.\n')
      sys.exit(1)

    # in original process. daemon is up and running. we're done.

  def daemonize(self):
    # fork off a child that can detach itself from this process.
    try:
      pid = os.fork()
    except OSError as e:
      raise ChildForkFailed(e.errno, e.strerror)

    if pid > 0:
      # we're in the parent. let's wait for the child to finish setting
      # things up -- on our exit, we want to ensure the child is accepting
      # connections.
      cpid, status = os.waitpid(pid, 0)
      assert pid == cpid
      if os.WIFEXITED(status):
        code = os.WEXITSTATUS(status)
        if code:
          raise ChildFailed(code)
        return DAEMON_RUNNING

      # the child did not exit cleanly.
      raise ChildTerminatedAbnormally(status)

    # we're in the child.

    # decouple from the parent process
    os.chdir('/')
    os.umask(0)
    os.setsid()

    # remember this pid so the second child can signal it.
    thispid = os.getpid()

    # register a signal handler so the SIGUSR1 doesn't stop the process.
    # this object will also record whether if got signalled.
    daemon_accepting = SignalCatcher(signal.SIGUSR1)

    # if the daemon process exits before sending SIGUSR1, then we need to see
    # the problem. trap SIGCHLD with a SignalCatcher.
    daemon_exit = SignalCatcher(signal.SIGCHLD)

    # perform the second fork
    try:
      pid = os.fork()
    except OSError as e:
      raise DaemonForkFailed(e.errno, e.strerror)

    if pid > 0:
      # in the parent.

      # we want to wait for the daemon to signal that it has created and
      # bound the socket, and is (thus) ready for connections. if the
      # daemon improperly exits before serving, we'll see SIGCHLD and the
      # .pause will return.
      ### we should add a timeout to this. allow an optional parameter to
      ### specify the timeout, in case it takes a long time to start up.
      signal.pause()

      if daemon_exit.signalled:
        # reap the daemon process, getting its exit code. bubble it up.
        cpid, status = os.waitpid(pid, 0)
        assert pid == cpid
        if os.WIFEXITED(status):
          code = os.WEXITSTATUS(status)
          if code:
            raise DaemonFailed(code)
          return DAEMON_NOT_RUNNING

        # the daemon did not exit cleanly.
        raise DaemonTerminatedAbnormally(status)

      if daemon_accepting.signalled:
        # the daemon is up and running, so save the pid and return success.
        if self.pidfile:
          # Be wary of symlink attacks
          try:
            os.remove(self.pidfile)
          except OSError:
            pass
          fd = os.open(self.pidfile, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0444)
          os.write(fd, '%d\n' % pid)
          os.close(fd)
        return DAEMON_STARTED

      # some other signal popped us out of the pause. the daemon might not
      # be running.
      raise ChildResumedIncorrectly()

    # we're a deamon now. get rid of the final remnants of the parent.
    # start by restoring default signal handlers
    signal.signal(signal.SIGUSR1, signal.SIG_DFL)
    signal.signal(signal.SIGCHLD, signal.SIG_DFL)
    sys.stdout.flush()
    sys.stderr.flush()
    si = open('/dev/null', 'r')
    so = open(self.logfile, 'a+')
    se = open(self.logfile, 'a+', 0)  # unbuffered
    os.dup2(si.fileno(), sys.stdin.fileno())
    os.dup2(so.fileno(), sys.stdout.fileno())
    os.dup2(se.fileno(), sys.stderr.fileno())
    # note: we could not inline the open() calls. after the fileno() completed,
    # the file would be closed, making the fileno invalid. gotta hold them
    # open until now:
    si.close()
    so.close()
    se.close()

    # TEST: don't release the parent immediately. the whole parent stack
    #       should pause along with this sleep.
    #time.sleep(10)

    # everything is set up. call the initialization function.
    self.setup()

    # sleep for one second before signalling. we want to make sure the
    # parent has called signal.pause()
    ### we should think of a better wait around the race condition.
    time.sleep(1)

    # okay. the daemon is ready. signal the parent to tell it we're set.
    os.kill(thispid, signal.SIGUSR1)

    # start the daemon now.
    self.run()

    # The daemon is shutting down, so toss the pidfile.
    try:
      os.remove(self.pidfile)
    except OSError:
      pass

    return DAEMON_COMPLETE

  def setup(self):
    raise NotImplementedError

  def run(self):
    raise NotImplementedError


class SignalCatcher(object):
  def __init__(self, signum):
    self.signalled = False
    signal.signal(signum, self.sig_handler)

  def sig_handler(self, signum, frame):
    self.signalled = True


class ChildTerminatedAbnormally(Exception):
  "The child process terminated abnormally."
  def __init__(self, status):
    Exception.__init__(self, status)
    self.status = status
  def __str__(self):
    return 'child terminated abnormally (0x%04x)' % self.status

class ChildFailed(Exception):
  "The child process exited with a failure code."
  def __init__(self, code):
    Exception.__init__(self, code)
    self.code = code
  def __str__(self):
    return 'child failed with exit code %d' % self.code

class ChildForkFailed(Exception):
  "The child process could not be forked."
  def __init__(self, errno, strerror):
    Exception.__init__(self, errno, strerror)
    self.errno = errno
    self.strerror = strerror
  def __str__(self):
    return 'child fork failed with error %d (%s)' % self.args

class ChildResumedIncorrectly(Exception):
  "The child resumed its operation incorrectly."

class DaemonTerminatedAbnormally(Exception):
  "The daemon process terminated abnormally."
  def __init__(self, status):
    Exception.__init__(self, status)
    self.status = status
  def __str__(self):
    return 'daemon terminated abnormally (0x%04x)' % self.status

class DaemonFailed(Exception):
  "The daemon process exited with a failure code."
  def __init__(self, code):
    Exception.__init__(self, code)
    self.code = code
  def __str__(self):
    return 'daemon failed with exit code %d' % self.code

class DaemonForkFailed(Exception):
  "The daemon process could not be forked."
  def __init__(self, errno, strerror):
    Exception.__init__(self, errno, strerror)
    self.errno = errno
    self.strerror = strerror
  def __str__(self):
    return 'daemon fork failed with error %d (%s)' % self.args
