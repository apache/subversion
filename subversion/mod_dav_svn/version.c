/*
 * version.c: mod_dav_svn versioning provider functions for Subversion
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
#include <mod_dav.h>
#include <apr_tables.h>
#include <apr_uuid.h>

#include "svn_fs.h"
#include "svn_xml.h"
#include "svn_repos.h"
#include "svn_dav.h"
#include "svn_time.h"

#include "dav_svn.h"


/* ### should move these report names to a public header to share with
   ### the client (and third parties). */
static const dav_report_elem avail_reports[] = {
  { SVN_XML_NAMESPACE, "update-report" },
  { SVN_XML_NAMESPACE, "log-report" },
  { NULL },
};

/* declare these static functions early, so we can use them anywhere. */
static dav_error *dav_svn_make_activity(dav_resource *resource);


/* Helper: attach an auto-generated svn:log property to a txn within
   an auto-checked-out working resource. */
static dav_error *set_auto_log_message(dav_resource *resource)
{
  const char *logmsg;
  svn_string_t *logval;
  svn_error_t *serr;

  if (! (resource->type == DAV_RESOURCE_TYPE_WORKING
         && resource->info->auto_checked_out))
    return dav_new_error(resource->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                         "set_auto_log_message called on invalid resource.");
  
  logmsg = apr_psprintf(resource->pool, 
                        "Autoversioning commit:  a non-deltaV client made "
                        "a change to\n%s", resource->info->repos_path);

  logval = svn_string_create(logmsg, resource->pool);

  serr = svn_repos_fs_change_txn_prop(resource->info->root.txn,
                                      SVN_PROP_REVISION_LOG, logval,
                                      resource->pool);
  if (serr)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "Error setting auto-log-message on "
                               "auto-checked-out resource's txn.");
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
                                     "activity does not exist");
        }

      /* ### correct HTTP error? */
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "There was a problem opening the "
                                 "transaction specified by this "
                                 "activity.");
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
    return dav_new_error_tag(resource->pool, HTTP_NOT_IMPLEMENTED,
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
  dav_error *derr;
  dav_svn_uri_info parse;

  /* Auto-Versioning Stuff */
  if (auto_checkout)
    {
      dav_resource *res; /* ignored */
      apr_uuid_t uuid;
      char uuid_buf[APR_UUID_FORMATTED_LENGTH + 1];

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
        return dav_new_error_tag(resource->pool, HTTP_METHOD_NOT_ALLOWED,
                                 SVN_ERR_UNSUPPORTED_FEATURE,
                                 "auto-checkout attempted on non-regular "
                                 "version-controlled resource.",
                                 SVN_DAV_ERROR_NAMESPACE,
                                 SVN_DAV_ERROR_TAG);

      if (resource->baselined)
        return dav_new_error_tag(resource->pool, HTTP_METHOD_NOT_ALLOWED,
                                 SVN_ERR_UNSUPPORTED_FEATURE,
                                 "auto-checkout attempted on baseline "
                                 "collection, which is not supported.",
                                 SVN_DAV_ERROR_NAMESPACE,
                                 SVN_DAV_ERROR_TAG);

      /* Come up with a unique activity name, put it in the resource. */
      apr_uuid_get(&uuid);
      apr_uuid_format(uuid_buf, &uuid);
      resource->info->root.activity_id = uuid_buf;

      /* Create a txn based on youngest rev, and create an associated
         activity id in the activity database. */
      derr = dav_svn_make_activity(resource);
      if (derr)
        return derr;
      
      /* Tweak the VCR in-place, making it into a WR.  (Ignore the
         NULL return value.) */
      res = dav_svn_create_working_resource(resource, uuid_buf, 
                                            resource->info->root.txn_name,
                                            TRUE /* tweak in place */);

      /* Remember that this resource was auto-checked-out, so that
         dav_svn_auto_versionable allows us to do an auto-checkin. */
      resource->info->auto_checked_out = TRUE;

      /* Finally, be sure to open the txn and txn_root in the
         resource.  Normally we only get a PUT on a WR uri, and
         prep_working() opens the txn automatically.  We need to make
         sure this WR is in the exact same state, ready for a PUT. */
      derr = open_txn(&resource->info->root.txn, resource->info->repos->fs,
                      resource->info->root.txn_name, resource->pool);
      if (derr)
        return derr;

      serr = svn_fs_txn_root(&resource->info->root.root,
                             resource->info->root.txn, resource->pool);
      if (serr != NULL)
        return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                   "Could not open the (txn) root of the "
                                   "repository");
        
      return NULL;
    }
  /* end of Auto-Versioning Stuff */

  if (resource->type != DAV_RESOURCE_TYPE_VERSION)
    {
      return dav_new_error_tag(resource->pool, HTTP_METHOD_NOT_ALLOWED,
                               SVN_ERR_UNSUPPORTED_FEATURE,
                               "CHECKOUT can only be performed on a version "
                               "resource [at this time].",
                               SVN_DAV_ERROR_NAMESPACE,
                               SVN_DAV_ERROR_TAG);
    }
  if (create_activity)
    {
      return dav_new_error_tag(resource->pool, HTTP_NOT_IMPLEMENTED,
                               SVN_ERR_UNSUPPORTED_FEATURE,
                               "CHECKOUT can not create an activity at this "
                               "time. Use MKACTIVITY first.",
                               SVN_DAV_ERROR_NAMESPACE,
                               SVN_DAV_ERROR_TAG);
    }
  if (is_unreserved)
    {
      return dav_new_error_tag(resource->pool, HTTP_NOT_IMPLEMENTED,
                               SVN_ERR_UNSUPPORTED_FEATURE,
                               "Unreserved checkouts are not yet available. "
                               "A version history may not be checked out more "
                               "than once, into a specific activity.",
                               SVN_DAV_ERROR_NAMESPACE,
                               SVN_DAV_ERROR_TAG);
    }
  if (activities == NULL)
    {
      return dav_new_error_tag(resource->pool, HTTP_CONFLICT,
                               SVN_ERR_INCOMPLETE_DATA,
                               "An activity must be provided for checkout.",
                               SVN_DAV_ERROR_NAMESPACE,
                               SVN_DAV_ERROR_TAG);
    }
  /* assert: nelts > 0.  the below check effectively means > 1. */
  if (activities->nelts != 1)
    {
      return dav_new_error_tag(resource->pool, HTTP_CONFLICT,
                               SVN_ERR_INCORRECT_PARAMS,
                               "Only one activity may be specified within the "
                               "CHECKOUT.",
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
                                 "properly.");
    }
  if (parse.activity_id == NULL)
    {
      return dav_new_error_tag(resource->pool, HTTP_CONFLICT,
                               SVN_ERR_INCORRECT_PARAMS,
                               "The provided href is not an activity URI.",
                               SVN_DAV_ERROR_NAMESPACE,
                               SVN_DAV_ERROR_TAG);
    }

  if ((txn_name = dav_svn_get_txn(resource->info->repos,
                                  parse.activity_id)) == NULL)
    {
      return dav_new_error_tag(resource->pool, HTTP_CONFLICT,
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
                                     "the baseline being checked out.");
        }

      if (resource->info->root.rev != youngest)
        {
          return dav_new_error_tag(resource->pool, HTTP_CONFLICT,
                                   SVN_ERR_APMOD_BAD_BASELINE,
                                   "The specified baseline is not the latest "
                                   "baseline, so it may not be checked out.",
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
                                     "Could not open the transaction tree.");
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
                                     "Could not get created-rev of txn node.");
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
                  return dav_new_error_tag
                    (resource->pool, HTTP_CONFLICT, serr->apr_err,
                     "Unable to fetch the node revision id of the version "
                     "resource within the transaction.",
                     SVN_DAV_ERROR_NAMESPACE,
                     SVN_DAV_ERROR_TAG);
                }
              if ((serr = svn_fs_node_id(&url_noderev_id,
                                         resource->info->root.root,
                                         resource->info->repos_path,
                                         resource->pool)))
                {
                  return dav_new_error_tag
                    (resource->pool, HTTP_CONFLICT, serr->apr_err,
                     "Unable to fetch the node revision id of the version "
                     "resource within the revision.",
                     SVN_DAV_ERROR_NAMESPACE,
                     SVN_DAV_ERROR_TAG);
                }
              if (svn_fs_compare_ids(url_noderev_id, txn_noderev_id) != 0)
                {
                  errorful = 1;
                }
            }
          if (errorful)
            {
#if 1
              return dav_new_error_tag
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
                                 "created-rev mismatch: r=%" SVN_REVNUM_T_FMT 
                                 ", t=%" SVN_REVNUM_T_FMT,
                                 resource->info->root.rev, txn_created_rev);
              
              return dav_new_error_tag(resource->pool, HTTP_CONFLICT, 
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
    return dav_new_error_tag(resource->pool, HTTP_INTERNAL_SERVER_ERROR,
                             SVN_ERR_UNSUPPORTED_FEATURE,
                             "UNCHECKOUT called on non-working resource.",
                             SVN_DAV_ERROR_NAMESPACE,
                             SVN_DAV_ERROR_TAG);

  /* Try to abort the txn if it exists;  but don't try too hard.  :-)  */
  if (resource->info->root.txn)
    svn_fs_abort_txn(resource->info->root.txn);

  resource->info->root.txn_name = NULL;
  resource->info->root.txn = NULL;

  /* We're no longer checked out. */
  resource->info->auto_checked_out = FALSE;

  /* Convert the working resource back into a regular one, in-place. */
  return dav_svn_working_to_regular_resource(resource);
}

