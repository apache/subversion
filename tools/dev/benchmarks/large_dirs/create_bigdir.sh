#!/bin/bash

#  set SVNPATH to the root of your SVN code working copy

SVNPATH="$('pwd')/subversion"

# if using the installed svn, you may need to adapt the following.
# Uncomment the VALGRIND line to use that tool instead of "time".
# Comment the SVNSERVE line to use file:// instead of svn://.

SVN=${SVNPATH}/svn/svn
SVNADMIN=${SVNPATH}/svnadmin/svnadmin   
SVNSERVE=${SVNPATH}/svnserve/svnserve
# VALGRIND="valgrind --tool=callgrind"

# set your data paths here

WC=/dev/shm/wc
REPOROOT=/dev/shm

# number of items per folder on the first run. It will be doubled
# after every iteration. The test will stop if MAXCOUNT has been
# reached or exceeded (and will not be executed for MAXCOUNT).

FILECOUNT=1
MAXCOUNT=20000

# only 1.7 supports server-side caching and uncompressed data transfer 

SERVEROPTS="-c 0 -M 400"

# from here on, we should be good

TIMEFORMAT=$'%3R  %3U  %3S'
REPONAME=dirs
PORT=54321
if [ "${SVNSERVE}" != "" ] ; then
  URL=svn://localhost:$PORT/$REPONAME
else
  URL=file://${REPOROOT}/$REPONAME
fi

# create repository

rm -rf $WC $REPOROOT/$REPONAME
mkdir $REPOROOT/$REPONAME
${SVNADMIN} create $REPOROOT/$REPONAME
echo -e "[general]\nanon-access = write\n" > $REPOROOT/$REPONAME/conf/svnserve.conf

# fire up svnserve

if [ "${SVNSERVE}" != "" ] ; then
  VERSION=$( ${SVN} --version | grep " version" | sed 's/.*\ 1\.\([0-9]\).*/\1/' )
  if [ "$VERSION" -lt "7" ]; then
    SERVEROPTS=""
  fi

  ${SVNSERVE} -Tdr ${REPOROOT} ${SERVEROPTS} --listen-port ${PORT} --foreground &
  PID=$!
  sleep 1
fi

# construct valgrind parameters

if [ "${VALGRIND}" != "" ] ; then
  VG_TOOL=$( echo ${VALGRIND} | sed 's/.*\ --tool=\([a-z]*\).*/\1/' )
  VG_OUTFILE="--${VG_TOOL}-out-file"
fi

# print header

echo -n "using "
${SVN} --version | grep " version"
echo

# init working copy

rm -rf $WC
${SVN} co $URL $WC > /dev/null

# functions that execute an SVN command

run_svn() {
  if [ "${VALGRIND}" == "" ] ; then
    time ${SVN} $1 $WC/$2 $3 > /dev/null
  else
    ${VALGRIND} ${VG_OUTFILE}="${VG_TOOL}.out.$1.$2" ${SVN} $1 $WC/$2 $3 > /dev/null
  fi
}

run_svn_ci() {
  if [ "${VALGRIND}" == "" ] ; then
    time ${SVN} ci $WC/$1 -m "" -q > /dev/null
  else
    ${VALGRIND} ${VG_OUTFILE}="${VG_TOOL}.out.ci_$2.$1" ${SVN} ci $WC/$1 -m "" -q > /dev/null
  fi
}

run_svn_cp() {
  if [ "${VALGRIND}" == "" ] ; then
    time ${SVN} cp $WC/$1 $WC/$2 > /dev/null
  else
    ${VALGRIND} ${VG_OUTFILE}="${VG_TOOL}.out.cp.$1" ${SVN} cp $WC/$1 $WC/$2 > /dev/null
  fi
}

run_svn_get() {
  if [ "${VALGRIND}" == "" ] ; then
    time ${SVN} $1 $URL $WC -q > /dev/null
  else
    ${VALGRIND} ${VG_OUTFILE}="${VG_TOOL}.out.$1.$2" ${SVN} $1 $URL $WC -q > /dev/null
  fi
}

# main loop 

while [ $FILECOUNT -lt $MAXCOUNT ]; do
  echo "Processing $FILECOUNT files in the same folder"

  echo -ne "\tCreating files ... \t real   user    sys\n"
  mkdir $WC/$FILECOUNT
  for i in `seq 1 ${FILECOUNT}`; do
    echo "File number $i" > $WC/$FILECOUNT/$i
  done    

  echo -ne "\tAdding files ...   \t"
  run_svn add $FILECOUNT -q

  echo -ne "\tRunning status ... \t"
  run_svn st $FILECOUNT -q

  echo -ne "\tCommit files ...   \t"
  run_svn_ci $FILECOUNT add
  
  echo -ne "\tListing files ...  \t"
  run_svn ls $FILECOUNT

  echo -ne "\tUpdating files ... \t"
  run_svn up $FILECOUNT -q

  echo -ne "\tLocal copy ...     \t"
  run_svn_cp $FILECOUNT ${FILECOUNT}_c

  echo -ne "\tCommit copy ...    \t"
  run_svn_ci ${FILECOUNT}_c copy

  echo -ne "\tDelete 1 file ...  \t"
  run_svn del ${FILECOUNT}_c -q

  echo -ne "\tDeleting files ... \t"
  time (
  for i in `seq 2 ${FILECOUNT}`; do
    ${SVN} del $WC/${FILECOUNT}_c/$i -q
  done )

  echo -ne "\tCommit deletions ...\t"
  run_svn_ci ${FILECOUNT}_c del

  rm -rf $WC

  echo -ne "\tExport all ...  \t"
  run_svn_get export $FILECOUNT

  rm -rf $WC
  mkdir $WC

  echo -ne "\tCheck out all ...  \t"
  run_svn_get co $FILECOUNT

  let FILECOUNT=2*FILECOUNT
  echo ""
done

# tear down

if [ "${SVNSERVE}" != "" ] ; then
  echo "killing svnserve ... "
  kill $PID
fi

