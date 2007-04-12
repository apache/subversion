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
 * @file Pool.h
 * @brief Interface of the class Pool
 */

#if !defined(AFX_POOL_H__4755FB06_B88C_451D_A0EE_91F5A547C30B__INCLUDED_)
#define AFX_POOL_H__4755FB06_B88C_451D_A0EE_91F5A547C30B__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

struct apr_pool_t;

/**
 * this class manages one apr pool. Objects of this class are allocated on
 * the stack of the SVNClient & SVNAdmin methods as the request pool.
 * Leaving the methods will destroy the pool.
 */
class Pool
{
public:
    Pool();
    ~Pool();

    operator apr_pool_t * () const;

    void clear();
private:
    /**
     * the apr pool request pool
     */
    apr_pool_t *pool;

    /**
     * We declare the copy constructor and assignment operator private
     * here, so that the compiler won't inadvertently use them for us.
     * The default copy constructor just copies all the data members,
     * which would create two pointers to the same pool, one of which
     * would get destroyed while the other thought it was still
     * valid...and BOOM!  Hence the private declaration.
     */
    Pool(Pool &that);
    Pool &operator= (Pool &that);
};
// !defined(AFX_POOL_H__4755FB06_B88C_451D_A0EE_91F5A547C30B__INCLUDED_)
#endif
