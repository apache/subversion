#!/bin/bash
#######################################################################
#
# svnmirror.sh
#
VERSION="0.0.4"
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
#
#   with suggestions from:
#        - tberman (#svn at freenode)
#          - he actually needed such a script :)
#        - Erik Huelsmann <e.huelsmann@gmx.net>
#          - the little if for remote version :)
#          - status reply
#        - Ben Collins-Sussman <sussman@collab.net>
#          - for some help with the svn commands
#        - Bjørn Magnus Mathisen <epic@generation.no>
#          - for pointing out i forgot to replace one ssh with $LSSH
# TODO:
#   - remove bashisms and port to plain old bourne shell
#
# License:
#   The same as svn itself. for latest version check:
#    http://svn.collab.net/repos/svn/trunk/subversion/LICENSE
#
# Thanks to the subversion team for their great work.
#
# Links:
#
#    If you do not like my solution check:
#       - svnpush
#         +  http://svn.collab.net/repos/svn/trunk/contrib/client-side/svn-push/svn-push.c
#       - svn replicate
#         + https://open.datacore.ch/read-only/
#       - SVN::Mirror and SVN::Web
#         + http://svn.elixus.org/repos/member/clkao/
#         + http://svn.elixus.org/svnweb/repos/browse/member/clkao/ 
#
#
# Changes:
#
#  0.0.4
#    added comandline switch -v which shows the version.
#    using markers now to find the config so it's not twice in this script
#    added/changed some more documentation
#
#  0.0.3
#    added comandline switches: -C (create config) -f (read config file)
#      -h (help) -q (quiet).
#
#  0.0.2
#    initial version
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
#######################################################################
#
# Mode:
#
# push == Mirror from local repository to remote (readonly) repository
# pull == Mirror from remote repository to local (readonly) repository
#
MODE="push"

# Absolute path of svnlook on the local host.
LSVNLOOK="/usr/bin/svnlook"
# Absolute path of svnadmin on the local host.
LSVNADMIN="/usr/bin/svnadmin"
# Absolute path to the repository on the local host.
LREPOS="/path/to/repos"

# Absolute path of ssh on the local host.
#   '-c blowfish -C' to speed up the transfer a bit :)
LSSH="/usr/bin/ssh -c blowfish -C"

# Name or IP of the remote host.
RHOST="host"
# UNIX username on the remote host.
RUSER="user"


# Absolute path of svnlook on the remote host.
RSVNLOOK="/usr/bin/svnlook"
# Absolute path of svnadmin on the remote host.
RSVNADMIN="/usr/bin/svnadmin"

# Absolute path to the repository on the remote host.
RREPOS="/path/to/repos"

# Additional commands you want to run before loading the dump in to the repos
# e.g. setting umask or change group via newgrp.
#
# Make sure the last char is a ";".
#
LADDITIONAL=""
RADDITIONAL=""

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
        echo "config file '${CFGFILE}' allready exists."
        exit 1
    fi
    SVNMIRROR="$0"
    if [ ! -f "$0" ]; then
        echo blorg
        SVNMIRROR=`which "$0"`
        if [ $? -ne 0 ]; then
            echo "could not locate $0"
            exit 1
        fi
    fi
    STARTLINE=`grep -n "^#_CONFIG_START_MARKER" "$SVNMIRROR" | sed 's/:.*$//'`
    ENDLINE=`grep -n "^#_CONFIG_END_MARKER" "$SVNMIRROR" | sed 's/:.*$//'`
    LINECOUNT=`expr $ENDLINE - $STARTLINE`
    head -n $ENDLINE "$SVNMIRROR" | tail -$LINECOUNT | \
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
                echo "config allready read" >&2
                exit 1
            elif [ ! -r "${OPTARG}" ]; then
                echo "cannot read config file '${OPTARG}'" >&2
                exit 1
            else
                source ${OPTARG}
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
# the actual script
#
#######################################################################
#
# Uncomment this line if you want to use keychain.
# You maybe have to tweak the path to the script.
#
# source ~/.keychain/$(uname -n)-sh

#
# getting version of the remote repository
#
RVERSION=$(${LSSH} ${RUSER}@${RHOST} ${RSVNLOOK} youngest ${RREPOS})
if [ -z "${RVERSION}" ] ; then
    echo "getting version of remote repository failed" >&2
    exit 1
fi

#
# getting version of the local repository
#
${LADDITIONAL}
LVERSION=$(${LSVNLOOK} youngest ${LREPOS})
if [ -z "${LVERSION}" ] ; then
    echo "getting version of local repository failed" >&2
    exit 1
fi

#
# compare revision numbers
#
if [ ${RVERSION} -eq ${LVERSION} ] ; then
    [ $VERBOSE = true ] && \
        echo "both repositories are already at ${LVERSION}"
    exit 0
fi

#
# syncing
#
RC=0
if [ $MODE = "push" ] ; then
    if [ ${RVERSION} -gt ${LVERSION} ] ; then
        echo "revision of remote repos is higher than local one" >&2
        exit 1
    fi
    REVRANGE="$((${RVERSION}+1)):${LVERSION}"
    [ $VERBOSE = true ] && \
        echo -n "syncing r${REVRANGE} to ${RHOST} ";
    ${LSVNADMIN} dump ${QUIET} --incremental -r${REVRANGE} ${LREPOS} | \
    ${LSSH} $RUSER@$RHOST "${RADDITIONAL} ${RSVNADMIN} load ${QUIET} ${RREPOS}" || \
    RC=1
elif [ $MODE = "pull" ] ; then
    if [ ${LVERSION} -gt ${RVERSION} ] ; then
        echo "revision of local repos is higher than remote one" >&2
        exit 1
    fi
    REVRANGE="$((${LVERSION}+1)):${RVERSION}"
    [ $VERBOSE = true ] && \
        echo -n "syncing r${REVRANGE} from ${RHOST} ";
    ${LSSH} ${RUSER}@${RHOST} \
    "${RADDITIONAL} ${RSVNADMIN} dump ${QUIET} --incremental -r${REVRANGE} ${RREPOS}" | \
    ${LSVNADMIN} load ${QUIET} ${LREPOS} || \
    RC=1
else
    echo "invalid mode \"${MODE}\" specified!" >&2
    exit 1
fi
if [ $RC -ne 0 ]; then
    echo "failed" >&2
else
    [ $VERBOSE = true ] && \
        echo "successfull completed."
fi
exit $RC

