/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003 CollabNet.  All rights reserved.
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
 * @endcopyright
 *
 * @file Pool.cpp
 * @brief Implementation of the class Pool
 */

#include "Pool.h"
#include "JNIUtil.h"
#include <svn_pools.h>


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

Pool::Pool(bool exclusive)
{
	m_pool = svn_pool_create(JNIUtil::getPool());
	if(!exclusive)
	{
		JNIUtil::setRequestPool(this);
	}
}

Pool::~Pool()
{
	if(JNIUtil::getRequestPool() == this)
	{
		JNIUtil::setRequestPool(NULL);
	}
	if(m_pool)
	{
		svn_pool_destroy (m_pool);
	}

}

apr_pool_t * Pool::pool () const
{
    return m_pool;
}
