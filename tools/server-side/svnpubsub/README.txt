### write a README


TODO:
- bulk update at startup time to avoid backlog warnings
- switch to host:port format in config file
- fold BDEC into Daemon
- fold WorkingCopy._get_match() into __init__
- remove wc_ready(). assume all WorkingCopy instances are usable.
  place the instances into .watch at creation. the .update_applies()
  just returns if the wc is disabled (eg. could not find wc dir)
- figure out way to avoid the ASF-specific PRODUCTION_RE_FILTER
  (a base path exclusion list should work for the ASF)
- add support for SIGHUP to reread the config and reinitialize working copies
- joes will write documentation for svnpubsub as these items become fulfilled
- make LOGLEVEL configurable


Installation instructions:

1. Set up an svnpubsub service.

   This directory should be checked out to /usr/local/svnpubsub (or /opt/svnpubsub
   on Debian).

   There are init scripts for several OSes in the rc.d/ directory; add them
   to your OS boot process in the usual way for your OS.  (For example, via
   rc.conf(5) or update-rc.d(8).)

2. Run "commit-hook.py $REPOS $REV" from your post-commit hook.

   (As of 1.7, these are the same ordered arguments the post-commmit hook
   itself receives, so you can just symlink commit-hook.py as hooks/post-commit
   hook if you don't need any other hooks to run in the server process.  (This
   isn't as insane as it sounds --- post-commit email hooks could also feed of
   svnpubsub, and thus not be run within the committing server thread, but on
   any other process or box that listens to the svnpubsub stream!))

3. Set up svnpubsub clients.

   (eg svnwcsub.py, svnpubsub/client.py,
       'curl -i http://${hostname}:2069/commits/json')


Other notes:

- svnwcsub.py will create a file called ".revision" in the root of the working
  copy it updates.  That file will contain `svn info | sed -ne s/^URL:.//p`.
