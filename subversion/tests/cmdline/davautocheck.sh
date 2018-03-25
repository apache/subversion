#!/bin/sh
#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#
# -*- mode: shell-script; -*-
# $Id$

# This script simplifies preparation of environment for Subversion client
# communicating with a server via DAV protocol. The prerequisites of such
# testing are:
#   - Subversion built using --enable-shared --with-apxs options,
#   - Working Apache 2 HTTPD Server with the apxs program reachable through
#     PATH or specified via the APXS Makefile variable or environment variable,
#   - Modules dav_module and log_config_module compiled as DSO or built into
#     Apache HTTPD Server executable.
# The basic intension of this script is to be able to perform "make check"
# operation over DAV without any configuration efforts whatsoever, provided
# that conditions above are met.
#
# The script will find Apache and all necessary modules including mod_dav_svn,
# create a temporary directory in subversion/tests/cmdline, create
# Apache 2 configuration file in this directory, start Apache 2 on a random
# port number higher than 1024, and execute Subversion command-line client
# test suites against this instance of HTTPD. Every vital configuration
# parameter is checked before the tests start. The script will ask questions
# about browsing Apache error log (default is "no") and about deleting
# temporary directory (default "yes") and pause for 32 seconds before
# proceeding with the default. HTTPD access log is also created in the
# temporary directory.
#
# Run this script without parameters to execute the full battery of tests:
#   subversion/tests/cmdline/davautocheck.sh
# Run this script with the name of a test suite to run this suite:
#   subversion/tests/cmdline/davautocheck.sh basic
# Run this script with the test suite name and test number to execute just this
# test:
#   subversion/tests/cmdline/davautocheck.sh basic 4
# This script can also be invoked via "make davautocheck".
#
# If the temporary directory is not deleted, it can be reused for further
# manual DAV protocol interoperation testing. HTTPD must be started by
# specifying configuration file on the command line:
#   httpd -f subversion/tests/cmdline/<httpd-...>/cfg
#
# If you want to run this against an *installed* HTTPD (for example, to test
# one version's client against another version's server) specify both APXS
# *and* MODULE_PATH for the other server:
#
#   APXS=/opt/svn/1.4.x/bin/apxs MODULE_PATH=/opt/svn/1.4.x/modules \
#     subversion/tests/cmdline/davautocheck.sh
#
# Other environment variables that are interpreted by this script:
#
#  make davautocheck CACHE_REVPROPS=1       # sets SVNCacheRevProps on
#
#  make davautocheck BLOCK_READ=1           # sets SVNBlockRead on
#
#  make davautocheck USE_SSL=1              # run over https
#
#  make davautocheck USE_HTTPV1=1           # sets SVNAdvertiseV2Protocol off
#
#  make davautocheck APACHE_MPM=event       # specifies the 2.4 MPM
#
#  make davautocheck SVN_PATH_AUTHZ=short_circuit  # SVNPathAuthz short_circuit
#
# Passing --no-tests as argv[1] will have the script start a server
# but not run any tests.  Passing --gdb or --lldb will do the same, and in
# addition spawn gdb/lldb in the foreground attached to the running server.

PYTHON=${PYTHON:-python}

SCRIPTDIR=$(dirname $0)
SCRIPT=$(basename $0)
STOPSCRIPT=$SCRIPTDIR/.$SCRIPT.stop

trap stop_httpd_and_die HUP TERM INT

# Ensure the server uses a known locale.
LC_ALL=C
export LC_ALL

stop_httpd_and_die() {
  [ -e "$HTTPD_PID" ] && kill $(cat "$HTTPD_PID")
  echo "HTTPD stopped."
  exit 1
}

say() {
  echo "$SCRIPT: $*"
}

fail() {
  say $*
  stop_httpd_and_die
}

query() {
  printf "%s" "$SCRIPT: $1 (y/n)? [$2] "
  if [ -n "$BASH_VERSION" ]; then
    read -n 1 -t 32
  else
    #
    prog="
import select as s
import sys
import tty, termios
tty.setcbreak(sys.stdin.fileno(), termios.TCSANOW)
if s.select([sys.stdin.fileno()], [], [], 32)[0]:
  sys.stdout.write(sys.stdin.read(1))
"
    stty_state=`stty -g`
    REPLY=`$PYTHON -u -c "$prog" "$@"`
    stty $stty_state
  fi
  echo
  [ "${REPLY:-$2}" = 'y' ]
}

