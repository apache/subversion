#!/bin/sh
#######################################################################
#
# svnmirror-test.sh
#
# Script for testing svn-mirror.sh
#
# License:
#    The same as svn itself. for latest version check:
#    http://svn.apache.org/repos/asf/subversion/trunk/subversion/LICENSE
#
#######################################################################

SVNMIRROR="$PWD/svnmirror.sh"

if [ ! -d tmp ]; then
	mkdir tmp
fi
cd tmp
TMPDIR="$PWD"

TMPOUT="$TMPDIR/svnmirror.out"
TMPERR="$TMPDIR/svnmirror.err"

RHOST="localhost"
LREPOS="${TMPDIR}/lrepos"
RREPOS="${TMPDIR}/rrepos"
LWC="${TMPDIR}/lwc"
RWC="${TMPDIR}/rwc"

PARAMS="push:none pull:none push:filter pull:filter"

mirror_check() {
	EXPRC="$1"
	EXPOUT="$2"
	EXPERR="$3"
	ERR=0
	$SVNMIRROR > $TMPOUT 2> $TMPERR
	RC="$?"
	RC2=0
	if [ $RC -gt 0 ]; then
		RC2=1
	fi
	if [ "$RC2" != "$RC" ]; then
		echo "unexpected return code: $RC (expected $EXPRC)"
		ERR=1
	fi
	if [ -n "$EXPOUT" ]; then
		egrep "$EXPOUT" "$TMPOUT" > /dev/null 2>&1
		GRC="$?"
		if [ $GRC -gt 0 ]; then
			echo "expected out not found"
			ERR=1
		fi
	fi
	if [ -n "$EXPERR" ]; then
		egrep "$EXPERR" "$TMPERR" > /dev/null 2>&1
		GRC="$?"
		if [ $GRC -gt 0 ]; then
			echo "expected err not found"
			ERR=1
		fi
	fi
	if [ "$ERR" = "0" ]; then
		echo "OK."
	else
		echo "+++ STDOUT +++"
		cat "$TMPOUT"
		echo "+++ STDERR +++"
		cat "$TMPERR"
		echo "+++ END +++"
	fi
}

for P in ${PARAMS}; do
	MODE=`echo "$P" | sed 's/:.*$//'`
	FLTR=`echo "$P" | sed 's/^.*://'`
	echo ""
	echo "*** Testing $MODE filter $FLTR ***"
	echo ""
	if [ "$MODE" = "push" ]; then
		SRCREPOS="$LREPOS"
		DSTREPOS="$RREPOS"
		SRCWC="$LWC"
		DSTWC="$RWC"
	else
		SRCREPOS="$RREPOS"
		DSTREPOS="$LREPOS"
		SRCWC="$RWC"
		DSTWC="$LWC"
	fi
	FILTER=""
	if [ "$FLTR" = "filter" ]; then
		FILTER="include trunk"
	fi
	if [ -d "$LREPOS" ]; then
		rm -rf "$LREPOS"
	fi
	if [ -d "$RREPOS" ]; then
		rm -rf "$RREPOS"
	fi
	if [ -d "$LWC" ]; then
		rm -rf "$LWC"
	fi
	if [ -d "$RWC" ]; then
		rm -rf "$RWC"
	fi
	svnadmin create "$RREPOS"
	svnadmin create "$LREPOS"
	svn co "file://$LREPOS" "$LWC" > /dev/null
	svn co "file://$RREPOS" "$RWC" > /dev/null
	export MODE FILTER LREPOS RREPOS RHOST

	echo "Test 1: both repos empty"
	mirror_check 0 "both repositories are already at" ""

	echo "Test 2: add trunk branches and tags."
	svn mkdir "$SRCWC/trunk" > /dev/null
	svn mkdir "$SRCWC/branches" > /dev/null
	svn mkdir "$SRCWC/tags" > /dev/null
	svn ci "$SRCWC" -m "add trunk branches and tags." > /dev/null
	mirror_check 0 "successfull completed." ""

	echo "Test 3: filter check."
	svn ls "file://$SRCREPOS" > "$TMPOUT"
	svn ls "file://$DSTREPOS" > "$TMPERR"
	if [ "$FLTR" = "none" ]; then
		CMD="cat"
	else
		CMD="egrep ^trunk/"
	fi
	N=`$CMD "$TMPOUT" | diff - "$TMPERR" | wc -l`
	if [ $N -gt 0 ]; then
		echo "error."
		echo "+++ STDOUT +++"
		cat "$TMPOUT"
		echo "+++ STDERR +++"
		cat "$TMPERR"
		echo "+++ END +++"
	else
		echo "OK."
	fi

	# should be the last test bacause all following would fail
	echo "Test 99: commit to destination repos."
	svn up "$DSTWC" > /dev/null
	echo "wrong repos" > "$DSTWC/trunk/wrong_repos.txt"
	svn add "$DSTWC/trunk/wrong_repos.txt" > /dev/null
	svn ci "$DSTWC" -m "commit to wrong repos" > /dev/null
	mirror_check 1 "" "revision of .* repos is higher than .* one"
done

