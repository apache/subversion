#!/bin/sh
#
# Copyright (c) 2004-2005, Awarix, Inc.
# All rights reserved.
#
# Subject to the following obligations and disclaimer of warranty,
# use and redistribution of this software, in source or object code
# forms, with or without modifications are expressly permitted by
# Awarix; provided, however, that:
#
#    (i)  Any and all reproductions of the source or object code
#         must include the copyright notice above and the following
#         disclaimer of warranties; and
#    (ii) No rights are granted, in any manner or form, to use
#         Awarix trademarks, including the mark "AWARIX"
#         on advertising, endorsements, or otherwise except as such
#         appears in the above copyright notice or in the software.
#
# THIS SOFTWARE IS BEING PROVIDED BY AWARIX "AS IS", AND
# TO THE MAXIMUM EXTENT PERMITTED BY LAW, AWARIX MAKES NO
# REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING
# THIS SOFTWARE, INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED
# WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE,
# OR NON-INFRINGEMENT.  AWARIX DOES NOT WARRANT, GUARANTEE,
# OR MAKE ANY REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS
# OF THE USE OF THIS SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY,
# RELIABILITY OR OTHERWISE.  IN NO EVENT SHALL AWARIX BE
# LIABLE FOR ANY DAMAGES RESULTING FROM OR ARISING OUT OF ANY USE
# OF THIS SOFTWARE, INCLUDING WITHOUT LIMITATION, ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, PUNITIVE, OR CONSEQUENTIAL
# DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES, LOSS OF
# USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
# THE USE OF THIS SOFTWARE, EVEN IF AWARIX IS ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGE.
#
# Author: Archie Cobbs  archie @ awarix dot com
#
# Acknowledgements:
#   John Belmonte <john@neggie.net> - metadata and usability improvements
#
# $HeadURL$
# $LastChangedDate$
# $LastChangedBy$
# $LastChangedRevision$

# Definitions (would like ':' in property names but can't because of bug 1971)
NAME="svnmerge"
SVN_MERGE_SVN="svn"
SVN_MERGE_PROP="${NAME}-integrated"
SRCREV=`echo '$Rev$' | sed 's/^\$Rev: \([0-9]\{1,\}\).\{0,\}$/\1/g'`
SRCDATE=`echo '$Date$' | sed 's/^\$Date: .\{0,\}(\(.\{0,\}\)).\{0,\}$/\1/g'`

# We expect non-localized output
LC_MESSAGES="C"
export LC_MESSAGES

# Subroutine to output usage message
usage()
{
    echo 'Usage:'
    echo "  ${NAME} init [-s] [-v] [-n] [-r revs] [-f file] [src]"
    echo '       Initialize merge tracking from "src" on the current working'
    echo '       directory. "revs" specifies the already-merged in revisions;'
    echo '       it defaults to "1-HEAD", where HEAD is the latest revision of'
    echo '       "src", if "src" is specified; if "src" is omitted, then "src"'
    echo '       (and optionally "revs") are computed from the "svn cp" history'
    echo '       of the current working directory.'
    echo ''
    echo "  ${NAME} avail [-s] [-v] [-l] [-d] [-r revs] [-S src] [dest]"
    echo '       Show unmerged revisions available for "dest" as a revision'
    echo '       list. If revision list "revs" is given, the revisions shown'
    echo '       will be limited to those also specified in "revs". If "dest"'
    echo '       is tracking only one source, "src" may be omitted.'
    echo '       Options specific to this command:'
    echo '         -l  Show corresponding log history instead of revision list'
    echo '         -d  Show corresponding diffs instead of revision list'
    echo ''
    echo "  ${NAME} merge [-s] [-v] [-n] [-r revs] [-f file] [-S src] [dest]"
    echo '       Merge in revisions specified by "revs" into "dest" from the'
    echo '       given "src" location. "revs" is the revision list specifying'
    echo '       revisions to merge in. Already merged-in revisions will not be'
    echo '       merged in again. Default for "revs" is "1-HEAD" where HEAD is'
    echo '       the latest revision of the "src" repository (i.e., merge all'
    echo '       available). If "dest" is tracking only one source, "src" may'
    echo '       be omitted.'
    echo ''
    echo '  Options common to multiple commands:'
    echo '         -v  Verbose mode: output more information about progress'
    echo '         -s  Show subversion commands that make changes'
    echo "         -n  Don't actually change anything, just pretend; implies -s"
    echo '         -f  Write a suitable commit log message into "file"'
    echo '         -r  Specify a revision list, consisting of revision numbers'
    echo '             and ranges separated by commas, e.g., "534,537-539,540"'
    echo ''
    echo '   "src" may be a repository path or a working directory.'
    echo '   "dest" is always a working directory and defaults to ".".'
    echo "   This is svnmerge revision ${SRCREV} dated ${SRCDATE}."
    echo ''
    exit 1
}

