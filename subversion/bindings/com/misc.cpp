/*
 * misc. utility functions
 *
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


#include "stdafx.h"

// Root pool every other pool is a subpool of this pool.
apr_pool_t *g_pool = NULL;

// Pool to use for operations on the UI thread, use a separate pool
// for the libsvn_client thread.
apr_pool_t *g_global_pool = NULL;

// Converts an svn_error_t to an HRESULT, and
// an IErrorInfo record.
HRESULT 
convert_err_to_hresult(svn_error_t *error)
{
	HRESULT hr;
	CComPtr<ICreateErrorInfo> spCreateErrorInfo;
	CComBSTR sbstrDesc;
	svn_error_t *next;
	svn_error_t *current;
	static char szFmt[] = "APR Error: %d, Source Error: %d";
	char szBuff[k_uicbINTAsDecimalString * 2 + sizeof(szFmt) + 1];

	memset(szBuff, 0, sizeof(szBuff));
	
	hr = CreateErrorInfo(&spCreateErrorInfo);
	if (FAILED(hr))
	{
		goto Cleanup;
	}

	hr = spCreateErrorInfo->SetGUID(IID_NULL);
	if (FAILED(hr)) 
		goto Cleanup;
	hr = spCreateErrorInfo->SetHelpContext(0);
	if (FAILED(hr)) 
		goto Cleanup;
	hr = spCreateErrorInfo->SetHelpFile(NULL);
	if (FAILED(hr)) 
		goto Cleanup;
	hr = spCreateErrorInfo->SetSource(W2BSTR(L"Subversion"));
	if (FAILED(hr)) 
		goto Cleanup;

	next = error;
	while (next != NULL) {
		current = next;
		_snprintf(
			szBuff, 
			sizeof(szBuff), 
			szFmt, 
			current->apr_err, 
			current->src_err);
		sbstrDesc.Append("\r\n");
		sbstrDesc.Append(current->message);
		sbstrDesc.Append("\r\n");
		next = current->child;	
	}
	
	hr = spCreateErrorInfo->SetDescription(sbstrDesc);
	if (FAILED(hr)) 
		goto Cleanup;

	SetErrorInfo(0, (IErrorInfo *)((IUnknown *)spCreateErrorInfo));

	hr = E_FAIL;

Cleanup:
	return hr;
}
