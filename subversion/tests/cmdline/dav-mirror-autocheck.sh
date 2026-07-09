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
# using svnmucc) and have grown to cover URI-encoded locations, the
# SVN-3445 payload-corruption regressions, proxied reads of transaction
# resources, and locks.  Any svn traffic liable to break over
# mirroring remains a good addition.
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
#  slave allows revprop changes
#  master syncs changes to slave
echo "#!/bin/sh" > "$SLAVE_REPOS/hooks/pre-revprop-change"
echo "#!/bin/sh" > "$MASTER_REPOS/hooks/post-revprop-change"
echo "#!/bin/sh" > "$MASTER_REPOS/hooks/post-commit"
echo "$SVNSYNC --non-interactive sync '$SYNC_URL' --username=svnsync --password=svnsync" \
    >> "$MASTER_REPOS/hooks/post-revprop-change"
echo "$SVNSYNC --non-interactive sync '$SYNC_URL' --username=svnsync --password=svnsync" \
    >> "$MASTER_REPOS/hooks/post-commit"

chmod 0755 "$SLAVE_REPOS/hooks/pre-revprop-change"
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

# Regression coverage for SVN-3445.  When the master and slave locations
# differ, the proxy must translate the location prefix in *protocol* URLs
# (hrefs in MERGE/CHECKOUT bodies) but must NOT touch *user payload*:
# svndiff-encoded file content in PUT bodies, property values in
# PROPPATCH bodies, and log-message/revprop skels in POST bodies.
say "Test case for versioned content munging (SVN-3445)"

# File content must round-trip verbatim whether it embeds the master URL
# or the slave URL (used to be mangled/rejected).
echo "$MASTER_URL" > "$HTTPD_ROOT/master-url.txt"
echo "$SLAVE_URL"  > "$HTTPD_ROOT/slave-url.txt"
$svnmucc put "$HTTPD_ROOT/master-url.txt" "$BASE_URL/master-url.txt" \
         put "$HTTPD_ROOT/slave-url.txt"  "$BASE_URL/slave-url.txt" \
  || fail "committing URL-bearing files failed (SVN-3445: PUT body rewritten?)"

master_file_content=$($SVNLOOK cat "$SLAVE_REPOS" master-url.txt)
[ "$master_file_content" = "$MASTER_URL" ] \
  || fail "file content embedding the master URL was munged: committed '$MASTER_URL', slave stores '$master_file_content'"
slave_file_content=$($SVNLOOK cat "$SLAVE_REPOS" slave-url.txt)
[ "$slave_file_content" = "$SLAVE_URL" ] \
  || fail "file content embedding the slave URL was munged: committed '$SLAVE_URL', slave stores '$slave_file_content'"
say "PASS: file content is preserved verbatim regardless of embedded URL"

# Property values must likewise round-trip verbatim: a PROPPATCH value that
# contains the slave URL used to be silently rewritten.
$svnmucc propset svn-3445-prop "$SLAVE_URL" "$BASE_URL/slave-url.txt" \
  || fail "propset of a value containing the slave URL failed"
prop_value=$($SVNLOOK propget "$SLAVE_REPOS" svn-3445-prop slave-url.txt)
[ "$prop_value" = "$SLAVE_URL" ] \
  || fail "property value embedding the slave URL was munged: set '$SLAVE_URL', slave stores '$prop_value'"
say "PASS: property value is preserved verbatim regardless of embedded URL"

# Commit log messages and revision properties travel inside the
# create-txn-with-props POST body (HTTPv2), a length-prefixed skel
# carrying only user data, never protocol URLs.  Rewriting that body
# corrupts the values, and when the location paths differ in length it
# breaks the skel framing outright.  Both must round-trip verbatim.
# (The $svnmucc wrapper bakes in -mm, so invoke $SVNMUCC directly.)
log_msg="log mentioning the slave URL: $SLAVE_URL"
$SVNMUCC --non-interactive --username jrandom --password rayjandom \
         -m "$log_msg" --with-revprop "svn-3445-revprop=$SLAVE_URL" \
         mkdir "$BASE_URL/log-url-dir" \
  || fail "commit with a log message containing the slave URL failed (POST body rewritten?)"
