/*
 * SVN.cpp : Implementation of CSVNWorkingCopy
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

#include "SVNCOM.h"
#include "SVN.h"
#include "misc.h"
#include "SVNStatus.h"

/////////////////////////////////////////////////////////////////////////////
// CSVNWorkingCopy

STDMETHODIMP 
CSVNWorkingCopy::InterfaceSupportsErrorInfo(REFIID riid)
{
	static const IID* arr[] = 
	{
		&IID_ISVNWorkingCopy
	};
	for (int i=0; i < sizeof(arr) / sizeof(arr[0]); i++)
	{
		if (::ATL::InlineIsEqualGUID(*arr[i],riid))
			return S_OK;
	}
	return S_FALSE;
}

//
// Sets pfIsValid to VARIANT_TRUE if the directory contains
// valid SVN meta-data.
//
STDMETHODIMP 
CSVNWorkingCopy::check_wc(BSTR bstrDir, VARIANT_BOOL *pfIsValid)
{
	USES_CONVERSION;
	HRESULT hr;
	svn_error_t *error;
	svn_boolean_t is_wc;

	if (pfIsValid == NULL)
		return E_POINTER;

	error = svn_wc_check_wc(W2A(bstrDir), &is_wc, g_pool);

	if (error) {
		hr = convert_err_to_hresult(error);
		goto Cleanup;
	}

	if (is_wc) 
		*pfIsValid = VARIANT_TRUE;
	else
		*pfIsValid = VARIANT_FALSE;

	hr = S_OK;

Cleanup:
	svn_pool_clear(g_pool);
	return hr;
}


// 
// Create a secondary thread to watch for file changes
// in the specified directory (bstrDir).
// This secondary thread fires "RefreshFiles" when 
// a change is detected.
//
STDMETHODIMP 
CSVNWorkingCopy::watch_dir(BSTR bstrDir)
{
	USES_CONVERSION;
	HRESULT hr;
	CHAR *psz;

	EnterCriticalSection(&_csNewDir);

	if (_hFindNotification_Stop == NULL)
	{
		_hFindNotification_NewDir = CreateEvent(
			NULL, FALSE, FALSE, NULL);
		if (_hFindNotification_NewDir == NULL)
		{
			hr = E_FAIL;
			goto Error;
		}
	
		_hFindNotification_Stop = CreateEvent(
			NULL, FALSE, FALSE, NULL);
		if (_hFindNotification_Stop == NULL)
		{
			hr = E_FAIL;
			goto Error;
		}
	
		_hFindNotification_Thread = (HANDLE)_beginthread(
			FileNotificationThreadProc,
			0,
			this);
		if (_hFindNotification_Thread == NULL)
		{
			hr = E_FAIL;
			goto Cleanup;
		}
	}
	else
	{
		// Free previous directory.
		free(_pszNotification_Dir);
	}

	// Setup new direcotry
	psz = W2A(bstrDir);
	_pszNotification_Dir = (char *)malloc(strlen(psz) + 1);
	strcpy(_pszNotification_Dir, psz);

	// Tell the thread that we have a directory to care about.
	SetEvent(_hFindNotification_NewDir);

	hr = S_OK;
Cleanup:
	LeaveCriticalSection(&_csNewDir);
	return hr;
Error:
	Assert(FAILED(hr));
	if (_hFindNotification_Stop != NULL)
		CloseHandle(_hFindNotification_Stop);
	if (_hFindNotification_NewDir != NULL)
		CloseHandle(_hFindNotification_NewDir);
	goto Cleanup;
}

// Pump any waiting messages in the message queue.
static void 
PumpWaitingMessages(void)
{
	MSG msg;

	//
	// Read all of the messages in this next loop, 
	// removing each message as we read it.
	//
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		//
		// If it's a quit message, we're out of here.
		//
		if (msg.message == WM_QUIT)
		{
			break;
		}

		//
		// Otherwise, dispatch the message.
		//
		DispatchMessage(&msg); 
	} 
}

//
// Thread procedure for the file/directory notification
// thread. This thread is created for the first time when
// some calls watch_dir.
// 
void 
CSVNWorkingCopy::FileNotificationThreadProc(void *pv)
{
	USES_CONVERSION;
	CSVNWorkingCopy *wc;
	// Keep track of the current directory we're watching.
	CHAR *pszCurrentDir = NULL;
	HANDLE aHandles[3];
	DWORD dw;
	CComBSTR sbstr;

	// Initialize COM on this thread
	if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED)))
		abort();

	wc = static_cast<CSVNWorkingCopy *>(pv);

	// Wait for initial all clear.
	WaitForSingleObject(wc->_hFindNotification_NewDir, INFINITE);

	aHandles[1] = wc->_hFindNotification_NewDir;
	aHandles[2] = wc->_hFindNotification_Stop;

NewDirectory:
	if (wc->_hFindNotification != NULL)
	{
		FindCloseChangeNotification(wc->_hFindNotification);
	}

	// Enter critical section to lock down _pszNotification_Dir
	EnterCriticalSection(&wc->_csNewDir);

	if (pszCurrentDir != NULL)
	{
		free(pszCurrentDir);
	}

	pszCurrentDir = (CHAR *)malloc(strlen(wc->_pszNotification_Dir) + 1);
	if (pszCurrentDir == NULL)
	{
		abort();
	}

	strcpy(pszCurrentDir, wc->_pszNotification_Dir);

	// Leave the critical section.
	LeaveCriticalSection(&wc->_csNewDir);

	sbstr.Empty();
	sbstr = A2BSTR(pszCurrentDir);

	wc->_hFindNotification = FindFirstChangeNotification(
		pszCurrentDir, FALSE,
		FILE_NOTIFY_CHANGE_FILE_NAME |
		FILE_NOTIFY_CHANGE_LAST_WRITE);
	if (wc->_hFindNotification == INVALID_HANDLE_VALUE)
	{
		abort();
	}

	aHandles[0] = wc->_hFindNotification;

	while (1)
	{
		dw = MsgWaitForMultipleObjects(
			3, aHandles,
			FALSE, INFINITE, QS_ALLINPUT);
		switch (dw)
		{
		// Change notification
		case WAIT_OBJECT_0:
			wc->Fire_RefreshFiles(sbstr);
			if (!FindNextChangeNotification(wc->_hFindNotification))
				abort();
			continue;
		// New directory
		case WAIT_OBJECT_0 + 1:
			goto NewDirectory;
		// End Thread
		case WAIT_OBJECT_0 + 2:
			_endthread();
		// Since this thread is COM enabled,
		// we have to be good citizens, and
		// pump any incoming messages to
		// our thread, so that we prevent broadcasted
		// SendMessage calls from hanging.
		case WAIT_OBJECT_0 + 3:
			PumpWaitingMessages();
			continue;
		}
	}
}

STDMETHODIMP 
CSVNWorkingCopy::wc_statuses(BSTR bstrPath, VARIANT_BOOL getAll, SAFEARRAY **ppsa)
{
	USES_CONVERSION;
	HRESULT hr;
	apr_hash_t *hash;
	svn_error_t *error;
	svn_wc_status_t *status;
	apr_hash_index_t *hi;
	SAFEARRAY *psa;
	SAFEARRAYBOUND rgsBound;
	IDispatch **paDisp;
	apr_ssize_t klen;
	int i;
	apr_size_t count;
	CHAR *pszKey;
	CComObject<CSVNStatus> *com_status = NULL;
	svn_boolean_t fLockedSA = FALSE;
        svn_boolean_t get_all = (getAll != VARIANT_TRUE ? FALSE : TRUE);

	hash = apr_hash_make(g_pool);
	error = svn_wc_statuses(hash, W2A(bstrPath),
                                FALSE, // FIXME: descend or not, rassilon?
                                get_all, FALSE, g_pool);
	if (error) {
		hr = convert_err_to_hresult(error);
		goto Cleanup;
	}

	count = apr_hash_count(hash);
	rgsBound.cElements = count;
	rgsBound.lLbound = 0;

	psa = SafeArrayCreate(VT_DISPATCH, 1, &rgsBound);
	if (psa == NULL)
		abort();

	hr = SafeArrayAccessData(psa, (void **)&paDisp);
	if (FAILED(hr))
		goto Cleanup;
	fLockedSA = TRUE;

	for (i = 0, hi = apr_hash_first(g_pool, hash); hi;
									 i++, hi = apr_hash_next(hi)) {
		apr_hash_this(hi, (const void **)&pszKey, &klen, (void **)&status);
		hr = CComObject<CSVNStatus>::CreateInstance(&com_status);
		if (FAILED(hr))
			goto Cleanup;
		// This is what we want since the hash key is an absolute path.
		pszKey = ((svn_stringbuf_t *)(status->entry->name))->data;
		// SVN_WC_ENTRY_THIS_DIR is ".", we don't care about its status.
		if (strcmp(pszKey, SVN_WC_ENTRY_THIS_DIR) == 0) {
			delete com_status;
			com_status = NULL;
			// Pretend the iteration didn't happen.
			i--;
			continue;
		}
		hr = com_status->init(status, pszKey);
		if (FAILED(hr))
			goto Cleanup;
		hr = com_status->QueryInterface(&paDisp[i]);		
		if (FAILED(hr))
			goto Cleanup;
		
		// NULL out the local variable,
		// the COM reference count is now owned by paDisp[i]
		com_status = NULL;
	}

	SafeArrayUnaccessData(psa);
	fLockedSA = FALSE;

	if (rgsBound.cElements > (UINT)i) {
		rgsBound.cElements = i;
		hr = SafeArrayRedim(psa, &rgsBound);
		if (FAILED(hr))
			goto Cleanup;
	}

	*ppsa = psa;

	hr = S_OK;
Cleanup:
	if (fLockedSA)
		SafeArrayUnaccessData(psa);
	delete com_status;
	svn_pool_clear(g_pool);
	return hr;
}
