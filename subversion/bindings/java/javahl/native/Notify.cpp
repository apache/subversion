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
#include "org_tigris_subversion_javahl_Notify_Action.h"
#include "org_tigris_subversion_javahl_Notify_Status.h"
#include "org_tigris_subversion_javahl_NodeKind.h"
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

Notify::Notify(jobject notify)
{
	m_notify = notify;
}

Notify::~Notify()
{
	if(m_notify != NULL)
	{
		JNIEnv *env = JNIUtil::getEnv();
		env->DeleteGlobalRef(m_notify);
	}
}

Notify * Notify::makeCNotify(jobject notify)
{
	if(notify == NULL)
		return NULL;
	JNIEnv *env = JNIUtil::getEnv();
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
	jobject myNotify = env->NewGlobalRef(notify);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	return new Notify(myNotify);
}
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
  Notify * notify = (Notify *) baton;
  if(notify)
  {
	notify->onNotify(path, action, kind, mime_type,
		content_state, prop_state, revision);
  }
}
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
	jstring jPath = JNIUtil::makeJString(path);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return;
	}

	jint jAction = -1; 
	switch(action)
	{
	case svn_wc_notify_add:
		jAction = org_tigris_subversion_javahl_Notify_Action_add;
		break;
	case svn_wc_notify_copy:
		jAction = org_tigris_subversion_javahl_Notify_Action_copy;
		break;
	case svn_wc_notify_delete:
		jAction = org_tigris_subversion_javahl_Notify_Action_delete;
		break;
	case svn_wc_notify_restore:
		jAction = org_tigris_subversion_javahl_Notify_Action_restore;
		break;
	case svn_wc_notify_revert:
		jAction = org_tigris_subversion_javahl_Notify_Action_revert;
		break;
	case svn_wc_notify_failed_revert:
		jAction = org_tigris_subversion_javahl_Notify_Action_failed_revert;
		break;
	case svn_wc_notify_resolve:
		jAction = org_tigris_subversion_javahl_Notify_Action_resolve;
		break;
	case svn_wc_notify_status:
		jAction = org_tigris_subversion_javahl_Notify_Action_status;
		break;
	case svn_wc_notify_skip:
		jAction = org_tigris_subversion_javahl_Notify_Action_skip;
		break;
	case svn_wc_notify_update_delete:
		jAction = org_tigris_subversion_javahl_Notify_Action_update_delete;
		break;
	case svn_wc_notify_update_add:
		jAction = org_tigris_subversion_javahl_Notify_Action_update_add;
		break;
	case svn_wc_notify_update_update:
		jAction = org_tigris_subversion_javahl_Notify_Action_update_update;
		break;
	case svn_wc_notify_update_completed:
		jAction = org_tigris_subversion_javahl_Notify_Action_update_completed;
		break;
	case svn_wc_notify_update_external:
		jAction = org_tigris_subversion_javahl_Notify_Action_update_external;
		break;
	case svn_wc_notify_commit_modified:
		jAction = org_tigris_subversion_javahl_Notify_Action_commit_modified;
		break;
	case svn_wc_notify_commit_added:
		jAction = org_tigris_subversion_javahl_Notify_Action_commit_added;
		break;
	case svn_wc_notify_commit_deleted:
		jAction = org_tigris_subversion_javahl_Notify_Action_commit_deleted;
		break;
	case svn_wc_notify_commit_replaced:
		jAction = org_tigris_subversion_javahl_Notify_Action_commit_replaced;
		break;
	case svn_wc_notify_commit_postfix_txdelta:
		jAction = org_tigris_subversion_javahl_Notify_Action_commit_postfix_txdelta;
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
	env->CallVoidMethod(m_notify, mid, jPath, jAction, jKind, jMimeType, jContentState, jPropState,
		(jlong)revision);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return;
	}
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

jint Notify::mapState(svn_wc_notify_state_t state)
{
	switch(state)
	{
	default:
	case svn_wc_notify_state_inapplicable:
		return org_tigris_subversion_javahl_Notify_Status_inapplicable;

	case svn_wc_notify_state_unknown:
		return org_tigris_subversion_javahl_Notify_Status_unknown;

	case svn_wc_notify_state_unchanged:
		return org_tigris_subversion_javahl_Notify_Status_unchanged;

	case svn_wc_notify_state_changed:
		return org_tigris_subversion_javahl_Notify_Status_changed;

	case svn_wc_notify_state_merged:
		return org_tigris_subversion_javahl_Notify_Status_merged;

	case svn_wc_notify_state_conflicted:
		return org_tigris_subversion_javahl_Notify_Status_conflicted;
	}

}