rev=$($SVNLOOK youngest "$SLAVE_REPOS")
stored_log=$($SVNLOOK propget --revprop -r "$rev" "$SLAVE_REPOS" svn:log)
[ "$stored_log" = "$log_msg" ] \
  || fail "log message was munged: committed '$log_msg', slave stores '$stored_log'"
stored_revprop=$($SVNLOOK propget --revprop -r "$rev" "$SLAVE_REPOS" svn-3445-revprop)
[ "$stored_revprop" = "$SLAVE_URL" ] \
  || fail "revprop value was munged: set '$SLAVE_URL', slave stores '$stored_revprop'"
say "PASS: log message and revprop values are preserved verbatim"

# The response side (mirror.c attaches the body-rewrite filter only for
# MERGE and PROPFIND, with the response_is_xml() gate as a backstop) is
# covered by the proxied-txn-reads test below.  The residual block further
# down exercises the one remaining SVN-3445 residual.

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

curl_auth="$CURL --silent --show-error --user jrandom:rayjandom"

# Commit an XML-mime-typed file for probe 3 below (before the txn is
# opened, so the txn tree contains it).
printf '<?xml version="1.0"?>\n<note><!-- %s --></note>\n' "$MASTER_URL" \
  > "$HTTPD_ROOT/xml-payload.xml"
$svnmucc put "$HTTPD_ROOT/xml-payload.xml" "$BASE_URL/xml-payload.xml" \
         propset svn:mime-type text/xml "$BASE_URL/xml-payload.xml" \
  || fail "committing the XML-typed payload file failed"

# Open a transaction directly on the master (raw HTTPv2 create-txn POST)
# and harvest its name from the SVN-Txn-Name response header.
txn_name=$($curl_auth --request POST \
  --header "Content-Type: application/vnd.svn-skel" --data "( create-txn )" \
  --dump-header - --output /dev/null "$MASTER_URL/!svn/me" \
  | sed -ne 's/^SVN-Txn-Name: *//p' | tr -d '\r')
[ -n "$txn_name" ] || fail "could not create a txn on the master"

# 1. Non-XML payload: GET the file through the SLAVE. The response is
#    proxied from the master and must arrive verbatim, the slave URL
#    embedded in the content must NOT have been rewritten.
proxied_get=$($curl_auth "$SLAVE_URL/!svn/txr/$txn_name/slave-url.txt")
[ "$proxied_get" = "$SLAVE_URL" ] \
  || fail "proxied GET of txn file content was munged: expected '$SLAVE_URL', got '$proxied_get'"

# 2. Protocol XML: PROPFIND on the txn file through the SLAVE. The
#    multistatus hrefs come from the master and MUST be rewritten to the
#    slave location.
proxied_propfind=$($curl_auth --request PROPFIND --header "Depth: 0" \
  "$SLAVE_URL/!svn/txr/$txn_name/slave-url.txt")
echo "$proxied_propfind" | grep -qF "/${SLAVE_LOCATION_URI}/" \
  || fail "proxied PROPFIND multistatus hrefs were not rewritten to the slave location: $proxied_propfind"
echo "$proxied_propfind" | grep -qF "/${MASTER_LOCATION_URI}/" \
  && fail "proxied PROPFIND multistatus still contains master-location hrefs: $proxied_propfind"

# 3. Regression guard: a versioned file whose svn:mime-type is XML must
#    never have its content rewritten on a proxied read.  Doubly protected:
#    GET responses do not receive the ReposRewrite body filter at all
#    (proxy_request_fixup() attaches it only for MERGE and PROPFIND), and
#    mod_dav_svn happens to emit no Content-Type for !svn/txr file GETs.
proxied_xml_get=$($curl_auth "$SLAVE_URL/!svn/txr/$txn_name/xml-payload.xml")
echo "$proxied_xml_get" | grep -qF "$MASTER_URL" \
  || fail "XML-typed txn file content did not round-trip a proxied read verbatim: got '$proxied_xml_get'"
say "PASS: XML-typed txn file content survives a proxied read verbatim"