get_loadmodule_config() {
  local SO="$($APXS -q LIBEXECDIR)/$1.so"

  # shared object module?
  if [ -r "$SO" ]; then
    local NM=$(echo "$1" | sed 's|mod_\(.*\)|\1_module|')
    echo "LoadModule $NM \"$SO\"" &&
    return
  fi

  # maybe it's built-in?
  "$HTTPD" -l | grep "$1\\.c" >/dev/null && return

  return 1
}

# Check apxs's SBINDIR and BINDIR for given program names
get_prog_name() {
  for prog in $*
  do
    for dir in $($APXS -q SBINDIR) $($APXS -q BINDIR)
    do
      if [ -e "$dir/$prog" ]; then
        echo "$dir/$prog" && return
      fi
    done
  done

  return 1
}

# Don't assume sbin is in the PATH.
# This is used to locate apxs when the script is invoked manually; when
# invoked by 'make davautocheck' the APXS environment variable is set.
PATH="$PATH:/usr/sbin:/usr/local/sbin"

# Find the source and build directories. The build dir can be found if it is
# the current working dir or the source dir.
ABS_SRCDIR=$(cd ${SCRIPTDIR}/../../../; pwd)
if [ -x subversion/svn/svn ]; then
  ABS_BUILDDIR=$(pwd)
elif [ -x $ABS_SRCDIR/subversion/svn/svn ]; then
  ABS_BUILDDIR=$ABS_SRCDIR
else
  fail "Run this script from the root of Subversion's build tree!"
fi

# Remove any proxy environmental variables that affect wget or curl.
# We don't need a proxy to connect to localhost and having the proxy
# environmental variables set breaks the Apache configuration file
# test below, since wget or curl will ask the proxy to connect to
# localhost.
unset PROXY
unset http_proxy
unset HTTPS_PROXY

# Pick up value from environment or PATH (also try apxs2 - for Debian)
if [ ${APXS:+set} ]; then
  :
elif APXS=$(grep '^APXS' $ABS_BUILDDIR/Makefile | sed 's/^APXS *= *//') && \
     [ -n "$APXS" ]; then
  :
elif APXS=$(which apxs); then
  :
elif APXS=$(which apxs2); then
  :
else
  fail "neither apxs or apxs2 found - required to run davautocheck"
fi

[ -x $APXS ] || fail "Can't execute apxs executable $APXS"

say "Using '$APXS'..."

# Pick up $USE_HTTPV1
ADVERTISE_V2_PROTOCOL=on
if [ ${USE_HTTPV1:+set} ]; then
 ADVERTISE_V2_PROTOCOL=off
fi

# Pick up $SVN_PATH_AUTHZ
SVN_PATH_AUTHZ_LINE=""
if [ ${SVN_PATH_AUTHZ:+set} ]; then
 SVN_PATH_AUTHZ_LINE="SVNPathAuthz      ${SVN_PATH_AUTHZ}"
fi

CACHE_REVPROPS_SETTING=off
if [ ${CACHE_REVPROPS:+set} ]; then
  CACHE_REVPROPS_SETTING=on
fi

BLOCK_READ_SETTING=off
if [ ${BLOCK_READ:+set} ]; then
  BLOCK_READ_SETTING=on
fi

if [ ${MODULE_PATH:+set} ]; then
    MOD_DAV_SVN="$MODULE_PATH/mod_dav_svn.so"
    MOD_AUTHZ_SVN="$MODULE_PATH/mod_authz_svn.so"
    MOD_DONTDOTHAT="$MODULE_PATH/mod_dontdothat.so"
else
    MOD_DAV_SVN="$ABS_BUILDDIR/subversion/mod_dav_svn/.libs/mod_dav_svn.so"
    MOD_AUTHZ_SVN="$ABS_BUILDDIR/subversion/mod_authz_svn/.libs/mod_authz_svn.so"
    MOD_DONTDOTHAT="$ABS_BUILDDIR/tools/server-side/mod_dontdothat/.libs/mod_dontdothat.so"
fi

[ -r "$MOD_DAV_SVN" ] \
  || fail "dav_svn_module not found, please use '--enable-shared --with-apxs' with your 'configure' script"
[ -r "$MOD_AUTHZ_SVN" ] \
  || fail "authz_svn_module not found, please use '--enable-shared --with-apxs' with your 'configure' script"
[ -r "$MOD_DONTDOTHAT" ] \
  || fail "dontdothat_module not found, please use '--enable-shared --with-apxs' with your 'configure' script"

