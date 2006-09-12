/*
 * version.c: mod_dav_svn versioning provider functions for Subversion
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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
#include <apr_tables.h>
#include <apr_uuid.h>

#include "svn_fs.h"
#include "svn_xml.h"
#include "svn_repos.h"
#include "svn_dav.h"
#include "svn_time.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_dav.h"
#include "svn_base64.h"

#include "dav_svn.h"


/* ### should move these report names to a public header to share with
   ### the client (and third parties). */
static const dav_report_elem avail_reports[] = {
  { SVN_XML_NAMESPACE, "update-report" },
  { SVN_XML_NAMESPACE, "log-report" },
  { SVN_XML_NAMESPACE, "dated-rev-report" },
  { SVN_XML_NAMESPACE, "get-locations" },
  { SVN_XML_NAMESPACE, "file-revs-report" },
  { SVN_XML_NAMESPACE, "get-locks-report" },
  { SVN_XML_NAMESPACE, "replay-report" },
  { NULL },
};

/* declare these static functions early, so we can use them anywhere. */
static dav_error *dav_svn_make_activity(dav_resource *resource);


svn_error_t *dav_svn_attach_auto_revprops(svn_fs_txn_t *txn,
                                          const char *fs_path,
                                          apr_pool_t *pool)
{
  const char *logmsg;
  svn_string_t *logval;
  svn_error_t *serr;

  logmsg = apr_psprintf(pool,  
                        "Autoversioning commit:  a non-deltaV client made "
                        "a change to\n%s", fs_path);

  logval = svn_string_create(logmsg, pool);
  if ((serr = svn_repos_fs_change_txn_prop(txn, SVN_PROP_REVISION_LOG, logval,
                                           pool)))
    return serr;

  /* Notate that this revision was created by autoversioning.  (Tools
     like post-commit email scripts might not care to send an email
     for every autoversioning change.) */
  if ((serr = svn_repos_fs_change_txn_prop(txn,
                                           SVN_PROP_REVISION_AUTOVERSIONED, 
                                           svn_string_create("*", pool),
                                           pool)))
    return serr;

  return SVN_NO_ERROR;
}


/* Helper: attach an auto-generated svn:log property to a txn within
   an auto-checked-out working resource. */
static dav_error *set_auto_revprops(dav_resource *resource)
{
  svn_error_t *serr;

  if (! (resource->type == DAV_RESOURCE_TYPE_WORKING
         && resource->info->auto_checked_out))
    return dav_new_error(resource->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                         "Set_auto_revprops called on invalid resource.");

  if ((serr = dav_svn_attach_auto_revprops(resource->info->root.txn,
                                           resource->info->repos_path,
                                           resource->pool)))
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "Error setting a revision property "
                               " on auto-checked-out resource's txn. ",
                               resource->pool);
  return NULL;
}


static dav_error *open_txn(svn_fs_txn_t **ptxn, svn_fs_t *fs,
                           const char *txn_name, apr_pool_t *pool)
{
  svn_error_t *serr;

  serr = svn_fs_open_txn(ptxn, fs, txn_name, pool);
  if (serr != NULL)
    {
      if (serr->apr_err == SVN_ERR_FS_NO_SUCH_TRANSACTION)
        {
          /* ### correct HTTP error? */
          return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                     "The transaction specified by the "
                                     "activity does not exist",
                                     pool);
        }

      /* ### correct HTTP error? */
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "There was a problem opening the "
                                 "transaction specified by this "
                                 "activity.",
                                 pool);
    }

  return NULL;
}

static void dav_svn_get_vsn_options(apr_pool_t *p, apr_text_header *phdr)
{
  /* Note: we append pieces with care for Web Folders's 63-char limit
     on the DAV: header */

  apr_text_append(p, phdr,
                  "version-control,checkout,working-resource");
  apr_text_append(p, phdr,
                  "merge,baseline,activity,version-controlled-collection");

  /* ### fork-control? */
}

static dav_error *dav_svn_get_option(const dav_resource *resource,
                                     const apr_xml_elem *elem,
                                     apr_text_header *option)
{
  /* ### DAV:version-history-collection-set */

  if (elem->ns == APR_XML_NS_DAV_ID)
    {
      if (strcmp(elem->name, "activity-collection-set") == 0)
        {
          apr_text_append(resource->pool, option,
                          "<D:activity-collection-set>");
          apr_text_append(resource->pool, option,
                          dav_svn_build_uri(resource->info->repos,
                                            DAV_SVN_BUILD_URI_ACT_COLLECTION,
                                            SVN_INVALID_REVNUM, NULL,
                                            1 /* add_href */, resource->pool));
          apr_text_append(resource->pool, option,
                          "</D:activity-collection-set>");
        }
    }

  return NULL;
}

static int dav_svn_versionable(const dav_resource *resource)
{
  return 0;
}

static dav_auto_version dav_svn_auto_versionable(const dav_resource *resource)
{
  /* The svn client attempts to proppatch a baseline when changing
     unversioned revision props.  Thus we allow baselines to be
     "auto-checked-out" by mod_dav.  See issue #916. */
  if (resource->type == DAV_RESOURCE_TYPE_VERSION
      && resource->baselined)
    return DAV_AUTO_VERSION_ALWAYS;

  /* No other autoversioning is allowed unless the SVNAutoversioning
     directive is used. */
  if (resource->info->repos->autoversioning)
    {
      /* This allows a straight-out PUT on a public file or collection
         VCR.  mod_dav's auto-versioning subsystem will check to see if
         it's possible to auto-checkout a regular resource. */
      if (resource->type == DAV_RESOURCE_TYPE_REGULAR)
        return DAV_AUTO_VERSION_ALWAYS;

      /* mod_dav's auto-versioning subsystem will also check to see if
         it's possible to auto-checkin a working resource that was
         auto-checked-out.  We *only* allow auto-versioning on a working
         resource if it was auto-checked-out. */
      if (resource->type == DAV_RESOURCE_TYPE_WORKING
          && resource->info->auto_checked_out)
        return DAV_AUTO_VERSION_ALWAYS;
    }

  /* Default:  whatever it is, assume it's not auto-versionable */
  return DAV_AUTO_VERSION_NEVER;
}

static dav_error *dav_svn_vsn_control(dav_resource *resource,
                                      const char *target)
{
  /* All mod_dav_svn resources are versioned objects;  so it doesn't
     make sense to call vsn_control on a resource that exists . */
  if (resource->exists)
    return dav_new_error(resource->pool, HTTP_BAD_REQUEST, 0,
                         "vsn_control called on already-versioned resource.");

  /* Only allow a NULL target, which means an create an 'empty' VCR. */
  if (target != NULL)
    return dav_svn__new_error_tag(resource->pool, HTTP_NOT_IMPLEMENTED,
                                  SVN_ERR_UNSUPPORTED_FEATURE,
                                  "vsn_control called with non-null target.",
                                  SVN_DAV_ERROR_NAMESPACE,
                                  SVN_DAV_ERROR_TAG);

  /* This is kind of silly.  The docstring for this callback says it's
     supposed to "put a resource under version control".  But in
     Subversion, all REGULAR resources (bc's or public URIs) are
     already under version control. So we don't need to do a thing to
     the resource, just return. */
  return NULL;
}