# Subroutine to output an error and bail
error()
{
    echo ${NAME}: ${1+"$@"}
    exit 1
}

# Subroutine to output progress message, unless in quiet mode
report()
{
    if [ "${SVN_MERGE_VERBOSE}" != "" ]; then
        echo ${NAME}: ${1+"$@"}
    fi
}

# Subroutine to output an error, usage, and bail
usage_error()
{
    echo ${NAME}: ${1+"$@"}
    usage
}

# Subroutine to do (or pretend to do) an SVN command
svn_command()
{
    if [ "${SVN_MERGE_SHOW_CMDS}" != "" ]; then
        echo "${SVN_MERGE_SVN}" ${1+"$@"}
    fi
    if [ "${SVN_MERGE_PRETEND}" = "" ]; then
        "${SVN_MERGE_SVN}" ${1+"$@"}
        if [ $? -ne 0 ]; then
            error command failed: ${1+"$@"}
        fi
    fi
}

# Check the current status of ${BRANCH_DIR} for up-to-dateness and local mods
check_branch_dir()
{
    report "checking status of \"${BRANCH_DIR}\""
    "${SVN_MERGE_SVN}" status -u "${BRANCH_DIR}" | grep -q '^.......\*' && \
        error "\"${BRANCH_DIR}\" is not up to date; please \"svn update\" first"
    [ `"${SVN_MERGE_SVN}" stat -q "${BRANCH_DIR}" \
      | sed '/^$/,$d' | wc -l` = "0" ] || \
        error "\"${BRANCH_DIR}\" has local modifications; it must be clean"
}

# Subroutine to clean up an URL or path
normalize_url()
{
    TEMP="$1"
    while true; do
        TEMP2=`echo "${TEMP}" | sed -e 's/$/\//g' \
          -e 's/\/[^/]\{1,\}\/\.\.\//\//g' -e 's/\/\.\//\//g' \
          -e 's/\([^:/]\)\/\//\1\//g' -e 's/\/$//g'`
        [ "${TEMP2}" != "${TEMP}" ] || break
        TEMP="${TEMP2}"
    done
    RETURN_VALUE="${TEMP}"
}

# Subroutine to parse out the start and end from a range like "123-456"
get_start_end()
{
    START=`echo "$1" | sed 's/^\([0-9]\{1,\}\)-\([0-9]\{1,\}\)$/\1/g'`
    END=`echo "$1" | sed 's/^\([0-9]\{1,\}\)-\([0-9]\{1,\}\)$/\2/g'`
}

# Subroutine to get all integrated revisions for a given head
get_all_integrated_revs()
{
    RETURN_VALUE=`"${SVN_MERGE_SVN}" propget "${SVN_MERGE_PROP}" "$1"`
}

# Subroutine to retrieve a target's integrated revisions for a given head
get_integrated_revs()
{
    TEMP=`"${SVN_MERGE_SVN}" propget "${SVN_MERGE_PROP}" "$2" | grep "^${1}:"`
    [ -z "${TEMP}" ] && \
        error no integration info available for repository path \"$1\"
    RETURN_VALUE="${TEMP#${1}:}"
}

