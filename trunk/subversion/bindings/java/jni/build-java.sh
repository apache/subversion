#!/bin/bash
#
# this is a pretty stupid script to build
# all of the java stuff included in the subdirs.
# will be replaced soon with some real makefile 
# thing...

CLASSPATH=$CLASSPATH:.:tests

# first step: compile all of the java sources
javac org/tigris/subversion/*.java
javac org/tigris/subversion/lib/*.java
javac *.java
javac tests/*.java

# second step: build the JNI header
javah -o svn_jni.h org.tigris.subversion.lib.ClientImpl
javah -o tests/nativewrapper.h NativeWrapper

# third step: give some extra information
echo "to run the tests, start them with:"
echo "run-tests.sh"



