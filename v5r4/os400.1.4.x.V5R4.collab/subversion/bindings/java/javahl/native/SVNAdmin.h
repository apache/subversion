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
 * @file SVNAdmin.h
 * @brief Interface of the class SVNAdmin
 */

#if !defined(AFX_SVNADMIN_H__9AD95B26_47BF_4430_8217_20B87ACCE87B__INCLUDED_)
#define AFX_SVNADMIN_H__9AD95B26_47BF_4430_8217_20B87ACCE87B__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <jni.h>
#include "SVNBase.h"
#include "Revision.h"
#include "Outputer.h"
#include "Inputer.h"
#include "MessageReceiver.h"
#include "Targets.h"

class SVNAdmin : public SVNBase
{
public:
    void rmlocks(const char *path, Targets &locks);
	jobjectArray lslocks(const char *path);
    void verify(const char *path, Outputer &messageOut,
                    Revision &revisionStart, Revision &revisionEnd);
    void setLog(const char *path, Revision &revision,
                    const char *message, bool bypassHooks);
    void rmtxns(const char *path, Targets &transactions);
    jlong recover(const char *path);
    void lstxns(const char *path, MessageReceiver &messageReceiver);
    void load(const char *path, Inputer &dataIn, Outputer &messageOut,
                  bool ignoreUUID, bool forceUUID, const char *relativePath);
    void listUnusedDBLogs(const char *path,
                              MessageReceiver &messageReceiver);
    void listDBLogs(const char *path, MessageReceiver &messageReceiver);
    void hotcopy(const char *path, const char *targetPath, bool cleanLogs);
    void dump(const char *path, Outputer &dataOut, Outputer &messageOut,
                  Revision &revsionStart, Revision &RevisionEnd,
                  bool incremental);
    void deltify(const char *path, Revision &start, Revision &end);
    void create(const char *path, bool ignoreUUID, bool forceUUID,
                    const char *configPath, const char *fstype);
    SVNAdmin();
    virtual ~SVNAdmin();
    void dispose(jobject jthis);
    static SVNAdmin * getCppObject(jobject jthis);

};
// !defined(AFX_SVNADMIN_H__9AD95B26_47BF_4430_8217_20B87ACCE87B__INCLUDED_)
#endif