# Subroutine to set a target's integrated revisions for a given head
set_integrated_revs()
{
    TEMP=`"${SVN_MERGE_SVN}" propget "${SVN_MERGE_PROP}" "$3" | grep -v "^${1}:"`
    TEMP=`echo "${TEMP} ${1}:${2}" | xargs -n 1 | sort`
    svn_command propset -q "${SVN_MERGE_PROP}" "${TEMP}" "$3"
}

# Subroutine to retrieve the default head of the given target
get_default_head()
{
    # To make bi-directional merges easier, find the target's
    # repository local path so it can be removed from the list of
    # possible integration sources.
    target_to_url "$1"
    url_to_rlpath "${RETURN_VALUE}"

    RETURN_VALUE=`"${SVN_MERGE_SVN}" propget "${SVN_MERGE_PROP}" "$1" | cut -d: -f 1 | grep -v "^${RETURN_VALUE}$"`
    [ -z "${RETURN_VALUE}" ] && error no integration info available
    [ `echo "${RETURN_VALUE}" | wc -l` -gt 1 ] && \
        error explicit \"src\" argument required
}

# Subroutine to parse, validate, and normalize a revision list.
# This input has commas separating ranges and any additional whitespace.
# The result has the form "123-123,125-127,128-130,132-132", i.e.,
# sorted with all adjacent, empty, and redundant ranges merged.
normalize_list()
{
    # Special case empty list
    TEMP=`echo "$1" | tr -d '[:space:]'`
    if [ "${TEMP}" = "" ]; then
        RETURN_VALUE=""
        return 0
    fi

    # See if list is well formed
    NUMPAT='[0-9]\{1,\}'
    RNGPAT="${NUMPAT}\(-${NUMPAT}\)\{0,1\}"
    LISTPAT="\(,\{0,1\}${RNGPAT},\{0,1\}\)\{0,\}"
    expr "${TEMP}" : "${LISTPAT}\$" >/dev/null || \
        usage_error invalid revision list \"$1\"

    # Now sort the list and compress out redundancies
    RESULT=''
    LAST_START=''
    LAST_END=''
    for RNG in `echo "${TEMP}" | tr , '\n' | sort -n -t - -k 1,2 \
      | sed 's/^\([0-9]\{1,\}\)$/\1-\1/g'`; do

        # Get range start and end
        get_start_end "${RNG}"

        # First revision is #1
        if [ "${START}" -le 0 ]; then
            START="1"
        fi

        # Completely ignore any empty ranges
        if [ "${START}" -gt "${END}" ]; then
            continue
        fi

        # First iteration?
        if [ "${LAST_START}" = "" ]; then
            LAST_START=${START}
            LAST_END=${END}
            continue
        fi

        # Does this range overlap with the previous?
        if [ "${START}" -le `expr "${LAST_END}" + 1` ]; then
            if [ "${END}" -gt "${LAST_END}" ]; then
                LAST_END=${END}
            fi
            continue
        fi

        # Break off discontigous range
        [ "${RESULT}" = "" ] || RESULT="${RESULT},"
        RESULT="${RESULT}${LAST_START}-${LAST_END}"
        LAST_START=${START}
        LAST_END=${END}
    done

    # Tack on final range
    if [ "${LAST_START}" != "" ]; then
        [ "${RESULT}" = "" ] || RESULT="${RESULT},"
        RESULT="${RESULT}${LAST_START}-${LAST_END}"
    fi

    # Done
    RETURN_VALUE="${RESULT}"
}

# Subroutine to compute the set $1 minus $2, where $1 and $2 are
# *normalized* revision lists. This is also pretty gross.
list_subtract()
{
    TEMP=''
    for ARNG in `echo $1 | tr ',' ' '`; do

        # Parse range
        get_start_end "${ARNG}"
        ASTART="${START}"
        AEND="${END}"

        # Iterate over subtracted ranges
        for BRNG in `echo $2 | tr ',' ' '`; do

            # Parse range
            get_start_end "${BRNG}"
            BSTART="${START}"
            BEND="${END}"

            # Is this BRNG entirely before or past ARNG?
            if [ ${ASTART} -gt ${BEND} ]; then
                continue
            elif [ ${BSTART} -gt ${AEND} ]; then
                break
            fi

            # Keep the initial part of ARNG missed by BRNG (if anything)
            [ "${TEMP}" = "" ] || TEMP="${TEMP},"
            TEMP="${TEMP}${ASTART}-`expr ${BSTART} - 1`"

            # Keep going with whatever remains of ARNG (if anything)
            if [ ${AEND} -gt ${BEND} ]; then
                ASTART=`expr ${BEND} + 1`
            else
                AEND=`expr ${ASTART} - 1`
                break
            fi
        done

        # Keep what's left of ARNG (if anything)
        [ "${TEMP}" = "" ] || TEMP="${TEMP},"
        TEMP="${TEMP}${ASTART}-${AEND}"
    done

    # Normalize the result
    normalize_list "${TEMP}"
}

