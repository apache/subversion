/*
 * utility functions to handle the java class
 * org.tigris.subversion.lib.Nodekind
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

#ifndef SVN_JNI_NODEKIND_H
#define SVN_JNI_NODEKIND_H

/* includes */
#include <jni.h>
#include <svn_types.h>

/* functions */

/**
 * create a new org.tigris.subversion.lib.Nodekind instance 
 *
 * @param JNIEnv JNI Environment
 * @param hasException
 * @param nodekind integer representation of the appropriate constants
 */
jobject 
nodekind__create(JNIEnv *env, jboolean *hasException, jint nodekind);

/**
 * create a new org.tigris.subversion.lib.Nodekind instance
 * and use the corresponding svn_nodekind_t as parameter
 */
jobject
nodekind__create_from_svn_node_kind(JNIEnv *env, jboolean *hasException,
                                   enum svn_node_kind nodekind);

#endif
/* 
 * local variables:
 * eval: (load-file "../../../../tools/dev/svn-dev.el")
 * end: 
 */
