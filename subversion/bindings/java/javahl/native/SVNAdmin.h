// SVNAdmin.h: interface for the SVNAdmin class.
//
//////////////////////////////////////////////////////////////////////

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
                    const char *configPath);
	SVNAdmin();
	virtual ~SVNAdmin();
	jlong getCppAddr();
	void finalize();
	void dispose(jobject jthis);
	static SVNAdmin * getCppObject(jobject jthis);

};

#endif
// !defined(AFX_SVNADMIN_H__9AD95B26_47BF_4430_8217_20B87ACCE87B__INCLUDED_)
