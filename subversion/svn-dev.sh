#!/bin/sh

echo ""
echo "I hope you sourced this script, i.e.:"
echo "   'source svn-dev.sh'"
echo "instead of running it the standard way; it wants to affect parent env."
echo ""

if [ -d ${HOME}/projects/subversion ]; then
   WC=${HOME}/projects/subversion
elif [ -d ${HOME}/src/subversion ]; then
   WC=${HOME}/src/subversion
else
   echo "I need to know where your working copy is."
   echo "Please modify me accordingly (subversion/subversion/svn-dev.sh)."
   exit 1
fi

LD_LIBRARY_PATH=.:${WC}/subversion/libsvn_string:${WC}/subversion/libsvn_subr:${WC}/subversion/libsvn_delta:${WC}/subversion/libsvn_wc:${WC}/subversion/apr:${LD_LIBRARY_PATH}

export LD_LIBRARY_PATH
