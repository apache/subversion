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

#ifndef WINSVN_MARHSALL_H_
#define WINSVN_MARSHALL_H_
#pragma once

// This class is a thread independant container
// of connection points.
// See example usage in svn_comCP.h.

// BUG BUG BUG BUG BUG BUG BUG BUG BUG BUG BUG BUG BUG BUG BUG BUG BUG BUG
// This class as is returns IMarshal * for IEnumConnectionPoint calls.
// This doesn't cause any problems currently.
// The only way to fix this is to have a per thread proxy cache.
// BUT... This requires catching DLL_THREAD_DETACH in DllMain in order 
// so the proxies won't leak. This is definately a "Don't fix what ain't broke"
// problem.
// BUG BUG BUG BUG BUG BUG BUG BUG BUG BUG BUG BUG BUG BUG BUG BUG BUG BUG

template <const IID* piid>
class CComDynamicMarshalledUnkArray
{
public:
	CComDynamicMarshalledUnkArray()
	{
		m_nSize = 0;
		m_ppUnk = NULL;
	}

	~CComDynamicMarshalledUnkArray()
	{
		if (m_nSize > 1)
			free(m_ppUnk);
	}

	DWORD 
	Add(IUnknown* pUnk);

	BOOL 
	Remove(DWORD dwCookie);

	static DWORD WINAPI 
	GetCookie(IUnknown** pp)
	{
		return (DWORD)*pp;
	}

	static IUnknown* WINAPI 
	GetUnknown(DWORD dwCookie)
	{
		return (IUnknown*)dwCookie;
	}

	IUnknown** 
	begin()
	{
		return (m_nSize < 2) ? &m_pUnk : m_ppUnk;
	}

	IUnknown** 
	end()
	{
		return (m_nSize < 2) ? (&m_pUnk)+m_nSize : &m_ppUnk[m_nSize];
	}

	IUnknown* 
	GetAt(int nIndex)
	{
		IUnknown *pUnk = NULL;
		IUnknown *pUnk2 = NULL;
		HRESULT hr;

		if (nIndex < 0 || nIndex >= m_nSize)
			return NULL;

		pUnk = (m_nSize < 2) ? m_pUnk : m_ppUnk[nIndex];
		// Unmarshall stream.
		// We have to use CoUnmarshallInterface instead of
		// CoGetInterfaceAndReleaseStream because
		// we're caching the Marshalled interface pointer
		// for whomever needs us.
		hr = CoUnmarshalInterface(
			reinterpret_cast<IStream *>(pUnk),
			*piid,
			(void **)&pUnk2);
		if (FAILED(hr))
			return NULL;
		return pUnk2;
	}

	int 
	GetSize() const
	{
		return m_nSize;
	}

	void 
	clear()
	{
		if (m_nSize > 1)
			free(m_ppUnk);
		m_nSize = 0;
	}
protected:
	union
	{
		IUnknown** m_ppUnk;
		IUnknown* m_pUnk;
	};
	int m_nSize;
};

template <const IID* piid>
inline DWORD 
CComDynamicMarshalledUnkArray<piid>::Add(IUnknown* pUnk)
{
	IUnknown** pp = NULL;
	IStream *pStream = NULL;
	HRESULT hr;

	// Marshall the IUnknown pointer, so that we can
	// use it in multiple threads.
	hr = CoMarshalInterThreadInterfaceInStream(
		*piid,
		pUnk,
		&pStream);
	if (FAILED(hr))
	{
		return 0;
	}

	pUnk = reinterpret_cast<IUnknown *>(pStream);
		
	if (m_nSize == 0) // no connections
	{
		m_pUnk = pUnk;
		m_nSize = 1;
		return (DWORD)m_pUnk;
	}
	else if (m_nSize == 1)
	{
		// create array
		pp = (IUnknown**)malloc(sizeof(IUnknown*)*_DEFAULT_VECTORLENGTH);
		if (pp == NULL)
			return 0;
		memset(pp, 0, sizeof(IUnknown*)*_DEFAULT_VECTORLENGTH);
		*pp = m_pUnk;
		m_ppUnk = pp;
		m_nSize = _DEFAULT_VECTORLENGTH;
	}
	for (pp = begin();pp<end();pp++)
	{
		if (*pp == NULL)
		{
			*pp = pUnk;
			return (DWORD)pUnk;
		}
	}
	int nAlloc = m_nSize*2;
	pp = (IUnknown**)realloc(m_ppUnk, sizeof(IUnknown*)*nAlloc);
	if (pp == NULL)
		return 0;
	m_ppUnk = pp;
	memset(&m_ppUnk[m_nSize], 0, sizeof(IUnknown*)*m_nSize);
	m_ppUnk[m_nSize] = pUnk;
	m_nSize = nAlloc;
	return (DWORD)pUnk;
}

template <const IID *piid>
inline BOOL 
CComDynamicMarshalledUnkArray<piid>::Remove(DWORD dwCookie)
{
	IUnknown** pp;
	if (dwCookie == NULL)
		return FALSE;
	if (m_nSize == 0)
		return FALSE;
	if (m_nSize == 1)
	{
		if ((DWORD)m_pUnk == dwCookie)
		{
			m_nSize = 0;
			return TRUE;
		}
		return FALSE;
	}
	for (pp=begin();pp<end();pp++)
	{
		if ((DWORD)*pp == dwCookie)
		{
			*pp = NULL;
			return TRUE;
		}
	}
	return FALSE;
}

#endif
