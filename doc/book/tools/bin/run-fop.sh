#!/bin/sh

# find-fop: Attempt to run fop (or fop.sh), fail articulately otherwise.
#
# Usage:    find-fop.sh BOOK_TOP [FOP_ARGS...]
#
# This script is meant to be invoked by subversion/doc/book/Makefile.
# The first argument is the top of the book directory, that is,
# subversion/doc/book (*not* subversion/doc/book/book), and the
# remaining arguments are passed along to `fop'.

BOOK_TOP=${1}

if [ "${BOOK_TOP}X" = X ]; then
  echo "usage:  find-fop.sh BOOK_TOP [FOP_ARGS...]"
  exit 1
fi

shift

# The fop of last resort.
DESPERATION_FOP_DIR=${BOOK_TOP}/tools/fop
DESPERATION_FOP_PGM=${DESPERATION_FOP_DIR}/fop.sh

if [ "${FOP_HOME}X" = X ]; then
  FOP_HOME=${DESPERATION_FOP_DIR}
  export FOP_HOME
fi


# Unfortunately, 'which' seems to behave slightly differently on every
# platform, making it unreliable for shell scripts.  Just do it inline
# instead.  Also, note that we search for `fop' or `fop.sh', since
# different systems seem to package it different ways.
SAVED_IFS=${IFS}
IFS=:
for dir in ${PATH}; do
   if [ -x ${dir}/fop -a "${FOP_PGM}X" = X ]; then
     FOP_PGM=${dir}/fop
   elif [ -x ${dir}/fop.sh -a "${FOP_PGM}X" = X ]; then
     FOP_PGM=${dir}/fop.sh
   fi
done
IFS=${SAVED_IFS}

if [ "${FOP_PGM}X" = X ]; then
  FOP_PGM=${DESPERATION_FOP_PGM}
fi

echo "(Using '${FOP_PGM}' for FOP)"

# FOP is noisy on stdout, and -q doesn't seem to help, so stuff that
# garbage into /dev/null.
${FOP_PGM} $@ | grep -v "\[ERROR\]"