# Subroutine to return a normalized list to a more pleasant form
beautify_list()
{
    TEMP=''
    for RNG in `echo "$1" | tr ',' ' '`; do
        get_start_end "${RNG}"
        [ "${TEMP}" = "" ] || TEMP="${TEMP},"
        TEMP="${TEMP}${START}"
        if [ "${END}" != "${START}" ]; then
            TEMP="${TEMP}-${END}"
        fi
    done
    RETURN_VALUE="${TEMP}"
}

# Subroutine to convert working copy path or repo URL $1 to a repo URL
target_to_url()
{
    if [ -d "$1" -a -d "$1/.svn" ]; then
        RETURN_VALUE=`"${SVN_MERGE_SVN}" info "$1" \
            | grep ^URL: | sed -e 's/^URL: \(.*\)$/\1/g'`
    else
        RETURN_VALUE="$1"
    fi
}

# Subroutine to compute the root repo URL given wc path or repo URL $1.
# Constrained to svn command line tools, we are stuck with this ugly trial-
# and-error implementation.  It could be made faster with a binary search.
get_repo_root()
{
    target_to_url "$1"
    while TEMP=`dirname ${RETURN_VALUE}` &&
            "${SVN_MERGE_SVN}" proplist "${TEMP}" >/dev/null 2>&1; do
        RETURN_VALUE="${TEMP}"
    done
}

# Subroutine to convert repo URL $1 to a repo-local path
url_to_rlpath()
{
    get_repo_root $1
    RETURN_VALUE="${1#${RETURN_VALUE}}"
}

# Subroutine to get copyfrom info for a given target
# NOTE: repo root has no copyfrom info.  In this case null is returned.
get_copyfrom()
{
    target_to_url "$1"
    url_to_rlpath "${RETURN_VALUE}"
    TEMP=`"${SVN_MERGE_SVN}" log -v --xml --stop-on-copy "$1" | tr '\n' ' '`
    TEMP2=`echo "${TEMP}" | sed -e 's#^.*\(<path .*action="A".*>'"${RETURN_VALUE}"'</path>\).*$#\1#'`
    if [ "${TEMP}" = "${TEMP2}" ]; then
        RETURN_VALUE=""
    else
        RETURN_VALUE=`echo "${TEMP2}" | sed -e 's/^.* copyfrom-path="\([^"]*\)".*$/\1/'`
        RETURN_VALUE="${RETURN_VALUE}:"`echo "${TEMP2}" | sed -e 's/^.* copyfrom-rev="\([^"]*\)".*$/\1/'`
    fi
}

# The "init" action
init()
{
    # Check branch directory
    check_branch_dir

    # Get initial revision list if not explicitly specified
    if [ "${REVS}" = "" ]; then
        REVS="1-${HEAD_REVISION}"
    fi

    # Normalize and beautify ${REVS}
    normalize_list "${REVS}"
    beautify_list "${RETURN_VALUE}"
    REVS="${RETURN_VALUE}"

    report marking "${BRANCH_DIR}" as already containing \
        revisions "${REVS}" of "${HEAD_URL}".

    # Set properties
    set_integrated_revs "${HEAD_PATH}" "${REVS}" "${BRANCH_DIR}"

    # Write out commit message if desired
    if [ "${SVN_MERGE_COMMIT_FILE}" != "" ]; then
        echo Initialized merge tracking via "${NAME}" with revisions \
          "${REVS}" from > "${SVN_MERGE_COMMIT_FILE}"
        echo "${HEAD_URL}" >> "${SVN_MERGE_COMMIT_FILE}"
        report wrote commit message to "${SVN_MERGE_COMMIT_FILE}"
    fi
}

