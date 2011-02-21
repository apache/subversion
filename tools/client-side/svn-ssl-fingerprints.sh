#!/bin/sh

# $0 --- list the fingerprints of SSL certificates that svn has seen before.
# 
# SYNOPSIS:
#     $0
#     $0 /path/to/.subversion

CONFIG_DIR=${1-$HOME/.subversion}
for i in $CONFIG_DIR/auth/svn.ssl.server/????????????????????????????????; do
  grep :// $i
  grep '.\{80\}' $i | sed 's/\(.\{64\}\)/\1\n/g' | openssl base64 -d | openssl x509 -inform der -noout -fingerprint | sed 's/=/\n/'
  echo
done
