// CommitMessage.h: interface for the CommitMessage class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_COMMITMESSAGE_H__9AD3F0B0_9DBB_4701_9EE7_3BE0AEB51EDB__INCLUDED_)
#define AFX_COMMITMESSAGE_H__9AD3F0B0_9DBB_4701_9EE7_3BE0AEB51EDB__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <jni.h>
struct apr_array_header_t;
class CommitMessage  
{
public:
	jstring getCommitMessage(apr_array_header_t *commit_items);
	static CommitMessage * makeCCommitMessage(jobject jcommitMessage);
	virtual ~CommitMessage();
private:
	jobject m_jcommitMessage;
	CommitMessage(jobject jcommitMessage);
};

#endif // !defined(AFX_COMMITMESSAGE_H__9AD3F0B0_9DBB_4701_9EE7_3BE0AEB51EDB__INCLUDED_)
