#!/bin/sh

# svn2cl.sh - front end shell script for svn2cl.xsl, calls xsltproc
#             with the correct parameters
# 
# Copyright (C) 2005, 2006 Arthur de Jong.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
# 3. The name of the author may not be used to endorse or promote
#    products derived from this software without specific prior
#    written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
# GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
# IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
# IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# exit on any failures
set -e
# report unset variables
set -u

# svn2cl version
VERSION="0.6"

# set default parameters
PWD=`pwd`
STRIPPREFIX=`basename $PWD`
LINELEN=75
GROUPBYDAY="no"
INCLUDEREV="no"
BREAKBEFOREMSG="no"
CHANGELOG=""
OUTSTYLE="cl"
SVNCMD="svn --verbose --xml log"
AUTHORSFILE=""

# do command line checking
prog=`basename $0`
while [ $# -gt 0 ]
do
  case "$1" in
    --strip-prefix)
      STRIPPREFIX="$2"
      shift 2 || { echo "$prog: option requires an argument -- $1";exit 1; }
      ;;
    --strip-prefix=*)
      STRIPPREFIX=`echo "$1" | sed 's/^--[a-z-]*=//'`
      shift
      ;;
    --linelen)
      LINELEN="$2";
      shift 2 || { echo "$prog: option requires an argument -- $1";exit 1; }
      ;;
    --linelen=*)
      LINELEN=`echo "$1" | sed 's/^--[a-z-]*=//'`
      shift
      ;;
    --group-by-day)
      GROUPBYDAY="yes";
      shift
      ;;
    -i|--include-rev)
      INCLUDEREV="yes";
      shift
      ;;
    --break-before-msg)
      BREAKBEFOREMSG="yes"
      shift
      ;;
    -f|--file|-o|--output)
      CHANGELOG="$2"
      shift 2 || { echo "$prog: option requires an argument -- $1";exit 1; }
      ;;
    --file=*|--output=*)
      CHANGELOG=`echo "$1" | sed 's/^--[a-z-]*=//'`
      shift
      ;;
    --stdout)
      CHANGELOG="-"
      shift
      ;;
    --authors)
      AUTHORSFILE="$2"
      shift 2 || { echo "$prog: option requires an argument -- $1";exit 1; }
      ;;
    --authors=*)
      AUTHORSFILE=`echo "$1" | sed 's/^--[a-z-]*=//'`
      shift
      ;;
    --html)
      OUTSTYLE="html"
      shift
      ;;
    -r|--revision|--targets|--username|--password|--config-dir|--limit)
      # add these as extra options to the command (with argument)
      arg=`echo "$2" | sed "s/'/'\"'\"'/g"`
      SVNCMD="$SVNCMD $1 '$arg'"
      shift 2 || { echo "$prog: option requires an argument -- $1";exit 1; }
      ;;
    --revision=*|--targets=*|--username=*|--password=*|--config-dir=*|--limit=*)
      # these are single argument versions of the above
      arg=`echo "$1" | sed "s/'/'\"'\"'/g"`
      SVNCMD="$SVNCMD '$arg'"
      shift
      ;;
    --stop-on-copy|--no-auth-cache|--non-interactive)
      # add these as simple options
      SVNCMD="$SVNCMD $1"
      shift
      ;;
    -V|--version)
      echo "$prog $VERSION";
      echo "Written by Arthur de Jong."
      echo ""
      echo "Copyright (C) 2005, 2006 Arthur de Jong."
      echo "This is free software; see the source for copying conditions.  There is NO"
      echo "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE."
      exit 0
      ;;
    -h|--help)
      echo "Usage: $prog [OPTION]... [PATH]..."
      echo "Generate a ChangeLog from a subversion repository."
      echo ""
      echo "  --strip-prefix=NAME  prefix to strip from all entries, defaults"
      echo "                       to the name of the current directory"
      echo "  --linelen=NUM        maximum length of an output line"
      echo "  --group-by-day       group changelog entries by day"
      echo "  -i, --include-rev    include revision numbers"
      echo "  --break-before-msg   add a line break between the log paths and"
      echo "                       log message"
      echo "  -o, --output=FILE    output to FILE instead of ChangeLog"
      echo "  -f, --file=FILE      alias for -o, --output"
      echo "  --stdout             output to stdout instead of ChangeLog"
      echo "  --authors=FILE       xml file to read for authors"
      echo "  --html               output as html instead of plain text"
      echo "  -h, --help           display this help and exit"
      echo "  -V, --version        output version information and exit"
      echo ""
      echo "PATH arguments and the following options are passed to the svn log"
      echo "command: -r, --revision, --target --stop-on-copy, --username,"
      echo "--password, --no-auth-cache, --non-interactive, --config-dir,"
      echo "--limit (see \`svn help log' for more information)."
      exit 0
      ;;
    -*)
      echo "$prog: invalid option -- $1"
      echo "Try \`$prog --help' for more information."
      exit 1
      ;;
    *)
      arg=`echo "$1" | sed "s/'/'\"'\"'/g"`
      SVNCMD="$SVNCMD '$arg'"
      shift
      ;;
  esac
done

# find the directory that this script resides in
prog="$0"
while [ -h "$prog" ]
do
  dir=`dirname "$prog"`
  prog=`ls -ld "$prog" | sed "s/^.*-> \(.*\)/\1/;/^[^/]/s,^,$dir/,"`
done
dir=`dirname $prog`
dir=`cd $dir && pwd`
XSL="$dir/svn2${OUTSTYLE}.xsl"

# find the absolute path of the authors file
# (otherwise xsltproc will find the file relative to svn2cl.xsl)
pwd=`pwd`
AUTHORSFILE=`echo "$AUTHORSFILE" | sed "/^[^/]/s|^|$pwd/|"`

# if no filename was specified, make one up
if [ -z "$CHANGELOG" ]
then
  CHANGELOG="ChangeLog"
  [ "$OUTSTYLE" != "cl" ] && CHANGELOG="$CHANGELOG.$OUTSTYLE"
fi

# redirect stdout to the changelog file if needed
if [ "x$CHANGELOG" != "x-" ]
then
  exec > "$CHANGELOG"
fi

# actually run the command we need
eval "$SVNCMD" | \
  xsltproc --stringparam strip-prefix "$STRIPPREFIX" \
           --stringparam linelen $LINELEN \
           --stringparam groupbyday $GROUPBYDAY \
           --stringparam include-rev $INCLUDEREV \
           --stringparam breakbeforemsg $BREAKBEFOREMSG \
           --stringparam authorsfile "$AUTHORSFILE" \
           "$XSL" -