dav_error *dav_svn_checkout(dav_resource *resource,
                            int auto_checkout,
                            int is_unreserved, int is_fork_ok,
                            int create_activity,
                            apr_array_header_t *activities,
                            dav_resource **working_resource)
{
  const char *txn_name;
  svn_error_t *serr;
  apr_status_t apr_err;
  dav_error *derr;
  dav_svn_uri_info parse;

  /* Auto-Versioning Stuff */
  if (auto_checkout)
    {
      dav_resource *res; /* ignored */
      const char *uuid_buf;
      void *data;
      const char *shared_activity, *shared_txn_name = NULL;

      /* Baselines can be auto-checked-out -- grudgingly -- so we can
         allow clients to proppatch unversioned rev props.  See issue
         #916. */
      if ((resource->type == DAV_RESOURCE_TYPE_VERSION)
          && resource->baselined)
        /* ### We're violating deltaV big time here, by allowing a
           dav_auto_checkout() on something that mod_dav assumes is a
           VCR, not a VR.  Anyway, mod_dav thinks we're checking out the
           resource 'in place', so that no working resource is returned.
           (It passes NULL as **working_resource.)  */
        return NULL;

      if (resource->type != DAV_RESOURCE_TYPE_REGULAR)
        return dav_svn__new_error_tag(resource->pool, HTTP_METHOD_NOT_ALLOWED,
                                      SVN_ERR_UNSUPPORTED_FEATURE,
                                      "auto-checkout attempted on non-regular "
                                      "version-controlled resource.",
                                      SVN_DAV_ERROR_NAMESPACE,
                                      SVN_DAV_ERROR_TAG);

      if (resource->baselined)
        return dav_svn__new_error_tag(resource->pool, HTTP_METHOD_NOT_ALLOWED,
                                      SVN_ERR_UNSUPPORTED_FEATURE,
                                      "auto-checkout attempted on baseline "
                                      "collection, which is not supported.",
                                      SVN_DAV_ERROR_NAMESPACE,
                                      SVN_DAV_ERROR_TAG);

      /* See if the shared activity already exists. */
      apr_err = apr_pool_userdata_get(&data,
                                      DAV_SVN_AUTOVERSIONING_ACTIVITY,
                                      resource->info->r->pool);
      if (apr_err)
        return dav_svn_convert_err(svn_error_create(apr_err, 0, NULL),
                                   HTTP_INTERNAL_SERVER_ERROR,
                                   "Error fetching pool userdata.",
                                   resource->pool);
      shared_activity = data;

      if (! shared_activity)
        {
          /* Build a shared activity for all auto-checked-out resources. */
          uuid_buf = svn_uuid_generate(resource->info->r->pool);
          shared_activity = apr_pstrdup(resource->info->r->pool, uuid_buf);

          derr = dav_svn_create_activity(resource->info->repos,
                                         &shared_txn_name,
                                         resource->info->r->pool);
          if (derr) return derr;

          derr = dav_svn_store_activity(resource->info->repos,
                                        shared_activity, shared_txn_name);
          if (derr) return derr;

          /* Save the shared activity in r->pool for others to use. */         
          apr_err = apr_pool_userdata_set(shared_activity,
                                          DAV_SVN_AUTOVERSIONING_ACTIVITY,
                                          NULL, resource->info->r->pool);
          if (apr_err)
            return dav_svn_convert_err(svn_error_create(apr_err, 0, NULL),
                                       HTTP_INTERNAL_SERVER_ERROR,
                                       "Error setting pool userdata.",
                                       resource->pool);
        }

      if (! shared_txn_name)
        {                       
          shared_txn_name = dav_svn_get_txn(resource->info->repos,
                                            shared_activity);
          if (! shared_txn_name)
            return dav_new_error(resource->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                                 "Cannot look up a txn_name by activity");
        }

      /* Tweak the VCR in-place, making it into a WR.  (Ignore the
         NULL return value.) */
      res = dav_svn_create_working_resource(resource,
                                            shared_activity, shared_txn_name,
                                            TRUE /* tweak in place */);

      /* Remember that this resource was auto-checked-out, so that
         dav_svn_auto_versionable allows us to do an auto-checkin and
         dav_svn_can_be_activity will allow this resource to be an
         activity. */
      resource->info->auto_checked_out = TRUE;
        
      /* The txn and txn_root must be open and ready to go in the
         resource's root object.  Normally prep_resource() will do
         this automatically on a WR's root object.  We're
         converting a VCR to WR forcibly, so it's now our job to
         make sure it happens. */
      derr = open_txn(&resource->info->root.txn, resource->info->repos->fs,
                      resource->info->root.txn_name, resource->pool);
      if (derr) return derr;
      
      serr = svn_fs_txn_root(&resource->info->root.root,
                             resource->info->root.txn, resource->pool);
      if (serr != NULL)
        return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                   "Could not open a (transaction) root "
                                   "in the repository",
                                   resource->pool);
      return NULL;
    }
  /* end of Auto-Versioning Stuff */

  if (resource->type != DAV_RESOURCE_TYPE_VERSION)
    {
      return dav_svn__new_error_tag(resource->pool, HTTP_METHOD_NOT_ALLOWED,
                                    SVN_ERR_UNSUPPORTED_FEATURE,
                                    "CHECKOUT can only be performed on a "
                                    "version resource [at this time].",
                                    SVN_DAV_ERROR_NAMESPACE,
                                    SVN_DAV_ERROR_TAG);
    }
  if (create_activity)
    {
      return dav_svn__new_error_tag(resource->pool, HTTP_NOT_IMPLEMENTED,
                                    SVN_ERR_UNSUPPORTED_FEATURE,
                                    "CHECKOUT can not create an activity at "
                                    "this time. Use MKACTIVITY first.",
                                    SVN_DAV_ERROR_NAMESPACE,
                                    SVN_DAV_ERROR_TAG);
    }
  if (is_unreserved)
    {
      return dav_svn__new_error_tag(resource->pool, HTTP_NOT_IMPLEMENTED,
                                    SVN_ERR_UNSUPPORTED_FEATURE,
                                    "Unreserved checkouts are not yet "
                                    "available. A version history may not be "
                                    "checked out more than once, into a "
                                    "specific activity.",
                                    SVN_DAV_ERROR_NAMESPACE,
                                    SVN_DAV_ERROR_TAG);
    }
  if (activities == NULL)
    {
      return dav_svn__new_error_tag(resource->pool, HTTP_CONFLICT,
                                    SVN_ERR_INCOMPLETE_DATA,
                                    "An activity must be provided for "
                                    "checkout.",
                                    SVN_DAV_ERROR_NAMESPACE,
                                    SVN_DAV_ERROR_TAG);
    }
  /* assert: nelts > 0.  the below check effectively means > 1. */
  if (activities->nelts != 1)
    {
      return dav_svn__new_error_tag(resource->pool, HTTP_CONFLICT,
                                    SVN_ERR_INCORRECT_PARAMS,
                                    "Only one activity may be specified within "
                                    "the CHECKOUT.",
                                    SVN_DAV_ERROR_NAMESPACE,
                                    SVN_DAV_ERROR_TAG);
    }

  serr = dav_svn_simple_parse_uri(&parse, resource,
                                  APR_ARRAY_IDX(activities, 0, const char *),
                                  resource->pool);
  if (serr != NULL)
    {
      /* ### is BAD_REQUEST proper? */
      return dav_svn_convert_err(serr, HTTP_CONFLICT,
                                 "The activity href could not be parsed "
                                 "properly.",
                                 resource->pool);
    }
  if (parse.activity_id == NULL)
    {
      return dav_svn__new_error_tag(resource->pool, HTTP_CONFLICT,
                                    SVN_ERR_INCORRECT_PARAMS,
                                    "The provided href is not an activity URI.",
                                    SVN_DAV_ERROR_NAMESPACE,
                                    SVN_DAV_ERROR_TAG);
    }

  if ((txn_name = dav_svn_get_txn(resource->info->repos,
                                  parse.activity_id)) == NULL)
    {
      return dav_svn__new_error_tag(resource->pool, HTTP_CONFLICT,
                                    SVN_ERR_APMOD_ACTIVITY_NOT_FOUND,
                                    "The specified activity does not exist.",
                                    SVN_DAV_ERROR_NAMESPACE,
                                    SVN_DAV_ERROR_TAG);
    }

  /* verify the specified version resource is the "latest", thus allowing
     changes to be made. */
  if (resource->baselined || resource->info->root.rev == SVN_INVALID_REVNUM)
    {
      /* a Baseline, or a standard Version Resource which was accessed
         via a Label against a VCR within a Baseline Collection. */
      /* ### at the moment, this branch is only reached for baselines */

      svn_revnum_t youngest;

      /* make sure the baseline being checked out is the latest */
      serr = svn_fs_youngest_rev(&youngest, resource->info->repos->fs,
                                 resource->pool);
      if (serr != NULL)
        {
          /* ### correct HTTP error? */
          return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                     "Could not determine the youngest "
                                     "revision for verification against "
                                     "the baseline being checked out.",
                                     resource->pool);
        }

      if (resource->info->root.rev != youngest)
        {
          return dav_svn__new_error_tag(resource->pool, HTTP_CONFLICT,
                                        SVN_ERR_APMOD_BAD_BASELINE,
                                        "The specified baseline is not the "
                                        "latest baseline, so it may not be "
                                        "checked out.",
                                        SVN_DAV_ERROR_NAMESPACE,
                                        SVN_DAV_ERROR_TAG);
        }

      /* ### hmm. what if the transaction root's revision is different
         ### from this baseline? i.e. somebody created a new revision while
         ### we are processing this commit.
         ###
         ### first question: what does the client *do* with a working
         ### baseline? knowing that, and how it maps to our backend, then
         ### we can figure out what to do here. */
    }
  else
    {
      /* standard Version Resource */

      svn_fs_txn_t *txn;
      svn_fs_root_t *txn_root;
      svn_revnum_t txn_created_rev;
      dav_error *err;

      /* open the specified transaction so that we can verify this version
         resource corresponds to the current/latest in the transaction. */
      if ((err = open_txn(&txn, resource->info->repos->fs, txn_name,
                          resource->pool)) != NULL)
        return err;

      serr = svn_fs_txn_root(&txn_root, txn, resource->pool);
      if (serr != NULL)
        {
          /* ### correct HTTP error? */
          return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                     "Could not open the transaction tree.",
                                     resource->pool);
        }

      /* assert: repos_path != NULL (for this type of resource) */


      /* Out-of-dateness check:  compare the created-rev of the item
         in the txn against the created-rev of the version resource
         being changed. */
      serr = svn_fs_node_created_rev(&txn_created_rev,
                                     txn_root, resource->info->repos_path,
                                     resource->pool);
      if (serr != NULL)
        {
          /* ### correct HTTP error? */
          return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                     "Could not get created-rev of "
                                     "transaction node.",
                                     resource->pool);
        }

      /* If txn_created_rev is invalid, that means it's already
         mutable in the txn... which means it has already passed this
         out-of-dateness check.  (Usually, this happens when looking
         at a parent directory of an already checked-out
         resource.)  

         Now, we come down to it.  If the created revision of the node
         in the transaction is different from the revision parsed from
         the version resource URL, we're in a bit of a quandry, and
         one of a few things could be true.

         - The client is trying to modify an old (out of date)
           revision of the resource.  This is, of course,
           unacceptable!

         - The client is trying to modify a *newer* revision.  If the
           version resource is *newer* than the transaction root, then
           the client started a commit, a new revision was created
           within the repository, the client fetched the new resource
           from that new revision, changed it (or merged in a prior
           change), and then attempted to incorporate that into the
           commit that was initially started.  We could copy that new
           node into our transaction and then modify it, but why
           bother?  We can stop the commit, and everything will be
           fine again if the user simply restarts it (because we'll
           use that new revision as the transaction root, thus
           incorporating the new resource, which they will then
           modify).
             
         - The path/revision that client is wishing to edit and the
           path/revision in the current transaction are actually the
           same node, and thus this created-rev comparison didn't
           really solidify anything after all. :-)
      */

      if (SVN_IS_VALID_REVNUM( txn_created_rev ))
        {
          int errorful = 0;

          if (resource->info->root.rev < txn_created_rev)
            {
              /* The item being modified is older than the one in the
                 transaction.  The client is out of date.  */
              errorful = 1;
            }
          else if (resource->info->root.rev > txn_created_rev)
            {
              /* The item being modified is being accessed via a newer
                 revision than the one in the transaction.  We'll
                 check to see if they are still the same node, and if
                 not, return an error. */
              const svn_fs_id_t *url_noderev_id, *txn_noderev_id;

              if ((serr = svn_fs_node_id(&txn_noderev_id, txn_root, 
                                         resource->info->repos_path,
                                         resource->pool)))
                {
                  err = dav_svn__new_error_tag
                    (resource->pool, HTTP_CONFLICT, serr->apr_err,
                     "Unable to fetch the node revision id of the version "
                     "resource within the transaction.",
                     SVN_DAV_ERROR_NAMESPACE,
                     SVN_DAV_ERROR_TAG);
                  svn_error_clear(serr);
                  return err;
                }
              if ((serr = svn_fs_node_id(&url_noderev_id,
                                         resource->info->root.root,
                                         resource->info->repos_path,
                                         resource->pool)))
                {
                  err = dav_svn__new_error_tag
                    (resource->pool, HTTP_CONFLICT, serr->apr_err,
                     "Unable to fetch the node revision id of the version "
                     "resource within the revision.",
                     SVN_DAV_ERROR_NAMESPACE,
                     SVN_DAV_ERROR_TAG);
                  svn_error_clear(serr);
                  return err;
                }
              if (svn_fs_compare_ids(url_noderev_id, txn_noderev_id) != 0)
                {
                  errorful = 1;
                }
            }
          if (errorful)
            {
#if 1
              return dav_svn__new_error_tag
                (resource->pool, HTTP_CONFLICT, SVN_ERR_FS_CONFLICT,
                 "The version resource does not correspond to the resource "
                 "within the transaction.  Either the requested version "
                 "resource is out of date (needs to be updated), or the "
                 "requested version resource is newer than the transaction "
                 "root (restart the commit).",
                 SVN_DAV_ERROR_NAMESPACE,
                 SVN_DAV_ERROR_TAG);

#else
              /* ### some debugging code */
              const char *msg;
              
              msg = apr_psprintf(resource->pool, 
                                 "created-rev mismatch: r=%ld, t=%ld",
                                 resource->info->root.rev, txn_created_rev);
              
              return dav_svn__new_error_tag(resource->pool, HTTP_CONFLICT, 
                                            SVN_ERR_FS_CONFLICT, msg,
                                            SVN_DAV_ERROR_NAMESPACE,
                                            SVN_DAV_ERROR_TAG);
#endif
            }
        }
    }
  *working_resource = dav_svn_create_working_resource(resource,
                                                      parse.activity_id,
                                                      txn_name,
                                                      FALSE);
  return NULL;
}

