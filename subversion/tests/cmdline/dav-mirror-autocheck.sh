#!/bin/bash
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
# Script to automate testing of an svnsync master/slave
# configuration.  Commits to the slave should write through
# to the master, and the master's post-commit hook svnsync's
# to the slave.  The test should be able to throw all kinds
# of svn operations at one or the other, and master/slave
# verified as identical in the end.
#
# Master / slave setup is achieved in a single httpd process
# using virtual hosts bound to different addresses on the
# loopback network (127.0.0.1, 127.0.0.2) for slave and
# master, respectively.
#
# The changes sent through the system started as the reproduction
# recipe for issue 2939 (https://issues.apache.org/jira/browse/SVN-2939,
# using svnmucc) and have grown to cover URI-encoded locations,
# COPY/MOVE Destination rewriting, the SVN-3445 payload-corruption
# regressions, proxied reads of transaction resources (including
# dead-property values and location-name collisions in rewritten
# hrefs), HTTPv1 MKACTIVITY/CHECKOUT, revision-property changes
# through the proxy, and locks.  Any svn traffic liable to break
# over mirroring remains a good addition.
#
# Most of the httpd setup was lifted from davautocheck.sh.
# The common boilerplate snippets to setup/start/stop httpd
# between the two could be factored out and shared.
#

SCRIPTDIR=$(dirname $0)
SCRIPT=$(basename $0)

trap stop_httpd_and_die SIGHUP SIGTERM SIGINT

# Ensure the server uses a known locale.
LC_ALL=C
export LC_ALL

function stop_httpd_and_die() {
  [ -e "$HTTPD_PID" ] && kill $(cat "$HTTPD_PID")
  exit 1
}

function say() {
  echo "$SCRIPT: $*"
}

function fail() {
  say "FAIL: " $*
  stop_httpd_and_die
}

# Authenticated curl against the test repositories.  CURL is resolved
# later; the functions below expand it at call time.
function curl_auth() {
  $CURL --silent --show-error --user jrandom:rayjandom "$@"
}

# Open a transaction on the master (raw HTTPv2 create-txn POST) and print
# its SVN-Txn-Name.  Used by probes that must read in-txn data through the
# slave; committed reads are served locally and never hit the rewrite.
function create_master_txn() {
  curl_auth \
    --request POST \
    --header "Content-Type: application/vnd.svn-skel" --data "( create-txn )" \
    --dump-header - --output /dev/null "$MASTER_URL/!svn/me" \
    | sed -ne 's/^SVN-Txn-Name: *//p' | tr -d '\r'
}

function delete_master_txn() {
  curl_auth --request DELETE --output /dev/null \
    "$MASTER_URL/!svn/txn/$1" \
    || say "WARNING: could not delete test txn $1 (continuing)" >&2
}

# PROPFIND PATH in a fresh master txn through the slave.  Stores the
# multistatus in DEST (a variable name) and leaves TXN_NAME set so
# callers can assert on the txn id in hrefs.  WHAT is a short label
# for the fail message.  Assigns in this shell so TXN_NAME survives
# (command substitution would lose it).
function propfind_slave_txr() {
  local dest="$1"
  local path="$2"
  local what="$3"
  TXN_NAME=$(create_master_txn)
  [ -n "$TXN_NAME" ] || fail "could not create a txn on the master${what:+ ($what)}"
  printf -v "$dest" '%s' "$(curl_auth --request PROPFIND --header "Depth: 0" \
    "$SLAVE_URL/!svn/txr/$TXN_NAME/$path")"
  delete_master_txn "$TXN_NAME"
}

function get_loadmodule_config() {
  local SO="$($APXS -q LIBEXECDIR)/$1.so"

  # shared object module?
  if [ -r "$SO" ]; then
    local NM=$(echo "$1" | sed 's|mod_\(.*\)|\1_module|')
    echo "LoadModule $NM \"$SO\"" &&
    return
  fi

  # maybe it's built-in?
  "$HTTPD" -l | grep -q "$1\\.c" && return

  return 1
}


