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
 * @file EnumMapper.cpp
 * @brief Implementation of the class EnumMapper
 */

#include "svn_types.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "EnumMapper.h"
#include "JNIUtil.h"
#include "JNIStringHolder.h"
#include "../include/org_apache_subversion_javahl_CommitItemStateFlags.h"
#include "../include/org_apache_subversion_javahl_NotifyStatus.h"
#include "../include/org_apache_subversion_javahl_NodeKind.h"
#include "../include/org_apache_subversion_javahl_Operation.h"
#include "../include/org_apache_subversion_javahl_LockStatus.h"
#include "../include/org_apache_subversion_javahl_StatusKind.h"
#include "../include/org_apache_subversion_javahl_Revision.h"
#include "../include/org_apache_subversion_javahl_ScheduleKind.h"
#include "../include/org_apache_subversion_javahl_ConflictDescriptor_Kind.h"
#include "../include/org_apache_subversion_javahl_ConflictDescriptor_Action.h"
#include "../include/org_apache_subversion_javahl_ConflictDescriptor_Reason.h"

/**
 * Map a C commit state flag constant to the Java constant.
 * @param state     the C commit state flage constant
 * @returns the Java constant
 */
jint EnumMapper::mapCommitMessageStateFlags(apr_byte_t flags)
{
  jint jstateFlags = 0;
  if (flags & SVN_CLIENT_COMMIT_ITEM_ADD)
    jstateFlags |=
      org_apache_subversion_javahl_CommitItemStateFlags_Add;
  if (flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
    jstateFlags |=
      org_apache_subversion_javahl_CommitItemStateFlags_Delete;
  if (flags & SVN_CLIENT_COMMIT_ITEM_TEXT_MODS)
    jstateFlags |=
      org_apache_subversion_javahl_CommitItemStateFlags_TextMods;
  if (flags & SVN_CLIENT_COMMIT_ITEM_PROP_MODS)
    jstateFlags |=
      org_apache_subversion_javahl_CommitItemStateFlags_PropMods;
  if (flags & SVN_CLIENT_COMMIT_ITEM_IS_COPY)
    jstateFlags |=
      org_apache_subversion_javahl_CommitItemStateFlags_IsCopy;
  return jstateFlags;
}

/**
 * Map a C notify state constant to the Java constant.
 * @param state     the C notify state constant
 * @returns the Java constant
 */
jint EnumMapper::mapNotifyState(svn_wc_notify_state_t state)
{
  switch(state)
    {
    default:
    case svn_wc_notify_state_inapplicable:
      return org_apache_subversion_javahl_NotifyStatus_inapplicable;

    case svn_wc_notify_state_unknown:
      return org_apache_subversion_javahl_NotifyStatus_unknown;

    case svn_wc_notify_state_unchanged:
      return org_apache_subversion_javahl_NotifyStatus_unchanged;

    case svn_wc_notify_state_missing:
      return org_apache_subversion_javahl_NotifyStatus_missing;

    case svn_wc_notify_state_obstructed:
      return org_apache_subversion_javahl_NotifyStatus_obstructed;

    case svn_wc_notify_state_changed:
      return org_apache_subversion_javahl_NotifyStatus_changed;

    case svn_wc_notify_state_merged:
      return org_apache_subversion_javahl_NotifyStatus_merged;

    case svn_wc_notify_state_conflicted:
      return org_apache_subversion_javahl_NotifyStatus_conflicted;
    }

}

/**
 * Map a C notify action constant to the Java constant.
 */
jobject EnumMapper::mapNotifyAction(svn_wc_notify_action_t action)
{
  switch(action)
    {
    case svn_wc_notify_add:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "add");

    case svn_wc_notify_copy:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "copy");

    case svn_wc_notify_delete:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "delete");

    case svn_wc_notify_restore:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "restore");

    case svn_wc_notify_revert:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "revert");

    case svn_wc_notify_failed_revert:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "failed_revert");

    case svn_wc_notify_resolved:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "resolved");

    case svn_wc_notify_skip:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "skip");

    case svn_wc_notify_status_completed:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "status_completed");

    case svn_wc_notify_status_external:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "status_external");

    case svn_wc_notify_update_delete:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "update_delete");

    case svn_wc_notify_update_add:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "update_add");

    case svn_wc_notify_update_replace:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "update_replace");

    case svn_wc_notify_update_update:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "update_update");

    case svn_wc_notify_update_completed:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "update_completed");

    case svn_wc_notify_update_external:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "update_external");

    case svn_wc_notify_commit_modified:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "commit_modified");

    case svn_wc_notify_commit_added:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "commit_added");

    case svn_wc_notify_commit_deleted:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "commit_deleted");

    case svn_wc_notify_commit_replaced:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "commit_replaced");

    case svn_wc_notify_commit_postfix_txdelta:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "commit_postfix_txdelta");

    case svn_wc_notify_blame_revision:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "blame_revision");

    case svn_wc_notify_locked:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "locked");

    case svn_wc_notify_unlocked:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "unlocked");

    case svn_wc_notify_failed_lock:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "failed_lock");

    case svn_wc_notify_failed_unlock:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "failed_unlock");

    case svn_wc_notify_exists:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "exists");

    case svn_wc_notify_changelist_set:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "changelist_set");

    case svn_wc_notify_changelist_clear:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "changelist_clear");

    case svn_wc_notify_merge_begin:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "merge_begin");

    case svn_wc_notify_foreign_merge_begin:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "foreign_merge_begin");

    case svn_wc_notify_property_added:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "property_added");

    case svn_wc_notify_property_modified:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "property_modified");

    case svn_wc_notify_property_deleted:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "property_deleted");

    case svn_wc_notify_property_deleted_nonexistent:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "property_deleted_nonexistent");

    case svn_wc_notify_revprop_set:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "revprop_set");

    case svn_wc_notify_revprop_deleted:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "revprop_deleted");

    case svn_wc_notify_merge_completed:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "merge_completed");

    case svn_wc_notify_tree_conflict:
      return mapEnum(JAVA_PACKAGE"/NotifyInformation$Action", "tree_conflict");

    default:
      return NULL;
    }
}

