#!/bin/sh
#######################################################################
#
# svnmirror.sh
#
VERSION="0.0.7"
#
# This script syncs changes from a developer repository to a
#             _READONLY_public_repository_
#
# It supports pushing or pulling the changes via ssh and svn tools.
# It is intended to be run manually or from cron.
#
#######################################################################
#
# Things you have to take care of:
#
# 1. You need write access to the directory structure on both boxes
#    for more see the warning at:
#    http://svnbook.red-bean.com/html-chunk/ch06s03.html
#
# 2. For running it from cron i suggest the use of the ssh agent e.g
#    via keychain <http://www.gentoo.org/proj/en/keychain.xml>
#    in general read "man ssh-agent" and "man ssh-keygen".
#
# 3. Do NOT run it from post commit scripts it can lead to broken public
#    repositories!
#
# 4. You do not have to be afraid of the public repos. If the pipe is
#    broken nothing will be committed to it.
#    21:40:47 <@sussman> tberman:  welcome to atomic commits.
#
# 5. For local syncing use "svnadmin hotcopy"
#    see: "svnadmin help hotcopy"
#
#######################################################################
#
# Authors:
#    - Martin Furter <mf@rola.ch>
#    - Marcus Rückert <darix@irssi.org>
#    - Joerg Sonnenberger <joerg@bec.de>
#
#    with suggestions from:
#       - tberman (#svn at freenode)
#         - he actually needed such a script :)
#       - Erik Huelsmann <e.huelsmann@gmx.net>
#         - the little if for remote version :)
#         - status reply
#       - Ben Collins-Sussman <sussman@collab.net>
#         - for some help with the svn commands
#       - Bjørn Magnus Mathisen <epic@generation.no>
#         - for pointing out i forgot to replace one ssh with $LSSH
#       - John Belmonte <john@neggie.net>
#         - for pointing out that we use stderr for a non-error message
#       - Loic Calvez <l.calvez@openchange.org>
#         - filtering
#
# Users:
#    our biggest users atm are Mono Project and MonoDevelop
#    Mono Team currently mirrors the repos every 30 minutes!:)
#    see http://www.mono-project.com/contributing/anonsvn.html
#
#    Openchange.org team is using this script with an excluding filter
#    to synchronise private and public svn server once a day.
#
# License:
#    The same as svn itself. for latest version check:
#    http://svn.collab.net/repos/svn/trunk/subversion/LICENSE
#
# Thanks to the subversion team for their great work.
#
# Links:
#    If you do not like our solution check:
#       - svnpush
#         + http://svn.collab.net/repos/svn/trunk/contrib/client-side/svn-push/svn-push.c
#       - svn replicate
#         + https://open.datacore.ch/read-only/
#       - SVN::Mirror and SVN::Web
#         + http://svn.elixus.org/repos/member/clkao/
#         + http://svn.elixus.org/svnweb/repos/browse/member/clkao/ 
#
# Changes:
#  0.0.7
#    + allow optional filtering using svndumpfilter
#
#  0.0.6
#    + print a non-error message to stdout instead of stderr.
#  0.0.5
#    + added DUMPPARAMS to specify the params for the svnadmin dump call
#    + added note about svnadmin 1.1 and "--deltas" cmdline option
#    * made the script POSIX sh-compatible
#      (zsh in native-mode does not work atm!)
#    * changed handling of default settings
#    * created config file has all lines commented out now
#    + check if necessary values are set
#    + added note about current users
#    * fixed documentation in the default config
#
#  0.0.4
#    + added comandline switch -v which shows the version.
#    + using markers now to find the config so it's not twice in this script
#    + added/changed some more documentation
#
#  0.0.3
#    + added comandline switches: -C (create config) -f (read config file)
#      -h (help) -q (quiet).
#
#  0.0.2
#    * initial version
#
#######################################################################
#
# uncomment this line for debugging
# set -x
#
#_CONFIG_START_MARKER
#######################################################################
#
# svnmirror default config
#
# Everything starting with "R" refers to the remote host
# with "L" to the local host
#
# the required variables are LREPOS, RREPOS and RHOST
#
#######################################################################
#
# Mode:
#
# push == Mirror from local repository to remote (readonly) repository
# pull == Mirror from remote repository to local (readonly) repository
#
DFLT_MODE="push"

#
# keychain = path to keychain sh file
# If the variable is not set, the script tries to read
# "$HOME/.keychain/`uname -n`-sh", if it is readable.
#
DFLT_KEYCHAIN=""

#
# DUMPPARMS
#
# see "svnadmin help dump" for it.
# default is "--incremental"
# 
# if you use svn 1.1 on both you should add "--deltas"
# it should speed up the transfer on slow lines.
#
DFLT_DUMPPARAMS="--incremental"