# Check apxs's SBINDIR and BINDIR for given program names
function get_prog_name() {
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

# splat out httpd config
function setup_config() {

  say "setting up config: " $1
cat > "$1" <<__EOF__
$LOAD_MOD_MPM
$LOAD_MOD_LOG_CONFIG
$LOAD_MOD_MIME
$LOAD_MOD_UNIXD
$LOAD_MOD_DAV
LoadModule          dav_svn_module "$MOD_DAV_SVN"
$LOAD_MOD_AUTH
$LOAD_MOD_AUTHN_CORE
$LOAD_MOD_AUTHN_FILE
$LOAD_MOD_PROXY
$LOAD_MOD_PROXY_HTTP
$LOAD_MOD_AUTHZ_CORE
$LOAD_MOD_AUTHZ_USER
$LOAD_MOD_AUTHZ_HOST

__EOF__

if "$HTTPD" -v | grep '/2\.[012]' >/dev/null; then
  cat >> "$1" <<__EOF__
LockFile            lock
User                $(id -un)
Group               $(id -gn)
__EOF__
else
HTTPD_LOCK="$HTTPD_ROOT/lock"
mkdir "$HTTPD_LOCK" \
  || fail "couldn't create lock directory '$HTTPD_LOCK'"
  cat >> "$1" <<__EOF__
# worker and prefork MUST have a mpm-accept lockfile in 2.3.0+
<IfModule worker.c>
  Mutex "file:$HTTPD_LOCK" mpm-accept
</IfModule>
<IfModule prefork.c>
  Mutex "file:$HTTPD_LOCK" mpm-accept
</IfModule>
__EOF__
fi

cat >> "$1" <<__EOF__
Listen              ${TEST_PORT}
ServerName          localhost
PidFile             "${HTTPD_ROOT}/pid"
LogFormat           "%h %l %u %t \"%r\" %>s %b" common
CustomLog           "${HTTPD_ROOT}/access_log" common
ErrorLog            "${HTTPD_ROOT}/error_log"
LogLevel            Debug
ServerRoot          "${HTTPD_ROOT}"
DocumentRoot        "${HTTPD_ROOT}"
CoreDumpDirectory   "${HTTPD_ROOT}"
TypesConfig         "${HTTPD_ROOT}/mime.types"
StartServers        4
MaxRequestsPerChild 0
<IfModule worker.c>
  ThreadsPerChild   8
</IfModule>
<IfModule event.c>
  ThreadsPerChild   8
</IfModule>
MaxClients          16
HostNameLookups     Off
LogFormat           "%h %l %u %t \"%r\" %>s %b \"%{Referer}i\" \"%{User-Agent}i\"" format
CustomLog           "${HTTPD_ROOT}/req" format
CustomLog           "${HTTPD_ROOT}/ops" "%t %u %{SVN-REPOS-NAME}e %{SVN-ACTION}e" env=SVN-ACTION

<Directory />
  AllowOverride     none
</Directory>
<Directory "${HTTPD_ROOT}">
  AllowOverride     none
  #Require           all granted
</Directory>

# slave
<VirtualHost ${SLAVE_HOST}>
  ServerName ${SLAVE_HOST}
  CustomLog           "${HTTPD_ROOT}/slave_access_log" common
  ErrorLog            "${HTTPD_ROOT}/slave_error_log"
# slave 'normal' location
  <Location "/${SLAVE_LOCATION}">
    DAV               svn
    SVNPath           "${SLAVE_REPOS}"
    # Deliberate trailing slash: SVNMasterURI must be canonicalized at parse time.
    SVNMasterURI      "${MASTER_URL}/"
    AuthType          Basic
    AuthName          "Subversion Repository"
    AuthUserFile      ${HTTPD_ROOT}/users
    Require           valid-user
  </Location>
# slave 'sync' location
  <Location "/${SYNC_LOCATION}">
   DAV svn
   SVNPath "${SLAVE_REPOS}"
   AuthType Basic
   AuthName "Slave Sync Repository"
   AuthUserFile ${HTTPD_ROOT}/users
   Require valid-user
</Location>
</VirtualHost>

# master
<VirtualHost ${MASTER_HOST}>
  ServerName ${MASTER_HOST}
  CustomLog           "${HTTPD_ROOT}/master_access_log" common
  ErrorLog            "${HTTPD_ROOT}/master_error_log"
  <Location "/${MASTER_LOCATION}">
    DAV               svn
    SVNPath           "${MASTER_REPOS}"
    AuthType          Basic
    AuthName          "Subversion Repository"
    AuthUserFile      ${HTTPD_ROOT}/users
    Require           valid-user
  </Location>
</VirtualHost>
__EOF__
}

function usage() {
  echo "usage: $SCRIPT <test-work-directory>" 1>&2
  echo "  e.g. \"$SCRIPT /tmp/test-work\"" 1>&2
  echo
  echo " " '<test-work-directory>' must not exist, \
    I will not clobber it for you 1>&2
  exit 1
}
### Start execution here ###

SCRIPT=$(basename $0)

NO_TESTS=
if [ "x$1" = 'x--no-tests' ]; then
  NO_TESTS=1
  shift
fi

if [ $# -ne 1 ] ; then
  usage
fi


# httpd ServerRoot, all test and runtime artifacts below here
# verify that this doesn't already exist - don't clobber
HTTPD_ROOT=$1

if [ -e "$HTTPD_ROOT" ] ; then
  say "ERROR: test work directory $HTTPD_ROOT already exists, please remove" 1>&2
  usage
fi

#set -e

# Don't assume sbin is in the PATH.
PATH="$PATH:/usr/sbin:/usr/local/sbin"

# Pick up value from environment or PATH (also try apxs2 - for Debian)
[ ${APXS:+set} ] \
 || APXS=$(which apxs) \
 || APXS=$(which apxs2) \
 || fail "neither apxs or apxs2 found - required to run $SCRIPT"

[ -x $APXS ] || fail "Can't execute apxs executable $APXS"

say APXS: $APXS

if [ -x subversion/svn/svn ]; then
  ABS_BUILDDIR=$(pwd)
elif [ -x $SCRIPTDIR/../../svn/svn ]; then
  pushd $SCRIPTDIR/../../../ >/dev/null
  ABS_BUILDDIR=$(pwd)
  popd >/dev/null
else
  fail "Run this script from the root of Subversion's build tree!"
fi

# find all our needed executables, in WC or via apxs
httpd="$($APXS -q PROGNAME)"
HTTPD=$(get_prog_name $httpd) || fail "HTTPD not found"
HTPASSWD=$(get_prog_name htpasswd htpasswd2) \
  || fail "Could not find htpasswd or htpasswd2"
SVN=$ABS_BUILDDIR/subversion/svn/svn
SVNADMIN=$ABS_BUILDDIR/subversion/svnadmin/svnadmin
SVNSYNC=$ABS_BUILDDIR/subversion/svnsync/svnsync
SVNMUCC=$ABS_BUILDDIR/subversion/svnmucc/svnmucc
SVNLOOK=$ABS_BUILDDIR/subversion/svnlook/svnlook

[ -x $HTTPD ] || fail "HTTPD '$HTTPD' not executable"
[ -x $HTPASSWD ] \
  || fail "HTPASSWD '$HTPASSWD' not executable"
[ -x $SVN ] || fail "SVN $SVN not built"
[ -x $SVNADMIN ] || fail "SVNADMIN $SVNADMIN not built"
[ -x $SVNSYNC ] || fail "SVNSYNC $SVNSYNC not built"
[ -x $SVNLOOK ] || fail "SVNLOOK $SVNLOOK not built"
[ -x $SVNMUCC ] || fail "SVNMUCC $SVNMUCC not built"

CURL=$(which curl) || fail "curl not found - required for proxied-read tests"

say HTTPD: $HTTPD
say SVN: $SVN
say SVNADMIN: $SVNADMIN
say SVNSYNC: $SVNSYNC
say SVNLOOK: $SVNLOOK
say SVNMUCC: $SVNMUCC

LOAD_MOD_DAV=$(get_loadmodule_config mod_dav) \
  || fail "DAV module not found"

LOAD_MOD_LOG_CONFIG=$(get_loadmodule_config mod_log_config) \
  || fail "log_config module not found"

# proxy needed for svnsync mirroring
LOAD_MOD_PROXY=$(get_loadmodule_config mod_proxy) \
  || fail "proxy module not found"
LOAD_MOD_PROXY_HTTP=$(get_loadmodule_config mod_proxy_http) \
  || fail "proxy_http module not found"

# needed for TypesConfig
LOAD_MOD_MIME=$(get_loadmodule_config mod_mime) \
  || fail "MIME module not found"

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
LOAD_MOD_AUTHZ_HOST="$(get_loadmodule_config mod_authz_host)" \
    || fail "Authz_Host module not found."
LOAD_MOD_UNIXD=$(get_loadmodule_config mod_unixd) \
    || fail "UnixD module not found"
}
LOAD_MOD_AUTHN_FILE="$(get_loadmodule_config mod_authn_file)" \
    || fail "Authn_File module not found."
LOAD_MOD_AUTHZ_USER="$(get_loadmodule_config mod_authz_user)" \
    || fail "Authz_User module not found."
}
if [ ${APACHE_MPM:+set} ]; then
    LOAD_MOD_MPM=$(get_loadmodule_config mod_mpm_$APACHE_MPM) \
      || fail "MPM module not found"
