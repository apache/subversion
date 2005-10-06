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

#if !defined(AFX_COMMITMESSAGE_H__9AD3F0B0_9DBB_4701_9EE7_3BE0AEB51EDB__INCLUDED_)
#define AFX_COMMITMESSAGE_H__9AD3F0B0_9DBB_4701_9EE7_3BE0AEB51EDB__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <jni.h>
struct apr_array_header_t;
/**
 * this class stores a java object implementing the CommitMessage interface
 */
class CommitMessage
{
public:
    jstring getCommitMessage(apr_array_header_t *commit_items);
    static CommitMessage * makeCCommitMessage(jobject jcommitMessage);
    ~CommitMessage();
private:
    /**
     * a global reference to the java object, because the reference
     * must be valid longer than the SVNClient.commitMessage call
     */
    jobject m_jcommitMessage;
    CommitMessage(jobject jcommitMessage);
};
// !defined(AFX_COMMITMESSAGE_H__9AD3F0B0_9DBB_4701_9EE7_3BE0AEB51EDB__INCLUDED_)
#endif 
