/*
 * utility functions to handle the java class
 * org.tigris.subversion.lib.StatusKind
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

#ifndef SVN_JNI_STATUSKIND_H
#define SVN_JNI_STATUSKIND_H

/* includes */
#include <jni.h>
#include <svn_wc.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* functions */

/**
 * create a new org.tigris.subversion.lib.StatusKind instance 
 *
 * @param JNIEnv JNI Environment
 * @param hasException
 * @param statuskind integer representation of the appropriate constants
 */
jobject 
statuskind__create(JNIEnv *env, jboolean *hasException, jint statuskind);

/**
 * create a new org.tigris.subversion.lib.StatusKind instance
 * and use the corresponding svn_wc_status_kind as parameter
 */
jobject
statuskind__create_from_svn_wc_status_kind(JNIEnv *env, jboolean *hasException,
                                   enum svn_wc_status_kind statuskind);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