static dav_error *dav_svn_uncheckout(dav_resource *resource)
{
  if (resource->type != DAV_RESOURCE_TYPE_WORKING)
    return dav_svn__new_error_tag(resource->pool, HTTP_INTERNAL_SERVER_ERROR,
                                  SVN_ERR_UNSUPPORTED_FEATURE,
                                  "UNCHECKOUT called on non-working resource.",
                                  SVN_DAV_ERROR_NAMESPACE,
                                  SVN_DAV_ERROR_TAG);

  /* Try to abort the txn if it exists;  but don't try too hard.  :-)  */
  if (resource->info->root.txn)
    svn_error_clear(svn_fs_abort_txn(resource->info->root.txn,
                                     resource->pool));

  /* Attempt to destroy the shared activity. */
  if (resource->info->root.activity_id)
    {
      dav_svn_delete_activity(resource->info->repos,
                              resource->info->root.activity_id);
      apr_pool_userdata_set(NULL, DAV_SVN_AUTOVERSIONING_ACTIVITY,
                            NULL, resource->info->r->pool);
    }

  resource->info->root.txn_name = NULL;
  resource->info->root.txn = NULL;

  /* We're no longer checked out. */
  resource->info->auto_checked_out = FALSE;

  /* Convert the working resource back into a regular one, in-place. */
  return dav_svn_working_to_regular_resource(resource);
}


