#!/bin/bash
#
# SVN Syntax Check Hook Script
# Copyright (c) 2007, Lucas Nealan <lucas@sizzo.org>, Facebook Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  
# 02110-1301  USA
#
# --------------------------------------------------------------------
#
# This script provides language independant syntax checking 
# functionality intended to be invoked from a subversion pre-commit
# hook.
#
# Invocation: /path/to/syntax-check $1 $2
#         or: source syntax-check 
#
# Requires bash 3.x or higher.
#

FPATTERN="\.\(php\|phpt\)$"
FLANG="PHP" 
SYNTAX_CMD="php"
SYNTAX_ARGS="-l"
# address to email notifications of syntax errors
NOTIFY_SYNTAX="user@domain.tld"
# adderss to notify for syntax script failures
NOTIFY_ERROR="user@domain.tld" 
# log of syntax errors, must be writable by svn server users
SYNTAX_LOG="/tmp/syntax.log"
BYPASSPW="change_this_syntax_bypass_password_please"

[ -z "$REPOS" ] && REPOS="$1"
[ -z "$TXN" ]   && TXN="$2"
[ -z "$MODE" ]  && if [ -n "$3" ]; then MODE="$3"; else MODE="-t"; fi
[ -z "$SVNLOOK" ] && SVNLOOK=svnlook
[ -z "$LOG" ] && LOG=`$SVNLOOK log $MODE "$TXN" "$REPOS"`
[ -z "$DIFF" ] && DIFF=`$SVNLOOK diff $MODE "$TXN" "$REPOS"`
[ -z "$AUTHOR" ] && AUTHOR=`$SVNLOOK author $MODE "$TXN" "$REPOS"`
[ -z "$CHANGEDFILES" ] && CHANGEDFILES=`$SVNLOOK changed $MODE "$TXN" "$REPOS"`
[ -n "$SYNTAXENABLED" ] && SYNTAXENABLED="1"

syntaxclean() {
  [ -d $1 ] && rm -rf $1
}

syntaxexit() {
  echo $1 >> /dev/stderr
  echo $1 >> $SYNTAX_LOG

  # save working dir and debug files
  if [ -d $WORKING ]; then
    [ -n "$DIFF" ] && echo $"$DIFF" > $WORKING/patch
    [ -n "$LOG" ] && echo $"$LOG" > $WORKING/svnlog
    [ -n "$CHANGED" ] && echo $"$CHANGED" > $WORKING/changed
    mv $WORKING $WORKING.failed
  fi

  [ -n "$NOTIFY_ERROR" ] && echo "$1 ($WORKING.failed)" | mail -s "syntax commit failed" $NOTIFY_ERROR

  # clean working dir if specified
  [ -n $2 ] && syntaxclean $2
  exit 1
}

function strlpad() {
  STR="$1"
  CHAR=$2
  LEN=$3
  L=${#STR}
  D=$((LEN-L))
  echo "$STR""`printf '%'$D's'| tr \  \"$CHAR\"`"
}

function errormessage() {
  echo
  HEADER=`strlpad "Error" "-" 76`
  echo "|--$HEADER|" >&2
  SPACES="`strlpad '' ' ' 78`"
  echo "|$SPACES|" >&2
  ERROR=`strlpad "$1" " " 77`
  echo "| $ERROR|" >&2
  echo "|$SPACES|" >&2
  LINE=`strlpad "" "-" 78`
  echo "|$LINE|" >&2
  echo 
}

if [ "$SYNTAXENABLED" == "1" ]; then
  # allow selective bypass of syntax check for commits
  [[ "$LOG" =~ "$BYPASSPW" ]] && return;

  # get changed file list and count
  NUMMATCHCHANGED=0
  if [ -n "$CHANGEDFILES" ]; then
    MATCHCHANGED=`echo $"$CHANGEDFILES" | grep "$FPATTERN"`
    [ -n "$MATCHCHANGED" ] && NUMMATCHCHANGED=`echo $"$MATCHCHANGED" | wc -l`
  fi

  # make sure matched files were changed
  if [ $NUMMATCHCHANGED -gt 0 ]; then
    # create temporary working directory 
    WORKING=/tmp/$(basename $0).$$
    [ -d $WORKING ] && rm -rf $WORKING
    mkdir $WORKING || syntaxexit "failed to create temp dir for syntax check: $WORKING"
    cd $WORKING

    # export changed files (no dirs) from local repo (speed)
    IFS=$'\n'
    for LINE in $MATCHCHANGED; do
      IFS=' '
      WORDS=($LINE)
      FSTATUS=${WORDS[0]}
      FNAME=${WORDS[1]}

      # only export modified and deleted files. new files wont exist in repo yet
      if [ "$FSTATUS" == "U" ] || [ "$FSTATUS" == "UU" ] || [ "$FSTATUS" == "A" ]; then
        TMPFNAME=${FNAME//\//.}
        $SVNLOOK cat $MODE "$TXN" "$REPOS" $FNAME > $TMPFNAME

        file `which $SYNTAX_CMD` || syntaxexit "unablet to find systax command binary: $SYNTAX_CMD"
        SYNTAXERROR=`$SYNTAX_CMD $SYNTAX_ARGS $TMPFNAME 2> $WORKING/$TMPFNAME.STDERR`
        SYNTAXRETURN=$?
        [ -s "$WORKING/$TMPFNAME.STDERR" ] && SYNTAXWARNING=`cat $WORKING/$TMPFNAME.STDERR`

        if [ "$SYNTAXRETURN" -ne 0 ] || [ -n "$SYNTAXWARNING" ]; then
          [ -n "$SYNTAXWARNING" ] && SYNTAXERROR=$SYNTAXWARNING

          # cleanup HTML out of PHP parse error so a human can read it
          if [ "$FLANG" == "PHP" ]; then
            SYNTAXERROR=`echo $SYNTAXERROR | sed -e 's/<[^<]*>//g' | cut -d',' -f 2`
            SYNTAXERROR=`echo $SYNTAXERROR | sed -e 's/\(on line [0-9]* \)/\1\n/g'`
          fi
          
          # sloppy email notification
          ETMP=$WORKING/sloppy.txt
          echo "$FLANG Syntax Error: $SYNTAXERROR" > $ETMP
          echo >> $ETMP
          echo Log: $LOG >> $ETMP
          echo >> $ETMP
          echo $"$DIFF" >> $ETMP
          cat $ETMP | mail -s "SVN SYNTAX ERROR: $AUTHOR" $NOTIFY_SYNTAX
          rm -f $ETMP

          echo "$AUTHOR: $FLANG Syntax Error: $SYNTAXERROR" >> $SYNTAX_LOG
          errormessage "$FLANG Syntax Error: $SYNTAXERROR"
          syntaxclean $WORKING
          exit 1
        fi
      fi
    done
    # exit within a loop only sets the return value of the loop itself, check this to exit
    [ $? -ne 0 ] && exit 1
  fi 
  syntaxclean $WORKING
fi
