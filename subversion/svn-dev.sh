#!/bin/sh

echo ""
echo "Note that you should source this script instead of running it, i.e.:"
echo "   'source svn-dev.sh'"
echo "because it needs to affect the parent's environment."
echo ""

if [ -d ${HOME}/projects/subversion ]; then
   WC=${HOME}/projects
elif [ -d ${HOME}/src/subversion ]; then
   WC=${HOME}/src
else
   echo "I need to know where your working copy is."
   echo "Please modify me accordingly (subversion/subversion/svn-dev.sh)."
   exit 1
fi

LD_LIBRARY_PATH=.:${WC}/subversion/libsvn_string:${WC}/subversion/apr:${LD_LIBRARY_PATH}

export LD_LIBRARY_PATH