# Clean up the open txn so later consistency checks aren't confused.
$curl_auth --request DELETE --output /dev/null \
  "$MASTER_URL/!svn/txn/$txn_name" \
  || say "WARNING: could not delete test txn $txn_name (continuing)"

say "PASS: proxied txn reads: content verbatim, protocol hrefs rewritten"

# The one remaining SVN-3445 residual: a dead-property value rewritten in
# a proxied PROPFIND multistatus.  Needs a proxied read of in-transaction
# data (reads of committed data are served locally by the slave and never
# traverse the response filter), so it reuses the curl txn-probe technique
# from the test above.
say "Test case for the SVN-3445 response-side residual (XFAIL expected)"

# The munged fingerprint: a master-URL value rewritten by the response
# filter keeps the master host but gains the slave location path.
master_url_munged="http://${MASTER_HOST}:${TEST_PORT}/${SLAVE_LOCATION_URI}"

# The residual: a dead-property VALUE containing the master URL, returned
# inside a proxied PROPFIND multistatus, is blindly rewritten along with
# the genuine hrefs (see the "FIXME (SVN-3445, residual)" comment in
# mirror.c's dav_svn__location_body_filter).
$svnmucc propset svn-3445-residual-prop "$MASTER_URL" "$BASE_URL/master-url.txt" \
  || fail "propset of a master-URL value failed"

# Open a fresh txn on the master (after the propset above, so its tree
# contains the property).
txn_name=$($curl_auth --request POST \
  --header "Content-Type: application/vnd.svn-skel" --data "( create-txn )" \
  --dump-header - --output /dev/null "$MASTER_URL/!svn/me" \
  | sed -ne 's/^SVN-Txn-Name: *//p' | tr -d '\r')
[ -n "$txn_name" ] || fail "could not create a txn on the master (residual tests)"

# Residual probe: PROPFIND (allprop) on the file through the SLAVE.
residual_propfind=$($curl_auth --request PROPFIND --header "Depth: 0" \
  "$SLAVE_URL/!svn/txr/$txn_name/master-url.txt")
if echo "$residual_propfind" | grep -qF "$MASTER_URL"; then
  say "XPASS: SVN-3445 residual appears fixed: dead-property value survived a"
  say "       proxied PROPFIND verbatim."
elif echo "$residual_propfind" | grep -qF "$master_url_munged"; then
  say "XFAIL (SVN-3445 residual): dead-property value was rewritten in the"
  say "       proxied multistatus: '$MASTER_URL' -> '$master_url_munged'."
else
  fail "residual PROPFIND contained neither the original nor the munged value: $residual_propfind"
fi

# Clean up the open txn.
$curl_auth --request DELETE --output /dev/null \
  "$MASTER_URL/!svn/txn/$txn_name" \
  || say "WARNING: could not delete residual-test txn $txn_name (continuing)"

# LOCK/UNLOCK are proxied methods, and locked-file commits push lock tokens
# through the proxy.  The lock comment travels as the DAV owner element in
# the LOCK body, assert the comment avoids the filters.
say "Test case for locks through the write-through proxy"

svncmd="$SVN --non-interactive --username=jrandom --password=rayjandom"
$svncmd checkout -q "$BASE_URL" "$HTTPD_ROOT/wc-lock" \
  || fail "checkout for lock test failed"
$svncmd lock -m "locked via slave: $SLAVE_URL" "$HTTPD_ROOT/wc-lock/slave-url.txt" \
  || fail "svn lock through the proxy failed"

# The lock must exist on the MASTER (locks are not versioned; svnsync does
# not carry them, the proxied LOCK must have created it there).
$SVNLOOK lock "$MASTER_REPOS" slave-url.txt | grep -q "Owner: jrandom" \
  || fail "lock did not reach the master repository"

# The client's own view of the comment is just the cached copy in its
# working-copy lock table, so inspect what the master actually stored.
lock_comment=$($SVNLOOK lock "$MASTER_REPOS" slave-url.txt)
echo "$lock_comment" | grep -qF "$SLAVE_URL" \
  || fail "stored lock comment was munged by the proxy: expected it to contain '$SLAVE_URL', master stores: $lock_comment"
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
