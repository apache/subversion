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
	
// SVNStatus.h : Declaration of the CSVNStatus COM object.

#ifndef WINSVN_STATUS_H_
#define WINSVN_STATUS_H_
#pragma once

#include "resource.h"       // main symbols

/////////////////////////////////////////////////////////////////////////////
// CSVNStatus
class ATL_NO_VTABLE CSVNStatus : 
	public CComObjectRootEx<CComMultiThreadModel>,
	public CComCoClass<CSVNStatus, &CLSID_SVNStatus>,
	public ISupportErrorInfo,
	public IDispatchImpl<ISVNStatus, &IID_ISVNStatus, &LIBID_SVNCOMLib>
{
public:
	CSVNStatus()
	{
		m_pUnkMarshaler = NULL;
		pszName = NULL;
	}

DECLARE_REGISTRY_RESOURCEID(IDR_SVNSTATUS)
DECLARE_GET_CONTROLLING_UNKNOWN()

DECLARE_PROTECT_FINAL_CONSTRUCT()

BEGIN_COM_MAP(CSVNStatus)
	COM_INTERFACE_ENTRY(ISVNStatus)
	COM_INTERFACE_ENTRY(IDispatch)
	COM_INTERFACE_ENTRY(ISupportErrorInfo)
	COM_INTERFACE_ENTRY_AGGREGATE(IID_IMarshal, m_pUnkMarshaler.p)
END_COM_MAP()

	HRESULT 
	FinalConstruct()
	{
		return CoCreateFreeThreadedMarshaler(
			GetControllingUnknown(), &m_pUnkMarshaler.p);
	}

	void 
	FinalRelease()
	{
		m_pUnkMarshaler.Release();
		if (pszName != NULL)
			delete pszName;
		pszName = NULL;
	}

	CComPtr<IUnknown> m_pUnkMarshaler;

	EWCStatus text_status;
    EWCStatus prop_status;
    enum svn_node_kind kind;     /* Is it a file, a dir, or... ?  */
	CHAR *pszName;

// ISupportsErrorInfo
	STDMETHOD(InterfaceSupportsErrorInfo)(REFIID riid);

// ISVNStatus
public:
	STDMETHOD(get_is_directory)(/*[out, retval]*/ VARIANT_BOOL *pVal);
	STDMETHOD(get_name)(/*[out, retval]*/ BSTR *pVal);
	STDMETHOD(get_prop_status)(/*[out, retval]*/ EWCStatus *pVal);
	STDMETHOD(get_text_status)(/*[out, retval]*/ EWCStatus *pVal);

// C++ only access
	HRESULT init(svn_wc_status_t *status, CHAR *pszName);

};

#endif //__SVNSTATUS_H_
