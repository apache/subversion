#!/bin/bash
#
# this is a pretty simple script, just with one simple use,
# to build the native library libsvn_jni.so which is used
# in the class "org.tigris.subversion.ClientImpl"
#
# I used this script on my linux box.
# In the future this will be integrated into the central makefile
#
# Feel invited to make improvement or integrate this
# in the main build process
#
if [ -z "$JAVA_HOME" ] ; then
  echo "environment variable JAVA_HOME is not set. You need to change this."
  exit 1
fi

#here come the common settings
COMMON_LIBS="-lapr -ldl -lmm -lcrypt -lpthread -lsvn_subr -lneon -lsvn_delta -lexpat -lsvn_client -lsvn_wc -lsvn_ra"
COMMON_INCLUDE="-I$JAVA_HOME/include -I$JAVA_HOME/include/linux -I../../../include"
COMMON_ARGS="-pthread -shared"
COMMON_OBJS="date.c misc.c status.c hashtable.c string.c j.c entry.c vector.c schedule.c revision.c nodekind.c statuskind.c"

#first, compile the main library
LIBS="$COMMON_LIBS"
OBJS="$COMMON_OBJS main.c clientimpl_status.c"
INCLUDE="$COMMON_INCLUDE"
NATIVE_CLASS_NAME="svn_jni"
ARGS="$COMMON_ARGS $INCLUDE $LIBS -o lib$NATIVE_CLASS_NAME.so $OBJS"
#cc $ARGS

#second, compile the library that is needed by the unit test
#INCLUDE="$COMMON_INCLUDE -I. -Itests"
INCLUDE="$COMMON_INCLUDE"
OBJS="$COMMON_OBJS tests/main.c tests/nativewrapper.c"
#OBJS="$COMMON_OBJS"
NATIVE_CLASS_NAME="svn_jni_nativewrapper"
OUTPUT="-o lib$NATIVE_CLASS_NAME.so"
ARGS="$COMMON_ARGS $INCLUDE $LIBS -o lib$NATIVE_CLASS_NAME.so $OBJS"
echo $ARGS
cc $ARGS