fi

if [ ${MODULE_PATH:+set} ]; then
    MOD_DAV_SVN="$MODULE_PATH/mod_dav_svn.so"
    MOD_AUTHZ_SVN="$MODULE_PATH/mod_authz_svn.so"
else
    MOD_DAV_SVN="$ABS_BUILDDIR/subversion/mod_dav_svn/.libs/mod_dav_svn.so"
    MOD_AUTHZ_SVN="$ABS_BUILDDIR/subversion/mod_authz_svn/.libs/mod_authz_svn.so"
fi

[ -r "$MOD_DAV_SVN" ] \
  || fail "dav_svn_module not found, please use '--enable-shared --enable-dso --with-apxs' with your 'configure' script"
[ -r "$MOD_AUTHZ_SVN" ] \
  || fail "authz_svn_module not found, please use '--enable-shared --enable-dso --with-apxs' with your 'configure' script"

export LD_LIBRARY_PATH="$ABS_BUILDDIR/subversion/libsvn_ra_local/.libs:$ABS_BUILDDIR/subversion/libsvn_ra_svn/.libs:$LD_LIBRARY_PATH"

MASTER_REPOS="${MASTER_REPOS:-"$HTTPD_ROOT/master_repos"}"
SLAVE_REPOS="${SLAVE_REPOS:-"$HTTPD_ROOT/slave_repos"}"

MASTER_HOST=127.0.0.2
SLAVE_HOST=127.0.0.1
#TEST_PORT=11111
TEST_PORT=$(($RANDOM+1024))

# Location directive elements for master, slave, and sync.  Master and slave
# deliberately use DIFFERENT locations so the proxy actually has to translate
# paths (slave root_dir <-> master URI), locations of DIFFERENT LENGTHS so a
# byte-level rewrite of versioned payload corrupts the svndiff framing loudly
# instead of passing silently (SVN-3445), and locations containing a
# URI-escapable character (a space) so every test exercises the encoding of
# both location paths end to end.  The *_LOCATION_URI forms are encoded (they go
# into URLs and comparisons against wire data).
MASTER_LOCATION="master loc"
MASTER_LOCATION_URI="master%20loc"
SLAVE_LOCATION="slave loc"
SLAVE_LOCATION_URI="slave%20loc"
SYNC_LOCATION="sync"

