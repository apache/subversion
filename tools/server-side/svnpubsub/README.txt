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
       'curl -sN http://${hostname}:2069/commits')
