#!/bin/bash
#
# this is a pretty simple script, just with one simple use,
# to build the native library libsvn_jni.so which is used
# in the class "org.tigris.subversion.ClientImpl"
#
# I used this script on my linux box.
# In the future this will be integrated into the central makefile
#
if [ -z "$JAVA_HOME" ] ; then
  echo "environment variable JAVA_HOME is not set. You need to change this."
  exit 1
fi
LIBS="-lapr -ldl -lmm -lcrypt -lpthread -lsvn_subr -lneon -lsvn_delta -lexpat -lsvn_client -lsvn_wc -lsvn_ra"
INCLUDE="-I$JAVA_HOME/include -I$JAVA_HOME/include/linux -I../../../include"
CC_ARGS="$INCLUDE $LIBS -pthread -shared"
NATIVE_CLASS_NAME="svn_jni"
OBJS="main.c date.c misc.c status.c hashtable.c string.c j.c entry.c clientimpl_status.c"
CC_ARGS="$CC_ARGS -o lib$NATIVE_CLASS_NAME.so $OBJS"
cc $CC_ARGS
