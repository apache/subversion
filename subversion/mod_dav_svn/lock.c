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
#include <apr_time.h>

#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_dav.h"
#include "svn_time.h"
#include "svn_pools.h"

#include "dav_svn.h"


/* Every provider needs to define an opaque locktoken type. */
struct dav_locktoken
{
  /* This is identical to the 'token' field of an svn_lock_t. */
  const char *uuid_str;
};


/* Helper func:  convert an svn_lock_t to a dav_lock, allocated in
   pool.  EXISTS_P indicates whether slock->path actually exists or not.
 */
static void
svn_lock_to_dav_lock(dav_lock **dlock,
                     const svn_lock_t *slock,
                     svn_boolean_t exists_p,
                     apr_pool_t *pool)
{
  dav_lock *lock = apr_pcalloc(pool, sizeof(*lock));
  dav_locktoken *token = apr_pcalloc(pool, sizeof(*token));

  lock->rectype = DAV_LOCKREC_DIRECT;
  lock->scope = DAV_LOCKSCOPE_EXCLUSIVE;
  lock->type = DAV_LOCKTYPE_WRITE;
  lock->depth = 0;
  lock->is_locknull = exists_p;

  token->uuid_str = apr_pstrdup(pool, slock->token);
  lock->locktoken = token;

  /* ### I really don't understand the difference here: */
  lock->owner = apr_pstrdup(pool, slock->owner);
  lock->auth_user = lock->owner;

  /* ### This is absurd.  apr_time.h has an apr_time_t->time_t func,
     but not the reverse?? */
  if (slock->expiration_date)
    lock->timeout = (time_t)slock->expiration_date / APR_USEC_PER_SEC;
  else
    lock->timeout = DAV_TIMEOUT_INFINITE;

  /* ### uhoh.  There's no concept of a lock creation-time in DAV.
         How do we get that value over to the client?  Maybe we should
         just get rid of that field in svn_lock_t?  */

  *dlock = lock;
}



/* Helper func:  convert a dav_lock to an svn_lock_t, allocated in pool. */
static void
dav_lock_to_svn_lock(svn_lock_t **slock,
                     const dav_lock *dlock,
                     const char *path,
                     apr_pool_t *pool)
{
  svn_lock_t *lock = apr_pcalloc(pool, sizeof(*lock));

  lock->path = apr_pstrdup(pool, path);

  lock->token = apr_pstrdup(pool, dlock->locktoken->uuid_str);

  /* DAV has no concept of lock creationdate, so assume 'now' */
  lock->creation_date = apr_time_now();

  /* ### Should we use 'auth_user' field instead?  What's the difference? */
  if (dlock->owner)
    lock->owner = apr_pstrdup(pool, dlock->owner);
  
  if (dlock->timeout)
    lock->expiration_date = (apr_time_t)dlock->timeout * APR_USEC_PER_SEC;
  else
    lock->expiration_date = 0; /* never expires */

  *slock = lock;
}





/* Return the supportedlock property for a resource */
static const char *
dav_svn_get_supportedlock(const dav_resource *resource)
{
  /* This is imitating what mod_dav_fs is doing.  Note that unlike
     mod_dav_fs, however, we don't support "shared" locks.  */

  /* ### it seems awfully weird that a provider knows that
     mod_dav is going to use 'D=DAV' for xml namespaces, no? */
  static const char supported[] = DEBUG_CR
    "<D:lockentry>" DEBUG_CR
    "<D:lockscope><D:exclusive/></D:lockscope>" DEBUG_CR
    "<D:locktype><D:write/></D:locktype>" DEBUG_CR
    "</D:lockentry>" DEBUG_CR;
    
  return supported;
}



/* Parse a lock token URI, returning a lock token object allocated
 * in the given pool.
 */
