/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
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
 * @file VersionExtended.cpp
 * @brief Implementation of the VersionExtended class
 */

#include "JNIUtil.h"
#include "VersionExtended.h"

VersionExtended *
VersionExtended::getCppObject(jobject jthis)
{
  if (!jthis)
    return NULL;

  static jfieldID fid = 0;
  jlong cppAddr = SVNBase::findCppAddrForJObject(
      jthis, &fid, JAVAHL_CLASS("/types/VersionExtended"));
  return (cppAddr == 0 ? NULL : reinterpret_cast<VersionExtended *>(cppAddr));
}

namespace {
static jobject getWrapperAddress(jobject jthat, volatile jfieldID *fid)
{
  JNIEnv *const env = JNIUtil::getEnv();
  if (!*fid)
    {
      *fid = env->GetFieldID(env->GetObjectClass(jthat), "wrapper",
                             JAVAHL_ARG("/types/VersionExtended;"));
      if (JNIUtil::isJavaExceptionThrown())
        {
          *fid = 0;
          return 0;
        }
    }

  jobject jthis = env->GetObjectField(jthat, *fid);
  if (JNIUtil::isJavaExceptionThrown())
    return 0;
  return jthis;
}
} // anonymous namespace

const VersionExtended *
VersionExtended::getCppObjectFromLinkedLib(jobject jthat)
{
  static volatile jfieldID fid;
  return getCppObject(getWrapperAddress(jthat, &fid));
}

const VersionExtended *
VersionExtended::getCppObjectFromLoadedLib(jobject jthat)
{
  static volatile jfieldID fid;
  return getCppObject(getWrapperAddress(jthat, &fid));
}

const VersionExtended *
VersionExtended::getCppObjectFromLinkedLibIterator(jobject jthat)
{
  static volatile jfieldID fid;
  return getCppObject(getWrapperAddress(jthat, &fid));
}

const VersionExtended *
VersionExtended::getCppObjectFromLoadedLibIterator(jobject jthat)
{
  static volatile jfieldID fid;
  return getCppObject(getWrapperAddress(jthat, &fid));
}

VersionExtended::~VersionExtended() {}

void VersionExtended::dispose(jobject jthis)
{
  static jfieldID fid = 0;
  SVNBase::dispose(jthis, &fid, JAVAHL_CLASS("/types/VersionExtended"));
}