/**
 * Map a C node kind constant to the Java constant.
 * @param state     the C node kind constant
 * @returns the Java constant
 */
jint EnumMapper::mapNodeKind(svn_node_kind_t nodeKind)
{
  switch(nodeKind)
    {
    case svn_node_none:
      return org_apache_subversion_javahl_NodeKind_none;

    case svn_node_file:
      return org_apache_subversion_javahl_NodeKind_file;

    case svn_node_dir:
      return org_apache_subversion_javahl_NodeKind_dir;

    case svn_node_unknown:
      return org_apache_subversion_javahl_NodeKind_unknown;

    default:
      return org_apache_subversion_javahl_NodeKind_unknown;
    }
}

/**
 * Map a C notify lock state constant to the Java constant.
 * @param state     the C notify lock state constant
 * @returns the Java constant
 */
jint EnumMapper::mapNotifyLockState(svn_wc_notify_lock_state_t state)
{
  switch(state)
    {
    case svn_wc_notify_lock_state_inapplicable:
      return org_apache_subversion_javahl_LockStatus_inapplicable;

    case svn_wc_notify_lock_state_unknown:
      return org_apache_subversion_javahl_LockStatus_unknown;

    case svn_wc_notify_lock_state_unchanged:
      return org_apache_subversion_javahl_LockStatus_unchanged;

    case svn_wc_notify_lock_state_locked:
      return org_apache_subversion_javahl_LockStatus_locked;

    case svn_wc_notify_lock_state_unlocked:
      return org_apache_subversion_javahl_LockStatus_unlocked;

    default:
      return org_apache_subversion_javahl_LockStatus_inapplicable;
    }
}

/**
 * Map a C wc schedule constant to the Java constant.
 * @param state     the C wc schedule constant
 * @returns the Java constant
 */
jint EnumMapper::mapScheduleKind(svn_wc_schedule_t schedule)
{
  switch(schedule)
    {
      /** Nothing special here */
    case svn_wc_schedule_normal:
      return org_apache_subversion_javahl_ScheduleKind_normal;

      /** Slated for addition */
    case svn_wc_schedule_add:
      return org_apache_subversion_javahl_ScheduleKind_add;

      /** Slated for deletion */
    case svn_wc_schedule_delete:
      return org_apache_subversion_javahl_ScheduleKind_delete;

      /** Slated for replacement (delete + add) */
    case svn_wc_schedule_replace:
      return org_apache_subversion_javahl_ScheduleKind_replace;

    default:
      return org_apache_subversion_javahl_ScheduleKind_normal;
    }
}

