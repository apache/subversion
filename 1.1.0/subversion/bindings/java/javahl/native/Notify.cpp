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
#include "../include/org_tigris_subversion_javahl_NotifyAction.h"
#include "../include/org_tigris_subversion_javahl_NotifyStatus.h"
#include "../include/org_tigris_subversion_javahl_NodeKind.h"
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
/**
 * Create a new object and store the java object
 * @param notify    global reference to the java object
 */
Notify::Notify(jobject notify)
{
    m_notify = notify;
}

/**
 * Destroy the object and delete the global reference to the java object
 */ 
Notify::~Notify()
{
    if(m_notify != NULL)
    {
        JNIEnv *env = JNIUtil::getEnv();
        env->DeleteGlobalRef(m_notify);
    }
}

/**
 * Create a C++ peer object for the java object
 * @param notify    a local reference to the java object
 */
Notify * Notify::makeCNotify(jobject notify)
{
    // if the java object is null -> no C++ peer needed
    if(notify == NULL)
        return NULL;
    JNIEnv *env = JNIUtil::getEnv();

    // sanity check, that the object implements Notify
    jclass clazz = env->FindClass(JAVA_PACKAGE"/Notify");
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    if(!env->IsInstanceOf(notify, clazz))
    {
        env->DeleteLocalRef(clazz);
        return NULL;
    }
    env->DeleteLocalRef(clazz);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    // make a global reference, because the reference is longer needed, than
    // the call
    jobject myNotify = env->NewGlobalRef(notify);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    // create the peer
    return new Notify(myNotify);
}
  /**
   * notification function passed as svn_wc_notify_func_t
   * @param baton notification instance is passed using this parameter
   * @param path on which action happen
   * @param action subversion action, see svn_wc_notify_action_t
   * @param kind node kind of path after action occurred
   * @param mime_type mime type of path after action occurred
   * @param content_state state of content after action occurred
   * @param prop_state state of properties after action occurred
   * @param revision revision number after action occurred
   */