static dav_error *
dav_svn_parse_locktoken(apr_pool_t *pool,
                        const char *char_token,
                        dav_locktoken **locktoken_p)
{
  dav_locktoken *token = apr_pcalloc(pool, sizeof(*token));
  
  /* Imitating mod_dav_fs again.  Hilariously, it also defines a
     locktoken just to be an apr uuid string!  */

  if (ap_strstr_c(char_token, "opaquelocktoken:") != char_token) 
    return dav_new_error(pool, HTTP_BAD_REQUEST,
                         DAV_ERR_LOCK_UNK_STATE_TOKEN,
                         "Client supplied lock token in unknown format.");

  char_token += 16;
  token->uuid_str = apr_pstrdup(pool, char_token);
  
  *locktoken_p = token;
  return 0;
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
  /* Imitating mod_dav_fs again.  Hilariously, it also defines a
     locktoken just to be an apr uuid string!  */

  return apr_pstrcat(p, "opaquelocktoken:", locktoken->uuid_str, NULL);
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
  return strcmp(lt1->uuid_str, lt2->uuid_str);
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
  dav_lockdb *db = apr_pcalloc(r->pool, sizeof(*db));

  db->hooks = &dav_svn_hooks_locks;
  db->ro = ro;
  db->info = NULL; /* we could add private context someday. */

  *lockdb = db;
  return 0;
}



/* Indicates completion of locking operations */
static void
dav_svn_close_lockdb(dav_lockdb *lockdb)
{
  /* nothing to do here. */
  return;
}



