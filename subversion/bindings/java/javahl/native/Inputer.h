// Inputer.h: interface for the Inputer class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_INPUTER_H__6896EB42_07D2_406B_A4A5_F2908AFF4815__INCLUDED_)
#define AFX_INPUTER_H__6896EB42_07D2_406B_A4A5_F2908AFF4815__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <jni.h>
#include <svn_io.h>
#include "Pool.h"

class Inputer  
{
	jobject m_jthis;
	static svn_error_t *read(void *baton, char *buffer, apr_size_t *len);
	static svn_error_t *close(void *baton);
public:
	Inputer(jobject jthis);
	~Inputer();
	svn_stream_t *getStream(const Pool & pool);
};

#endif 
// !defined(AFX_INPUTER_H__6896EB42_07D2_406B_A4A5_F2908AFF4815__INCLUDED_)
