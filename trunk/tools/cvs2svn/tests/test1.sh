#!/bin/sh -e

./test1-create-repo.sh  || {
    echo FAIL: $0 $1 didn\'t create repository.
     exit 1
}
../cvs2svn.pl -d test1/repo test1/svn || {
    echo FAIL: $0 $1 cvs2svn.pl died.
    exit 1
}
exit 0
