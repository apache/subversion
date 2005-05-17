/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2005 CollabNet.  All rights reserved.
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
 * @file EnumMapper.cpp
 * @brief Implementation of the class EnumMapper
 */
#include "EnumMapper.h"
#include "../include/org_tigris_subversion_javahl_CommitItemStateFlags.h"
#include "../include/org_tigris_subversion_javahl_NotifyAction.h"
#include "../include/org_tigris_subversion_javahl_NotifyStatus.h"
#include "../include/org_tigris_subversion_javahl_NodeKind.h"
#include "../include/org_tigris_subversion_javahl_LockStatus.h"
#include "../include/org_tigris_subversion_javahl_StatusKind.h"
#include "../include/org_tigris_subversion_javahl_Revision.h"
#include "../include/org_tigris_subversion_javahl_ScheduleKind.h"
/**
 * map a C commit state flag constant to the java constant
 * @param state     the c commit state flage constant
 * @returns the java constant
 */
jint EnumMapper::mapCommitMessageStateFlags(apr_byte_t flags)
{
    jint jstateFlags = 0;
    if(flags & SVN_CLIENT_COMMIT_ITEM_ADD)
        jstateFlags |=
            org_tigris_subversion_javahl_CommitItemStateFlags_Add;
    if(flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
        jstateFlags |=
            org_tigris_subversion_javahl_CommitItemStateFlags_Delete;
    if(flags & SVN_CLIENT_COMMIT_ITEM_TEXT_MODS)
        jstateFlags |=
            org_tigris_subversion_javahl_CommitItemStateFlags_TextMods;
    if(flags & SVN_CLIENT_COMMIT_ITEM_PROP_MODS)
        jstateFlags |=
            org_tigris_subversion_javahl_CommitItemStateFlags_PropMods;
    if(flags & SVN_CLIENT_COMMIT_ITEM_IS_COPY)
        jstateFlags |=
            org_tigris_subversion_javahl_CommitItemStateFlags_IsCopy;
    return jstateFlags;
}
/**
 * map a C notify state constant to the java constant
 * @param state     the c notify state constant
 * @returns the java constant
 */
jint EnumMapper::mapNotifyState(svn_wc_notify_state_t state)
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
/**
 * map a C notify action constant to the java constant
 * @param state     the c notify action constant
 * @returns the java constant
 */
jint EnumMapper::mapNotifyAction(svn_wc_notify_action_t action)
{
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
    case svn_wc_notify_locked:
        /* Lock a file */
        jAction = org_tigris_subversion_javahl_NotifyAction_locked;
        break;
    case svn_wc_notify_unlocked:
        /* Lock a file */
        jAction = org_tigris_subversion_javahl_NotifyAction_unlocked;
        break;
    case svn_wc_notify_failed_lock:
        /* Lock a file */
        jAction = org_tigris_subversion_javahl_NotifyAction_failed_lock;
        break;
    case svn_wc_notify_failed_unlock:
        /* Lock a file */
        jAction = org_tigris_subversion_javahl_NotifyAction_failed_unlock;
        break;
    }
    return jAction;
}
/**
 * map a C node kind constant to the java constant
 * @param state     the c node kind constant
 * @returns the java constant
 */
jint EnumMapper::mapNodeKind(svn_node_kind_t nodeKind)
{
    jint jKind = org_tigris_subversion_javahl_NodeKind_unknown;
    switch(nodeKind)
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
    return jKind;
}
/**
 * map a C notify lock state constant to the java constant
 * @param state     the c notify lock state constant
 * @returns the java constant
 */
jint EnumMapper::mapNotifyLockState(svn_wc_notify_lock_state_t state)
{
    jint jState = org_tigris_subversion_javahl_LockStatus_inapplicable;
    switch(state)
    {
    case svn_wc_notify_lock_state_inapplicable:
        jState = org_tigris_subversion_javahl_LockStatus_inapplicable;
        break;
    case svn_wc_notify_lock_state_unknown:
        jState = org_tigris_subversion_javahl_LockStatus_unknown;
        break;
    case svn_wc_notify_lock_state_unchanged:
        jState = org_tigris_subversion_javahl_LockStatus_unchanged;
        break;
    case svn_wc_notify_lock_state_locked:
        jState = org_tigris_subversion_javahl_LockStatus_locked;
        break;
    case svn_wc_notify_lock_state_unlocked:
        jState = org_tigris_subversion_javahl_LockStatus_unlocked;
        break;
    }
    return jState;
}
/**
 * map a C wc schedule constant to the java constant
 * @param state     the c wc schedule constant
 * @returns the java constant
 */
jint EnumMapper::mapScheduleKind(svn_wc_schedule_t schedule)
{
    switch(schedule)
    {
    /** Nothing special here */
    case svn_wc_schedule_normal:
        return org_tigris_subversion_javahl_ScheduleKind_normal;

    /** Slated for addition */
    case svn_wc_schedule_add:
        return org_tigris_subversion_javahl_ScheduleKind_add;

    /** Slated for deletion */
    case svn_wc_schedule_delete:
        return org_tigris_subversion_javahl_ScheduleKind_delete;

    /** Slated for replacement (delete + add) */
    case svn_wc_schedule_replace:
        return org_tigris_subversion_javahl_ScheduleKind_replace;

    default:
        return org_tigris_subversion_javahl_ScheduleKind_normal;
    }
}
/**
 * map a C wc state constant to the java constant
 * @param state     the c wc state constant
 * @returns the java constant
 */
jint EnumMapper::mapStatusKind(svn_wc_status_kind svnKind)
{
    switch(svnKind)
    {
    case svn_wc_status_none:
    default:
        return org_tigris_subversion_javahl_StatusKind_none;
    case svn_wc_status_unversioned:
        return org_tigris_subversion_javahl_StatusKind_unversioned;
    case svn_wc_status_normal:
        return org_tigris_subversion_javahl_StatusKind_normal;
    case svn_wc_status_added:
        return org_tigris_subversion_javahl_StatusKind_added;
    case svn_wc_status_missing:
        return org_tigris_subversion_javahl_StatusKind_missing;
    case svn_wc_status_deleted:
        return org_tigris_subversion_javahl_StatusKind_deleted;
    case svn_wc_status_replaced:
        return org_tigris_subversion_javahl_StatusKind_replaced;
    case svn_wc_status_modified:
        return org_tigris_subversion_javahl_StatusKind_modified;
    case svn_wc_status_merged:
        return org_tigris_subversion_javahl_StatusKind_merged;
    case svn_wc_status_conflicted:
        return org_tigris_subversion_javahl_StatusKind_conflicted;
    case svn_wc_status_ignored:
        return org_tigris_subversion_javahl_StatusKind_ignored;
    case svn_wc_status_obstructed:
        return org_tigris_subversion_javahl_StatusKind_obstructed;
    case svn_wc_status_external:
        return org_tigris_subversion_javahl_StatusKind_external;
    case svn_wc_status_incomplete:
        return org_tigris_subversion_javahl_StatusKind_incomplete;
    }
}