dav_error *dav_svn_checkin(dav_resource *resource,
                           int keep_checked_out,
                           dav_resource **version_resource)
{
  svn_error_t *serr;
  dav_error *err;
  const char *uri;

  /* ### mod_dav has a flawed architecture, in the sense that it first
     tries to auto-checkin the modified resource, then attempts to
     auto-checkin the parent resource (if the parent resource was
     auto-checked-out).  Instead, the provider should be in charge:
     mod_dav should provide a *set* of resources that need
     auto-checkin, and the provider can decide how to do it.  (One
     txn?  Many txns?  Etc.) */

  if (resource->type != DAV_RESOURCE_TYPE_WORKING)
    return dav_new_error_tag(resource->pool, HTTP_INTERNAL_SERVER_ERROR,
                             SVN_ERR_UNSUPPORTED_FEATURE,
                             "CHECKIN called on non-working resource.",
                             SVN_DAV_ERROR_NAMESPACE,
                             SVN_DAV_ERROR_TAG);

  /* Try to commit the txn if it exists. */
  if (resource->info->root.txn_name)
    {
      svn_fs_txn_t *txn;
      const char *conflict_msg;
      svn_revnum_t new_rev;

      err = open_txn(&txn, resource->info->repos->fs,
                     resource->info->root.txn_name, resource->pool);
      
      /* If we failed to open the txn, don't worry about it.  It may
         have already been committed when a child resource was
         checked in.  */
      if (! err)
        {
          err = set_auto_log_message(resource);
          if (err)
            return err;

          serr = svn_repos_fs_commit_txn(&conflict_msg,
                                         resource->info->repos->repos,
                                         &new_rev, resource->info->root.txn);
          if (serr != NULL)
            {
              const char *msg;
              svn_fs_abort_txn(resource->info->root.txn);
              
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
              
              return dav_svn_convert_err(serr, HTTP_CONFLICT, msg);
            }

          /* Commit was successful. */

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
        }

      /* whether the txn was committed, aborted, or inaccessible,
         it's gone now.  the resource needs to lose all knowledge
         of it. */
      resource->info->root.txn_name = NULL;
      resource->info->root.txn = NULL;
    }

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

  /* Find the DAV:creationdate element and get the requested time from it. */
  ns = dav_svn_find_ns(doc->namespaces, "DAV:");
  if (ns != -1)
    {
      for (child = doc->root->first_child; child != NULL; child = child->next)
        {
          if (child->ns != ns || strcmp(child->name, "creationdate") != 0)
            continue;
          /* If this fails, we'll notice below. */
          svn_time_from_cstring(&tm, child->first_cdata.first->text,
                                resource->pool);
        }
    }

  if (tm == (apr_time_t) -1)
    {
      return dav_new_error(resource->pool, HTTP_BAD_REQUEST, 0,
                           "The request does not contain a valid "
                           "'DAV:creationdate' element.");
    }

  /* Do the actual work of finding the revision by date. */
  if (svn_repos_dated_revision(&rev, resource->info->repos->repos, tm,
                               resource->pool) != SVN_NO_ERROR)
    {
      return dav_new_error(resource->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                           "Could not access revision times.");
    }

  bb = apr_brigade_create(resource->pool, output->c->bucket_alloc);
  ap_fprintf(output, bb,
             DAV_XML_HEADER DEBUG_CR
             "<S:dated-rev-report xmlns:S=\"" SVN_XML_NAMESPACE "\" "
             "xmlns:D=\"DAV:\">" DEBUG_CR
             "<D:version-name>%" SVN_REVNUM_T_FMT "</D:version-name>"
             "</S:dated-rev-report>", rev);
  ap_fflush(output, bb);

  return NULL;
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
    }

  /* ### what is a good error for an unknown report? */
  return dav_new_error_tag(resource->pool, HTTP_NOT_IMPLEMENTED,
                           SVN_ERR_UNSUPPORTED_FEATURE,
                           "The requested report is unknown.",
                           SVN_DAV_ERROR_NAMESPACE,
                           SVN_DAV_ERROR_TAG);
}

