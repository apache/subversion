/*
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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

// stdafx.h : include file for standard system include files,
//      or project specific include files that are used frequently,
//      but are changed infrequently

#if !defined(AFX_STDAFX_H__C6D9D561_DBA4_4B5F_B745_EEE2067A9253__INCLUDED_)
#define AFX_STDAFX_H__C6D9D561_DBA4_4B5F_B745_EEE2067A9253__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#define STRICT
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0400
#endif
#define _ATL_APARTMENT_THREADED

#include <atlbase.h>
//You may derive a class from CComModule and use it if you want to override
//something, but do not change the name of _Module
extern CComModule _Module;
#include <atlcom.h>
#include <atlconv.h>
#include <stdio.h>
#include <crtdbg.h>
#define Assert(exp) _ASSERTE(exp)

extern "C" {
#include <apr_hash.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_general.h>

#include <svn_types.h>
#include <svn_error.h>
#include <svn_pools.h>
#include <svn_path.h>
#include <svn_delta.h>
#include <svn_wc.h>
};

#include "misc.h"


//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_STDAFX_H__C6D9D561_DBA4_4B5F_B745_EEE2067A9253__INCLUDED)
