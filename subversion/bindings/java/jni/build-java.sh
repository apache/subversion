#!/bin/bash
#
# this is a pretty stupid script to build
# all of the java stuff included in the subdirs.
# will be replaced soon with some real makefile 
# thing...

# first step: compile all of the java sources
javac org/tigris/subversion/*.java
javac org/tigris/subversion/lib/*.java
javac *.java

# second step: build the JNI header
javah -o svn_jni.h org.tigris.subversion.lib.ClientImpl

# third step: give some extra information
echo "to run the examples, first set the shell variable."
echo "type:"
echo "export LD_LIBRARY_PATH=."
echo "otherwise the library wont be found!"
echo "run the sample apps with:"
echo "java TestAdd"