/**
 * Map a C wc state constant to the Java constant.
 * @param state     the C wc state constant
 * @returns the Java constant
 */
jint EnumMapper::mapStatusKind(svn_wc_status_kind svnKind)
{
  switch(svnKind)
    {
    case svn_wc_status_none:
    default:
      return org_apache_subversion_javahl_StatusKind_none;

    case svn_wc_status_unversioned:
      return org_apache_subversion_javahl_StatusKind_unversioned;

    case svn_wc_status_normal:
      return org_apache_subversion_javahl_StatusKind_normal;

    case svn_wc_status_added:
      return org_apache_subversion_javahl_StatusKind_added;

    case svn_wc_status_missing:
      return org_apache_subversion_javahl_StatusKind_missing;

    case svn_wc_status_deleted:
      return org_apache_subversion_javahl_StatusKind_deleted;

    case svn_wc_status_replaced:
      return org_apache_subversion_javahl_StatusKind_replaced;

    case svn_wc_status_modified:
      return org_apache_subversion_javahl_StatusKind_modified;

    case svn_wc_status_merged:
      return org_apache_subversion_javahl_StatusKind_merged;

    case svn_wc_status_conflicted:
      return org_apache_subversion_javahl_StatusKind_conflicted;

    case svn_wc_status_ignored:
      return org_apache_subversion_javahl_StatusKind_ignored;

    case svn_wc_status_obstructed:
      return org_apache_subversion_javahl_StatusKind_obstructed;

    case svn_wc_status_external:
      return org_apache_subversion_javahl_StatusKind_external;

    case svn_wc_status_incomplete:
      return org_apache_subversion_javahl_StatusKind_incomplete;
    }
}

jint EnumMapper::mapConflictKind(svn_wc_conflict_kind_t kind)
{
  switch (kind)
    {
    case svn_wc_conflict_kind_text:
    default:
      return org_apache_subversion_javahl_ConflictDescriptor_Kind_text;

    case svn_wc_conflict_kind_property:
      return org_apache_subversion_javahl_ConflictDescriptor_Kind_property;
    }
}

jint EnumMapper::mapConflictAction(svn_wc_conflict_action_t action)
{
  switch (action)
    {
    case svn_wc_conflict_action_edit:
    default:
      return org_apache_subversion_javahl_ConflictDescriptor_Action_edit;

    case svn_wc_conflict_action_add:
      return org_apache_subversion_javahl_ConflictDescriptor_Action_add;

    case svn_wc_conflict_action_delete:
      return org_apache_subversion_javahl_ConflictDescriptor_Action_delete;
    }
}

jint EnumMapper::mapConflictReason(svn_wc_conflict_reason_t reason)
{
  switch (reason)
    {
    case svn_wc_conflict_reason_edited:
    default:
      return org_apache_subversion_javahl_ConflictDescriptor_Reason_edited;

    case svn_wc_conflict_reason_obstructed:
      return org_apache_subversion_javahl_ConflictDescriptor_Reason_obstructed;

    case svn_wc_conflict_reason_deleted:
      return org_apache_subversion_javahl_ConflictDescriptor_Reason_deleted;

    case svn_wc_conflict_reason_missing:
      return org_apache_subversion_javahl_ConflictDescriptor_Reason_missing;

    case svn_wc_conflict_reason_unversioned:
      return org_apache_subversion_javahl_ConflictDescriptor_Reason_unversioned;

    case svn_wc_conflict_reason_added:
      return org_apache_subversion_javahl_ConflictDescriptor_Reason_added;
    }
}

