/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2004 CollabNet.  All rights reserved.
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
