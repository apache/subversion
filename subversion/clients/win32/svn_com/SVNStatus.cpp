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

// SVNStatus.cpp : Implementation of CSVNStatus
#include "stdafx.h"
#include "SVNCOM.h"
#include "SVNStatus.h"

/////////////////////////////////////////////////////////////////////////////
// CSVNStatus

STDMETHODIMP 
CSVNStatus::InterfaceSupportsErrorInfo(REFIID riid)
{
	static const IID* arr[] = 
	{
		&IID_ISVNStatus
	};
	for (int i=0; i < sizeof(arr) / sizeof(arr[0]); i++)
	{
		if (::ATL::InlineIsEqualGUID(*arr[i],riid))
			return S_OK;
	}
	return S_FALSE;
}

STDMETHODIMP 
CSVNStatus::get_text_status(EWCStatus *pVal)
{
	*pVal = text_status;
	return S_OK;
}

STDMETHODIMP 
CSVNStatus::get_prop_status(EWCStatus *pVal)
{
	*pVal = prop_status;
	return S_OK;
}

STDMETHODIMP
CSVNStatus::get_name(BSTR *pVal)
{
	*pVal = A2BSTR(pszName);
	return S_OK;
}

STDMETHODIMP CSVNStatus::get_is_directory(VARIANT_BOOL *pfVal)
{
	*pfVal = kind == svn_node_dir ? VARIANT_TRUE : VARIANT_FALSE;
	return S_OK;
}

HRESULT 
CSVNStatus::init(svn_wc_status_t *status, CHAR *psz)
{
	text_status = (EWCStatus)status->text_status;
	prop_status = (EWCStatus)status->prop_status;
	if (status->entry != NULL) {
		kind = status->entry->kind;
	}
	// If entry is NULL, then we don't know about this
	// node in the WC, and it must be a file atm.
	// FIX: This could be a directory later.
	else {
		kind = svn_node_file;
		text_status = eWCStatus_NotInWC;
		prop_status = eWCStatus_NotInWC;
	}
	pszName = (CHAR *)malloc(strlen(psz) + 1);
	strcpy(pszName, psz);

	return S_OK;	
}
