/**
 * @copyright
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
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
