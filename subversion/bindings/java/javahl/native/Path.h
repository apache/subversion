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
 * @file Path.h
 * @brief Interface of the class Path
 */

#if !defined(AFX_PATH_H__A143CB2A_1115_4770_8CD5_AA33CCD285FA__INCLUDED_)
#define AFX_PATH_H__A143CB2A_1115_4770_8CD5_AA33CCD285FA__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <string>

  /**
   * Encapsulation for Subversion Path handling
   */
class Path  
{
  private:
    std::string m_path;

    /**
     * initialize the class
     *
     * @param path Path string
     */
    void init (const char * path);
public:
    /**
     * Constructor that takes a string as parameter.
     * The string is converted to subversion internal
     * representation. The string is copied.
     *
     * @param path Path string
     */
    Path (const std::string & path = "");
    
    /**
     * Constructor
     *
     * @see Path::Path (const std::string &)
     * @param path Path string
     */
    Path (const char * path);

    /**
     * Copy constructor
     *
     * @param path Path to be copied
     */
    Path (const Path & path);

    /**
     * Assignment operator
     */
    Path& operator=(const Path&);

    /**
     * @return Path string
     */
    const std::string &
    path () const;

    /**
     * @return Path string as c string
     */
    const char * 
    c_str() const;
};

#endif // !defined(AFX_PATH_H__A143CB2A_1115_4770_8CD5_AA33CCD285FA__INCLUDED_)
