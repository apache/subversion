/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2005, 2007 CollabNet.  All rights reserved.
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
 * @file EnumMapper.h
 * @brief Interface of the class EnumMapper
 */

#ifndef ENUM_MAPPER_H
#define ENUM_MAPPER_H

#include <jni.h>
#include "svn_client.h"
#include "svn_wc.h"
#include "svn_types.h"

/**
 * This class contains all the mappers between the C enum's and the
 * matching Java int's.
 */
class EnumMapper
{
 public:
  static jint mapCommitMessageStateFlags(apr_byte_t flags);
  static jint mapNotifyState(svn_wc_notify_state_t state);
  static jint mapNotifyAction(svn_wc_notify_action_t action);
  static jint mapNodeKind(svn_node_kind_t nodeKind);
  static jint mapNotifyLockState(svn_wc_notify_lock_state_t state);
  static jint mapStatusKind(svn_wc_status_kind svnKind);
  static jint mapScheduleKind(svn_wc_schedule_t schedule);
  static jint mapConflictKind(svn_wc_conflict_kind_t kind);
  static jint mapConflictAction(svn_wc_conflict_action_t action);
  static jint mapConflictReason(svn_wc_conflict_reason_t reason);
  static jint mapDepth(svn_depth_t depth);
  static jint mapOperation(svn_wc_operation_t);
};

#endif  // ENUM_MAPPER_H
