/*
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
 */
	
// SVN.h : Declaration of the CSVNWorkingCopy

#ifndef WINSVN_H_
#define WINSVN_H_
#pragma once

#include "resource.h"       // main symbols
#include "svn_comCP.h"

/////////////////////////////////////////////////////////////////////////////
// CSVN
class ATL_NO_VTABLE CSVNWorkingCopy : 
	public CComObjectRootEx<CComMultiThreadModel>,
	public CComCoClass<CSVNWorkingCopy, &CLSID_SVNWorkingCopy>,
	public ISupportErrorInfo,
	public IConnectionPointContainerImpl<CSVNWorkingCopy>,
	public IDispatchImpl<ISVNWorkingCopy, &IID_ISVNWorkingCopy, &LIBID_SVNCOMLib>,
	public CProxy_ISVNEvents< CSVNWorkingCopy >
{
public:
	CSVNWorkingCopy()
	{
		m_pUnkMarshaler = NULL;
		_hFindNotification_Stop = NULL;
		_hFindNotification_NewDir = NULL;
		_hFindNotification = NULL;
		_hFindNotification_Thread = NULL;
		_pszNotification_Dir = NULL;
		InitializeCriticalSection(&_csNewDir);
	}

DECLARE_REGISTRY_RESOURCEID(IDR_SVN)
DECLARE_GET_CONTROLLING_UNKNOWN()

DECLARE_PROTECT_FINAL_CONSTRUCT()

BEGIN_COM_MAP(CSVNWorkingCopy)
	COM_INTERFACE_ENTRY(ISVNWorkingCopy)
	COM_INTERFACE_ENTRY(IDispatch)
	COM_INTERFACE_ENTRY(ISupportErrorInfo)
	COM_INTERFACE_ENTRY(IConnectionPointContainer)
	COM_INTERFACE_ENTRY_AGGREGATE(IID_IMarshal, m_pUnkMarshaler.p)
	COM_INTERFACE_ENTRY_IMPL(IConnectionPointContainer)
END_COM_MAP()
BEGIN_CONNECTION_POINT_MAP(CSVNWorkingCopy)
	CONNECTION_POINT_ENTRY(DIID__ISVNEvents)
END_CONNECTION_POINT_MAP()


	HRESULT FinalConstruct()
	{
		return CoCreateFreeThreadedMarshaler(
			GetControllingUnknown(), &m_pUnkMarshaler.p);
	}

	void FinalRelease()
	{
		m_pUnkMarshaler.Release();
		if (_hFindNotification_Thread != NULL)
		{
			SetEvent(_hFindNotification_Stop);
		}
		if (_pszNotification_Dir != NULL)
		{
			free(_pszNotification_Dir);
		}
	}

	CComPtr<IUnknown> m_pUnkMarshaler;

	HANDLE _hFindNotification_Stop;
	HANDLE _hFindNotification_NewDir;
	HANDLE _hFindNotification;
	HANDLE _hFindNotification_Thread;
	CRITICAL_SECTION _csNewDir;
	CHAR *_pszNotification_Dir;

// ISupportsErrorInfo
	STDMETHOD(InterfaceSupportsErrorInfo)(REFIID riid);

// ISVNWorkingCopy
public:
	STDMETHOD(wc_statuses)(/*[in]*/ BSTR bstrPath, /*[in]*/ VARIANT_BOOL getAll, /*[out]*/ SAFEARRAY **ppsa);
	STDMETHOD(watch_dir)(/*[in]*/ BSTR bstrDir);
	STDMETHOD(check_wc)(/*[in]*/ BSTR bstrDir, /*[out, retval]*/ VARIANT_BOOL *pfIsValid); 
private:
	static void FileNotificationThreadProc(void *);
};

#endif //__SVN_H_
