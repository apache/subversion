// MessageReceiver.h: interface for the MessageReceiver class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_MESSAGERECEIVER_H__D30A4B70_A630_45B3_AB3E_402A5AD7E6BA__INCLUDED_)
#define AFX_MESSAGERECEIVER_H__D30A4B70_A630_45B3_AB3E_402A5AD7E6BA__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <jni.h>

class MessageReceiver  
{
	jobject m_jthis;
public:
	MessageReceiver(jobject jthis);
	~MessageReceiver();
	void receiveMessage(const char *message);

};

#endif
// !defined(AFX_MESSAGERECEIVER_H__D30A4B70_A630_45B3_AB3E_402A5AD7E6BA__INCLUDED_)