void
Notify::notify (
    void *baton,
    const char *path,
    svn_wc_notify_action_t action,
    svn_node_kind_t kind,
    const char *mime_type,
    svn_wc_notify_state_t content_state,
    svn_wc_notify_state_t prop_state,
    svn_revnum_t revision)
{
    // an Notify object is used as the baton
    Notify * notify = (Notify *) baton;
    if(notify) // sanity check
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
void
Notify::onNotify (
    const char *path,
    svn_wc_notify_action_t action,
    svn_node_kind_t kind,
    const char *mime_type,
    svn_wc_notify_state_t content_state,
    svn_wc_notify_state_t prop_state,
    svn_revnum_t revision)
{
    JNIEnv *env = JNIUtil::getEnv();
    // java method id will not change during the time this library is loaded, 
    // so it can be cached. 
    static jmethodID mid = 0;
    if(mid == 0)
    {
        jclass clazz = env->FindClass(JAVA_PACKAGE"/Notify");
        //jclass clazz = env->GetObjectClass(m_notify);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return;
        }
        mid = env->GetMethodID(clazz, "onNotify", "(Ljava/lang/String;IILjava/lang/String;IIJ)V");
        if(JNIUtil::isJavaExceptionThrown() || mid == 0)
        {
            return;
        }
        env->DeleteLocalRef(clazz);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return;
        }
    }

    // convert the parameter to their java relatives
    jstring jPath = JNIUtil::makeJString(path);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }

    jint jAction = -1;
    // this is a switch to make the java constants independent from the C 
    // constants
    switch(action)
    {
    case svn_wc_notify_add:
        /* Adding a path to revision control. */
        jAction = org_tigris_subversion_javahl_NotifyAction_add;
        break;
    case svn_wc_notify_copy:
        /* Copying a versioned path. */
        jAction = org_tigris_subversion_javahl_NotifyAction_copy;
        break;
    case svn_wc_notify_delete:
        /* Deleting a versioned path. */
        jAction = org_tigris_subversion_javahl_NotifyAction_delete;
        break;
    case svn_wc_notify_restore:
        /* Restoring a missing path from the pristine text-base. */
        jAction = org_tigris_subversion_javahl_NotifyAction_restore;
        break;
    case svn_wc_notify_revert:
        /* Reverting a modified path. */
        jAction = org_tigris_subversion_javahl_NotifyAction_revert;
        break;
    case svn_wc_notify_failed_revert:
        /* A revert operation has failed. */
        jAction = org_tigris_subversion_javahl_NotifyAction_failed_revert;
        break;
    case svn_wc_notify_resolved:
        /* Resolving a conflict. */
        jAction = org_tigris_subversion_javahl_NotifyAction_resolved;
        break;
    case svn_wc_notify_status_completed:
        /* The last notification in a status (including status on externals). */
        jAction = org_tigris_subversion_javahl_NotifyAction_status_completed;
        break;
    case svn_wc_notify_status_external:
        /* Running status on an external module. */
        jAction = org_tigris_subversion_javahl_NotifyAction_status_external;
        break;
    case svn_wc_notify_skip:
        /* Skipping a path. */
        jAction = org_tigris_subversion_javahl_NotifyAction_skip;
        break;
    case svn_wc_notify_update_delete:
        /* Got a delete in an update. */
        jAction = org_tigris_subversion_javahl_NotifyAction_update_delete;
        break;
    case svn_wc_notify_update_add:
        /* Got an add in an update. */
        jAction = org_tigris_subversion_javahl_NotifyAction_update_add;
        break;
    case svn_wc_notify_update_update:
        /* Got any other action in an update. */
        jAction = org_tigris_subversion_javahl_NotifyAction_update_update;
        break;
    case svn_wc_notify_update_completed:
        /* The last notification in an update (including updates of externals). */
        jAction = org_tigris_subversion_javahl_NotifyAction_update_completed;
        break;
    case svn_wc_notify_update_external:
        /* Updating an external module. */
        jAction = org_tigris_subversion_javahl_NotifyAction_update_external;
        break;
    case svn_wc_notify_commit_modified:
        /* Committing a modification. */
        jAction = org_tigris_subversion_javahl_NotifyAction_commit_modified;
        break;
    case svn_wc_notify_commit_added:
        /* Committing an addition. */
        jAction = org_tigris_subversion_javahl_NotifyAction_commit_added;
        break;
    case svn_wc_notify_commit_deleted:
        /* Committing a deletion. */
        jAction = org_tigris_subversion_javahl_NotifyAction_commit_deleted;
        break;
    case svn_wc_notify_commit_replaced:
        /* Committing a replacement. */
        jAction = org_tigris_subversion_javahl_NotifyAction_commit_replaced;
        break;
    case svn_wc_notify_commit_postfix_txdelta:
        /* Transmitting post-fix text-delta data for a file. */
        jAction = org_tigris_subversion_javahl_NotifyAction_commit_postfix_txdelta;
        break;
    case svn_wc_notify_blame_revision:
        /* Processed a single revision's blame. */
        jAction = org_tigris_subversion_javahl_NotifyAction_blame_revision;
        break;
    }
    jint jKind = org_tigris_subversion_javahl_NodeKind_unknown;
    switch(kind)
    {
    case svn_node_none:
        jKind = org_tigris_subversion_javahl_NodeKind_none;
        break;
    case svn_node_file:
        jKind = org_tigris_subversion_javahl_NodeKind_file;
        break;
    case svn_node_dir:
        jKind = org_tigris_subversion_javahl_NodeKind_dir;
        break;
    case svn_node_unknown:
        jKind = org_tigris_subversion_javahl_NodeKind_unknown;
        break;
    }
    jstring jMimeType = JNIUtil::makeJString(mime_type);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }
    jint jContentState = mapState(content_state);
    jint jPropState = mapState(prop_state);

    // call the java method
    env->CallVoidMethod(m_notify, mid, jPath, jAction, jKind, jMimeType, jContentState, jPropState,
        (jlong)revision);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }

    // release all the temporary java objects
    env->DeleteLocalRef(jPath);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }
    env->DeleteLocalRef(jMimeType);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }
}
/**
 * map a C state constant to the java constant
 * @param state     the c state constant
 * @returns the java constant
 */
jint Notify::mapState(svn_wc_notify_state_t state)
{
    switch(state)
    {
    default:
    case svn_wc_notify_state_inapplicable:
        return org_tigris_subversion_javahl_NotifyStatus_inapplicable;

    case svn_wc_notify_state_unknown:
        return org_tigris_subversion_javahl_NotifyStatus_unknown;

    case svn_wc_notify_state_unchanged:
        return org_tigris_subversion_javahl_NotifyStatus_unchanged;

    case svn_wc_notify_state_missing:
        return org_tigris_subversion_javahl_NotifyStatus_missing;

    case svn_wc_notify_state_obstructed:
        return org_tigris_subversion_javahl_NotifyStatus_obstructed;

    case svn_wc_notify_state_changed:
        return org_tigris_subversion_javahl_NotifyStatus_changed;

    case svn_wc_notify_state_merged:
        return org_tigris_subversion_javahl_NotifyStatus_merged;

    case svn_wc_notify_state_conflicted:
        return org_tigris_subversion_javahl_NotifyStatus_conflicted;
    }

}
