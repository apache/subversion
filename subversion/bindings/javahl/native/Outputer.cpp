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
 * @file Outputer.cpp
 * @brief Implementation of the class Outputer
 */

#include "Outputer.h"
#include "JNIUtil.h"
#include "JNIByteArray.h"

/**
 * Create an Outputer object.
 * @param jthis the Java object to be stored
 */
Outputer::Outputer(jobject jthis)
{
  m_jthis = jthis;
}

/**
 * Destroy an Inputer object.
 */
Outputer::~Outputer()
{
  // The m_jthis does not need to be destroyed, because it is the
  // passed in parameter to the Java method.
}

/**
 * Create a svn_stream_t structure for this object.  This will be used
 * as an output stream by Subversion.
 * @param pool  the pool, from which the structure is allocated
 * @return the output stream
 */
svn_stream_t *Outputer::getStream(const SVN::Pool &pool)
{
  // Create a stream with this as the baton and set the write and
  // close functions.
  svn_stream_t *ret = svn_stream_create(this, pool.pool());
  svn_stream_set_write(ret, Outputer::write);
  svn_stream_set_close(ret, Outputer::close);
  return ret;
}

/**
 * Implements svn_write_fn_t to write data out from Subversion.
 * @param baton     an Outputer object for the callback
 * @param buffer    the buffer for the write data
 * @param len       on input the buffer len, on output the number of written
 *                  bytes
 * @return a subversion error or SVN_NO_ERROR
 */
svn_error_t *Outputer::write(void *baton, const char *buffer, apr_size_t *len)
{
  JNIEnv *env = JNIUtil::getEnv();

  // An object of our class is passed in as the baton.
  Outputer *that = (Outputer*)baton;

  // The method id will not change during the time this library is
  // loaded, so it can be cached.
  static jmethodID mid = 0;
  if (mid == 0)
    {
      jclass clazz = env->FindClass(JAVA_PACKAGE"/OutputInterface");
      if (JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;

      mid = env->GetMethodID(clazz, "write", "([B)I");
      if (JNIUtil::isJavaExceptionThrown() || mid == 0)
        return SVN_NO_ERROR;

      env->DeleteLocalRef(clazz);
      if (JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;
    }

  // convert the data to a Java byte array
  jbyteArray data = JNIUtil::makeJByteArray((const signed char*)buffer,
                                            *len);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  // write the data
  jint written = env->CallIntMethod(that->m_jthis, mid, data);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  // return the number of bytes written
  *len = written;

  return SVN_NO_ERROR;
}

/**
 * Implements svn_close_fn_t to close the output stream.
 * @param baton     an Outputer object for the callback
 * @return a subversion error or SVN_NO_ERROR
 */
svn_error_t *Outputer::close(void *baton)
{
  JNIEnv *env = JNIUtil::getEnv();

  // An object of our class is passed in as the baton
  Outputer *that = (Outputer*)baton;

  // The method id will not change during the time this library is
  // loaded, so it can be cached.
  static jmethodID mid = 0;
  if (mid == 0)
    {
      jclass clazz = env->FindClass(JAVA_PACKAGE"/OutputInterface");
      if (JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;

      mid = env->GetMethodID(clazz, "close", "()V");
      if (JNIUtil::isJavaExceptionThrown() || mid == 0)
        return SVN_NO_ERROR;

      env->DeleteLocalRef(clazz);
      if (JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;
    }

  // Call the Java object, to close the stream.
  env->CallVoidMethod(that->m_jthis, mid);
  // No need to check for exception here because we return anyway.

  return SVN_NO_ERROR;
}
