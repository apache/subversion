// Outputer.h: interface for the Outputer class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_OUTPUTER_H__C4342EBB_BD8A_4DA3_A6B8_BC28CF9B3DF1__INCLUDED_)
#define AFX_OUTPUTER_H__C4342EBB_BD8A_4DA3_A6B8_BC28CF9B3DF1__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <jni.h>
#include <svn_io.h>
#include "Pool.h"

class Outputer  
{
	jobject m_jthis;
	static svn_error_t *write(void *baton, const char *buffer, apr_size_t *len);
	static svn_error_t *close(void *baton);
public:
	Outputer(jobject jthis);
	~Outputer();
	svn_stream_t *getStream(const Pool & pool);

};

#endif // !defined(AFX_OUTPUTER_H__C4342EBB_BD8A_4DA3_A6B8_BC28CF9B3DF1__INCLUDED_)