# Absolute path of svnlook on the local host.
DFLT_LSVNLOOK="/usr/bin/svnlook"

# Absolute path of svnadmin on the local host.
DFLT_LSVNADMIN="/usr/bin/svnadmin"

# Absolute path of svndumpfilter on the local machine.
DFLT_LSVNDUMPFILTER="/usr/bin/svndumpfilter"

# Absolute path to the repository on the local host.
# REQUIRED
DFLT_LREPOS="/path/to/repos"

# Absolute path of ssh on the local host.
#   '-c blowfish -C' to speed up the transfer a bit :)
DFLT_LSSH="/usr/bin/ssh -c blowfish -C"

# Name or IP of the remote host.
# REQUIRED
DFLT_RHOST="host"

# UNIX username on the remote host.
# defaults to the local username.
DFLT_RUSER="user"

# Absolute path of svnlook on the remote host.
DFLT_RSVNLOOK="/usr/bin/svnlook"

# Absolute path of svnadmin on the remote host.
DFLT_RSVNADMIN="/usr/bin/svnadmin"

# Absolute path of svndumpfilter on the local machine.
DFLT_RSVNDUMPFILTER="/usr/bin/svndumpfilter"

# Absolute path to the repository on the remote host.
# REQUIRED
DFLT_RREPOS="/path/to/repos"

# Additional commands you want to run before loading the dump in to the repos
# e.g. setting umask or change group via newgrp.
#
# Make sure the last char is a ";".
#
DFLT_LADDITIONAL=""
DFLT_RADDITIONAL=""

#
# with the filter directive you can optionally include a svndumpfilter
# into the pipe. see svndumpfilter help for more.
#
# examples:
#
# this will skip trunk/foo.conf from syncing
# 
# FILTER="exclude trunk/foo.conf"
#
# this filter will only sync trunk
#
# FILTER="include trunk/"
#
#
DFLT_FILTER=""
#_CONFIG_END_MARKER
#######################################################################
#
# create a config file
#
#######################################################################

create_config()
{
    CFGFILE="$1"
    if [ -f "${CFGFILE}" ]; then
        echo "config file '${CFGFILE}' already exists." >&2
        exit 1
    fi
    SVNMIRROR="$0"
    if [ ! -f "$0" ]; then
        SVNMIRROR=`which "$0"`
        if [ $? -ne 0 ]; then
            echo "could not locate $0" >&2
            exit 1
        fi
    fi
    STARTLINE=`grep -n "^#_CONFIG_START_MARKER" "$SVNMIRROR" | sed 's/:.*$//'`
    ENDLINE=`grep -n "^#_CONFIG_END_MARKER" "$SVNMIRROR" | sed 's/:.*$//'`
    ENDLINE=`expr ${ENDLINE} - 1`
    LINECOUNT=`expr ${ENDLINE} - ${STARTLINE}`
    head -n ${ENDLINE} "$SVNMIRROR" | tail -$LINECOUNT | sed -e 's/^#/##/' -e 's/^DFLT_/# /g' | \
        sed 's/default config/config/' > "$CFGFILE"
}

#######################################################################
#
# parse the commandline
#
#######################################################################
#
# 

show_help()
{
    echo ""
    echo "usage:"
    echo "  svnmirror.sh [-f configfile] [-h] [-q] [-v]"
    echo "  svnmirror.sh -C configfile"
    echo ""
    echo "  -C configfile   create a config file"
    echo "  -f configfile   read config from the specified file"
    echo "  -h              show this help"
    echo "  -q              quiet (-q -q for really quiet)"
    echo "  -v              show version"
    echo ""
    echo "Note: For -f, configfile is relative to the current PATH settings"
    echo ""
}

CONFIGREAD=false
QUIET=""
VERBOSE=true
while getopts C:f:hqv OPTION; do
    case "$OPTION" in
        C)
            create_config "${OPTARG}"
            exit 0
            ;;
        f)
            if [ ${CONFIGREAD} = true ]; then
                echo "config already read" >&2
                exit 1
            elif [ ! -r "${OPTARG}" ]; then
                echo "cannot read config file '${OPTARG}'" >&2
                exit 1
            else
                . ${OPTARG}
            fi
            ;;
        h)
            show_help
            exit 0
            ;;
        q)
            if [ -z "${QUIET}" ]; then
                QUIET="-q"
            else
                VERBOSE=false
            fi
            ;;
        v)
            echo "svnmirror.sh $VERSION"
            exit 0
            ;;
        \?)
            echo "  for help use $0 -h"
            exit 1
            ;;
    esac
done