/* Closure object for cleanup_deltify. */
struct cleanup_deltify_baton
{
  /* The repository in which to deltify.  We use a path instead of an
     object, because it's difficult to obtain a repos or fs object
     with the right lifetime guarantees. */
  const char *repos_path;

  /* The revision number against which to deltify. */
  svn_revnum_t revision;

  /* The pool to use for all temporary allocation while working.  This
     may or may not be the same as the pool on which the cleanup is
     registered, but obviously it must have a lifetime at least as
     long as that pool. */
  apr_pool_t *pool;
};


/* APR pool cleanup function to deltify against a just-committed
   revision.  DATA is a 'struct cleanup_deltify_baton *'.

   If any errors occur, log them in the httpd server error log, but
   return APR_SUCCESS no matter what, as this is a pool cleanup
   function and deltification is not a matter of correctness
   anyway. */
static apr_status_t cleanup_deltify(void *data)
{
  struct cleanup_deltify_baton *cdb = data;
  svn_repos_t *repos;
  svn_error_t *err;

  /* It's okay to allocate in the pool that's being cleaned up, and
     it's also okay to register new cleanups against that pool.  But
     if you create subpools of it, you must make sure to destroy them
     at the end of the cleanup.  So we do all our work in this
     subpool, then destroy it before exiting. */
  apr_pool_t *subpool = svn_pool_create(cdb->pool);

  err = svn_repos_open(&repos, cdb->repos_path, subpool);
  if (err)
    {
      ap_log_perror(APLOG_MARK, APLOG_ERR, err->apr_err, cdb->pool,
                    "cleanup_deltify: error opening repository '%s'",
                    cdb->repos_path);
      svn_error_clear(err);
      goto cleanup;
    }

  err = svn_fs_deltify_revision(svn_repos_fs(repos),
                                cdb->revision, subpool);
  if (err)
    {
      ap_log_perror(APLOG_MARK, APLOG_ERR, err->apr_err, cdb->pool,
                    "cleanup_deltify: error deltifying against revision %ld"
                    " in repository '%s'",
                    cdb->revision, cdb->repos_path);
      svn_error_clear(err);
    }

 cleanup:
  svn_pool_destroy(subpool);

  return APR_SUCCESS;
}


/* Register the cleanup_deltify function on POOL, which should be the
   connection pool for the request.  This way the time needed for
   deltification won't delay the response to the client.

   REPOS is the repository in which deltify, and REVISION is the
   revision against which to deltify.  POOL is both the pool on which
   to register the cleanup function and the pool that will be used for
   temporary allocations while deltifying. */
static void register_deltification_cleanup(svn_repos_t *repos,
                                           svn_revnum_t revision,
                                           apr_pool_t *pool)
{
  struct cleanup_deltify_baton *cdb = apr_palloc(pool, sizeof(*cdb));
  
  cdb->repos_path = svn_repos_path(repos, pool);
  cdb->revision = revision;
  cdb->pool = pool;
  
  apr_pool_cleanup_register(pool, cdb, cleanup_deltify, apr_pool_cleanup_null);
}


dav_error *dav_svn_checkin(dav_resource *resource,
                           int keep_checked_out,
                           dav_resource **version_resource)
{
  svn_error_t *serr;
  dav_error *err;
  apr_status_t apr_err;
  const char *uri;
  const char *shared_activity;
  void *data;

  /* ### mod_dav has a flawed architecture, in the sense that it first
     tries to auto-checkin the modified resource, then attempts to
     auto-checkin the parent resource (if the parent resource was
     auto-checked-out).  Instead, the provider should be in charge:
     mod_dav should provide a *set* of resources that need
     auto-checkin, and the provider can decide how to do it.  (One
     txn?  Many txns?  Etc.) */

  if (resource->type != DAV_RESOURCE_TYPE_WORKING)
    return dav_svn__new_error_tag(resource->pool, HTTP_INTERNAL_SERVER_ERROR,
                                  SVN_ERR_UNSUPPORTED_FEATURE,
                                  "CHECKIN called on non-working resource.",
                                  SVN_DAV_ERROR_NAMESPACE,
                                  SVN_DAV_ERROR_TAG);

  /* If the global autoversioning activity still exists, that means
     nobody's committed it yet. */
  apr_err = apr_pool_userdata_get(&data,
                                  DAV_SVN_AUTOVERSIONING_ACTIVITY,
                                  resource->info->r->pool);
  if (apr_err)
    return dav_svn_convert_err(svn_error_create(apr_err, 0, NULL),
                               HTTP_INTERNAL_SERVER_ERROR,
                               "Error fetching pool userdata.",
                               resource->pool);
  shared_activity = data;

  /* Try to commit the txn if it exists. */
  if (shared_activity
      && (strcmp(shared_activity, resource->info->root.activity_id) == 0))
    {
      const char *shared_txn_name;
      const char *conflict_msg;
      svn_revnum_t new_rev;

      shared_txn_name = dav_svn_get_txn(resource->info->repos,
                                        shared_activity);
      if (! shared_txn_name)
        return dav_new_error(resource->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "Cannot look up a txn_name by activity");

      /* Sanity checks */
      if (resource->info->root.txn_name
          && (strcmp(shared_txn_name, resource->info->root.txn_name) != 0))
        return dav_new_error(resource->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "Internal txn_name doesn't match"
                             " autoversioning transaction.");
     
      if (! resource->info->root.txn)
        /* should already be open by dav_svn_checkout */
        return dav_new_error(resource->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "Autoversioning txn isn't open "
                             "when it should be.");
      
      err = set_auto_revprops(resource);
      if (err)
        return err;
      
      serr = svn_repos_fs_commit_txn(&conflict_msg,
                                     resource->info->repos->repos,
                                     &new_rev, 
                                     resource->info->root.txn,
                                     resource->pool);

      if (serr != NULL)
        {
          const char *msg;
          svn_error_clear(svn_fs_abort_txn(resource->info->root.txn,
                                           resource->pool));
          
          if (serr->apr_err == SVN_ERR_FS_CONFLICT)
            {
              msg = apr_psprintf(resource->pool,
                                 "A conflict occurred during the CHECKIN "
                                 "processing. The problem occurred with  "
                                 "the \"%s\" resource.",
                                 conflict_msg);
            }
          else
            msg = "An error occurred while committing the transaction.";

          /* Attempt to destroy the shared activity. */
          dav_svn_delete_activity(resource->info->repos, shared_activity);
          apr_pool_userdata_set(NULL, DAV_SVN_AUTOVERSIONING_ACTIVITY,
                                NULL, resource->info->r->pool);
          
          return dav_svn_convert_err(serr, HTTP_CONFLICT, msg,
                                     resource->pool);
        }

      /* Attempt to destroy the shared activity. */
      dav_svn_delete_activity(resource->info->repos, shared_activity);
      apr_pool_userdata_set(NULL, DAV_SVN_AUTOVERSIONING_ACTIVITY,
                            NULL, resource->info->r->pool);
            
      /* Commit was successful, so schedule deltification. */
      register_deltification_cleanup(resource->info->repos->repos,
                                     new_rev,
                                     resource->info->r->connection->pool);
      
      /* If caller wants it, return the new VR that was created by
         the checkin. */
      if (version_resource)
        {
          uri = dav_svn_build_uri(resource->info->repos,
                                  DAV_SVN_BUILD_URI_VERSION,
                                  new_rev, resource->info->repos_path,
                                  0, resource->pool);
          
          err = dav_svn_create_version_resource(version_resource, uri,
                                                resource->pool);
          if (err)
            return err;
        }
    } /* end of commit stuff */
  
  /* The shared activity was either nonexistent to begin with, or it's
     been committed and is only now nonexistent.  The resource needs
     to forget about it. */
  resource->info->root.txn_name = NULL;
  resource->info->root.txn = NULL;
 
  /* Convert the working resource back into an regular one. */
  if (! keep_checked_out)
    {
      resource->info->auto_checked_out = FALSE;
      return dav_svn_working_to_regular_resource(resource);
    } 

  return NULL;
}

