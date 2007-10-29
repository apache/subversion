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
 * @file MessageReceiver.h
 * @brief Interface of the class MessageReceiver
 */

#ifndef MESSAGERECEIVER_H
#define MESSAGERECEIVER_H

#include <jni.h>

/**
 * This class holds a SVNAdmin.MessageReceiver objects and sends
 * messages to it.
 */
class MessageReceiver
{
  /**
   * A local reference to the Java object.
   */
  jobject m_jthis;
 public:
  MessageReceiver(jobject jthis);
  ~MessageReceiver();
  void receiveMessage(const char *message);
};

#endif  // MESSAGERECEIVER_H
