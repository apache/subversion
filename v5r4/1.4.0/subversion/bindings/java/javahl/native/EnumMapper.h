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
 * @file EnumMapper.h
 * @brief Interface of the class EnumMapper
 */

#if !defined(AFX_ENUMMAPPER_H__35D135AE_08C8_4722_8BF2_361449BC2FF9__INCLUDED_)
#define AFX_ENUMMAPPER_H__35D135AE_08C8_4722_8BF2_361449BC2FF9__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <jni.h>
#include "svn_client.h"
#include "svn_wc.h"
#include "svn_types.h"
/**
 * this class contains all the mappers between the C enum's and the matching
 * java int's.
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
};

#endif // !defined(AFX_ENUMMAPPER_H__35D135AE_08C8_4722_8BF2_361449BC2FF9__INCLUDED_)