static dav_error *dav_svn_avail_reports(const dav_resource *resource,
                                        const dav_report_elem **reports)
{
  /* ### further restrict to the public space? */
  if (resource->type != DAV_RESOURCE_TYPE_REGULAR) {
    *reports = NULL;
    return NULL;
  }

  *reports = avail_reports;
  return NULL;
}

static int dav_svn_report_label_header_allowed(const apr_xml_doc *doc)
{
  return 0;
}

/* Respond to a S:dated-rev-report request.  The request contains a
 * DAV:creationdate element giving the requested date; the response
 * contains a DAV:version-name element giving the most recent revision
 * as of that date. */
static dav_error * dav_svn__drev_report(const dav_resource *resource,
                                        const apr_xml_doc *doc,
                                        ap_filter_t *output)
{
  apr_xml_elem *child;
  int ns;
  apr_time_t tm = (apr_time_t) -1;
  svn_revnum_t rev;
  apr_bucket_brigade *bb;
  svn_error_t *err;
  apr_status_t apr_err;
  dav_error *derr = NULL;

  /* Find the DAV:creationdate element and get the requested time from it. */
  ns = dav_svn_find_ns(doc->namespaces, "DAV:");
  if (ns != -1)
    {
      for (child = doc->root->first_child; child != NULL; child = child->next)
        {
          if (child->ns != ns || strcmp(child->name, "creationdate") != 0)
            continue;
          /* If this fails, we'll notice below, so ignore any error for now. */
          svn_error_clear
            (svn_time_from_cstring(&tm, dav_xml_get_cdata(child,
                                                          resource->pool, 1),
                                   resource->pool));
        }
    }

  if (tm == (apr_time_t) -1)
    {
      return dav_new_error(resource->pool, HTTP_BAD_REQUEST, 0,
                           "The request does not contain a valid "
                           "'DAV:creationdate' element.");
    }

  /* Do the actual work of finding the revision by date. */
  if ((err = svn_repos_dated_revision(&rev, resource->info->repos->repos, tm,
                                      resource->pool)) != SVN_NO_ERROR)
    {
      svn_error_clear(err);
      return dav_new_error(resource->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                           "Could not access revision times.");
    }

  bb = apr_brigade_create(resource->pool, output->c->bucket_alloc);
  apr_err = ap_fprintf(output, bb,
                       DAV_XML_HEADER DEBUG_CR
                       "<S:dated-rev-report xmlns:S=\"" SVN_XML_NAMESPACE "\" "
                       "xmlns:D=\"DAV:\">" DEBUG_CR
                       "<D:version-name>%ld</D:version-name>"
                       "</S:dated-rev-report>", rev);
  if (apr_err)
    derr = dav_svn_convert_err(svn_error_create(apr_err, 0, NULL),
                               HTTP_INTERNAL_SERVER_ERROR,
                               "Error writing REPORT response.",
                               resource->pool);

  /* Flush the contents of the brigade (returning an error only if we
     don't already have one). */
  if (((apr_err = ap_fflush(output, bb))) && (! derr))
    derr = dav_svn_convert_err(svn_error_create(apr_err, 0, NULL),
                               HTTP_INTERNAL_SERVER_ERROR,
                               "Error flushing brigade.",
                               resource->pool);

  return derr;
}


/* Respond to a get-locks-report request.  See description of this
   report in libsvn_ra_dav/fetch.c.  */
static dav_error * dav_svn__get_locks_report(const dav_resource *resource,
                                             const apr_xml_doc *doc,
                                             ap_filter_t *output)
{
  apr_bucket_brigade *bb;
  svn_error_t *err;
  apr_status_t apr_err;
  apr_hash_t *locks;
  dav_svn_authz_read_baton arb;
  apr_hash_index_t *hi;
  apr_pool_t *subpool;

  /* The request URI should be a public one representing an fs path. */
  if ((! resource->info->repos_path)
      || (! resource->info->repos->repos))
    return dav_new_error(resource->pool, HTTP_BAD_REQUEST, 0,
                         "get-locks-report run on resource which doesn't "
                         "represent a path within a repository.");

  arb.r = resource->info->r;
  arb.repos = resource->info->repos;

  /* Fetch the locks, but allow authz_read checks to happen on each. */
  if ((err = svn_repos_fs_get_locks(&locks,
                                    resource->info->repos->repos,
                                    resource->info->repos_path,
                                    dav_svn_authz_read_func(&arb), &arb,
                                    resource->pool)) != SVN_NO_ERROR)
    return dav_svn_convert_err(err, HTTP_INTERNAL_SERVER_ERROR,
                               err->message, resource->pool);      

  bb = apr_brigade_create(resource->pool, output->c->bucket_alloc);

  /* start sending report */
  apr_err = ap_fprintf(output, bb,
                       DAV_XML_HEADER DEBUG_CR
                       "<S:get-locks-report xmlns:S=\"" SVN_XML_NAMESPACE "\" "
                       "xmlns:D=\"DAV:\">" DEBUG_CR);
  if (apr_err)
    return dav_svn_convert_err(svn_error_create(apr_err, 0, NULL),
                               HTTP_INTERNAL_SERVER_ERROR,
                               "Error writing REPORT response.",
                               resource->pool);

  /* stream the locks */
  subpool = svn_pool_create(resource->pool);
  for (hi = apr_hash_first(resource->pool, locks); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const svn_lock_t *lock;
      const char *path_quoted, *token_quoted;
      const char *creation_str, *expiration_str;
      const char *owner_to_send, *comment_to_send;
      svn_boolean_t owner_base64 = FALSE, comment_base64 = FALSE;

      svn_pool_clear(subpool);
      apr_hash_this(hi, &key, NULL, &val);
      lock = val;
      
      path_quoted = apr_xml_quote_string(subpool, lock->path, 1);
      token_quoted = apr_xml_quote_string(subpool, lock->token, 1);
      creation_str = svn_time_to_cstring(lock->creation_date, subpool);

      apr_err = ap_fprintf(output, bb,
                           "<S:lock>" DEBUG_CR
                           "<S:path>%s</S:path>" DEBUG_CR
                           "<S:token>%s</S:token>" DEBUG_CR
                           "<S:creationdate>%s</S:creationdate>" DEBUG_CR,
                           path_quoted, token_quoted, creation_str);
      if (apr_err)
        return dav_svn_convert_err(svn_error_create(apr_err, 0, NULL),
                                   HTTP_INTERNAL_SERVER_ERROR,
                                   "Error writing REPORT response.",
                                   resource->pool);
      
      if (lock->expiration_date)
        {
          expiration_str = svn_time_to_cstring(lock->expiration_date, subpool);
          apr_err = ap_fprintf(output, bb,
                               "<S:expirationdate>%s</S:expirationdate>"
                               DEBUG_CR, expiration_str);
          if (apr_err)
            return dav_svn_convert_err(svn_error_create(apr_err, 0, NULL),
                                       HTTP_INTERNAL_SERVER_ERROR,
                                       "Error writing REPORT response.",
                                       resource->pool);
        }

      if (svn_xml_is_xml_safe(lock->owner, strlen(lock->owner)))
        {
          owner_to_send = apr_xml_quote_string(subpool, lock->owner, 1);
        }
      else
        {
          svn_string_t owner_string;
          const svn_string_t *encoded_owner;

          owner_string.data = lock->owner;
          owner_string.len = strlen(lock->owner);         
          encoded_owner = svn_base64_encode_string(&owner_string, subpool);
          owner_to_send = encoded_owner->data;
          owner_base64 = TRUE;
        }
          
      apr_err = ap_fprintf(output, bb,
                           "<S:owner %s>%s</S:owner>" DEBUG_CR,
                           owner_base64 ? "encoding=\"base64\"" : "",
                           owner_to_send);
      if (apr_err)
        return dav_svn_convert_err(svn_error_create(apr_err, 0, NULL),
                                   HTTP_INTERNAL_SERVER_ERROR,
                                   "Error writing REPORT response.",
                                   resource->pool);          

      if (lock->comment)
        {
          if (svn_xml_is_xml_safe(lock->comment, strlen(lock->comment)))
            {
              comment_to_send = apr_xml_quote_string(subpool,
                                                     lock->comment, 1);
            }
          else
            {
              svn_string_t comment_string;
              const svn_string_t *encoded_comment;
              
              comment_string.data = lock->comment;
              comment_string.len = strlen(lock->comment);         
              encoded_comment = svn_base64_encode_string(&comment_string,
                                                         subpool);
              comment_to_send = encoded_comment->data;
              comment_base64 = TRUE;
            }

          apr_err = ap_fprintf(output, bb,
                               "<S:comment %s>%s</S:comment>" DEBUG_CR,
                               comment_base64 ? "encoding=\"base64\"" : "",
                               comment_to_send);
          if (apr_err)
            return dav_svn_convert_err(svn_error_create(apr_err, 0, NULL),
                                       HTTP_INTERNAL_SERVER_ERROR,
                                       "Error writing REPORT response.",
                                       resource->pool);
        }
          
      apr_err = ap_fprintf(output, bb, "</S:lock>" DEBUG_CR);
      if (apr_err)
        return dav_svn_convert_err(svn_error_create(apr_err, 0, NULL),
                                   HTTP_INTERNAL_SERVER_ERROR,
                                   "Error writing REPORT response.",
                                   resource->pool);
    } /* end of hash loop */
  svn_pool_destroy(subpool);

  /* finish the report */
  apr_err = ap_fprintf(output, bb, "</S:get-locks-report>" DEBUG_CR);
  if (apr_err)
    return dav_svn_convert_err(svn_error_create(apr_err, 0, NULL),
                               HTTP_INTERNAL_SERVER_ERROR,
                               "Error writing REPORT response.",
                               resource->pool);

  /* Flush the contents of the brigade (returning an error only if we
     don't already have one). */
  if ((apr_err = ap_fflush(output, bb)))
    return dav_svn_convert_err(svn_error_create(apr_err, 0, NULL),
                               HTTP_INTERNAL_SERVER_ERROR,
                               "Error flushing brigade.",
                               resource->pool);

  return NULL;
}



