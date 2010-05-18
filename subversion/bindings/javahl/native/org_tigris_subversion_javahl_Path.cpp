/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
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
 * @file org_tigris_subversion_javahl_Path.cpp
 * @brief Implementation of the native methods in the Java class Path
 */

#include <jni.h>
#include "../include/org_tigris_subversion_javahl_Path.h"
#include "JNIUtil.h"
#include "JNIStackElement.h"
#include "JNIStringHolder.h"
#include "Path.h"

JNIEXPORT jboolean JNICALL
Java_org_tigris_subversion_javahl_Path_isValid(JNIEnv *env,
                                               jclass jthis,
                                               jstring jpath)
{
  JNIEntry(Path, isValid);
  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return JNI_FALSE;

  return Path::isValid(path);
}