# "avail" action
avail()
{
    # Default --avail display type is "revisions"
    [ "${AVAIL_DISPLAY}" != "" ] || AVAIL_DISPLAY="revisions"

    # Calculate outstanding revisions
    list_subtract "1-${HEAD_REVISION}" "${MERGED_REVS}"
    AVAIL_REVS="${RETURN_VALUE}"

    # Limit to revisions specified by -r (if any)
    if [ "${REVS}" != "" ]; then
        normalize_list "${REVS}"
        list_subtract "1-${HEAD_REVISION}" "${RETURN_VALUE}"
        list_subtract "${AVAIL_REVS}" "${RETURN_VALUE}"
        AVAIL_REVS="${RETURN_VALUE}"
    fi

    # Show them, either numerically, in log format, or as diffs
    case "${AVAIL_DISPLAY}" in
        revisions)
            beautify_list "${AVAIL_REVS}"
            echo "${RETURN_VALUE}"
            ;;
        logs)
            for RNG in `echo "${AVAIL_REVS}" | tr ',' ' ' | tr '-' ':'`; do
                svn_command log --incremental -v -r "${RNG}" "${HEAD_URL}"
            done
            ;;
        diffs)
            for RNG in `echo "${AVAIL_REVS}" | tr ',' ' '`; do
                get_start_end "${RNG}"
                echo ''
                echo "${NAME}: changes in revisions ${RNG} follow"
                echo ''
                # Note: the starting revision number to 'svn diff' is
                # NOT inclusive so we have to subtract one from ${START}.
                svn_command diff -r `expr ${START} - 1`:${END} "${HEAD_URL}"
            done
            ;;
        *)
            error internal error
    esac
}

# "merge" action
merge()
{
    # Check branch directory
    check_branch_dir

    # Default to merging all outstanding revisions
    if [ "${REVS}" = "" ]; then
        REVS="1-${HEAD_REVISION}"
    fi

    # Parse desired merge revisions
    normalize_list "${REVS}"
    REVS="${RETURN_VALUE}"

    # Calculate subset of REVS which is not in MERGED_REVS
    list_subtract "${REVS}" "${MERGED_REVS}"
    REVS="${RETURN_VALUE}"
    beautify_list "${REVS}"
    BREVS="${RETURN_VALUE}"

    # Save "svnmerge-integrated" property value from before the merge
    get_all_integrated_revs
    OLDREVS="${RETURN_VALUE}"

    # Show what we're doing
    beautify_list "${MERGED_REVS}"
    report "\"${BRANCH_DIR}\" already contains revisions ${RETURN_VALUE}"
    report merging in 'revision(s)' "${BREVS}" from "${HEAD_URL}"

    # Do the merge(s). Note: the starting revision number to 'svn merge'
    # is NOT inclusive so we have to subtract one from ${START}.
    for RNG in `echo "${REVS}" | tr ',' ' '`; do
        get_start_end "${RNG}"
        svn_command merge -r `expr ${START} - 1`:${END} \
          "${HEAD_URL}" "${BRANCH_DIR}"
    done

    # Revert any merged-in changes to the "svnmerge-integrated" property.
    # We only want updates to this property to happen via explicit action.
    svn_command propset -q "${SVN_MERGE_PROP}" "${OLDREVS}" "${BRANCH_DIR}"

    # Write out commit message if desired
    if [ "${SVN_MERGE_COMMIT_FILE}" != "" ]; then
        echo "Merged revisions ${BREVS} via ${NAME} from" \
          > "${SVN_MERGE_COMMIT_FILE}"
        echo "${HEAD_PATH}" >> "${SVN_MERGE_COMMIT_FILE}"
        report wrote commit message to "${SVN_MERGE_COMMIT_FILE}"
    fi

    # Update list of merged revisions
    normalize_list "${MERGED_REVS},${REVS}"
    beautify_list "${RETURN_VALUE}"
    set_integrated_revs "${HEAD_PATH}" "${RETURN_VALUE}" "${BRANCH_DIR}"
}

