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
import stat
import multiprocessing  # requires Python 2.6


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
            DaemonTerminatedAbnormally, DaemonForkFailed), e:
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
    ### review error situations. map to backwards compat. ??
    ### be mindful of daemonize_exit().
    ### we should try and raise ChildFailed / ChildTerminatedAbnormally.
    ### ref: older revisions. OR: remove exceptions.

    child_is_ready = multiprocessing.Event()
    child_completed = multiprocessing.Event()

    p = multiprocessing.Process(target=self._first_child,
                                args=(child_is_ready, child_completed))
    p.start()

    # Wait for the child to finish setting things up (in case we need
    # to communicate with it). It will only exit when ready.
    ### use a timeout here! (parameterized, of course)
    p.join()

    ### need to propagate errors, to adjust the return codes
    if child_completed.is_set():
      ### what was the exit status?
      return DAEMON_COMPLETE
    if child_is_ready.is_set():
      return DAEMON_RUNNING

    ### how did we get here?! the immediate child should not exit without
    ### signalling ready/complete. some kind of error.
    return DAEMON_STARTED

  def _first_child(self, child_is_ready, child_completed):
    # we're in the child.

    ### NOTE: the original design was a bit bunk. Exceptions raised from
    ### this point are within the child processes. We need to signal the
    ### errors to the parent in other ways.

    # decouple from the parent process
    os.chdir('/')
    os.umask(0)
    os.setsid()

    # remember this pid so the second child can signal it.
    thispid = os.getpid()

    # if the daemon process exits before signalling readiness, then we
    # need to see the problem. trap SIGCHLD with a SignalCatcher.
    daemon_exit = SignalCatcher(signal.SIGCHLD)

    # perform the second fork
    try:
      pid = os.fork()
    except OSError as e:
      ### this won't make it to the parent process
      raise DaemonForkFailed(e.errno, e.strerror)

    if pid > 0:
      # in the parent.


      # Wait for the child to be ready for operation.
      while True:
        # The readiness event will invariably be signalled early/first.
        # If it *doesn't* get signalled because the child has prematurely
        # exited, then we will pause 10ms before noticing the exit. The
        # pause is acceptable since that is aberrant/unexpected behavior.
        ### is there a way to break this wait() on a signal such as SIGCHLD?
        ### parameterize this wait, in case the app knows children may
        ### fail quickly?
        if child_is_ready.wait(timeout=0.010):
          # The child signalled readiness. Yay!
          break
        if daemon_exit.signalled:
          # Whoops. The child exited without signalling :-(
          break
        # Python 2.6 compat: .wait() may exit when set, but return None
        if child_is_ready.is_set():
          break
        # A simple timeout. The child is taking a while to prepare. Go
        # back and wait for readiness.

      if daemon_exit.signalled:
        # Tell the parent that the child has exited.
        ### we need to communicate the exit status, if possible.
        child_completed.set()

        # reap the daemon process, getting its exit code. bubble it up.
        cpid, status = os.waitpid(pid, 0)
        assert pid == cpid
        if os.WIFEXITED(status):
          code = os.WEXITSTATUS(status)
          if code:
            ### this won't make it to the parent process
            raise DaemonFailed(code)
          ### this return value is ignored
          return DAEMON_NOT_RUNNING

        # the daemon did not exit cleanly.
        ### this won't make it to the parent process
        raise DaemonTerminatedAbnormally(status)

      # child_is_ready got asserted. the daemon is up and running, so
      # save the pid and return success.
      if self.pidfile:
        # Be wary of symlink attacks
        try:
          os.remove(self.pidfile)
        except OSError:
          pass
        fd = os.open(self.pidfile, os.O_WRONLY | os.O_CREAT | os.O_EXCL,
                     stat.S_IRUSR | stat.S_IRGRP | stat.S_IROTH)
        os.write(fd, '%d\n' % pid)
        os.close(fd)

      ### this return value is ignored
      return DAEMON_STARTED

      ### old code. what to do with this? throw ChildResumedIncorrectly
      ### or just toss this and the exception.
      # some other signal popped us out of the pause. the daemon might not
      # be running.
      ### this won't make it to the parent process
      raise ChildResumedIncorrectly()

    # we're a daemon now. get rid of the final remnants of the parent:
    # restore the signal handlers and switch std* to the proper files.
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

    ### TEST: don't release the parent immediately. the whole parent stack
    ###       should pause along with this sleep.
    #time.sleep(10)

    # everything is set up. call the initialization function.
    self.setup()

    ### TEST: exit before signalling.
    #sys.exit(0)
    #sys.exit(1)

    # the child is now ready for parent/anyone to communicate with it.
    child_is_ready.set()

    # start the daemon now.
    self.run()

    # The daemon is shutting down, so toss the pidfile.
    if self.pidfile:
      try:
        os.remove(self.pidfile)
      except OSError:
        pass

    ### this return value is ignored
    return DAEMON_COMPLETE

  def setup(self):
    raise NotImplementedError

  def run(self):
    raise NotImplementedError


class _Detacher(Daemon):
  def __init__(self, target, logfile='/dev/null', pidfile=None,
               args=(), kwargs={}):
    Daemon.__init__(self, logfile, pidfile)
    self.target = target
    self.args = args
    self.kwargs = kwargs

  def setup(self):
    pass

  def run(self):
    self.target(*self.args, **self.kwargs)


def run_detached(target, *args, **kwargs):
  """Simple function to run TARGET as a detached daemon.

  The additional arguments/keywords will be passed along. This function
  does not return -- sys.exit() will be called as appropriate.

  (capture SystemExit if logging/reporting is necessary)
  ### if needed, a variant of this func could be written to not exit
  """
  d = _Detacher(target, args=args, kwargs=kwargs)
  d.daemonize_exit()


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
