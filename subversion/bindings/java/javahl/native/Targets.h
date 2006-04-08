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
 * @file Targets.h
 * @brief Interface of the class Targets
 */

#if !defined(AFX_TARGETS_H__61202731_41A4_43FF_97C4_7E26DC255BF1__INCLUDED_)
#define AFX_TARGETS_H__61202731_41A4_43FF_97C4_7E26DC255BF1__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <jni.h>
struct apr_array_header_t;
struct svn_error_t;
class Pool;
#include "Path.h"
#include <vector>

class Targets
{
private:
    std::vector<Path> m_targets;
    jobjectArray m_targetArray;
    svn_error_t *m_error_occured;
    bool m_doesNotContainsPath;
public:
    Targets(jobjectArray jtargets);
    Targets(const char *path);
    void add(const char *path);
    ~Targets();
    const apr_array_header_t *array(const Pool & pool);
    svn_error_t *error_occured();
    void setDoesNotContainsPath();
};
// !defined(AFX_TARGETS_H__61202731_41A4_43FF_97C4_7E26DC255BF1__INCLUDED_)
#endif
