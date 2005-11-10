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
 * @file Targets.cpp
 * @brief Implementation of the class Targets
 */

#include "Targets.h"
#include "Pool.h"
#include "JNIUtil.h"
#include "JNIStringHolder.h"
#include <apr_tables.h>
#include <apr_strings.h>
#include "svn_path.h"
#include <iostream>
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

Targets::~Targets()
{
    if(m_targetArray != NULL)
    {
        JNIUtil::getEnv()->DeleteLocalRef(m_targetArray);
    }
}
Targets::Targets(const char *path)
{
    m_targetArray = NULL;
    m_targets.push_back (path);
    m_error_occured = NULL;
    m_doesNotContainsPath = false;
}
void Targets::add(const char *path)
{
    m_targets.push_back (path);
}
const apr_array_header_t *Targets::array (const Pool & pool)
{
    if(m_targetArray != NULL)
    {
        JNIEnv *env = JNIUtil::getEnv();
        jint arraySize = env->GetArrayLength(m_targetArray);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
        jclass clazz = env->FindClass("java/lang/String");
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
        for( int i = 0; i < arraySize; i++)
        {
            jobject elem = env->GetObjectArrayElement(m_targetArray, i);
            if(JNIUtil::isJavaExceptionThrown())
            {
                return NULL;
            }
            if(env->IsInstanceOf(elem, clazz))
            {
                JNIStringHolder text((jstring)elem);
                if(JNIUtil::isJavaExceptionThrown())
                {
                    return NULL;
                }
                const char *tt = (const char *)text;
                if(!m_doesNotContainsPath)
                {
                    svn_error_t *err = JNIUtil::preprocessPath(tt, pool.pool());
                    if(err != NULL)
                    {
                        m_error_occured = err;
                        break;
                    }
                }
                m_targets.push_back(tt);
            }
            if(JNIUtil::isJavaExceptionThrown())
            {
                return NULL;
            }
            JNIUtil::getEnv()->DeleteLocalRef(elem);
        }
        JNIUtil::getEnv()->DeleteLocalRef(clazz);
        //JNIUtil::getEnv()->DeleteLocalRef(m_targetArray);
        m_targetArray = NULL;
    }

    std::vector<Path>::const_iterator it;

    apr_pool_t *apr_pool = pool.pool ();
    apr_array_header_t *apr_targets =
      apr_array_make (apr_pool,
                      m_targets.size(),
                      sizeof (const char *));

    for (it = m_targets.begin (); it != m_targets.end (); it++)
    {
      const Path &path = *it;
      const char * target =
        apr_pstrdup (apr_pool, path.c_str());
      (*((const char **) apr_array_push (apr_targets))) = target;
    }

    return apr_targets;
}

Targets::Targets(jobjectArray jtargets)
{
    m_targetArray = jtargets;
    m_error_occured = NULL;
}

svn_error_t *Targets::error_occured()
{
    return m_error_occured;
}

void Targets::setDoesNotContainsPath()
{
    m_doesNotContainsPath = true;
}