svn_depth_t EnumMapper::toDepth(jobject jdepth)
{
  JNIEnv *env = JNIUtil::getEnv();

  jstring jname = getName(JAVA_PACKAGE"/Depth", jdepth);
  if (JNIUtil::isJavaExceptionThrown())
    return (svn_depth_t)0;

  JNIStringHolder str(jname);
  std::string name((const char *)str);

  if (name == "infinity")
    return svn_depth_infinity;
  else if (name == "immediates")
    return svn_depth_immediates;
  else if (name == "files")
    return svn_depth_files;
  else if (name == "empty")
    return svn_depth_empty;
  else if (name == "exclude")
    return svn_depth_exclude;
  else
    return svn_depth_unknown;
}

jobject EnumMapper::mapDepth(svn_depth_t depth)
{
  switch (depth)
    {
    case svn_depth_unknown:
    default:
      return mapEnum(JAVA_PACKAGE"/Depth", "unknown");

    case svn_depth_exclude:
      return mapEnum(JAVA_PACKAGE"/Depth", "exclude");

    case svn_depth_empty:
      return mapEnum(JAVA_PACKAGE"/Depth", "empty");

    case svn_depth_files:
      return mapEnum(JAVA_PACKAGE"/Depth", "files");

    case svn_depth_immediates:
      return mapEnum(JAVA_PACKAGE"/Depth", "immediates");

    case svn_depth_infinity:
      return mapEnum(JAVA_PACKAGE"/Depth", "infinity");
    }
}

jint EnumMapper::mapOperation(svn_wc_operation_t operation)
{
  switch (operation)
    {
    case svn_wc_operation_none:
    default:
      return org_apache_subversion_javahl_Operation_none;
    case svn_wc_operation_update:
      return org_apache_subversion_javahl_Operation_update;
    case svn_wc_operation_switch:
      return org_apache_subversion_javahl_Operation_switched;
    case svn_wc_operation_merge:
      return org_apache_subversion_javahl_Operation_merge;
    }
}

jobject EnumMapper::mapTristate(svn_tristate_t tristate)
{
  switch (tristate)
    {
    case svn_tristate_unknown:
    default:
      return mapEnum(JAVA_PACKAGE"/Tristate", "Unknown");
    case svn_tristate_true:
      return mapEnum(JAVA_PACKAGE"/Tristate", "True");
    case svn_tristate_false:
      return mapEnum(JAVA_PACKAGE"/Tristate", "False");
    }
}

svn_opt_revision_kind EnumMapper::toRevisionKind(jobject jkind)
{
  JNIEnv *env = JNIUtil::getEnv();

  jstring jname = getName(JAVA_PACKAGE"/Revision$Kind", jkind);
  if (JNIUtil::isJavaExceptionThrown())
    return svn_opt_revision_unspecified;

  JNIStringHolder str(jname);
  std::string name((const char *)str);

  if (name == "number")
    return svn_opt_revision_number;
  else if (name == "date")
    return svn_opt_revision_date;
  else if (name == "committed")
    return svn_opt_revision_committed;
  else if (name == "previous")
    return svn_opt_revision_previous;
  else if (name == "base")
    return svn_opt_revision_base;
  else if (name == "working")
    return svn_opt_revision_working;
  else if (name == "head")
    return svn_opt_revision_head;
  else
    return svn_opt_revision_unspecified;
}

jobject EnumMapper::mapEnum(const char *clazzName, const char *name)
{
  std::string methodSig("(Ljava/lang/String;)L");
  methodSig.append(clazzName);
  methodSig.append(";");

  JNIEnv *env = JNIUtil::getEnv();
  jclass clazz = env->FindClass(clazzName);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jmethodID mid = env->GetStaticMethodID(clazz, "valueOf", methodSig.c_str());
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jstring jname = JNIUtil::makeJString(name);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jobject jdepth = env->CallStaticObjectMethod(clazz, mid, jname);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  env->DeleteLocalRef(jname);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  env->DeleteLocalRef(clazz);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  return jdepth;
}

jstring EnumMapper::getName(const char *clazzName, jobject jenum)
{
  JNIEnv *env = JNIUtil::getEnv();
  jclass clazz = env->FindClass(clazzName);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jmethodID mid = env->GetMethodID(clazz, "name", "()Ljava/lang/String;");
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jstring jname = (jstring) env->CallObjectMethod(jenum, mid);

  env->DeleteLocalRef(clazz);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  return jname;
}
