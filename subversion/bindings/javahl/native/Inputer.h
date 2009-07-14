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
 * @file Inputer.h
 * @brief Interface of the class Inputer
 */

#ifndef INPUTER_H
#define INPUTER_H

#include <jni.h>
#include "svn_io.h"
#include "Pool.h"

/**
 * This class contains a Java objects implementing the interface Inputer and
 * implements the functions read & close of svn_stream_t.
 */
class Inputer
{
 private:
  /**
   * A local reference to the Java object.
   */
  jobject m_jthis;
  static svn_error_t *read(void *baton, char *buffer, apr_size_t *len);
  static svn_error_t *close(void *baton);
 public:
  Inputer(jobject jthis);
  ~Inputer();
  svn_stream_t *getStream(const SVN::Pool &pool);
};

#endif // INPUTER_H