static apr_status_t send_get_locations_report(ap_filter_t *output,
                                              apr_bucket_brigade *bb,
                                              const dav_resource *resource,
                                              apr_hash_t *fs_locations)
{
  apr_hash_index_t *hi;
  apr_pool_t *pool;
  apr_status_t apr_err;

  pool = resource->pool;

  apr_err = ap_fprintf(output, bb, DAV_XML_HEADER DEBUG_CR
                       "<S:get-locations-report xmlns:S=\"" SVN_XML_NAMESPACE
                       "\" xmlns:D=\"DAV:\">" DEBUG_CR);
  if (apr_err)
    return apr_err;

  for (hi = apr_hash_first(pool, fs_locations); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *value;
      const char *path_quoted;

      apr_hash_this(hi, &key, NULL, &value);
      path_quoted = apr_xml_quote_string(pool, value, 1);
      apr_err = ap_fprintf(output, bb, "<S:location "
                           "rev=\"%ld\" path=\"%s\"/>" DEBUG_CR,
                           *(const svn_revnum_t *)key, path_quoted);
      if (apr_err)
        return apr_err;
    }
  return ap_fprintf(output, bb, "</S:get-locations-report>" DEBUG_CR);
}

dav_error *dav_svn__get_locations_report(const dav_resource *resource,
                                         const apr_xml_doc *doc,
                                         ap_filter_t *output)
{
  svn_error_t *serr;
  dav_error *derr = NULL;
  apr_status_t apr_err;
  apr_bucket_brigade *bb;
  dav_svn_authz_read_baton arb;

  /* The parameters to do the operation on. */
  const char *relative_path = NULL;
  const char *abs_path;
  svn_revnum_t peg_revision = SVN_INVALID_REVNUM;
  apr_array_header_t *location_revisions;

  /* XML Parsing Variables */
  int ns;
  apr_xml_elem *child;

  apr_hash_t *fs_locations;

  location_revisions = apr_array_make(resource->pool, 0,
                                      sizeof(svn_revnum_t));

  /* Sanity check. */
  ns = dav_svn_find_ns(doc->namespaces, SVN_XML_NAMESPACE);
  if (ns == -1)
    {
      return dav_svn__new_error_tag(resource->pool, HTTP_BAD_REQUEST, 0,
                                    "The request does not contain the 'svn:' "
                                    "namespace, so it is not going to have "
                                    "certain required elements.",
                                    SVN_DAV_ERROR_NAMESPACE,
                                    SVN_DAV_ERROR_TAG);
    }

  /* Gather the parameters. */
  for (child = doc->root->first_child; child != NULL; child = child->next)
    {
      /* If this element isn't one of ours, then skip it. */
      if (child->ns != ns)
        continue;

      if (strcmp(child->name, "peg-revision") == 0)
        peg_revision = SVN_STR_TO_REV(dav_xml_get_cdata(child,
                                                        resource->pool, 1));
      else if (strcmp(child->name, "location-revision") == 0)
        {
          svn_revnum_t revision
            = SVN_STR_TO_REV(dav_xml_get_cdata(child, resource->pool, 1));
          APR_ARRAY_PUSH(location_revisions, svn_revnum_t) = revision;
        }
      else if (strcmp(child->name, "path") == 0)
        {
          relative_path = dav_xml_get_cdata(child, resource->pool, 0);
          if ((derr = dav_svn__test_canonical(relative_path, resource->pool)))
            return derr;
        }
    }

  /* Now we should have the parameters ready - let's
     check if they are all present. */
  if (! (relative_path && SVN_IS_VALID_REVNUM(peg_revision)))
    {
      return dav_svn__new_error_tag(resource->pool, HTTP_BAD_REQUEST, 0,
                                    "Not all parameters passed.",
                                    SVN_DAV_ERROR_NAMESPACE,
                                    SVN_DAV_ERROR_TAG);       
    }

  /* Append the relative path to the base FS path to get an absolute
     repository path. */
  abs_path = svn_path_join(resource->info->repos_path, relative_path,
                           resource->pool);

  /* Build an authz read baton */
  arb.r = resource->info->r;
  arb.repos = resource->info->repos;

  serr = svn_repos_trace_node_locations(resource->info->repos->fs,
                                        &fs_locations, abs_path, peg_revision,
                                        location_revisions,
                                        dav_svn_authz_read_func(&arb), &arb,
                                        resource->pool);

  if (serr)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 serr->message, resource->pool);
    }

  bb = apr_brigade_create(resource->pool, output->c->bucket_alloc);

  apr_err = send_get_locations_report(output, bb, resource, fs_locations);

  if (apr_err)
    derr = dav_svn_convert_err(svn_error_create(apr_err, 0, NULL),
                               HTTP_INTERNAL_SERVER_ERROR,
                               "Error writing REPORT response.",
                               resource->pool);

  /* Flush the contents of the brigade (returning an error only if we
     don't already have one). */
  if (((apr_err = ap_fflush(output, bb))) && (! derr))
    return dav_svn_convert_err(svn_error_create(apr_err, 0, NULL),
                               HTTP_INTERNAL_SERVER_ERROR,
                               "Error flushing brigade.",
                               resource->pool);

  return derr;
}

