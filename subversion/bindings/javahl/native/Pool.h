/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2007 CollabNet.  All rights reserved.
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

#ifndef POOL_H
#define POOL_H

#include "svn_pools.h"

/**
 * This class manages one APR pool.  Objects of this class are
 * allocated on the stack of the SVNClient and SVNAdmin methods as the
 * request pool.  Leaving the methods will destroy the pool.
 */
class Pool
{
 public:
  Pool();
  ~Pool();
  apr_pool_t *pool() const;
  void clear() const;

 private:
  /**
   * The apr pool request pool.
   */
  apr_pool_t *m_pool;

  /**
   * We declare the copy constructor and assignment operator private
   * here, so that the compiler won't inadvertently use them for us.
   * The default copy constructor just copies all the data members,
   * which would create two pointers to the same pool, one of which
   * would get destroyed while the other thought it was still
   * valid...and BOOM!  Hence the private declaration.
   */
  Pool(Pool &that);
  Pool &operator=(Pool &that);
};

// The following one-line functions are best inlined by the compiler, and
// need to be implemented in the header file for that to happen.

inline
apr_pool_t *Pool::pool() const
{
  return m_pool;
}

inline
void Pool::clear() const
{
  svn_pool_clear(m_pool);
}


#endif // POOL_H