/* Take a resource out of the lock-null state. */
static dav_error *
dav_svn_remove_locknull_state(dav_lockdb *lockdb,
                              const dav_resource *resource)
{
  /* ### perhaps our resource->info context should keep track if a
     resource is in 'locknull' state', and not merely non-existent?
     According to RFC 2518, 'locknull' resources are supposed to be
     listed as children of their parent collections (e.g. a PROPFIND
     on the parent).  */

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
  apr_uuid_t uuid;
  dav_locktoken *token = apr_pcalloc(resource->pool, sizeof(*token));
  dav_lock *dlock = apr_pcalloc(resource->pool, sizeof(*dlock));
  char *uuid_str = apr_pcalloc (resource->pool, APR_UUID_FORMATTED_LENGTH + 1);
  
  dlock->rectype = DAV_LOCKREC_DIRECT;
  dlock->is_locknull = resource->exists;
  dlock->scope = DAV_LOCKSCOPE_EXCLUSIVE;
  dlock->type = DAV_LOCKTYPE_WRITE;
  dlock->depth = 0;

  /* Generate a UUID. */
  /* ### perhaps this should be a func in libsvn_fs.so, shared by
     mod_dav_svn and both fs back-ends??  */
  apr_uuid_get (&uuid);
  apr_uuid_format (uuid_str, &uuid);
  token->uuid_str = uuid_str;
  dlock->locktoken = token;

  /* allowing mod_dav to fill in dlock->timeout, owner, auth_user. */
  /* dlock->info and dlock->next are NULL by default. */

  *lock = dlock;
  return 0;  
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
  svn_error_t *serr;
  svn_lock_t *slock;
  dav_lock *lock = NULL;

  /* We only support exclusive locks, not shared ones.  So this
     function always returns a "list" of exactly one lock, or just a
     NULL list.  The 'calltype' arg is also meaningless, since we
     don't support locks on collections.  */
  
  /* ### TODO: call authz_read callback here.  If the resource is
     unreadable, we don't want to say anything about locks attached to
     it.*/
  
  serr = svn_fs_get_lock_from_path(&slock,
                                   resource->info->repos->fs,
                                   resource->info->repos_path,
                                   resource->pool);
  if (serr)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "Failed to check path for a lock.",
                               resource->pool);

  if (slock != NULL)
    svn_lock_to_dav_lock(&lock, slock, resource->exists, resource->pool);

  *locks = lock;
  return 0;  
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
  svn_error_t *serr;
  svn_lock_t *slock;
  dav_lock *dlock;
  
  /* ### TODO: call authz_read callback here.  If the resource is
     unreadable, we don't want to say anything about locks attached to
     it.*/
  
  serr = svn_fs_get_lock_from_token(&slock,
                                    resource->info->repos->fs,
                                    locktoken->uuid_str,
                                    resource->pool);
  if (serr &&
      ((serr->apr_err == SVN_ERR_FS_BAD_LOCK_TOKEN)
       || (serr->apr_err == SVN_ERR_FS_LOCK_EXPIRED)))
    dlock = NULL;
  else if (serr)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "Failed to lookup lock via token.",
                               resource->pool);

  if (slock != NULL)
    svn_lock_to_dav_lock(&dlock, slock, resource->exists, resource->pool);

  *lock = dlock;
  return 0;  
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
  svn_error_t *serr;
  svn_lock_t *slock;

  /* ### TODO: call authz_read callback here.  If the resource is
     unreadable, we don't want to say anything about locks attached to
     it.*/
  
  serr = svn_fs_get_lock_from_path(&slock,
                                   resource->info->repos->fs,
                                   resource->info->repos_path,
                                   resource->pool);
  if (serr)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "Failed to check path for a lock.",
                               resource->pool);

  *locks_present = slock ? 1 : 0;
  return 0;
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
  svn_lock_t *slock;
  svn_error_t *serr;

  if (lock->next)
    return dav_new_error(resource->pool, HTTP_BAD_REQUEST,
                         DAV_ERR_LOCK_SAVE_LOCK,
                         "Tried to attach multiple locks to a resource.");

  /* Convert the dav_lock into an svn_lock_t. */
  dav_lock_to_svn_lock(&slock, lock, resource->info->repos_path,
                       resource->pool);

  /* Now use the svn_lock_t to actually perform the lock. */
  serr = svn_repos_fs_attach_lock(slock,
                                  resource->info->repos->repos,
                                  resource->pool);
  if (serr)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "Failed to create new lock.",
                               resource->pool);

  return 0;
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
  svn_error_t *serr;

  if (locktoken == NULL)
    return dav_new_error(resource->pool, HTTP_BAD_REQUEST,
                         DAV_ERR_IF_ABSENT,
                         "Cannot unlock a resource without a token.");
  
  serr = svn_repos_fs_unlock(resource->info->repos->repos,
                             locktoken->uuid_str,
                             0, /* don't forcibly break the lock */
                             resource->pool);
  if (serr)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "Failed to remove a lock.",
                               resource->pool);
  return 0;
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
  /* We're not looping over a list of locks, since we only support one
     lock per resource. */
  dav_locktoken *token = ltl->locktoken;
  svn_error_t *serr;
  svn_lock_t *slock;
  dav_lock *dlock;

  /* ### TODO: call authz_read callback here.  If the resource is
     unreadable, we don't want to say anything about locks attached to
     it.*/

  /* Convert the token into an svn_lock_t. */
  serr = svn_fs_get_lock_from_token(&slock,
                                    resource->info->repos->fs,
                                    token->uuid_str,
                                    resource->pool);
  if (serr)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "Token doesn't point to a lock.",
                               resource->pool);


  /* Tweak the expiration_date to the new expiration time. */
  slock->expiration_date = (apr_time_t)new_time * APR_USEC_PER_SEC;

  /* Now use the tweaked svn_lock_t to 'refresh' the existing lock. */
  serr = svn_repos_fs_attach_lock(slock,
                                  resource->info->repos->repos,
                                  resource->pool);
  if (serr)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "Failed to refresh existing lock.",
                               resource->pool);

  /* Convert the refreshed lock into a dav_lock and return it. */
  svn_lock_to_dav_lock(&dlock, slock, resource->exists, resource->pool);
  *locks = dlock;

  return 0;
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

  /* ### no reason to implement this func, see docstring. */

/* static dav_error *
   dav_svn_lookup_resource(dav_lockdb *lockdb,
                           const dav_locktoken *locktoken,
                           const dav_resource *start_resource,
                           const dav_resource **resource)
  {
    return 0;
  }
*/




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
  NULL,
  NULL                          /* hook structure context */
};
