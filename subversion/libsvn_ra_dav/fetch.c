/*
 * fetch.c :  routines for fetching updates and checkouts
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */



#include <string.h>     /* for strrchr() */

#include <apr_pools.h>
#include <apr_tables.h>
#include <apr_strings.h>

#include <http_basic.h>
#include <http_utils.h>
#include <dav_basic.h>
#include <dav_207.h>
#include <dav_props.h>
#include <hip_xml.h>

#include "svn_error.h"
#include "svn_delta.h"
#include "svn_ra.h"
#include "svn_path.h"

#include "ra_dav.h"


/* when we begin a checkout, we fetch these from the "public" resources to
   steer us towards a Baseline Collection. we fetch the resourcetype to
   verify that we're accessing a collection. */
static const dav_propname starting_props[] =
{
  { "DAV:", "version-controlled-configuration" },
  { "SVN:", "baseline-relative-path" },
  { "DAV:", "resourcetype" },
  { NULL }
};

/* if we need to directly ask the VCC for the "latest" baseline, these are
   the properties to fetch. */
static const dav_propname vcc_props[] =
{
  { "DAV:", "checked-in" },
  { NULL }
};

/* when speaking to a Baseline to reach the Baseline Collection, fetch these
   properties. */
static const dav_propname baseline_props[] =
{
  { "DAV:", "baseline-collection" },
  { "DAV:", "version-name" },
  { NULL }
};

/* fetch these properties from all resources in the Baseline Collection
   during a checkout. */
static const dav_propname fetch_props[] =
{
  { "DAV:", "resourcetype" },
  { "DAV:", "checked-in" },
  { NULL }
};

typedef struct {
  /* the information for this subdir. if rsrc==NULL, then this is a sentinel
     record in fetch_ctx_t.subdirs to close the directory implied by the
     parent_baton member. */
  svn_ra_dav_resource_t *rsrc;

  /* the directory containing this subdirectory. */
  void *parent_baton;

} subdir_t;

typedef struct {
  apr_pool_t *pool;

  svn_txdelta_window_handler_t handler;
  void *handler_baton;

} file_read_ctx_t;

#define POP_SUBDIR(sds) (((subdir_t **)(sds)->elts)[--(sds)->nelts])
#define PUSH_SUBDIR(sds,s) (*(subdir_t **)apr_array_push(sds) = (s))

typedef svn_error_t * (*prop_setter_t)(void *baton,
                                       svn_string_t *name,
                                       svn_string_t *value);


static svn_string_t *my_basename(const char *url, apr_pool_t *pool)
{
  svn_string_t *s = svn_string_create(url, pool);

  svn_path_canonicalize(s, svn_path_url_style);

  /* ### creates yet another string. let's optimize this stuff... */
  return svn_path_last_component(s, svn_path_url_style, pool);
}

static const char *get_vsn_url(const svn_ra_dav_resource_t *rsrc)
{
  return apr_hash_get(rsrc->propset,
                      SVN_RA_DAV__PROP_CHECKED_IN, APR_HASH_KEY_STRING);
}

static svn_error_t *store_vsn_url(const svn_ra_dav_resource_t *rsrc,
                                  void *baton,
                                  prop_setter_t setter,
                                  svn_string_t *vsn_url_name,
                                  apr_pool_t *pool)
{
  const char *vsn_url;
  svn_string_t *vsn_url_value;
  svn_error_t *err;

  /* store the version URL as a property */
  vsn_url = get_vsn_url(rsrc);
  if (vsn_url == NULL)
    return NULL;

  vsn_url_value = svn_string_create(vsn_url, pool);
  err = (*setter)(baton, vsn_url_name, vsn_url_value);
  if (err)
    return svn_error_quick_wrap(err,
                                "could not save the URL of the "
                                "version resource");

  return NULL;
}