#######################################################################
#
# add default values
#
#######################################################################
MODE="${MODE:-$DFLT_MODE}"
DUMPPARAMS="${DUMPPARAMS:-$DFLT_DUMPPARAMS}"
LSVNLOOK="${LSVNLOOK:-$DFLT_LSVNLOOK}"
LSVNADMIN="${LSVNADMIN:-$DFLT_LSVNADMIN}"
LSVNDUMPFILTER="${LSVNDUMPFILTER:-$DFLT_LSVNDUMPFILTER}"
LSSH="${LSSH:-$DFLT_LSSH}"
RUSER="${RUSER:+$RUSER@}"
RSVNLOOK="${RSVNLOOK:-$DFLT_RSVNLOOK}"
RSVNADMIN="${RSVNADMIN:-$DFLT_RSVNADMIN}"
RSVNDUMPFILTER="${RSVNDUMPFILTER:-$DFLT_RSVNDUMPFILTER}"
LADDITIONAL="${LADDITIONAL:-$DFLT_LADDITIONAL}"
RADDITIONAL="${RADDITIONAL:-$DFLT_RADDITIONAL}"

echo "${LREPOS:?Required variable LREPOS is not set in config file}" > /dev/null
echo "${RREPOS:?Required variable RREPOS is not set in config file}" > /dev/null
echo "${RHOST:?Required variable RHOST is not set in config file}" > /dev/null

KEYCHAIN="${KEYCHAIN:-$HOME/.keychain/`uname -n`-sh}"
[ -n "${KEYCHAIN}" -a -r "${KEYCHAIN}" ] && . ${KEYCHAIN}

#######################################################################
#
# the actual script
#
#######################################################################
#

#
# getting version of the remote repository
#
RVERSION=`${LSSH} ${RUSER}${RHOST} ${RSVNLOOK} youngest ${RREPOS}`
if [ -z "${RVERSION}" ] ; then
    echo "getting version of remote repository failed" >&2
    exit 1
fi

#
# getting version of the local repository
#
${LADDITIONAL}
LVERSION=`${LSVNLOOK} youngest ${LREPOS}`
if [ -z "${LVERSION}" ] ; then
    echo "getting version of local repository failed" >&2
    exit 1
fi

#
# compare revision numbers
#
if [ ${RVERSION} -eq ${LVERSION} ] ; then
    [ ${VERBOSE} = true ] && \
        echo "both repositories are already at ${LVERSION}"
    exit 0
fi

#
# syncing
#
RC=0
if [ ${MODE} = "push" ] ; then
    if [ ${RVERSION} -gt ${LVERSION} ] ; then
        echo "revision of remote repos is higher than local one" >&2
        exit 1
    fi
    DUMPFILTER="cat"
    if [ -n "$FILTER" ] ; then
        DUMPFILTER="${LSVNDUMPFILTER} ${FILTER}"
        [ ${VERBOSE} = true ] && echo "using filter 'svndumpfilter ${FILTER}"
    fi
    REVRANGE="`expr ${RVERSION} + 1`:${LVERSION}"
    [ ${VERBOSE} = true ] && \
        echo -n "syncing r${REVRANGE} to ${RHOST} ";
    ${LSVNADMIN} dump ${QUIET} ${DUMPPARAMS} -r${REVRANGE} ${LREPOS} | ${DUMPFILTER} | \
    ${LSSH} ${RUSER}${RHOST} "${RADDITIONAL} ${RSVNADMIN} load ${QUIET} ${RREPOS}" || \
    RC=1
elif [ ${MODE} = "pull" ] ; then
    if [ ${LVERSION} -gt ${RVERSION} ] ; then
        echo "revision of local repos is higher than remote one" >&2
        exit 1
    fi
    DUMPFILTER=""
    if [ -n "$FILTER" ] ; then
        DUMPFILTER="| ${LSVNDUMPFILTER} ${FILTER}"
        [ ${VERBOSE} = true ] && echo "using filter 'svndumpfilter ${FILTER}"
    fi
    REVRANGE="`expr ${LVERSION} + 1`:${RVERSION}"
    [ ${VERBOSE} = true ] && \
        echo -n "syncing r${REVRANGE} from ${RHOST} ";
    ${LSSH} ${RUSER}${RHOST} \
    "${RADDITIONAL} ${RSVNADMIN} dump ${QUIET} ${DUMPPARAMS} -r${REVRANGE} ${RREPOS} ${DUMPFILTER}" | \
    ${LSVNADMIN} load ${QUIET} ${LREPOS} || \
    RC=1
else
    echo "invalid mode \"${MODE}\" specified!" >&2
    exit 1
fi
if [ ${RC} -ne 0 ]; then
    echo "failed" >&2
else
    [ ${VERBOSE} = true ] && \
        echo "successfull completed."
fi
exit ${RC}