for d in "$ABS_BUILDDIR"/subversion/*/.libs; do
  if [ -z "$BUILDDIR_LIBRARY_PATH" ]; then
    BUILDDIR_LIBRARY_PATH="$d"
  else
    BUILDDIR_LIBRARY_PATH="$BUILDDIR_LIBRARY_PATH:$d"
  fi
done

case "`uname`" in
  Darwin*)
    DYLD_LIBRARY_PATH="$BUILDDIR_LIBRARY_PATH:$DYLD_LIBRARY_PATH"
    export DYLD_LIBRARY_PATH
    ;;
  *)
    LD_LIBRARY_PATH="$BUILDDIR_LIBRARY_PATH:$LD_LIBRARY_PATH"
    export LD_LIBRARY_PATH
    ;;
esac

httpd="$($APXS -q PROGNAME)"
HTTPD=$(get_prog_name $httpd) || fail "HTTPD '$HTTPD' not found"
[ -x $HTTPD ] || fail "HTTPD '$HTTPD' not executable"

"$HTTPD" -v 1>/dev/null 2>&1 \
  || fail "HTTPD '$HTTPD' doesn't start properly"

HTPASSWD=$(get_prog_name htpasswd htpasswd2) \
  || fail "Could not find htpasswd or htpasswd2"
[ -x $HTPASSWD ] \
  || fail "HTPASSWD '$HTPASSWD' not executable"
say "Using '$HTPASSWD'..."

LOAD_MOD_DAV=$(get_loadmodule_config mod_dav) \
  || fail "DAV module not found"

LOAD_MOD_LOG_CONFIG=$(get_loadmodule_config mod_log_config) \
  || fail "log_config module not found"

# needed for TypesConfig
LOAD_MOD_MIME=$(get_loadmodule_config mod_mime) \
  || fail "MIME module not found"

LOAD_MOD_ALIAS=$(get_loadmodule_config mod_alias) \
  || fail "ALIAS module not found"

# needed for Auth*, Require, etc. directives
LOAD_MOD_AUTH=$(get_loadmodule_config mod_auth) \
  || {
say "Monolithic Auth module not found. Assuming we run against Apache 2.1+"
LOAD_MOD_AUTH="$(get_loadmodule_config mod_auth_basic)" \
    || fail "Auth_Basic module not found."
LOAD_MOD_ACCESS_COMPAT="$(get_loadmodule_config mod_access_compat)" \
    && {
say "Found modules for Apache 2.3.0+"
LOAD_MOD_AUTHN_CORE="$(get_loadmodule_config mod_authn_core)" \
    || fail "Authn_Core module not found."
LOAD_MOD_AUTHZ_CORE="$(get_loadmodule_config mod_authz_core)" \
    || fail "Authz_Core module not found."
LOAD_MOD_UNIXD=$(get_loadmodule_config mod_unixd) \
    || fail "UnixD module not found"
}
LOAD_MOD_AUTHN_FILE="$(get_loadmodule_config mod_authn_file)" \
    || fail "Authn_File module not found."
LOAD_MOD_AUTHZ_USER="$(get_loadmodule_config mod_authz_user)" \
    || fail "Authz_User module not found."
LOAD_MOD_AUTHZ_GROUPFILE="$(get_loadmodule_config mod_authz_groupfile)" \
    || fail "Authz_GroupFile module not found."
LOAD_MOD_AUTHZ_HOST="$(get_loadmodule_config mod_authz_host)" \
    || fail "Authz_Host module not found."
}
if [ ${APACHE_MPM:+set} ]; then
    LOAD_MOD_MPM=$(get_loadmodule_config mod_mpm_$APACHE_MPM) \
      || fail "MPM module not found"
fi
if [ x"$APACHE_MPM" = x"event" ] && [ x"$FS_TYPE" = x"bdb" ]; then
  fail "FS_TYPE=bdb and APACHE_MPM=event are mutually exclusive (see SVN-4157)"
fi
if [ ${USE_SSL:+set} ]; then
    LOAD_MOD_SSL=$(get_loadmodule_config mod_ssl) \
      || fail "SSL module not found"
fi

# Stop any previous instances, os we can re-use the port.
if [ -x $STOPSCRIPT ]; then $STOPSCRIPT ; sleep 1; fi

ss > /dev/null 2>&1 || netstat > /dev/null 2>&1 || fail "unable to find ss or netstat required to find a free port"

HTTPD_PORT=3691
while \
  (ss -ltn sport = :$HTTPD_PORT 2>&1 | grep :$HTTPD_PORT > /dev/null ) \
  || \
  (netstat -an 2>&1 | grep $HTTPD_PORT | grep 'LISTEN' > /dev/null ) \
  do
  HTTPD_PORT=$(( HTTPD_PORT + 1 ))
  if [ $HTTPD_PORT -eq 65536 ]; then
    # Most likely the loop condition is true regardless of $HTTPD_PORT
    fail "ss/netstat claim you have no free ports for httpd to listen on."
  fi
done
HTTPD_ROOT="$ABS_BUILDDIR/subversion/tests/cmdline/httpd-$(date '+%Y%m%d-%H%M%S')"
HTTPD_CFG="$HTTPD_ROOT/cfg"
HTTPD_PID="$HTTPD_ROOT/pid"
HTTPD_ACCESS_LOG="$HTTPD_ROOT/access_log"
HTTPD_ERROR_LOG="$HTTPD_ROOT/error_log"
HTTPD_MIME_TYPES="$HTTPD_ROOT/mime.types"
HTTPD_DONTDOTHAT="$HTTPD_ROOT/dontdothat"
if [ -z "$BASE_URL" ]; then
  BASE_URL="http://localhost:$HTTPD_PORT"
else
  # Specify the public name of the host when using a proxy on another host, the
  # port number will be appended.
  BASE_URL="$BASE_URL:$HTTPD_PORT"
fi
HTTPD_USERS="$HTTPD_ROOT/users"
HTTPD_GROUPS="$HTTPD_ROOT/groups"

mkdir "$HTTPD_ROOT" \
  || fail "couldn't create temporary directory '$HTTPD_ROOT'"

say "Using directory '$HTTPD_ROOT'..."

if [ ${USE_SSL:+set} ]; then
  say "Setting up SSL"
  BASE_URL="https://localhost:$HTTPD_PORT"
# A self-signed certifcate for localhost that expires after 2039-12-30
# generated via:
#   openssl req -new -x509 -nodes -days 10000 -out cert.pem -keyout cert-key.pem
# This is embedded, rather than generated on-the-fly, to avoid consuming
# system entropy.
  SSL_CERTIFICATE_FILE="$HTTPD_ROOT/cert.pem"
cat > "$SSL_CERTIFICATE_FILE" <<__EOF__
-----BEGIN CERTIFICATE-----
MIIC7zCCAligAwIBAgIJALP1pLDiJRtuMA0GCSqGSIb3DQEBBQUAMFkxCzAJBgNV
BAYTAkFVMRMwEQYDVQQIEwpTb21lLVN0YXRlMSEwHwYDVQQKExhJbnRlcm5ldCBX
aWRnaXRzIFB0eSBMdGQxEjAQBgNVBAMTCWxvY2FsaG9zdDAeFw0xMjA4MTMxNDA5
MDRaFw0zOTEyMzAxNDA5MDRaMFkxCzAJBgNVBAYTAkFVMRMwEQYDVQQIEwpTb21l
LVN0YXRlMSEwHwYDVQQKExhJbnRlcm5ldCBXaWRnaXRzIFB0eSBMdGQxEjAQBgNV
BAMTCWxvY2FsaG9zdDCBnzANBgkqhkiG9w0BAQEFAAOBjQAwgYkCgYEA9kBx6trU
WQnFNDrW+dU159zEbSWGts3ScITIMTLE4EclMh50SP2BnJDnetkNO8JhPXOm4KZi
XdJugWAk0NmpawhAk3xVxHh5N8wwyPk3IMx7+Yu+sgcsd0Dj9YK1fIazgTUp/Dsk
VGJvqu+kgNYxPvzWi/OsBLW/ZNp+spTzoAcCAwEAAaOBvjCBuzAdBgNVHQ4EFgQU
f7OIDackB7zzPm10aiQgq9WzRdQwgYsGA1UdIwSBgzCBgIAUf7OIDackB7zzPm10
aiQgq9WzRdShXaRbMFkxCzAJBgNVBAYTAkFVMRMwEQYDVQQIEwpTb21lLVN0YXRl
MSEwHwYDVQQKExhJbnRlcm5ldCBXaWRnaXRzIFB0eSBMdGQxEjAQBgNVBAMTCWxv
Y2FsaG9zdIIJALP1pLDiJRtuMAwGA1UdEwQFMAMBAf8wDQYJKoZIhvcNAQEFBQAD
gYEAD2rdgeVYCSEeseEfFCTNte//rDsT3coO9SbGOpmlCJ5TfbmXjs2YaQZH7NST
mla3hw2Bf9ppTUw1ZWvOVgD3mpxAbYNBA/4HaxmK4GlS2kZsKiMr0xgcVGjmEIW/
HS9q+PHwStDKNSyYc1+m+bUmeRGUKLgC4kuBF7JDK8A2WYc=
-----END CERTIFICATE-----
__EOF__
  SSL_CERTIFICATE_KEY_FILE="$HTTPD_ROOT/cert-key.pem"
cat > "$SSL_CERTIFICATE_KEY_FILE" <<__EOF__
-----BEGIN RSA PRIVATE KEY-----
MIICXQIBAAKBgQD2QHHq2tRZCcU0Otb51TXn3MRtJYa2zdJwhMgxMsTgRyUyHnRI
/YGckOd62Q07wmE9c6bgpmJd0m6BYCTQ2alrCECTfFXEeHk3zDDI+TcgzHv5i76y
Byx3QOP1grV8hrOBNSn8OyRUYm+q76SA1jE+/NaL86wEtb9k2n6ylPOgBwIDAQAB
AoGBAJBzhV+rNl10qcXVrj2noJN+oYsVNE0Pt55hhb22dl7J3TvlOXmHm/xn1CHw
KR8hC0GtEfs+Hv3CbyhdabtJs2L7QxO5VjgLO+onBmAOw1iPF9DjbMcAlFJnoOWI
HYwANOWGp2jRxL5cHUfrBVCgUISen3VUZEnQkr4n/Zty/QEBAkEA/XIZ3oh5MiFA
o4IaFaFQpBc6K/e6fnM0217scaPvfZiYS1k9Fx/UQTAGsxJOnhnsi04WgHPMS5wB
RP4/PiIGIQJBAPi7yIKKS4E8hWBZL+79TI8Zm2uehGCB8V6m9k7e3I82To9Tgcow
qZHsAPtN50fg85I94L3REg2FSQlDlzbMkScCQQC2pweLv/EQNrS94eJomkRirban
vzYxMVfzjRp737iWXGXNT7feNXsjq7f4UAZGnMpDrvg6hLnD999WWKE9ZwnhAkBl
c9p9/EB9zxyrxtT5StGuUIiHJdnirz2vGLTASMB3nXP/m9UFjkGr5jIkTos2Uzel
/50qbxtI7oNyxuHnlRrjAkASfQ51kaBcABYRiacesQi94W/kE3MkgHWkCXNb6//u
gxk/ezALZ8neJzJudzRkX3auGwH1ne9vCM1ED5dkM54H
-----END RSA PRIVATE KEY-----
__EOF__
  SSL_MAKE_VAR="SSL_CERT=$SSL_CERTIFICATE_FILE"
  SSL_TEST_ARG="--ssl-cert $SSL_CERTIFICATE_FILE"
fi

say "Adding users for lock authentication"
$HTPASSWD -bc $HTTPD_USERS jrandom   rayjandom
$HTPASSWD -b  $HTTPD_USERS jconstant rayjandom
$HTPASSWD -b  $HTTPD_USERS __dumpster__ __loadster__
$HTPASSWD -b  $HTTPD_USERS JRANDOM   rayjandom
$HTPASSWD -b  $HTTPD_USERS JCONSTANT rayjandom

say "Adding groups for mod_authz_svn tests"
cat > "$HTTPD_GROUPS" <<__EOF__
random: jrandom
constant: jconstant
__EOF__

touch $HTTPD_MIME_TYPES

cat >  "$HTTPD_DONTDOTHAT" <<__EOF__
[recursive-actions]
/ = deny

__EOF__

cat > "$HTTPD_CFG" <<__EOF__
$LOAD_MOD_MPM
$LOAD_MOD_SSL
$LOAD_MOD_LOG_CONFIG
$LOAD_MOD_MIME
$LOAD_MOD_ALIAS
$LOAD_MOD_UNIXD
$LOAD_MOD_DAV
LoadModule          dav_svn_module "$MOD_DAV_SVN"
$LOAD_MOD_AUTH
$LOAD_MOD_AUTHN_CORE
$LOAD_MOD_AUTHN_FILE
$LOAD_MOD_AUTHZ_CORE
$LOAD_MOD_AUTHZ_USER
$LOAD_MOD_AUTHZ_GROUPFILE
$LOAD_MOD_AUTHZ_HOST
$LOAD_MOD_ACCESS_COMPAT
LoadModule          authz_svn_module "$MOD_AUTHZ_SVN"
LoadModule          dontdothat_module "$MOD_DONTDOTHAT"

__EOF__

if "$HTTPD" -v | grep '/2\.[012]' >/dev/null; then
  cat >> "$HTTPD_CFG" <<__EOF__
LockFile            lock
User                $(id -un)
Group               $(id -gn)
__EOF__
else
HTTPD_LOCK="$HTTPD_ROOT/lock"
mkdir "$HTTPD_LOCK" \
  || fail "couldn't create lock directory '$HTTPD_LOCK'"
  cat >> "$HTTPD_CFG" <<__EOF__
# worker and prefork MUST have a mpm-accept lockfile in 2.3.0+
<IfModule worker.c>
  Mutex "file:$HTTPD_LOCK" mpm-accept
</IfModule>
<IfModule prefork.c>
  Mutex "file:$HTTPD_LOCK" mpm-accept
</IfModule>
__EOF__
fi

if [ ${USE_SSL:+set} ]; then
cat >> "$HTTPD_CFG" <<__EOF__
SSLEngine on
SSLCertificateFile $SSL_CERTIFICATE_FILE
SSLCertificateKeyFile $SSL_CERTIFICATE_KEY_FILE
__EOF__
fi

cat >> "$HTTPD_CFG" <<__EOF__
Listen              $HTTPD_PORT
ServerName          localhost
PidFile             "$HTTPD_PID"
LogFormat           "%h %l %u %t \"%r\" %>s %b \"%f\"" common
CustomLog           "$HTTPD_ACCESS_LOG" common
ErrorLog            "$HTTPD_ERROR_LOG"
LogLevel            debug
ServerRoot          "$HTTPD_ROOT"
DocumentRoot        "$HTTPD_ROOT"
ScoreBoardFile      "$HTTPD_ROOT/run"
CoreDumpDirectory   "$HTTPD_ROOT"
TypesConfig         "$HTTPD_MIME_TYPES"
StartServers        4
MaxRequestsPerChild 0
<IfModule worker.c>
  ThreadsPerChild   8
</IfModule>
<IfModule event.c>
  ThreadsPerChild   8
</IfModule>
MaxClients          32
HostNameLookups     Off
LogFormat           "%h %l %u %t \"%r\" %>s %b \"%{Referer}i\" \"%{User-Agent}i\"" format
CustomLog           "$HTTPD_ROOT/req" format
CustomLog           "$HTTPD_ROOT/ops" "%t %u %{SVN-REPOS-NAME}e %{SVN-ACTION}e" env=SVN-ACTION

<Directory />
  AllowOverride     none
</Directory>
<Directory "$HTTPD_ROOT">
  AllowOverride     none
  #Require           all granted
</Directory>

<Location /svn-test-work/repositories>
__EOF__
location_common() {
cat >> "$HTTPD_CFG" <<__EOF__
  DAV               svn
  AuthzSVNAccessFile "$ABS_BUILDDIR/subversion/tests/cmdline/svn-test-work/authz"
  AuthType          Basic
  AuthName          "Subversion Repository"
  AuthUserFile      $HTTPD_USERS
  SVNAdvertiseV2Protocol ${ADVERTISE_V2_PROTOCOL}
  SVNCacheRevProps  ${CACHE_REVPROPS_SETTING}
  SVNListParentPath On
  SVNBlockRead      ${BLOCK_READ_SETTING}
__EOF__
}
location_common
cat >> "$HTTPD_CFG" <<__EOF__
  SVNParentPath     "$ABS_BUILDDIR/subversion/tests/cmdline/svn-test-work/repositories"
  Require           valid-user
  ${SVN_PATH_AUTHZ_LINE}
</Location>
<Location /ddt-test-work/repositories>
__EOF__
location_common
cat >> "$HTTPD_CFG" <<__EOF__
  SVNParentPath     "$ABS_BUILDDIR/subversion/tests/cmdline/svn-test-work/repositories"
  Require           valid-user
  ${SVN_PATH_AUTHZ_LINE}
  DontDoThatConfigFile "$HTTPD_DONTDOTHAT"
</Location>
<Location /svn-test-work/local_tmp/repos>
__EOF__
location_common
cat >> "$HTTPD_CFG" <<__EOF__
  SVNPath           "$ABS_BUILDDIR/subversion/tests/cmdline/svn-test-work/local_tmp/repos"
  Require           valid-user
  ${SVN_PATH_AUTHZ_LINE}
</Location>
<Location /authz-test-work/anon>
  DAV               svn
  SVNParentPath     "$ABS_BUILDDIR/subversion/tests/cmdline/svn-test-work/local_tmp"
  AuthzSVNAccessFile "$ABS_BUILDDIR/subversion/tests/cmdline/svn-test-work/authz"
  SVNAdvertiseV2Protocol ${ADVERTISE_V2_PROTOCOL}
  SVNCacheRevProps  ${CACHE_REVPROPS_SETTING}
  SVNListParentPath On
  # This may seem unnecessary but granting access to everyone here is necessary
  # to exercise a bug with httpd 2.3.x+.  The "Require all granted" syntax is
  # new to 2.3.x+ which we can detect with the mod_authz_core.c module
  # signature.  Use the "Allow from all" syntax with older versions for symmetry.
  <IfModule mod_authz_core.c>
    Require all granted
  </IfModule>
  <IfModule !mod_authz_core.c>
    Allow from all
  </IfModule>
  ${SVN_PATH_AUTHZ_LINE}
</Location>
<Location /authz-test-work/mixed>
__EOF__
location_common
cat >> "$HTTPD_CFG" <<__EOF__
  SVNParentPath     "$ABS_BUILDDIR/subversion/tests/cmdline/svn-test-work/local_tmp"
  Require           valid-user
  Satisfy Any
  ${SVN_PATH_AUTHZ_LINE}
</Location>
<Location /authz-test-work/mixed-noauthwhenanon>
__EOF__
location_common
cat >> "$HTTPD_CFG" <<__EOF__
  SVNParentPath     "$ABS_BUILDDIR/subversion/tests/cmdline/svn-test-work/local_tmp"
  Require           valid-user
  AuthzSVNNoAuthWhenAnonymousAllowed On
  SVNPathAuthz On
</Location>
<Location /authz-test-work/authn>
__EOF__
location_common
cat >> "$HTTPD_CFG" <<__EOF__
  SVNParentPath     "$ABS_BUILDDIR/subversion/tests/cmdline/svn-test-work/local_tmp"
  Require           valid-user
  ${SVN_PATH_AUTHZ_LINE}
</Location>
<Location /authz-test-work/authn-anonoff>
__EOF__
location_common
cat >> "$HTTPD_CFG" <<__EOF__
  SVNParentPath     "$ABS_BUILDDIR/subversion/tests/cmdline/svn-test-work/local_tmp"
  Require           valid-user
  AuthzSVNAnonymous Off
  SVNPathAuthz On
</Location>
<Location /authz-test-work/authn-lcuser>
__EOF__
location_common
cat >> "$HTTPD_CFG" <<__EOF__
  SVNParentPath     "$ABS_BUILDDIR/subversion/tests/cmdline/svn-test-work/local_tmp"
  Require           valid-user
  AuthzForceUsernameCase Lower
  ${SVN_PATH_AUTHZ_LINE}
</Location>
<Location /authz-test-work/authn-group>
__EOF__
location_common
cat >> "$HTTPD_CFG" <<__EOF__
  SVNParentPath     "$ABS_BUILDDIR/subversion/tests/cmdline/svn-test-work/local_tmp"
  AuthGroupFile     $HTTPD_GROUPS
  Require           group random
  AuthzSVNAuthoritative Off
  SVNPathAuthz On
</Location>
<IfModule mod_authz_core.c>
  <Location /authz-test-work/sallrany>
__EOF__
location_common
cat >> "$HTTPD_CFG" <<__EOF__
    SVNParentPath     "$ABS_BUILDDIR/subversion/tests/cmdline/svn-test-work/local_tmp"
    AuthzSendForbiddenOnFailure On
    Satisfy All
    <RequireAny>
      Require valid-user
      Require expr req('ALLOW') == '1'
    </RequireAny>
    ${SVN_PATH_AUTHZ_LINE}
  </Location>
  <Location /authz-test-work/sallrall>
__EOF__
location_common
cat >> "$HTTPD_CFG" <<__EOF__
    SVNParentPath     "$ABS_BUILDDIR/subversion/tests/cmdline/svn-test-work/local_tmp"
    AuthzSendForbiddenOnFailure On
    Satisfy All
    <RequireAll>
      Require valid-user
      Require expr req('ALLOW') == '1'
    </RequireAll>
    ${SVN_PATH_AUTHZ_LINE}
  </Location>
</IfModule>
RedirectMatch permanent ^/svn-test-work/repositories/REDIRECT-PERM-(.*)\$ /svn-test-work/repositories/\$1
RedirectMatch           ^/svn-test-work/repositories/REDIRECT-TEMP-(.*)\$ /svn-test-work/repositories/\$1
__EOF__


# Our configure script extracts the HTTPD version from
# headers. However, that may not be the same as the runtime version;
# an example of this discrepancy occurs on OSX 1.9.5, where the
# headers report 2.2.26 but the server reports 2.2.29. Since our tests
# use the version to interpret test case results, use the actual
# runtime version here to avoid spurious test failures.
HTTPD_VERSION=$("$HTTPD" -V -f $HTTPD_CFG | grep '^Server version:' | sed 's|^.*/\([0-9]*\.[0-9]*\.[0-9]*\).*$|\1|')

