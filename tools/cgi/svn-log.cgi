#!/bin/sh
# Call svn_log.pl with your favorite repository.
  
echo "Content-type: text/plain"
echo ""
/var/www/html/svngui/svn-log.pl /home/svnroot/svngui