static svn_error_t * fetch_dirents(svn_ra_session_t *ras,
                                   const char *url,
                                   void *dir_baton,
                                   apr_array_header_t *subdirs,
                                   apr_array_header_t *files,
                                   prop_setter_t setter,
                                   svn_string_t *vsn_url_name,
                                   apr_pool_t *pool)
{
  apr_hash_t *dirents;
  struct uri parsed_url;
  apr_hash_index_t *hi;

  SVN_ERR( svn_ra_dav__get_props(&dirents, ras, url, DAV_DEPTH_ONE, NULL,
                                 fetch_props, pool) );

  uri_parse(url, &parsed_url, NULL);

  for (hi = apr_hash_first(dirents); hi != NULL; hi = apr_hash_next(hi))
    {
      void *val;
      svn_ra_dav_resource_t *r;

      apr_hash_this(hi, NULL, NULL, &val);
      r = val;

      if (r->is_collection)
        {
          if (uri_compare(parsed_url.path, r->url) == 0)
            {
              /* don't insert "this dir" into the set of subdirs */

              /* store the version URL for this resource */
              SVN_ERR( store_vsn_url(r, dir_baton, setter, vsn_url_name,
                                     pool) );
            }
          else
            {
              subdir_t *subdir = apr_palloc(pool, sizeof(*subdir));

              subdir->rsrc = r;
              subdir->parent_baton = dir_baton;

              PUSH_SUBDIR(subdirs, subdir);

              printf("  ... pushing subdir: %s\n", r->url);
            }
        }
      else
        {
          svn_ra_dav_resource_t **file = apr_array_push(files);
          *file = r;

          printf("  ... found file: %s -> %s\n", r->url, get_vsn_url(r));
        }
    }

  uri_free(&parsed_url);

  return SVN_NO_ERROR;
}

static void fetch_file_reader(void *userdata, const char *buf, size_t len)
{
  file_read_ctx_t *frc = userdata;
  svn_txdelta_window_t window = { 0 };
  svn_txdelta_op_t op;
  svn_string_t data = { (char *)buf, len, len, frc->pool };
  svn_error_t *err;

  if (len == 0)
    {
      /* file is complete. */
      err = (*frc->handler)(NULL, frc->handler_baton);
      if (err)
        {
          /* ### how to abort the read loop? */
        }
      return;
    }

  op.action_code = svn_txdelta_new;
  op.offset = 0;
  op.length = len;

  window.tview_len = len;       /* result will be this long */
  window.num_ops = 1;
  window.ops_size = 1;          /* ### why is this here? */
  window.ops = &op;
  window.new_data = &data;
  window.pool = frc->pool;

  err = (*frc->handler)(&window, frc->handler_baton);
  if (err)
    {
      /* ### how to abort the read loop? */
    }
}

static svn_error_t *fetch_file(svn_ra_session_t *ras,
                               const svn_ra_dav_resource_t *rsrc,
                               void *dir_baton,
                               svn_string_t *vsn_url_name,
                               const svn_delta_edit_fns_t *editor,
                               apr_pool_t *pool)
{
  file_read_ctx_t frc = { 0 };
  svn_error_t *err;
  svn_error_t *err2;
  svn_string_t *name;
  void *file_baton;
  int rv;

  printf("fetching and saving %s\n", rsrc->url);

  name = my_basename(rsrc->url, pool);
  err = (*editor->add_file)(name, dir_baton,
                            NULL, SVN_INVALID_REVNUM,
                            &file_baton);
  if (err)
    return svn_error_quick_wrap(err, "could not add a file");

  err = (*editor->apply_textdelta)(file_baton,
                                   &frc.handler,
                                   &frc.handler_baton);
  if (err)
    {
      err = svn_error_quick_wrap(err, "could not save file");
      /* ### do we really need to bother with closing the file_baton? */
      goto error;
    }

  frc.pool = pool;

  rv = http_read_file(ras->sess, rsrc->url, fetch_file_reader, &frc);
  if (rv != HTTP_OK)
    {
      /* ### other GET responses? */

      /* ### need an SVN_ERR here */
      return svn_error_create(APR_EGENERAL, 0, NULL, pool,
                              http_get_error(ras->sess));
    }

  /* note: handler_baton was "closed" in fetch_file_reader() */

  /* ### fetch properties */

  /* store the version URL as a property */
  err = store_vsn_url(rsrc, file_baton, editor->change_file_prop,
                      vsn_url_name, pool);

 error:
  err2 = (*editor->close_file)(file_baton);
  return err ? err : err2;
}

