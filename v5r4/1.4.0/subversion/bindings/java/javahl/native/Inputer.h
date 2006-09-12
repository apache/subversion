/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2004 CollabNet.  All rights reserved.
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
 * @file Inputer.h
 * @brief Interface of the class Inputer
 */

#if !defined(AFX_INPUTER_H__6896EB42_07D2_406B_A4A5_F2908AFF4815__INCLUDED_)
#define AFX_INPUTER_H__6896EB42_07D2_406B_A4A5_F2908AFF4815__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <jni.h>
#include "svn_io.h"
#include "Pool.h"

/**
 * This class contains a java objects implementing the interface Inputer and
 * implements the functions read & close of svn_stream_t
 */
class Inputer
{
private:
    /**
     * a local reference to the java object
     */
    jobject m_jthis;
    static svn_error_t *read(void *baton, char *buffer, apr_size_t *len);
    static svn_error_t *close(void *baton);
public:
    Inputer(jobject jthis);
    ~Inputer();
    svn_stream_t *getStream(const Pool & pool);
};
// !defined(AFX_INPUTER_H__6896EB42_07D2_406B_A4A5_F2908AFF4815__INCLUDED_)
#endif