static dav_error *dav_svn_deliver_report(request_rec *r,
                                         const dav_resource *resource,
                                         const apr_xml_doc *doc,
                                         ap_filter_t *output)
{
  int ns = dav_svn_find_ns(doc->namespaces, SVN_XML_NAMESPACE);

  if (doc->root->ns == ns)
    {
      /* ### note that these report names should have symbols... */

      if (strcmp(doc->root->name, "update-report") == 0)
        {
          return dav_svn__update_report(resource, doc, output);
        }
      else if (strcmp(doc->root->name, "log-report") == 0)
        {
          return dav_svn__log_report(resource, doc, output);
        }
      else if (strcmp(doc->root->name, "dated-rev-report") == 0)
        {
          return dav_svn__drev_report(resource, doc, output);
        }
      else if (strcmp(doc->root->name, "get-locations") == 0)
        {
          return dav_svn__get_locations_report(resource, doc, output);
        }
      else if (strcmp(doc->root->name, "file-revs-report") == 0)
        {
          return dav_svn__file_revs_report(resource, doc, output);
        }
      else if (strcmp(doc->root->name, "get-locks-report") == 0)
        {
          return dav_svn__get_locks_report(resource, doc, output);
        }
      else if (strcmp(doc->root->name, "replay-report") == 0)
        {
          return dav_svn__replay_report(resource, doc, output);
        }

      /* NOTE: if you add a report, don't forget to add it to the
       *       avail_reports[] array at the top of this file.
       */
    }

  /* ### what is a good error for an unknown report? */
  return dav_svn__new_error_tag(resource->pool, HTTP_NOT_IMPLEMENTED,
                                SVN_ERR_UNSUPPORTED_FEATURE,
                                "The requested report is unknown.",
                                SVN_DAV_ERROR_NAMESPACE,
                                SVN_DAV_ERROR_TAG);
}

static int dav_svn_can_be_activity(const dav_resource *resource)
{
  /* If our resource is marked as auto_checked_out'd, then we allow this to
   * be an activity URL.  Otherwise, it must be a real activity URL that
   * doesn't already exist.
   */
  return (resource->info->auto_checked_out == TRUE ||
          (resource->type == DAV_RESOURCE_TYPE_ACTIVITY &&
           !resource->exists));
}

static dav_error *dav_svn_make_activity(dav_resource *resource)
{
  const char *activity_id = resource->info->root.activity_id;
  const char *txn_name;
  dav_error *err;

  /* sanity check:  make sure the resource is a valid activity, in
     case an older mod_dav doesn't do the check for us. */
  if (! dav_svn_can_be_activity(resource))
    return dav_svn__new_error_tag(resource->pool, HTTP_FORBIDDEN,
                                  SVN_ERR_APMOD_MALFORMED_URI,
                                  "Activities cannot be created at that "
                                  "location; query the "
                                  "DAV:activity-collection-set property.",
                                  SVN_DAV_ERROR_NAMESPACE,
                                  SVN_DAV_ERROR_TAG);
   
  err = dav_svn_create_activity(resource->info->repos, &txn_name,
                                resource->pool);
  if (err != NULL)
    return err;

  err = dav_svn_store_activity(resource->info->repos, activity_id, txn_name);
  if (err != NULL)
    return err;

  /* everything is happy. update the resource */
  resource->info->root.txn_name = txn_name;
  resource->exists = 1;
  return NULL;
}



dav_error *dav_svn__build_lock_hash(apr_hash_t **locks,
                                    request_rec *r,
                                    const char *path_prefix,
                                    apr_pool_t *pool)
{
  apr_status_t apr_err;
  dav_error *derr;
  void *data = NULL;
  apr_xml_doc *doc = NULL;
  apr_xml_elem *child, *lockchild;
  int ns;
  apr_hash_t *hash = apr_hash_make(pool);
  
  /* Grab the request body out of r->pool, as it contains all of the
     lock tokens.  It should have been stashed already by our custom
     input filter. */
  apr_err = apr_pool_userdata_get(&data, "svn-request-body", r->pool);
  if (apr_err)
    return dav_svn_convert_err(svn_error_create(apr_err, 0, NULL),
                               HTTP_INTERNAL_SERVER_ERROR,
                               "Error fetching pool userdata.",
                               pool);
  doc = data;
  if (! doc)
    {
      *locks = hash;
      return SVN_NO_ERROR;
    }
  
  /* Sanity check. */
  ns = dav_svn_find_ns(doc->namespaces, SVN_XML_NAMESPACE);
  if (ns == -1)
    {
      /* If there's no svn: namespace in the body, then there are
         definitely no lock-tokens to harvest.  This is likely a
         request from an old client. */
      *locks = hash;
      return SVN_NO_ERROR;
    }

  if ((doc->root->ns == ns) 
      && (strcmp(doc->root->name, "lock-token-list") == 0))
    {
      child = doc->root;
    }
  else
    {
      /* Search doc's children until we find the <lock-token-list>. */
      for (child = doc->root->first_child; child != NULL; child = child->next)
        {
          /* if this element isn't one of ours, then skip it */
          if (child->ns != ns)
            continue;
          
          if (strcmp(child->name, "lock-token-list") == 0)
            break;
        }
    }

  /* Did we find what we were looking for? */
  if (! child)
    {
      *locks = hash;
      return SVN_NO_ERROR;
    }

  /* Then look for N different <lock> structures within. */
  for (lockchild = child->first_child; lockchild != NULL;
       lockchild = lockchild->next)
    {
      const char *lockpath = NULL, *locktoken = NULL;
      apr_xml_elem *lfchild;

      if (strcmp(lockchild->name, "lock") != 0)
        continue;

      for (lfchild = lockchild->first_child; lfchild != NULL;
           lfchild = lfchild->next)
        {
          if (strcmp(lfchild->name, "lock-path") == 0)
            {
              const char *cdata = dav_xml_get_cdata(lfchild, pool, 0);
              if ((derr = dav_svn__test_canonical(cdata, pool)))
                return derr;
                  
              /* Create an absolute fs-path */
              lockpath = svn_path_join(path_prefix, cdata, pool);
              if (lockpath && locktoken)
                {
                  apr_hash_set(hash, lockpath, APR_HASH_KEY_STRING, locktoken);
                  lockpath = NULL;
                  locktoken = NULL;
                }
            }
          else if (strcmp(lfchild->name, "lock-token") == 0)
            {
              locktoken = dav_xml_get_cdata(lfchild, pool, 1);
              if (lockpath && *locktoken)
                {
                  apr_hash_set(hash, lockpath, APR_HASH_KEY_STRING, locktoken);
                  lockpath = NULL;
                  locktoken = NULL;
                }
            }
        }
    }
  
  *locks = hash;
  return SVN_NO_ERROR;
}



