#!/bin/bash
#
# this is a pretty stupid script to build
# all of the java stuff included in the subdirs.
# will be replaced soon with some real makefile 
# thing...

# first step: compile all of the java sources
javac -classpath . org/tigris/subversion/*.java
javac -classpath . org/tigris/subversion/lib/*.java
javac -classpath . *.java
javac -classpath $CLASSPATH:.:tests tests/*.java

# second step: build the JNI header
javah -classpath . -o svn_jni.h org.tigris.subversion.lib.ClientImpl
javah -classpath tests -o tests/nativewrapper.h NativeWrapper

# third step: give some extra information
echo "to run the tests, start them with:"
echo "run-tests.sh"

