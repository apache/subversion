/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003 CollabNet.  All rights reserved.
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
 * @file MessageReceiver.cpp
 * @brief Implementation of the class MessageReceiver
 */

#include "MessageReceiver.h"
#include "JNIUtil.h"

/**
 * Create a new object and store the local reference to the Java
 * object.
 */
MessageReceiver::MessageReceiver(jobject jthis)
{
  m_jthis = jthis;
}

/**
 * Destroy the object.
 */
MessageReceiver::~MessageReceiver()
{
  // The m_callback does not need to be destroyed, because it is the
  // passed in parameter to the Java method.
}

/**
 * send a message to the Java object.
 * @param message   the message to be send
 */
void MessageReceiver::receiveMessage(const char *message)
{
  JNIEnv *env = JNIUtil::getEnv();

  // The method id will not change during the time this library is
  // loaded, so it can be cached.
  static jmethodID mid = 0;
  if (mid == 0)
    {
      jclass clazz = env->FindClass(JAVA_PACKAGE"/SVNAdmin$MessageReceiver");
      if (JNIUtil::isJavaExceptionThrown())
        return;

      mid = env->GetMethodID(clazz, "receiveMessageLine",
                             "(Ljava/lang/String;)V");
      if (JNIUtil::isJavaExceptionThrown() || mid == 0)
        return;

      env->DeleteLocalRef(clazz);
      if (JNIUtil::isJavaExceptionThrown())
        return;
    }

  // Convert the message to a Java string.
  jstring jmsg = JNIUtil::makeJString(message);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  // Call the Java method.
  env->CallVoidMethod(m_jthis, mid);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  // Delete the Java string.
  env->DeleteLocalRef(jmsg);
  if (JNIUtil::isJavaExceptionThrown())
    return;
}