dav_error *dav_svn__push_locks(dav_resource *resource,
                               apr_hash_t *locks,
                               apr_pool_t *pool)
{
  svn_fs_access_t *fsaccess;
  apr_hash_index_t *hi;
  svn_error_t *serr;
  
  serr = svn_fs_get_access(&fsaccess, resource->info->repos->fs);
  if (serr)
    {
      /* If an authenticated user name was attached to the request,
         then dav_svn_get_resource() should have already noticed and
         created an fs_access_t in the filesystem.  */
      return dav_svn__sanitize_error(serr, "Lock token(s) in request, but "
                                     "missing an user name", HTTP_BAD_REQUEST,
                                     resource->info->r);
    }
  
  for (hi = apr_hash_first(pool, locks); hi; hi = apr_hash_next(hi))
    {
      const char *token;
      void *val;
      apr_hash_this(hi, NULL, NULL, &val);
      token = val;
      
      serr = svn_fs_access_add_lock_token(fsaccess, token);
      if (serr)
        return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                   "Error pushing token into filesystem.",
                                   pool);
    }

  return NULL;
}


/* Helper for dav_svn_merge().  Free every lock in LOCKS.  The locks
   live in REPOS.  Log any errors for REQUEST.  Use POOL for temporary
   work.*/
static svn_error_t *release_locks(apr_hash_t *locks,
                                  svn_repos_t *repos,
                                  request_rec *r,
                                  apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  const void *key;
  void *val;
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_error_t *err;

  for (hi = apr_hash_first(pool, locks); hi; hi = apr_hash_next(hi))
    {
      svn_pool_clear(subpool);
      apr_hash_this(hi, &key, NULL, &val);

      /* The lock may be stolen or broken sometime between
         svn_fs_commit_txn() and this post-commit cleanup.  So ignore
         any errors from this command; just free as many locks as we can. */
      err = svn_repos_fs_unlock(repos, key, val, FALSE, subpool);

      if (err) /* If we got an error, just log it and move along. */
          ap_log_rerror(APLOG_MARK, APLOG_ERR, err->apr_err, r,
                        "%s", err->message);

      svn_error_clear(err);
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}



static dav_error *dav_svn_merge(dav_resource *target, dav_resource *source,
                                int no_auto_merge, int no_checkout,
                                apr_xml_elem *prop_elem,
                                ap_filter_t *output)
{
  apr_pool_t *pool;
  dav_error *err;
  svn_fs_txn_t *txn;
  const char *conflict;
  svn_error_t *serr;
  char *post_commit_err = NULL;
  svn_revnum_t new_rev;
  apr_hash_t *locks;
  svn_boolean_t disable_merge_response = FALSE;

  /* We'll use the target's pool for our operation. We happen to know that
     it matches the request pool, which (should) have the proper lifetime. */
  pool = target->pool;

  /* ### what to verify on the target? */

  /* ### anything else for the source? */
  if (source->type != DAV_RESOURCE_TYPE_ACTIVITY)
    {
      return dav_svn__new_error_tag(pool, HTTP_METHOD_NOT_ALLOWED,
                                    SVN_ERR_INCORRECT_PARAMS,
                                    "MERGE can only be performed using an "
                                    "activity as the source [at this time].",
                                    SVN_DAV_ERROR_NAMESPACE,
                                    SVN_DAV_ERROR_TAG);
    }

  /* Before attempting the final commit, we need to push any incoming
     lock-tokens into the filesystem's access_t.   Normally they come
     in via 'If:' header, and dav_svn_get_resource() automatically
     notices them and does this work for us.  In the case of MERGE,
     however, svn clients are sending them in the request body. */

  err = dav_svn__build_lock_hash(&locks, target->info->r,
                                 target->info->repos_path,
                                 pool);
  if (err != NULL)
    return err;

  if (apr_hash_count(locks))
    {
      err = dav_svn__push_locks(source, locks, pool);
      if (err != NULL)
        return err;
    }

  /* We will ignore no_auto_merge and no_checkout. We can't do those, but the
     client has no way to assert that we *should* do them. This should be fine
     because, presumably, the client has no way to do the various checkouts
     and things that would necessitate an auto-merge or checkout during the
     MERGE processing. */

  /* open the transaction that we're going to commit. */
  if ((err = open_txn(&txn, source->info->repos->fs,
                      source->info->root.txn_name, pool)) != NULL)
    return err;

  /* all righty... commit the bugger. */
  serr = svn_repos_fs_commit_txn(&conflict, source->info->repos->repos,
                                 &new_rev, txn, pool);

  /* If the error was just a post-commit hook failure, we ignore it.
     Otherwise, we deal with it.
     ### TODO: Figure out if the MERGE response can grow a means by
     which to marshal back both the success of the commit (and its
     commit info) and the failure of the post-commit hook.  */
  if (serr && (serr->apr_err != SVN_ERR_REPOS_POST_COMMIT_HOOK_FAILED))
    {
      const char *msg;
      svn_error_clear(svn_fs_abort_txn(txn, pool));

      if (serr->apr_err == SVN_ERR_FS_CONFLICT)
        {
          /* ### we need to convert the conflict path into a URI */
          msg = apr_psprintf(pool,
                             "A conflict occurred during the MERGE "
                             "processing. The problem occurred with the "
                             "\"%s\" resource.",
                             conflict);
        }
      else
        msg = "An error occurred while committing the transaction.";

      return dav_svn_convert_err(serr, HTTP_CONFLICT, msg, pool);
    }
  else if (serr)
    {
      if (serr->child && serr->child->message)
        post_commit_err = apr_pstrdup(pool, serr->child->message);
      svn_error_clear(serr);
    }

  /* Commit was successful, so schedule deltification. */
  register_deltification_cleanup(source->info->repos->repos, new_rev,
                                 source->info->r->connection->pool);

  /* We've detected a 'high level' svn action to log. */
  apr_table_set(target->info->r->subprocess_env, "SVN-ACTION",
                apr_psprintf(target->info->r->pool,
                             "commit r%" SVN_REVNUM_T_FMT, new_rev));

  /* Since the commit was successful, the txn ID is no longer valid.
     Store an empty txn ID in the activity database so that when the
     client deletes the activity, we don't try to open and abort the
     transaction. */
  err = dav_svn_store_activity(source->info->repos,
                               source->info->root.activity_id, "");
  if (err != NULL)
    return err;

  /* Check the dav_resource->info area for information about the
     special X-SVN-Options: header that may have come in the http
     request. */
  if (source->info->svn_client_options != NULL)
    {
      /* The client might want us to release all locks sent in the
         MERGE request. */
      if ((NULL != (ap_strstr_c(source->info->svn_client_options,
                                SVN_DAV_OPTION_RELEASE_LOCKS)))
          && apr_hash_count(locks))
        {
          serr = release_locks(locks, source->info->repos->repos, 
                               source->info->r, pool);
          if (serr != NULL)
            return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                       "Error releasing locks", pool);
        }

      /* The client might want us to disable the merge response altogether. */
      if (NULL != (ap_strstr_c(source->info->svn_client_options,
                               SVN_DAV_OPTION_NO_MERGE_RESPONSE)))
        disable_merge_response = TRUE;
    }

  /* process the response for the new revision. */
  return dav_svn__merge_response(output, source->info->repos, new_rev,
                                 post_commit_err, prop_elem,
                                 disable_merge_response, pool);
}

const dav_hooks_vsn dav_svn_hooks_vsn = {
  dav_svn_get_vsn_options,
  dav_svn_get_option,
  dav_svn_versionable,
  dav_svn_auto_versionable,
  dav_svn_vsn_control,
  dav_svn_checkout,
  dav_svn_uncheckout,
  dav_svn_checkin,
  dav_svn_avail_reports,
  dav_svn_report_label_header_allowed,
  dav_svn_deliver_report,
  NULL,                 /* update */
  NULL,                 /* add_label */
  NULL,                 /* remove_label */
  NULL,                 /* can_be_workspace */
  NULL,                 /* make_workspace */
  dav_svn_can_be_activity,
  dav_svn_make_activity,
  dav_svn_merge,
};
