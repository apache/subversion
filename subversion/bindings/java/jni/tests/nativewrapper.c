/**
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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
 *
 */

#include <jni.h>
#include "../svn_jni_tests.h"
#include "../vector.h"

JNIEXPORT jobject JNICALL 
Java_NativeWrapper_vectorCreate
(JNIEnv *env, jclass vectorClass)
{
  return vector__create(env, NULL);
}

JNIEXPORT void JNICALL 
Java_NativeWrapper_vectorAdd
(JNIEnv *env, jclass clazz, jobject vector, jobject value)
{
  vector__add(env, vector, value, NULL);
}

/* 
 * local variables:
 * eval: (load-file "../../../../../../../svn-dev.el")
 * end: 
 */