START="$HTTPD -f $HTTPD_CFG"
printf \
'#!/bin/sh
if [ -d "%s" ]; then
  printf "Stopping previous HTTPD instance..."
  if %s -k stop; then
    # httpd had no output; echo a newline.
    echo ""
  elif [ -s "%s" ]; then
    # httpd would have printed an error terminated by a newline.
    kill -9 "`cat %s`"
  fi
fi
' >$STOPSCRIPT "$HTTPD_ROOT" "$START" "$HTTPD_PID" "$HTTPD_PID"
chmod +x $STOPSCRIPT

$START -t > /dev/null \
  || fail "Configuration file didn't pass the check, most likely modules couldn't be loaded"

# need to pause for some time to let HTTPD start
$START &
sleep 2

say "HTTPD $HTTPD_VERSION started and listening on '$BASE_URL'..."
#query "Ready" "y"

# Perform a trivial validation of our httpd configuration by
# downloading a file and comparing it to the original copy.
### The file at the path "/cfg" can't be retrieved from Apache 2.3+.
### We get a 500 ISE, with the following error in the log from httpd's
### server/request.c:ap_process_request_internal():
###   [Wed Feb 22 13:06:55 2006] [crit] [client 127.0.0.1] configuration error:  couldn't check user: /cfg
HTTP_FETCH=wget
HTTP_FETCH_OUTPUT="--no-check-certificate -q -O"
type wget > /dev/null 2>&1
if [ $? -ne 0 ]; then
  type curl > /dev/null 2>&1
  if [ $? -ne 0 ]; then
    fail "Neither curl or wget found."
  fi
  HTTP_FETCH=curl
  HTTP_FETCH_OUTPUT='-s -k -o'
