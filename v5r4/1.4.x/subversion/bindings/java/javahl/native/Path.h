/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2006 CollabNet.  All rights reserved.
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
 * @file Path.h
 * @brief Interface of the C++ class Path.
 */

#if !defined(AFX_PATH_H__A143CB2A_1115_4770_8CD5_AA33CCD285FA__INCLUDED_)
#define AFX_PATH_H__A143CB2A_1115_4770_8CD5_AA33CCD285FA__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <string>
#include <jni.h>
struct svn_error_t;
/**
 * Encapsulation for Subversion Path handling
 */
class Path  
{
private:
    // the path to be stored
    std::string m_path;

    svn_error_t *m_error_occured;

    /**
     * initialize the class
     *
     * @param pi_path Path string
     */
    void init(const char * pi_path);

public:
    /**
     * Constructor that takes a string as parameter.
     * The string is converted to subversion internal
     * representation. The string is copied.
     *
     * @param pi_path Path string
     */
    Path(const std::string & pi_path = "");
    
    /**
     * Constructor
     *
     * @see Path::Path (const std::string &)
     * @param pi_path Path string
     */
    Path(const char * pi_path);

    /**
     * Copy constructor
     *
     * @param pi_path Path to be copied
     */
    Path(const Path & pi_path);

    /**
     * Assignment operator
     */
    Path& operator=(const Path&);

    /**
     * @return Path string
     */
    const std::string &
      path() const;

    /**
     * @return Path string as c string
     */
    const char * 
    c_str() const;

    svn_error_t * 
    error_occured() const;

    /**
     * Returns whether @a path is non-NULL and passes the @c
     * svn_path_check_valid() test.
     *
     * @since 1.4.0
     */
    static jboolean isValid(const char *path);
};

// !defined(AFX_PATH_H__A143CB2A_1115_4770_8CD5_AA33CCD285FA__INCLUDED_)
#endif 
