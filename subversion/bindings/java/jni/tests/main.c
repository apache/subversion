/**
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
#include <apr.h>

/*
 * JNI OnLoad Handler
 */
JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *jvm, void *reserved)
{
  apr_initialize();

  return JNI_VERSION_1_2;
}

/*
 * JNI UnLoad Handler
 */
JNIEXPORT OnUnload(JavaVM *jvm, void *reserved)
{
  apr_terminate();
}
