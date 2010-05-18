#!/bin/sh

# run-fop: Attempt to run fop (or fop.sh), fail articulately otherwise.
#
# Usage:    run-fop.sh [FOP_ARGS...]
#
# This script is meant to be invoked by book translation Makefiles.
# Arguments are passed along to `fop'.

# If the user has a .foprc, source it.
if [ -f ${HOME}/.foprc ]; then
  . ${HOME}/.foprc
fi

# The fop of last resort.
DESPERATION_FOP_DIR="`dirname \"$0\"`/../fop"
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
PATH=${PATH}:${FOP_HOME}
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