# Get the desired action, compute getopt flags, and apply defaults
[ $# -ge 1 ] || usage_error no action specified
case "$1" in
    init)
        FLAGS="svnr:f:"
        BRANCH_DIR="."
        ;;
    avail)
        FLAGS="svldr:S:"
        BRANCH_DIR="."
        AVAIL_DISPLAY="revisions"
        ;;
    merge)
        FLAGS="svnr:f:S:"
        BRANCH_DIR="."
        ;;
    help)
        usage
        ;;
    -*)
        usage_error "no action specified"
        ;;
    *)
        usage_error "unknown action \"$1\""
        ;;
esac
ACTION="$1"
shift

# Unset variables we don't want to inherit from the environment
unset REVS

# Parse remaining command line
ARGS=`getopt "${FLAGS}" $*`
[ $? = 0 ] || usage
set -- ${ARGS}

for i in "$@"; do
    case "$i" in
        -f)
            SVN_MERGE_COMMIT_FILE="$2"
            shift; shift
            ;;
        -r)
            REVS="$2"
            shift; shift
            ;;
        -d)
            AVAIL_DISPLAY="diffs"
            shift
            ;;
        -l)
            AVAIL_DISPLAY="logs"
            shift
            ;;
        -v)
            SVN_MERGE_VERBOSE="true"
            shift
            ;;
        -n)
            SVN_MERGE_PRETEND="true"
            SVN_MERGE_SHOW_CMDS="true"
            shift
            ;;
        -s)
            SVN_MERGE_SHOW_CMDS="true"
            shift
            ;;
        -S)
            HEAD="$2"
            shift
            shift
            ;;
        --)
            shift
            break
            ;;
    esac
done

# Now parse the non-flag command line parameters
case "${ACTION}" in
    init)
        case $# in
            1)
                HEAD="$1"
                ;;
            0)
                ;;
            *)
                usage_error wrong number of parameters
        esac
        ;;
    avail)
        case $# in
            1)
                BRANCH_DIR="$1"
                ;;
            0)
                ;;
            *)
                usage_error wrong number of parameters
        esac
        ;;
    merge)
        case $# in
            1)
                BRANCH_DIR="$1"
                ;;
            0)
                ;;
            *)
                usage_error wrong number of parameters
        esac
        ;;
esac

# Validate branch-dir
[ -d "${BRANCH_DIR}" -a -d "${BRANCH_DIR}/.svn" ] || \
    error \"${BRANCH_DIR}\" is not a subversion working directory

# Normalize ${BRANCH_DIR}
normalize_url "${BRANCH_DIR}"
BRANCH_DIR="${RETURN_VALUE}"

# See if we need to upgrade revision metadata from previous schemes:
#   * revision and head data were stored individually in
#       svnmerge-[LABEL-]{head,revs} properties
#   * head used URL's instead of repo-local paths
if TEMP=`"${SVN_MERGE_SVN}" proplist "${BRANCH_DIR}" | grep -Ew "svnmerge-(.+-)?head"`; then
    echo "${NAME}: old property names detected; an upgrade is required."
    echo ''
    echo 'Please execute and commit these changes to upgrade:'
    echo ''
    INTEGRATED=""
    for OLD_PROP in ${TEMP}; do
        OLD_PROP=${OLD_PROP%-head}
        echo "  svn propdel ${OLD_PROP}-head ${BRANCH_DIR}"
        echo "  svn propdel ${OLD_PROP}-revs ${BRANCH_DIR}"
        HEAD=`"${SVN_MERGE_SVN}" propget "${OLD_PROP}-head" "${BRANCH_DIR}"`
        REVS=`"${SVN_MERGE_SVN}" propget "${OLD_PROP}-revs" "${BRANCH_DIR}"`
        if echo "${HEAD}" | grep -qE '^[[:alpha:]][-+.[:alnum:]]*://'; then
            url_to_rlpath "${HEAD}"
            HEAD="${RETURN_VALUE}"
        fi
        INTEGRATED="${INTEGRATED} ${HEAD}:${REVS}"
    done
    INTEGRATED=`echo "${INTEGRATED}" | xargs -n 1 | sort`
    echo "  svn propset ${SVN_MERGE_PROP} \"${INTEGRATED}\" ${BRANCH_DIR}"
    echo ''
    exit 1
