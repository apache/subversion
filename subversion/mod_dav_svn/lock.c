/*
 * lock.c: mod_dav_svn locking provider functions
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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



#include <httpd.h>
#include <http_log.h>
#include <mod_dav.h>
#include <apr_uuid.h>

#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_dav.h"
#include "svn_time.h"
#include "svn_pools.h"

#include "dav_svn.h"



/* Return the supportedlock property for a resource */
static const char *
dav_svn_get_supportedlock(const dav_resource *resource)
{
  return "";  /* temporary: just to suppress compile warnings */
}



/* Parse a lock token URI, returning a lock token object allocated
 * in the given pool.
 */
static dav_error *
dav_svn_parse_locktoken(apr_pool_t *pool,
                        const char *char_token,
                        dav_locktoken **locktoken_p)
{
  return 0;  /* temporary: just to suppress compile warnings */
}



/* Format a lock token object into a URI string, allocated in
 * the given pool.
 *
 * Always returns non-NULL.
 */
static const char *
dav_svn_format_locktoken(apr_pool_t *p,
                         const dav_locktoken *locktoken)
{
  return "";  /* temporary: just to suppress compile warnings */
}



/* Compare two lock tokens.
 *
 * Result < 0  => lt1 < lt2
 * Result == 0 => lt1 == lt2
 * Result > 0  => lt1 > lt2
 */
static int
dav_svn_compare_locktoken(const dav_locktoken *lt1,
                          const dav_locktoken *lt2)
{
  return 0;  /* temporary: just to suppress compile warnings */
}



/* Open the provider's lock database.
 *
 * The provider may or may not use a "real" database for locks
 * (a lock could be an attribute on a resource, for example).
 *
 * The provider may choose to use the value of the DAVLockDB directive
 * (as returned by dav_get_lockdb_path()) to decide where to place
 * any storage it may need.
 *
 * The request storage pool should be associated with the lockdb,
 * so it can be used in subsequent operations.
 *
 * If ro != 0, only readonly operations will be performed.
 * If force == 0, the open can be "lazy"; no subsequent locking operations
 * may occur.
 * If force != 0, locking operations will definitely occur.
 */
static dav_error *
dav_svn_open_lockdb(request_rec *r,
                    int ro,
                    int force,
                    dav_lockdb **lockdb)
{
  return 0;  /* temporary: just to suppress compile warnings */
}



/* Indicates completion of locking operations */
static void
dav_svn_close_lockdb(dav_lockdb *lockdb)
{
  return;
}



/* Take a resource out of the lock-null state. */
static dav_error *
dav_svn_remove_locknull_state(dav_lockdb *lockdb,
                              const dav_resource *resource)
{
  return 0;  /* temporary: just to suppress compile warnings */
}



/*
** Create a (direct) lock structure for the given resource. A locktoken
** will be created.
**
** The lock provider may store private information into lock->info.
*/
static dav_error *
dav_svn_create_lock(dav_lockdb *lockdb,
                    const dav_resource *resource,
                    dav_lock **lock)
{
  return 0;  /* temporary: just to suppress compile warnings */
}



/*
** Get the locks associated with the specified resource.
**
** If resolve_locks is true (non-zero), then any indirect locks are
** resolved to their actual, direct lock (i.e. the reference to followed
** to the original lock).
**
** The locks, if any, are returned as a linked list in no particular
** order. If no locks are present, then *locks will be NULL.
**
** #define DAV_GETLOCKS_RESOLVED   0    -- resolve indirects to directs 
** #define DAV_GETLOCKS_PARTIAL    1    -- leave indirects partially filled 
** #define DAV_GETLOCKS_COMPLETE   2    -- fill out indirect locks
*/
static dav_error *
dav_svn_get_locks(dav_lockdb *lockdb,
                  const dav_resource *resource,
                  int calltype,
                  dav_lock **locks)
{
  return 0;  /* temporary: just to suppress compile warnings */
}



