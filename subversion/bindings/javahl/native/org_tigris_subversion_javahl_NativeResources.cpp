/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2005-2007 CollabNet.  All rights reserved.
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
 * @endcopyright
 *
 * @file org_tigris_subversion_javahl_NativeResources.cpp
 * @brief Implementation of the native methods in the Java class
 * NativeResources.
 */

#include "JNIUtil.h"
#include "../include/org_tigris_subversion_javahl_NativeResources.h"

JNIEXPORT void JNICALL
Java_org_tigris_subversion_javahl_NativeResources_initNativeLibrary
(JNIEnv *env, jclass jclazz)
{
  // No usual JNIEntry here, as the prerequisite native library
  // initialization is performed here.
  JNIUtil::JNIGlobalInit(env);
}
