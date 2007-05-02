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
 * @file Notify.cpp
 * @brief Implementation of the class Notify
 */

#include "Notify.h"
#include "JNIUtil.h"
#include "EnumMapper.h"

/**
 * Create a new object and store the Java object.
 * @param notify    global reference to the Java object
 */
Notify::Notify(jobject p_notify)
{
  m_notify = p_notify;
}

/**
 * Destroy the object and delete the global reference to the Java
 * object.
 */
Notify::~Notify()
{
  if (m_notify != NULL)
    {
      JNIEnv *env = JNIUtil::getEnv();
      env->DeleteGlobalRef(m_notify);
    }
}

/**
 * Create a C++ peer object for the Java object.
 * @param notify    a local reference to the Java object
 */
Notify *Notify::makeCNotify(jobject notify)
{
  // If the Java object is null -> no C++ peer needed.
  if (notify == NULL)
    return NULL;
  JNIEnv *env = JNIUtil::getEnv();

  // Sanity check, that the object implements Notify.
  jclass clazz = env->FindClass(JAVA_PACKAGE"/Notify");
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  if (!env->IsInstanceOf(notify, clazz))
    {
      env->DeleteLocalRef(clazz);
      return NULL;
    }
  env->DeleteLocalRef(clazz);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  // Make a global reference, because the reference is longer
  // needed, than the call.
  jobject myNotify = env->NewGlobalRef(notify);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  // Create the peer.
  return new Notify(myNotify);
}

/**
 * Notification function passed as svn_wc_notify_func_t.
 * @param baton notification instance is passed using this parameter
 * @param path on which action happen
 * @param action subversion action, see svn_wc_notify_action_t
 * @param kind node kind of path after action occurred
 * @param mime_type mime type of path after action occurred
 * @param content_state state of content after action occurred
 * @param prop_state state of properties after action occurred
 * @param revision revision number after action occurred
 */
void Notify::notify(void *baton,
                    const char *path,
                    svn_wc_notify_action_t action,
                    svn_node_kind_t kind,
                    const char *mime_type,
                    svn_wc_notify_state_t content_state,
                    svn_wc_notify_state_t prop_state,
                    svn_revnum_t revision)
{
  // A Notify object is used as the baton.
  Notify *notify = (Notify *) baton;
  if (notify) // sanity check
    {
      // call our method
      notify->onNotify(path, action, kind, mime_type,
                       content_state, prop_state, revision);
    }
}

/**
 * Handler for Subversion notifications.
 *
 * @param path on which action happen
 * @param action subversion action, see svn_wc_notify_action_t
 * @param kind node kind of path after action occurred
 * @param mime_type mime type of path after action occurred
 * @param content_state state of content after action occurred
 * @param prop_state state of properties after action occurred
 * @param revision revision number  after action occurred
 */
void Notify::onNotify(const char *path,
                      svn_wc_notify_action_t action,
                      svn_node_kind_t kind,
                      const char *mime_type,
                      svn_wc_notify_state_t content_state,
                      svn_wc_notify_state_t prop_state,
                      svn_revnum_t revision)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Java method id will not change during the time this library is
  // loaded, so it can be cached.
  static jmethodID mid = 0;
  if (mid == 0)
    {
      jclass clazz = env->FindClass(JAVA_PACKAGE"/Notify");
      //jclass clazz = env->GetObjectClass(m_notify);
      if (JNIUtil::isJavaExceptionThrown())
        return;

      mid = env->GetMethodID(clazz,
                             "onNotify",
                             "(Ljava/lang/String;IILjava/lang/String;IIJ)V");
      if (JNIUtil::isJavaExceptionThrown() || mid == 0)
        return;

      env->DeleteLocalRef(clazz);
      if (JNIUtil::isJavaExceptionThrown())
        return;
    }

  // Convert the parameters to their Java relatives.
  jstring jPath = JNIUtil::makeJString(path);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  jint jAction = EnumMapper::mapNotifyAction(action);
  jint jKind = EnumMapper::mapNodeKind(kind);
  jstring jMimeType = JNIUtil::makeJString(mime_type);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  jint jContentState = EnumMapper::mapNotifyState(content_state);
  jint jPropState = EnumMapper::mapNotifyState(prop_state);

  // Call the Java method
  env->CallVoidMethod(m_notify, mid, jPath, jAction, jKind, jMimeType,
                      jContentState, jPropState, (jlong)revision);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  // Release all the temporary Java objects.
  env->DeleteLocalRef(jPath);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  env->DeleteLocalRef(jMimeType);
  if (JNIUtil::isJavaExceptionThrown())
    return;
}