MASTER_URL="http://${MASTER_HOST}:${TEST_PORT}/${MASTER_LOCATION_URI}"
SLAVE_URL="http://${SLAVE_HOST}:${TEST_PORT}/${SLAVE_LOCATION_URI}"
SYNC_URL="http://${SLAVE_HOST}:${TEST_PORT}/${SYNC_LOCATION}"

# User-data payloads that include the rewrite-anchor tag plus a location
# root -- the byte sequence the body filter matches.  Protocol XML
# escapes this; file content and skels carry it raw.
HREF_IN_MASTER="<D:href>/${MASTER_LOCATION_URI}"
HREF_IN_SLAVE="<D:href>/${SLAVE_LOCATION_URI}"

BASE_URL="$SLAVE_URL"

# setup server and repositories
say "setting up in ${HTTPD_ROOT}:"
mkdir -p $HTTPD_ROOT || fail "cannot mkdir $HTTPD_ROOT"
HTTPD_CONFIG=$HTTPD_ROOT/cfg
setup_config $HTTPD_CONFIG
touch $HTTPD_ROOT/mime.types
HTTPD_USERS="$HTTPD_ROOT/users"
$HTPASSWD -bc $HTTPD_USERS jrandom   rayjandom
$HTPASSWD -b  $HTTPD_USERS jconstant rayjandom
$HTPASSWD -b  $HTTPD_USERS scm scm
$HTPASSWD -b  $HTTPD_USERS svnsync svnsync
$SVNADMIN create "$MASTER_REPOS" || fail "create master repos failed"
$SVNADMIN create "$SLAVE_REPOS" || fail "create slave repos failed"
# dup them
$SVNADMIN dump "$MASTER_REPOS" | $SVNADMIN load "$SLAVE_REPOS" \
  || fail "duplicate repositories failed"
# make sure uuid's match
read MASTER_UUID < "$MASTER_REPOS/db/uuid"
read SLAVE_UUID < "$SLAVE_REPOS/db/uuid"
[ "$SLAVE_UUID" = "$MASTER_UUID" ] \
  || fail "master/slave uuid mismatch"
# setup hooks:
#  slave and master allow revprop changes (the latter so a proxied
#  svn propset --revprop can succeed)
#  master syncs changes to slave
echo "#!/bin/sh" > "$SLAVE_REPOS/hooks/pre-revprop-change"
echo "#!/bin/sh" > "$MASTER_REPOS/hooks/pre-revprop-change"
echo "#!/bin/sh" > "$MASTER_REPOS/hooks/post-revprop-change"
echo "#!/bin/sh" > "$MASTER_REPOS/hooks/post-commit"
echo "$SVNSYNC --non-interactive sync '$SYNC_URL' --username=svnsync --password=svnsync" \
    >> "$MASTER_REPOS/hooks/post-revprop-change"
echo "$SVNSYNC --non-interactive sync '$SYNC_URL' --username=svnsync --password=svnsync" \
    >> "$MASTER_REPOS/hooks/post-commit"

chmod 0755 "$SLAVE_REPOS/hooks/pre-revprop-change"
chmod 0755 "$MASTER_REPOS/hooks/pre-revprop-change"
chmod 0755 "$MASTER_REPOS/hooks/post-revprop-change"
chmod 0755 "$MASTER_REPOS/hooks/post-commit"

say "created master and slave repositories"

# test config
$HTTPD -f $HTTPD_CONFIG -t || fail "httpd config failure in $HTTPD_CONFIG"

# start httpd
echo -n "${SCRIPT}: starting httpd: "
$HTTPD -f $HTTPD_CONFIG -k start || fail "httpd start failed"
echo "."
say initializing svnsync to $SYNC_URL
HTTPD_PID=$HTTPD_ROOT/pid
$SVNSYNC initialize --non-interactive "$SYNC_URL" "$MASTER_URL" \
    --username=svnsync --password=svnsync \
    || fail "svnsync initialize failed"

if [ $NO_TESTS ]; then
  echo "MASTER_URL=$MASTER_URL"
  echo "SLAVE_URL=$SLAVE_URL"
  exit
fi

# OK, let's start testing! Commit changes to slave, expect
# them to proxy through to the master, and then
# svnsync back to the slave
#
# reproducible test case from:
# https://issues.apache.org/jira/browse/SVN-2939
#
BASE_URL="$SLAVE_URL"
say running svnmucc test to $BASE_URL
svnmucc="$SVNMUCC --non-interactive --username jrandom --password rayjandom -mm"

$svnmucc mkdir "$BASE_URL/trunk" mkdir "$BASE_URL/trunk/dir1" mkdir "$BASE_URL/trunk/dir1/dir2"
$svnmucc rm "$BASE_URL/trunk/dir1/dir2"
$svnmucc cp 2 "$BASE_URL/trunk" "$BASE_URL/branch" put /dev/null "$BASE_URL/branch/dir1/dir2"
$svnmucc rm "$BASE_URL/branch" cp 2 "$BASE_URL/trunk" "$BASE_URL/branch" put /dev/null "$BASE_URL/branch/dir1/dir2"

