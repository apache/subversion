#!/bin/sh

SVN_PROG=../../../client/svn
XML_DIR=../../xml
TEST_WC_PREFIX="test-wc"
COMMIT_RESULTFILE_NAME=commit

check_status()
{
    res=$?
    if [ $res -ne 0 ]; then
      echo Oops, problem: ${@-"(no further details)"}
      exit $res
    fi
}
