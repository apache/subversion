#!/bin/sh

echo ""
echo "I hope you sourced this script, i.e.:"
echo "   'source svn-dev.sh'"
echo "instead of running it the standard way; it wants to affect parent env."
echo ""

if [ "X${SVN_WC}" = "X" ]; then
  if [ -d ${HOME}/projects/subversion ]; then
     SVN_WC=${HOME}/projects/subversion
  elif [ -d ${HOME}/src/subversion ]; then
     SVN_WC=${HOME}/src/subversion
  else
     echo "I need to know where your working copy is."
     echo "Please set the SVN_WC environment variable to that directory."
     exit 1
  fi
else
  if [ ! -d ${SVN_WC} ]; then
    echo "${SVN_WC} does not exist, update your SVN_WC environment variable."
  fi
fi

LD_LIBRARY_PATH=.:${SVN_WC}/subversion/libsvn_string:${SVN_WC}/subversion/libsvn_subr:${SVN_WC}/subversion/libsvn_delta:${SVN_WC}/subversion/libsvn_wc:${SVN_WC}/subversion/apr:${LD_LIBRARY_PATH}

export SVN_WC
export LD_LIBRARY_PATH