fi

# Previous svnmerge versions allowed trailing /'s in the repository
# local path.  Newer versions of svnmerge will trim trailing /'s
# appearing in the command line, so if there are any properties with
# trailing /'s, they will not be properly matched later on, so require
# the user to change them now.
if TEMP=`"${SVN_MERGE_SVN}" propget "${SVN_MERGE_PROP}" "${BRANCH_DIR}" | grep -E '/+:[-,0-9]+$'`; then
    echo "${NAME}: old property values detected; an upgrade is required."
    echo ''
    echo 'Please execute and commit these changes to upgrade:'
    echo ''
    echo "svn propget ${SVN_MERGE_PROP} ${BRANCH_DIR} > svnmerge1.txt"
    echo "sed -e 's/\/*\(:[-,0-9]*\)/\1/g' < svnmerge1.txt > svnmerge2.txt"
    echo "svn propset ${SVN_MERGE_PROP} ${BRANCH_DIR} -F svnmerge2.txt"
    echo "rm svnmerge1.txt svnmerge2.txt"
    exit 1
fi

# Calculate ${HEAD_URL} and ${HEAD_PATH}
if [ -z "${HEAD}" ]; then
    if [ "${ACTION}" = "init" ]; then
        get_copyfrom "${BRANCH_DIR}"
        [ -z "${RETURN_VALUE}" ] && \
            error no copyfrom info available.  Explicit \"src\" argument required.
        HEAD_PATH=`echo "${RETURN_VALUE}" | cut -d: -f 1`
        [ -z "${REVS}" ] && REVS="1-"`echo "${RETURN_VALUE}" | cut -d: -f 2`
    else
        get_default_head "${BRANCH_DIR}"
        HEAD_PATH="${RETURN_VALUE}"
    fi
    get_repo_root "${BRANCH_DIR}"
    HEAD_URL="${RETURN_VALUE}/${HEAD_PATH}"
else
    # The source was given as a command line argument and is stored in
    # HEAD.  Ensure that the specified source does not end in a /,
    # otherwise it's easy to have the same source path listed more
    # than once in the integrated version properties, with and without
    # trailing /'s.
    HEAD=`echo ${HEAD} | sed -e 's/\/*$//'`

    target_to_url "${HEAD}"
    HEAD_URL="${RETURN_VALUE}"
    url_to_rlpath "${HEAD_URL}"
    HEAD_PATH="${RETURN_VALUE}"
fi

# Sanity check ${HEAD_URL}
echo "${HEAD_URL}" | grep -qE '^[[:alpha:]][-+.[:alnum:]]*://' ||
    error "\"${HEAD_URL}\" is not a valid URL or working directory"

# Normalize head URL
normalize_url "${HEAD_URL}"
HEAD_URL="${RETURN_VALUE}"

# Get previously merged revisions (except when --init)
if [ "${ACTION}" != "init" ]; then
    get_integrated_revs "${HEAD_PATH}" "${BRANCH_DIR}"
    normalize_list "${RETURN_VALUE}"
    MERGED_REVS="${RETURN_VALUE}"
fi

# Get latest revision of head
report checking latest revision of "${HEAD_URL}"
HEAD_REVISION=`"${SVN_MERGE_SVN}" proplist --revprop -r HEAD "${HEAD_URL}" \
  | sed -e 's/.* \([0-9]\{1,\}\).*$/\1/g' -e 1q`
if ! expr "${HEAD_REVISION}" : '[0-9]\{1,\}$' >/dev/null; then
    error "can't get head revision of \"${HEAD_URL}\" (got \"${REVISION}\")"
fi
report latest revision of "${HEAD_URL}" is "${HEAD_REVISION}"

# Perform action
${ACTION}