static int dav_svn_can_be_activity(const dav_resource *resource)
{
  return resource->type == DAV_RESOURCE_TYPE_ACTIVITY && !resource->exists;
}

static dav_error *dav_svn_make_activity(dav_resource *resource)
{
  const char *activity_id = resource->info->root.activity_id;
  const char *txn_name;
  dav_error *err;

  /* ### need to check some preconditions? */

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
  svn_revnum_t new_rev;
  svn_boolean_t disable_merge_response = FALSE;

  /* We'll use the target's pool for our operation. We happen to know that
     it matches the request pool, which (should) have the proper lifetime. */
  pool = target->pool;

  /* ### what to verify on the target? */

  /* ### anything else for the source? */
  if (source->type != DAV_RESOURCE_TYPE_ACTIVITY)
    {
      return dav_new_error_tag(pool, HTTP_METHOD_NOT_ALLOWED,
                               SVN_ERR_INCORRECT_PARAMS,
                               "MERGE can only be performed using an activity "
                               "as the source [at this time].",
                               SVN_DAV_ERROR_NAMESPACE,
                               SVN_DAV_ERROR_TAG);
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
                                 &new_rev, txn);
  if (serr != NULL)
    {
      const char *msg;
      svn_fs_abort_txn(txn);

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

      return dav_svn_convert_err(serr, HTTP_CONFLICT, msg);
    }

  /* Check the dav_resource->info area for information about the
     special X-SVN-Options: header that may have come in the http
     request.  If the header contains "no-merge-response", then pass
     the correct boolean value to the routine below. */
  if (source->info->svn_client_options != NULL)
    {
      if (NULL != (ap_strstr_c(source->info->svn_client_options,
                               SVN_DAV_OPTION_NO_MERGE_RESPONSE)))
        disable_merge_response = TRUE;
    }

  /* process the response for the new revision. */
  return dav_svn__merge_response(output, source->info->repos, new_rev,
                                 prop_elem, disable_merge_response, pool);
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