say "svn log on $BASE_URL : "
$SVN --username jrandom --password rayjandom log -vq "$BASE_URL"


# verify result: should be at rev 4 in both repos
# FIXME: do more rigorous verification here
MASTER_HEAD=`$SVNLOOK youngest "$MASTER_REPOS"`
SLAVE_HEAD=`$SVNLOOK youngest "$SLAVE_REPOS"`

say checking consistency of master, slave repositories:

if [ "$MASTER_HEAD" != "4" ] || [ "$SLAVE_HEAD" != "4" ] ;
then
  say FAIL: master, slave are at rev $MASTER_HEAD, $SLAVE_HEAD, not 4
  say server may be started/stopped manually with:
  say "  $HTTPD -f $HTTPD_CONFIG -k start|stop"
  fail charred remains in $HTTPD_ROOT for your perusal
fi

say "PASS: master, slave are both at r4, as expected"

# Encoding regression coverage (originally r917523, see
# http://svn.haxx.se/dev/archive-2011-03/0641.shtml). A path containing a
# space must survive the proxy's URI rewriting encoded exactly once: the
# COPY below sends a Destination header that proxy_request_fixup_destination()
# must translate from the slave location to the master URI without re-encoding
# the already-encoded "%20" (the bug double-encoded it to "%2520", silently
# creating a path literally named "branch%20new"). The follow-up PUT
# exercises the same encoding in the request-body location filter.
say "Test case for encoding of paths with a space (regression r917523)"

$svnmucc cp 2 "$BASE_URL/trunk" "$BASE_URL/branch new" \
  || fail "COPY to a path with a space failed (Destination mis-encoded?)"
$svnmucc put /dev/null "$BASE_URL/branch new/file" \
  || fail "PUT under a path with a space failed (request body mis-encoded?)"

# A successful commit isn't enough: a double-encoded Destination still commits,
# but lands as "branch%20new" on the master. Assert the decoded name instead.
$SVNLOOK tree --full-paths "$MASTER_REPOS" | grep -Fq "branch new/" \
  || fail "path with a space was double-encoded by the proxy (expected 'branch new', got '%20'-escaped name on master)"

say "PASS: committing a path which has a space in it passes"

# An explicit move is a COPY plus a DELETE of the source within one txn.
# The Destination rewrite is the same path as the space-in-name COPY
# above; this checks the pair lands as a move on the master.
say "Test case for move (COPY + DELETE) through the proxy"

$svnmucc mkdir "$BASE_URL/move-src" \
  || fail "creating move source failed"
$svnmucc mv "$BASE_URL/move-src" "$BASE_URL/move-src-moved" \
  || fail "move through the proxy failed"
$SVNLOOK tree --full-paths "$MASTER_REPOS" | grep -Fq "move-src-moved/" \
  || fail "moved directory missing on the master"
$SVNLOOK tree --full-paths "$MASTER_REPOS" | grep -Fq "move-src/" \
  && fail "move source still present on the master"
say "PASS: move (copy + delete) works through the proxy"

# Regression coverage for SVN-3445.  When the master and slave locations
# differ, the proxy must translate the location prefix in *protocol* URLs
# (hrefs in MERGE/CHECKOUT bodies) but must NOT touch *user payload*:
# svndiff-encoded file content in PUT bodies, property values in
# PROPPATCH bodies, and log-message/revprop skels in POST bodies.
say "Test case for versioned content munging (SVN-3445)"

# File content must round-trip verbatim whether it embeds the master or
# slave rewrite-anchor sequence (used to be mangled/rejected).
echo "$HREF_IN_MASTER" > "$HTTPD_ROOT/master-url.txt"
echo "$HREF_IN_SLAVE"  > "$HTTPD_ROOT/slave-url.txt"
$svnmucc put "$HTTPD_ROOT/master-url.txt" "$BASE_URL/master-url.txt" \
         put "$HTTPD_ROOT/slave-url.txt"  "$BASE_URL/slave-url.txt" \
  || fail "committing href-bearing files failed (SVN-3445: PUT body rewritten?)"

master_file_content=$($SVNLOOK cat "$SLAVE_REPOS" master-url.txt)
[ "$master_file_content" = "$HREF_IN_MASTER" ] \
  || fail "file content embedding the master href tag was munged: committed '$HREF_IN_MASTER', slave stores '$master_file_content'"
slave_file_content=$($SVNLOOK cat "$SLAVE_REPOS" slave-url.txt)
[ "$slave_file_content" = "$HREF_IN_SLAVE" ] \
  || fail "file content embedding the slave href tag was munged: committed '$HREF_IN_SLAVE', slave stores '$slave_file_content'"
say "PASS: file content is preserved verbatim regardless of embedded href tag"

# Property values must likewise round-trip verbatim: a PROPPATCH value that
# contains the rewrite-anchor sequence used to be silently rewritten.
$svnmucc propset svn-3445-prop "$HREF_IN_SLAVE" "$BASE_URL/slave-url.txt" \
  || fail "propset of a value containing the slave href tag failed"