static svn_error_t * begin_checkout(svn_ra_session_t *ras,
                                    svn_revnum_t revision,
                                    svn_string_t **activity_url,
                                    svn_revnum_t *target_rev,
                                    const char **bc_root)
{
  apr_pool_t *pool = ras->pool;
  svn_ra_dav_resource_t *rsrc;
  const char *vcc;
  const char *relpath;
  const char *bc;
  const char *vsn_name;

  /* ### if REVISION means "get latest", then we can use an expand-property
     ### REPORT rather than two PROPFINDs to reach the baseline-collection */

  /* Fetch the activity-collection-set from the server. */
  /* ### also need to fetch/validate the DAV capabilities */
  SVN_ERR( svn_ra_dav__get_activity_url(activity_url, ras, ras->root.path,
                                        pool) );

  /* fetch the DAV:version-controlled-configuration, and the
     SVN:baseline-relative-path properties from the session root URL */

  SVN_ERR( svn_ra_dav__get_props_resource(&rsrc, ras, ras->root.path,
                                          NULL, starting_props, pool) );
  if (!rsrc->is_collection)
    {
      /* ### eek. what to do? */

      /* ### need an SVN_ERR here */
      return svn_error_create(APR_EGENERAL, 0, NULL, pool,
                              "URL does not identify a collection.");
    }

  vcc = apr_hash_get(rsrc->propset, SVN_RA_DAV__PROP_VCC, APR_HASH_KEY_STRING);
  relpath = apr_hash_get(rsrc->propset,
                         SVN_RA_DAV__PROP_BASELINE_RELPATH,
                         APR_HASH_KEY_STRING);
  printf("vcc='%s' relpath='%s'\n", vcc, relpath);
  if (vcc == NULL || relpath == NULL)
    {
      /* ### better error reporting... */

      /* ### need an SVN_ERR here */
      return svn_error_create(APR_EGENERAL, 0, NULL, pool,
                              "The VCC and/or relative-path properties "
                              "were not found on the resource.");
    }

  if (revision == SVN_INVALID_REVNUM)
    {
      /* Fetch the latest revision */

      const char *baseline;

      /* Get the Baseline from the DAV:checked-in value, then fetch its
         DAV:baseline-collection property. */
      SVN_ERR( svn_ra_dav__get_props_resource(&rsrc, ras, vcc, NULL,
                                              vcc_props, pool) );
      baseline = apr_hash_get(rsrc->propset, SVN_RA_DAV__PROP_CHECKED_IN,
                              APR_HASH_KEY_STRING);
      if (baseline == NULL)
        {
          /* ### better error reporting... */

          /* ### need an SVN_ERR here */
          return svn_error_create(APR_EGENERAL, 0, NULL, pool,
                                  "DAV:checked-in was not present on the "
                                  "version-controlled configuration.");
        }
      printf("baseline='%s'\n", baseline);

      SVN_ERR( svn_ra_dav__get_props_resource(&rsrc, ras, baseline, NULL,
                                              baseline_props, pool) );
    }
  else
    {
      /* Fetch a specific revision */

      char label[20];

      /* ### send Label hdr, get DAV:baseline-collection [from the baseline] */

      apr_snprintf(label, sizeof(label), "%ld", revision);
      SVN_ERR( svn_ra_dav__get_props_resource(&rsrc, ras, vcc, label,
                                              baseline_props, pool) );
    }

  /* rsrc is the Baseline. We will checkout from the DAV:baseline-collection.
     The revision we are checking out is in DAV:version-name */
  bc = apr_hash_get(rsrc->propset,
                    SVN_RA_DAV__PROP_BASELINE_COLLECTION, APR_HASH_KEY_STRING);
  vsn_name = apr_hash_get(rsrc->propset,
                          SVN_RA_DAV__PROP_VERSION_NAME, APR_HASH_KEY_STRING);
  printf("bc='%s' vsn_name='%s'\n", bc, vsn_name);
  if (bc == NULL || vsn_name == NULL)
    {
      /* ### better error reporting... */

      /* ### need an SVN_ERR here */
      return svn_error_create(APR_EGENERAL, 0, NULL, pool,
                              "DAV:baseline-collection and/or "
                              "DAV:version-name was not present on the "
                              "baseline resource.");
    }

  *target_rev = atoi(vsn_name);

  /* The root for the checkout is the Baseline Collection root, plus the
     relative location of the public URL to its repository root. */
  /* ### this assumes bc has a trailing slash. fix this code one day. */
  *bc_root = apr_psprintf(pool, "%s%s", bc, relpath);

  return NULL;
}