fi
$HTTP_FETCH $HTTP_FETCH_OUTPUT "$HTTPD_CFG-copy" "$BASE_URL/cfg"
diff "$HTTPD_CFG" "$HTTPD_CFG-copy" > /dev/null \
  || fail "HTTPD doesn't operate according to the generated configuration"
rm "$HTTPD_CFG-copy"

say "HTTPD is good"

if [ $# -eq 1 ] && [ "x$1" = 'x--no-tests' ]; then
  echo "http://localhost:$HTTPD_PORT/svn-test-work/repositories"
  exit
fi

if [ $# -eq 1 ] && [ "x$1" = 'x--lldb' ]; then
  echo "http://localhost:$HTTPD_PORT/svn-test-work/repositories"
  $STOPSCRIPT && lldb --one-line=run -- $START -X
  exit
fi

if [ $# -eq 1 ] && [ "x$1" = 'x--gdb' ]; then
  echo "http://localhost:$HTTPD_PORT/svn-test-work/repositories"
  $STOPSCRIPT && gdb -silent -ex r -args $START -X
  exit
fi

if type time > /dev/null ; then TIME_CMD() { time "$@"; } ; else TIME_CMD() { "$@"; } ; fi

MAKE=${MAKE:-make}

say "starting the tests..."

CLIENT_CMD="$ABS_BUILDDIR/subversion/svn/svn"

if [ "$HTTP_LIBRARY" = "" ]; then
  say "Using default dav library"
  "$CLIENT_CMD" --version | egrep '^[*] ra_(neon|serf)' >/dev/null \
    || fail "Subversion client couldn't find and/or load ra_dav library"
else
  say "Requesting dav library '$HTTP_LIBRARY'"
  "$CLIENT_CMD" --version | egrep "^[*] ra_$HTTP_LIBRARY" >/dev/null \
    || fail "Subversion client couldn't find and/or load ra_dav library '$HTTP_LIBRARY'"
fi

if [ $# = 0 ]; then
  TIME_CMD "$MAKE" check "BASE_URL=$BASE_URL" "HTTPD_VERSION=$HTTPD_VERSION" $SSL_MAKE_VAR
  r=$?
else
  (cd "$ABS_BUILDDIR/subversion/tests/cmdline/"
  TEST="$1"
  shift
  TIME_CMD "$ABS_SRCDIR/subversion/tests/cmdline/${TEST}_tests.py" "--url=$BASE_URL" "--httpd-version=$HTTPD_VERSION" $SSL_TEST_ARG "$@")
  r=$?
fi

say "Finished testing..."

kill $(cat "$HTTPD_PID")

query 'Browse server access log' n \
  && less "$HTTPD_ACCESS_LOG"

query 'Browse server error log' n \
  && less "$HTTPD_ERROR_LOG"

query 'Delete HTTPD root directory' y \
  && rm -fr "$HTTPD_ROOT/"

say 'Done'

exit $r