/*
** Find a particular lock on a resource (specified by its locktoken).
**
** *lock will be set to NULL if the lock is not found.
**
** Note that the provider can optimize the unmarshalling -- only one
** lock (or none) must be constructed and returned.
**
** If partial_ok is true (non-zero), then an indirect lock can be
** partially filled in. Otherwise, another lookup is done and the
** lock structure will be filled out as a DAV_LOCKREC_INDIRECT.
*/
static dav_error *
dav_svn_find_lock(dav_lockdb *lockdb,
                  const dav_resource *resource,
                  const dav_locktoken *locktoken,
                  int partial_ok,
                  dav_lock **lock)
{
  return 0;  /* temporary: just to suppress compile warnings */
}



/*
** Quick test to see if the resource has *any* locks on it.
**
** This is typically used to determine if a non-existent resource
** has a lock and is (therefore) a locknull resource.
**
** WARNING: this function may return TRUE even when timed-out locks
**          exist (i.e. it may not perform timeout checks).
*/
static dav_error *
dav_svn_has_locks(dav_lockdb *lockdb,
                  const dav_resource *resource,
                  int *locks_present)
{
  return 0;  /* temporary: just to suppress compile warnings */
}



/*
** Append the specified lock(s) to the set of locks on this resource.
**
** If "make_indirect" is true (non-zero), then the specified lock(s)
** should be converted to an indirect lock (if it is a direct lock)
** before appending. Note that the conversion to an indirect lock does
** not alter the passed-in lock -- the change is internal the
** append_locks function.
**
** Multiple locks are specified using the lock->next links.
*/
static dav_error *
dav_svn_append_locks(dav_lockdb *lockdb,
                     const dav_resource *resource,
                     int make_indirect,
                     const dav_lock *lock)
{
  return 0;  /* temporary: just to suppress compile warnings */
}



/*
** Remove any lock that has the specified locktoken.
**
** If locktoken == NULL, then ALL locks are removed.
*/
static dav_error *
dav_svn_remove_lock(dav_lockdb *lockdb,
                    const dav_resource *resource,
                    const dav_locktoken *locktoken)
{
  return 0;  /* temporary: just to suppress compile warnings */
}



/*
** Refresh all locks, found on the specified resource, which has a
** locktoken in the provided list.
**
** If the lock is indirect, then the direct lock is referenced and
** refreshed.
**
** Each lock that is updated is returned in the <locks> argument.
** Note that the locks will be fully resolved.
*/
static dav_error *
dav_svn_refresh_locks(dav_lockdb *lockdb,
                      const dav_resource *resource,
                      const dav_locktoken_list *ltl,
                      time_t new_time,
                      dav_lock **locks)
{
  return 0;  /* temporary: just to suppress compile warnings */
}



/*
** Look up the resource associated with a particular locktoken.
**
** The search begins at the specified <start_resource> and the lock
** specified by <locktoken>.
**
** If the resource/token specifies an indirect lock, then the direct
** lock will be looked up, and THAT resource will be returned. In other
** words, this function always returns the resource where a particular
** lock (token) was asserted.
**
** NOTE: this function pointer is allowed to be NULL, indicating that
**       the provider does not support this type of functionality. The
**       caller should then traverse up the repository hierarchy looking
**       for the resource defining a lock with this locktoken.
*/
static dav_error *
dav_svn_lookup_resource(dav_lockdb *lockdb,
                        const dav_locktoken *locktoken,
                        const dav_resource *start_resource,
                        const dav_resource **resource)
{
  return 0;  /* temporary: just to suppress compile warnings */
}





/* The main locking vtable, provided to mod_dav */

const dav_hooks_locks dav_svn_hooks_locks = {
  dav_svn_get_supportedlock,
  dav_svn_parse_locktoken,
  dav_svn_format_locktoken,
  dav_svn_compare_locktoken,
  dav_svn_open_lockdb,
  dav_svn_close_lockdb,
  dav_svn_remove_locknull_state,
  dav_svn_create_lock,
  dav_svn_get_locks,
  dav_svn_find_lock,
  dav_svn_has_locks,
  dav_svn_append_locks,
  dav_svn_remove_lock,
  dav_svn_refresh_locks,
  dav_svn_lookup_resource,
  NULL                          /* hook structure context */
};