svn_error_t * svn_ra_dav__do_checkout(void *session_baton,
                                      svn_revnum_t revision,
                                      const svn_delta_edit_fns_t *editor,
                                      void *edit_baton)
{
  svn_ra_session_t *ras = session_baton;
  int recurse = 1;      /* ### until it gets passed to us */

  svn_error_t *err;
  void *root_baton;
  svn_string_t *act_url_name;
  svn_string_t *vsn_url_name;
  svn_string_t *activity_url;
  svn_revnum_t target_rev;
  const char *bc_root;
  subdir_t *subdir;
  apr_array_header_t *subdirs;  /* subdirs to scan (subdir_t *) */
  apr_array_header_t *files;    /* files to grab (svn_ra_dav_resource_t *) */

  /* ### use quick_wrap rather than SVN_ERR on some of these? */

  /* begin the checkout process by fetching some basic information */
  SVN_ERR( begin_checkout(ras, revision, &activity_url, &target_rev,
                          &bc_root) );

  /* all the files we checkout will have TARGET_REV for the revision */
  SVN_ERR( (*editor->set_target_revision)(edit_baton, target_rev) );

  /* In the checkout case, we don't really have a base revision, so
     pass SVN_IGNORED_REVNUM. */
  SVN_ERR( (*editor->replace_root)(edit_baton, SVN_IGNORED_REVNUM,
                                   &root_baton) );

  /* store the subdirs into an array for processing, rather than recursing */
  subdirs = apr_array_make(ras->pool, 5, sizeof(subdir_t *));
  files = apr_array_make(ras->pool, 10, sizeof(svn_ra_dav_resource_t *));

  /* Build a directory resource for the root. We'll pop this off and fetch
     the information for it. */
  subdir = apr_palloc(ras->pool, sizeof(*subdir));
  subdir->parent_baton = root_baton;

  subdir->rsrc = apr_pcalloc(ras->pool, sizeof(*subdir->rsrc));
  subdir->rsrc->url = bc_root;

  PUSH_SUBDIR(subdirs, subdir);

  /* ### damn. gotta build a string. */
  act_url_name = svn_string_create(SVN_RA_DAV__LP_ACTIVITY_URL, ras->pool);
  vsn_url_name = svn_string_create(SVN_RA_DAV__LP_VSN_URL, ras->pool);

  do
    {
      const char *url;
      void *parent_baton;
      void *this_baton;
      int i;

      /* pop a subdir off the stack */
      while (1)
        {
          subdir = POP_SUBDIR(subdirs);
          parent_baton = subdir->parent_baton;

          if (subdir->rsrc != NULL)
            {
              url = subdir->rsrc->url;
              break;
            }
          /* sentinel reached. close the dir. possibly done! */

          err = (*editor->close_directory) (parent_baton);
          if (err)
            return svn_error_quick_wrap(err, "could not finish directory");

          if (subdirs->nelts == 0)
            return SVN_NO_ERROR;
        }

      if (strlen(url) > strlen(bc_root))
        {
          svn_string_t *name;

          /* We're not in the root, add a directory */
          name = my_basename(url, ras->pool);

          printf("adding directory: %s\n", name->data);
          err = (*editor->add_directory) (name, parent_baton,
                                          NULL, SVN_INVALID_REVNUM,
                                          &this_baton);
          if (err)
            return svn_error_quick_wrap(err, "could not add directory");
        }
      else 
        {
          /* We are operating in the root of the repository */
          this_baton = root_baton;
        }

      /* add a sentinel. this will be used to signal a close_directory
         for this directory's baton. */
      subdir = apr_pcalloc(ras->pool, sizeof(*subdir));
      subdir->parent_baton = this_baton;
      PUSH_SUBDIR(subdirs, subdir);

      err = fetch_dirents(ras, url, this_baton, subdirs, files,
                          editor->change_dir_prop, vsn_url_name, ras->pool);
      if (err)
        return svn_error_quick_wrap(err, "could not fetch directory entries");

      /* ### use set_wc_dir_prop() */

      /* store the activity URL as a property */
      err = (*editor->change_dir_prop)(this_baton, act_url_name, activity_url);
      if (err)
        /* ### should we close the dir batons first? */
        return svn_error_quick_wrap(err,
                                    "could not save the URL to indicate "
                                    "where to create activities");

      /* process each of the files that were found */
      for (i = files->nelts; i--; )
        {
          svn_ra_dav_resource_t *rsrc = ((svn_ra_dav_resource_t **)files->elts)[i];

          err = fetch_file(ras, rsrc, this_baton, vsn_url_name, editor,
                           ras->pool);
          if (err)
            /* ### should we close the dir batons first? */
            return svn_error_quick_wrap(err, "could not checkout a file");
        }
      /* reset the list of files */
      files->nelts = 0;

    } while (recurse && subdirs->nelts > 0);

  /* ### should never reach??? */
  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------- */

svn_error_t *svn_ra_dav__get_latest_revnum(void *session_baton,
                                           svn_revnum_t *latest_revnum)
{
  svn_ra_session_t *ras = session_baton;
  apr_pool_t *pool = ras->pool;
  svn_ra_dav_resource_t *rsrc;
  const char *vcc;
  const char *baseline;
  const char *vsn_name;

  /* ### this whole sequence can/should be replaced with an expand-property
     ### REPORT when that is available on the server. */

  /* ### should we perform an OPTIONS to validate the server we're about
     ### to talk to? */

  /* fetch the DAV:version-controlled-configuration from the session's URL */
  SVN_ERR( svn_ra_dav__get_props_resource(&rsrc, ras, ras->root.path,
                                          NULL, starting_props, pool) );
  vcc = apr_hash_get(rsrc->propset, SVN_RA_DAV__PROP_VCC, APR_HASH_KEY_STRING);
  printf("vcc='%s'\n", vcc);
  if (vcc == NULL)
    {
      /* ### better error reporting... */

      /* ### need an SVN_ERR here */
      return svn_error_create(APR_EGENERAL, 0, NULL, pool,
                              "Could not determine the VCC.");
    }

  /* Get the Baseline from the DAV:checked-in value */
  SVN_ERR( svn_ra_dav__get_props_resource(&rsrc, ras, vcc, NULL,
                                          vcc_props, pool) );
  baseline = apr_hash_get(rsrc->propset, SVN_RA_DAV__PROP_CHECKED_IN,
                          APR_HASH_KEY_STRING);
  if (baseline == NULL)
    {
      /* ### better error reporting... */

      /* ### need an SVN_ERR here */
      return svn_error_create(APR_EGENERAL, 0, NULL, pool,
                              "DAV:checked-in was not present on the "
                              "version-controlled configuration.");
    }
  printf("baseline='%s'\n", baseline);

  /* rsrc will be the latest Baseline. The revision is in DAV:version-name */
  SVN_ERR( svn_ra_dav__get_props_resource(&rsrc, ras, baseline, NULL,
                                          baseline_props, pool) );
  vsn_name = apr_hash_get(rsrc->propset,
                          SVN_RA_DAV__PROP_VERSION_NAME, APR_HASH_KEY_STRING);
  printf("vsn_name='%s'\n", vsn_name);
  if (vsn_name == NULL)
    {
      /* ### better error reporting... */

      /* ### need an SVN_ERR here */
      return svn_error_create(APR_EGENERAL, 0, NULL, pool,
                              "DAV:version-name was not present on the "
                              "baseline resource.");
    }

  *latest_revnum = atoi(vsn_name);

  return NULL;
}

/* -------------------------------------------------------------------------
**
** UPDATE HANDLING
**
** ### docco...
*/

static svn_error_t * reporter_set_path(void *report_baton,
                                       svn_string_t *path,
                                       svn_revnum_t revision)
{
  return SVN_NO_ERROR;
}

static svn_error_t * reporter_finish_report(void *report_baton)
{
  return SVN_NO_ERROR;
}

static const svn_ra_reporter_t ra_dav_reporter = {
  reporter_set_path,
  reporter_finish_report
};

svn_error_t * svn_ra_dav__do_update(void *session_baton,
                                    const svn_ra_reporter_t **reporter,
                                    void **report_baton,
                                    svn_revnum_t revision_to_update_to,
                                    const svn_delta_edit_fns_t *wc_update,
                                    void *wc_update_baton)
{
  *reporter = &ra_dav_reporter;

  /* ### need something here */
  /* ### put the session and wc_* values into the baton */
  *report_baton = NULL;

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
