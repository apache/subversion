/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003 QintSoft.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://svnup.tigris.org/.
 * ====================================================================
 * @endcopyright
 *
 * @file Path.cpp
 * @brief Implementation of the class Path
 */

#include "Path.h"
#include "svn_path.h"
#include "JNIUtil.h"
#include "Pool.h"

Path::Path (const char * path)
{
init (path);
}

Path::Path (const std::string & path) 
{
init (path.c_str ());
}

Path::Path (const Path & path) 
{
init (path.c_str ());
}

void
Path::init (const char * path)
{
	if(*path == 0)
	{
		m_path = "";
	}
	else
	{
		const char * int_path = svn_path_internal_style (path, JNIUtil::getRequestPool()->pool() );

		m_path = int_path;
	}
}

const std::string &
Path::path () const
{
return m_path;
}

const char * 
Path::c_str() const
{
return m_path.c_str ();
}

Path& 
Path::operator=(const Path & path)
{
init (path.c_str ());
return *this;
}
