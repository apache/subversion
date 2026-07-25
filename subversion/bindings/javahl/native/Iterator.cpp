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
 * @file Iterator.cpp
 * @brief Implementation of the class Iterator
 */

#include "Iterator.h"

namespace {
jobject init_iterator(jobject jiterable, bool persistent)
{
  // A null iterable is allowed, we'll leave the iterator null and
  // nothing will happen in hasNext() and next().
  if (!jiterable)
    return NULL;

  JNIEnv* env = JNIUtil::getEnv();
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  static jmethodID iterator_mid = 0;
  if (0 == iterator_mid)
    {
      jclass cls = env->FindClass("java/lang/Iterable");
      if (JNIUtil::isExceptionThrown())
        return NULL;
      iterator_mid = env->GetMethodID(cls, "iterator",
                                      "()Ljava/util/Iterator;");
      if (JNIUtil::isExceptionThrown())
        return NULL;
    }

  jobject jiterator = env->CallObjectMethod(jiterable, iterator_mid);
  if (JNIUtil::isExceptionThrown())
    return NULL;
  return (persistent ? env->NewGlobalRef(jiterator) : jiterator);
}
} // anonymous namespace


Iterator::Iterator(jobject jiterable)
  : m_persistent(false),
    m_jiterator(init_iterator(jiterable, false))
{}

Iterator::Iterator(jobject jiterable, bool)
  : m_persistent(true),
    m_jiterator(init_iterator(jiterable, true))
{}

Iterator::~Iterator()
{
  if (m_persistent && m_jiterator)
    JNIUtil::getEnv()->DeleteGlobalRef(m_jiterator);
}

bool Iterator::hasNext() const
{
  if (!m_jiterator)
    return false;

  JNIEnv* env = JNIUtil::getEnv();

  static jmethodID hasNext_mid = 0;
  if (0 == hasNext_mid)
    {
      jclass cls = env->FindClass("java/util/Iterator");
      if (JNIUtil::isExceptionThrown())
        return false;
      hasNext_mid = env->GetMethodID(cls, "hasNext", "()Z");
      if (JNIUtil::isExceptionThrown())
        return false;
    }

  return bool(env->CallBooleanMethod(m_jiterator, hasNext_mid));
}

jobject Iterator::next() const
{
  if (!m_jiterator)
    return NULL;

  JNIEnv* env = JNIUtil::getEnv();
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  static jmethodID next_mid = 0;
  if (0 == next_mid)
    {
      jclass cls = env->FindClass("java/util/Iterator");
      if (JNIUtil::isExceptionThrown())
        return NULL;
      next_mid = env->GetMethodID(cls, "next", "()Ljava/lang/Object;");
      if (JNIUtil::isExceptionThrown())
        return NULL;
    }

  return env->CallObjectMethod(m_jiterator, next_mid);
}
