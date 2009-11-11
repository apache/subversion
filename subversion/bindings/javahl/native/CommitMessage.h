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
 * @file CommitMessage.h
 * @brief Interface of the class CommitMessage
 */

#ifndef COMMITMESSAGE_H
#define COMMITMESSAGE_H

#include <jni.h>
struct apr_array_header_t;

/**
 * This class stores a Java object implementing the CommitMessage
 * interface.
 */
class CommitMessage
{
 public:
  /**
   * Deletes the global reference to m_jcommitMessage.
   */
  ~CommitMessage();

  jstring getCommitMessage(const apr_array_header_t *commit_items);

  /**
   * Create a C++ holding object for the Java object passed into the
   * native code.
   *
   * @param jcommitMessage The local reference to a
   * org.tigris.subversion.javahl.CommitMessage Java commit message
   * object.
   */
  static CommitMessage *makeCCommitMessage(jobject jcommitMessage);

 private:
  /**
   * A global reference to the Java object, because the reference
   * must be valid longer than the SVNClient.commitMessage call.
   */
  jobject m_jcommitMessage;

  /**
   * Create a commit message object.
   *
   * @param jcommitMessage The Java object to receive the callback.
   */
  CommitMessage(jobject jcommitMessage);
};

#endif  // COMMITMESSAGE_H