prop_value=$($SVNLOOK propget "$SLAVE_REPOS" svn-3445-prop slave-url.txt)
[ "$prop_value" = "$HREF_IN_SLAVE" ] \
  || fail "property value embedding the slave href tag was munged: set '$HREF_IN_SLAVE', slave stores '$prop_value'"
say "PASS: property value is preserved verbatim regardless of embedded href tag"

# Commit log messages and revision properties travel inside the
# create-txn-with-props POST body (HTTPv2), a length-prefixed skel
# carrying only user data, never protocol URLs.  Rewriting that body
# corrupts the values, and when the location paths differ in length it
# breaks the skel framing outright.  Both must round-trip verbatim.
# (The $svnmucc wrapper bakes in -mm, so invoke $SVNMUCC directly.)
log_msg="log mentioning the slave href tag: $HREF_IN_SLAVE"
$SVNMUCC --non-interactive --username jrandom --password rayjandom \
         -m "$log_msg" --with-revprop "svn-3445-revprop=$HREF_IN_SLAVE" \
         mkdir "$BASE_URL/log-url-dir" \
  || fail "commit with a log message containing the slave href tag failed (POST body rewritten?)"
rev=$($SVNLOOK youngest "$SLAVE_REPOS")
stored_log=$($SVNLOOK propget --revprop -r "$rev" "$SLAVE_REPOS" svn:log)
[ "$stored_log" = "$log_msg" ] \
  || fail "log message was munged: committed '$log_msg', slave stores '$stored_log'"
stored_revprop=$($SVNLOOK propget --revprop -r "$rev" "$SLAVE_REPOS" svn-3445-revprop)
[ "$stored_revprop" = "$HREF_IN_SLAVE" ] \
  || fail "revprop value was munged: set '$HREF_IN_SLAVE', slave stores '$stored_revprop'"
say "PASS: log message and revprop values are preserved verbatim"

# PROPPATCH on !svn/rev is a different path from the create-txn-with-props
# POST above: the body is an opaque property value and must stay out of
# the rewrite.  The master's pre-revprop-change hook is enabled above
# so this can succeed.
say "Test case for revision-property change through the proxy"

rev=$($SVNLOOK youngest "$MASTER_REPOS")
$SVN propset --revprop -r "$rev" --non-interactive \
     --username jrandom --password rayjandom \
     test:revprop "url is $HREF_IN_SLAVE" "$SLAVE_URL" \
  || fail "revprop change through the proxy failed"
stored_rp=$($SVNLOOK propget --revprop -r "$rev" "$MASTER_REPOS" test:revprop)
[ "$stored_rp" = "url is $HREF_IN_SLAVE" ] \
  || fail "revprop value was munged through the proxy: stored '$stored_rp'"
say "PASS: revision property change through the proxy is stored verbatim"

# Response-side SVN-3445 coverage: reads of transaction resources are
# proxied to the master; the proxy must rewrite hrefs in protocol XML
# (PROPFIND multistatus) but leave file content untouched.
#
# These probes must be raw HTTP (curl), not svn commands, at both ends:
# no svn client operation creates a commit transaction and leaves it open
# (svn/svnmucc create, commit and abort their txns within one operation),
# and no svn command can address the private !svn/txr/ URIs.  An
# 'svn cat' of the public URL would be served locally by the slave
# without ever traversing the proxy or the response filter under test.
say "Test case for proxied reads of txn resources (SVN-3445 response side)"

# Commit an XML-mime-typed file for the GET probe below (before the txn is
# opened, so the txn tree contains it).
printf '<?xml version="1.0"?>\n<note><!-- %s --></note>\n' "$HREF_IN_MASTER" \
  > "$HTTPD_ROOT/xml-payload.xml"
$svnmucc put "$HTTPD_ROOT/xml-payload.xml" "$BASE_URL/xml-payload.xml" \
         propset svn:mime-type text/xml "$BASE_URL/xml-payload.xml" \
  || fail "committing the XML-typed payload file failed"

txn_name=$(create_master_txn)
[ -n "$txn_name" ] || fail "could not create a txn on the master"

# Non-XML payload: GET the file through the SLAVE. The response is
# proxied from the master and must arrive verbatim, the slave href
# tag embedded in the content must NOT have been rewritten.
proxied_get=$(curl_auth "$SLAVE_URL/!svn/txr/$txn_name/slave-url.txt")
[ "$proxied_get" = "$HREF_IN_SLAVE" ] \
  || fail "proxied GET of txn file content was munged: expected '$HREF_IN_SLAVE', got '$proxied_get'"

# Regression guard: a versioned file whose svn:mime-type is XML must
# never have its content rewritten on a proxied read.  Doubly protected:
# GET responses do not receive the ReposRewrite body filter at all
# (proxy_request_fixup() attaches it only for MERGE and PROPFIND), and
# mod_dav_svn happens to emit no Content-Type for !svn/txr file GETs.
proxied_xml_get=$(curl_auth "$SLAVE_URL/!svn/txr/$txn_name/xml-payload.xml")
echo "$proxied_xml_get" | grep -qF "$HREF_IN_MASTER" \
  || fail "XML-typed txn file content did not round-trip a proxied read verbatim: got '$proxied_xml_get'"
