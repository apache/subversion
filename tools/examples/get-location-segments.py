#!/usr/bin/env python
import sys
import os
from svn import client, ra, core

def printer(segment, pool):
  path = segment.path is not None and segment.path or "(null)"
  print("r%d-r%d: %s" % (segment.range_start, segment.range_end, path))


def parse_args(args):
  argc = len(sys.argv)

  # parse the target URL and optional peg revision
  path_pieces = args[0].split('@')
  if len(path_pieces) > 1:
    peg_revision = int(path_pieces[-1])
    assert peg_revision >= 0
    url = '@'.join(path_pieces[:-1])
  else:
    peg_revision = core.SVN_INVALID_REVNUM
    url = path_pieces[0]
  url = core.svn_path_canonicalize(url)

  # parse the revision range, if any
  if argc > 2:
    rev_pieces = args[1].split(':')
    num_revs = len(rev_pieces)
    assert num_revs < 3
    if num_revs == 2:
      start_revision = int(rev_pieces[0])
      end_revision = int(rev_pieces[1])
    else:
      start_revision = end_revision = int(rev_pieces[0])
    assert(start_revision >= 0)
    assert(end_revision >= 0)
  else:
    start_revision = peg_revision
    end_revision = 0

  # validate
  if start_revision >= 0 \
     and end_revision >= 0 \
     and end_revision > start_revision:
    raise Exception("End revision must not be younger than start revision")
  if peg_revision >= 0 \
     and start_revision >= 0 \
     and start_revision > peg_revision:
    raise Exception("Start revision must not be younger than peg revision")

  return url, peg_revision, start_revision, end_revision


def main():
  try:
    url, peg_revision, start_revision, end_revision = parse_args(sys.argv[1:])
  except Exception, e:
    sys.stderr.write("""Usage: %s URL[@PEG-REV] [START-REV[:END-REV]]

Trace the history of URL@PEG-REV, printing the location(s) of its
existence between START-REV and END-REV.  If START-REV is not
provided, the entire history of URL@PEG-REV back to its origin will be
displayed.  If provided, START-REV must not be younger than PEG-REV.
If END-REV is provided, it must not be younger than START-REV.

(This is a wrapper around Subversion's svn_ra_get_location_segments() API.)

ERROR: %s
""" % (os.path.basename(sys.argv[0]), str(e)))
    sys.exit(1)

  core.svn_config_ensure(None)
  ctx = client.ctx_t()
  providers = [
    client.get_simple_provider(),
    client.get_username_provider(),
    client.get_ssl_server_trust_file_provider(),
    client.get_ssl_client_cert_file_provider(),
    client.get_ssl_client_cert_pw_file_provider(),
    ]
  ctx.auth_baton = core.svn_auth_open(providers)
  ctx.config = core.svn_config_get_config(None)

  ra_callbacks = ra.callbacks_t()
  ra_callbacks.auth_baton = ctx.auth_baton
  ra_session = ra.open(url, ra_callbacks, None, ctx.config)
  ra.get_location_segments(ra_session, "", peg_revision,
                           start_revision, end_revision, printer)

if __name__ == "__main__":
  main()