delete_master_txn "$txn_name"
say "PASS: XML-typed txn file content survives a proxied read verbatim"

# Protocol XML: PROPFIND on the txn file through the SLAVE. The
# multistatus hrefs come from the master and MUST be rewritten to the
# slave location.
propfind_slave_txr proxied_propfind "slave-url.txt" "proxied reads"
echo "$proxied_propfind" | grep -qF "/${SLAVE_LOCATION_URI}/" \
  || fail "proxied PROPFIND multistatus hrefs were not rewritten to the slave location: $proxied_propfind"
echo "$proxied_propfind" | grep -qF "/${MASTER_LOCATION_URI}/" \
  && fail "proxied PROPFIND multistatus still contains master-location hrefs: $proxied_propfind"

say "PASS: proxied txn reads: content verbatim, protocol hrefs rewritten"

# Dead-property values in a proxied PROPFIND must survive verbatim while
# the surrounding hrefs are translated.  Needs a proxied read of in-txn
# data (committed reads are served locally and never hit the filter).
say "Test case for dead-property values in a proxied multistatus"

$svnmucc propset url-bearing-prop "$HREF_IN_MASTER" "$BASE_URL/master-url.txt" \
  || fail "propset of a master href-tag value failed"

propfind_slave_txr deadprop_propfind "master-url.txt" "dead-property test"
# The value is XML-escaped in the multistatus; the raw tag must not appear
# as a rewritten protocol href.
echo "$deadprop_propfind" | grep -qF "&lt;D:href&gt;/${MASTER_LOCATION_URI}" \
  || fail "dead-property value did not survive the proxied PROPFIND verbatim: $deadprop_propfind"
echo "$deadprop_propfind" | grep -qF "<D:href>/${SLAVE_LOCATION_URI}/" \
  || fail "multistatus href was not translated to the slave location: $deadprop_propfind"
say "PASS: dead-property values survive proxied multistatus href translation"

# A repos path component named like the master location must survive
# root translation in proxied multistatus hrefs.
say "Test case for location-name collision in proxied hrefs"

$svnmucc mkdir "$BASE_URL/${MASTER_LOCATION_URI}" \
  || fail "committing a directory named after the master location failed"
$SVNLOOK tree --full-paths "$MASTER_REPOS" | grep -Fq "${MASTER_LOCATION}/" \
  || fail "directory named after the master location missing on the master"

propfind_slave_txr collision_propfind "${MASTER_LOCATION_URI}" "collision test"
echo "$collision_propfind" | grep -qF "/$TXN_NAME/${MASTER_LOCATION_URI}" \
  || fail "href component named after the master location did not survive the proxied PROPFIND: $collision_propfind"
echo "$collision_propfind" | grep -qF "<D:href>/${SLAVE_LOCATION_URI}/" \
  || fail "collision multistatus href root was not translated to the slave location: $collision_propfind"
say "PASS: href components named after a location survive the anchored rewrite"

# The HTTPv1 commit opening, emulated with curl (no modern client speaks
# it, but mod_dav_svn still serves it): MKACTIVITY names an activity, and
# CHECKOUT of a version resource carries that activity's href -- as
# <D:href> in the request body -- which the request-side filter must
# translate for the master to resolve it.  The 201 itself proves the body
# translation (an untranslated activity href cannot resolve on the
# master); the Location header must come back rewritten to the slave
# root with no doubled slash; and a PROPFIND of the resulting working
# resource covers the !svn/wrk/ routing branch.
say "Test case for v1 commit opening (MKACTIVITY/CHECKOUT) through the proxy"

v1_activity="dav-mirror-v1-activity-$$"
mka_status=$(curl_auth --request MKACTIVITY -o /dev/null -w "%{http_code}" \
  "$SLAVE_URL/!svn/act/$v1_activity")
[ "$mka_status" = "201" ] \
  || fail "MKACTIVITY through the proxy failed (HTTP $mka_status)"

rev=$($SVNLOOK youngest "$SLAVE_REPOS")
printf '<?xml version="1.0" encoding="utf-8"?><D:checkout xmlns:D="DAV:"><D:activity-set><D:href>/%s/!svn/act/%s</D:href></D:activity-set></D:checkout>' \
  "$SLAVE_LOCATION_URI" "$v1_activity" > "$HTTPD_ROOT/checkout-body.xml"
v1_location=$(curl_auth --request CHECKOUT --header "Content-Type: text/xml" \
  --data @"$HTTPD_ROOT/checkout-body.xml" --dump-header - --output /dev/null \
  "$SLAVE_URL/!svn/ver/$rev/master-url.txt" \
  | sed -ne 's/^Location: *//p' | tr -d '\r')
[ -n "$v1_location" ] \
  || fail "v1 CHECKOUT through the proxy returned no Location header (body href untranslated?)"
echo "$v1_location" | grep -qF "$SLAVE_URL/!svn/wrk/$v1_activity/master-url.txt" \
  || fail "CHECKOUT Location was not rewritten cleanly to the slave root: '$v1_location'"

wrk_propfind=$(curl_auth --request PROPFIND --header "Depth: 0" \
  "$SLAVE_URL/!svn/wrk/$v1_activity/master-url.txt")
echo "$wrk_propfind" | grep -qF "<D:href>/${SLAVE_LOCATION_URI}/!svn/wrk/" \
  || fail "working-resource PROPFIND href was not translated to the slave location: $wrk_propfind"

curl_auth --request DELETE --output /dev/null "$SLAVE_URL/!svn/act/$v1_activity" \
  || say "WARNING: could not delete v1 test activity (continuing)" >&2
say "PASS: v1 activity checkout, Location rewrite, and wrk reads work through the proxy"

# LOCK/UNLOCK are proxied methods, and locked-file commits push lock tokens
# through the proxy.  The lock comment travels as the DAV owner element in
# the LOCK body, assert the comment avoids the filters.
say "Test case for locks through the write-through proxy"

svncmd="$SVN --non-interactive --username=jrandom --password=rayjandom"
$svncmd checkout -q "$BASE_URL" "$HTTPD_ROOT/wc-lock" \
  || fail "checkout for lock test failed"
$svncmd lock -m "locked via slave: $HREF_IN_SLAVE" "$HTTPD_ROOT/wc-lock/slave-url.txt" \
  || fail "svn lock through the proxy failed"

# The lock must exist on the MASTER (locks are not versioned; svnsync does
# not carry them, the proxied LOCK must have created it there).
$SVNLOOK lock "$MASTER_REPOS" slave-url.txt | grep -q "Owner: jrandom" \
  || fail "lock did not reach the master repository"

# The client's own view of the comment is just the cached copy in its
# working-copy lock table, so inspect what the master actually stored.
lock_comment=$($SVNLOOK lock "$MASTER_REPOS" slave-url.txt)
echo "$lock_comment" | grep -qF "$HREF_IN_SLAVE" \
  || fail "stored lock comment was munged by the proxy: expected it to contain '$HREF_IN_SLAVE', master stores: $lock_comment"
say "PASS: lock comment survives the proxy verbatim"

# Committing a change to the locked file sends the lock token with the
# proxied request (inside the MERGE body's lock-token-list), the commit
# must succeed and (by default) release the lock.
echo "locked change" >> "$HTTPD_ROOT/wc-lock/slave-url.txt"
$svncmd commit -m "commit to locked file via slave" "$HTTPD_ROOT/wc-lock" \
  || fail "commit to a locked file through the proxy failed"
[ -z "$($SVNLOOK lock "$MASTER_REPOS" slave-url.txt)" ] \
  || fail "lock was not released by the commit on the master"

# Explicit unlock is a separate proxied method, exercise it too.
$svncmd update -q "$HTTPD_ROOT/wc-lock" \
  || fail "update of the lock working copy failed"
$svncmd lock -m "re-locked for explicit unlock" "$HTTPD_ROOT/wc-lock/slave-url.txt" \
  || fail "re-locking through the proxy failed"
$SVNLOOK lock "$MASTER_REPOS" slave-url.txt | grep -q "Owner: jrandom" \
  || fail "re-acquired lock did not reach the master repository"
$svncmd unlock "$HTTPD_ROOT/wc-lock/slave-url.txt" \
  || fail "explicit svn unlock through the proxy failed"
[ -z "$($SVNLOOK lock "$MASTER_REPOS" slave-url.txt)" ] \
  || fail "explicit unlock did not release the lock on the master"

say "PASS: lock, locked commit, commit release, and explicit unlock work through the proxy"

# Test case for commit to out-dated(though target path is up to date) slave.
# See issue #3860 for details.
say "Test case for out-dated slave commit"

svn="$SVN --non-interactive --username=jrandom --password=rayjandom"
# Make a working copy of the slave.
$svn checkout $SLAVE_URL $HTTPD_ROOT/wc
cd $HTTPD_ROOT/wc
# Add a new file named newfile and commit it.
touch branch/newfile
$svn add branch/newfile
$svn commit -mm

say "De-activating post-commit hook on $MASTER_REPOS to make $SLAVE_REPOS go out of sync"
mv "$MASTER_REPOS/hooks/post-commit" "$MASTER_REPOS/hooks/post-commit_"

echo "Change made to file in branch" > $HTTPD_ROOT/wc/branch/newfile
$svn ci -m "Commit from slave"

MASTER_HEAD=`$SVNLOOK youngest "$MASTER_REPOS"`
SLAVE_HEAD=`$SVNLOOK youngest "$SLAVE_REPOS"`
say "Now the slave is at r$SLAVE_HEAD and master is at r$MASTER_HEAD."

# Now any other commit operation will fail with an out-of-date error

$svn cp -m "Creating a branch" ^/trunk ^/branch/newbranch \
  || fail "Commits fail with an out-of-date slave"

say "PASS: Commits succeed even with an out-of-date slave"
say "Some house-keeping..."
say "Re-activating the post-commit hook on the master repo: $MASTER_REPOS."
mv "$MASTER_REPOS/hooks/post-commit_" "$MASTER_REPOS/hooks/post-commit"
say "Syncing slave with master."
$SVNSYNC --non-interactive sync "$SYNC_URL" --username=svnsync --password=svnsync
# shut it down
echo -n "${SCRIPT}: stopping httpd: "
$HTTPD -f $HTTPD_CONFIG -k stop
echo "."
exit 0
