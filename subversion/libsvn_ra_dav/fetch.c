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
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */



#define APR_WANT_STRFUNC
#include <apr_want.h> /* for strcmp() */

#include <apr_pools.h>
#include <apr_tables.h>
#include <apr_strings.h>
#include <apr_portable.h>

#include <ne_basic.h>
#include <ne_utils.h>
#include <ne_basic.h>
#include <ne_207.h>
#include <ne_props.h>
#include <ne_xml.h>

#include "svn_error.h"
#include "svn_pools.h"
#include "svn_delta.h"
#include "svn_io.h"
#include "svn_ra.h"
#include "svn_path.h"
#include "svn_xml.h"

#include "ra_dav.h"


#define CHKERR(e) if (1) { err = (e); if (err != NULL) goto error; } else

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
  svn_error_t *err;
  svn_txdelta_window_handler_t handler;
  void *handler_baton;

} file_read_ctx_t;

#define POP_SUBDIR(sds) (((subdir_t **)(sds)->elts)[--(sds)->nelts])
#define PUSH_SUBDIR(sds,s) (*(subdir_t **)apr_array_push(sds) = (s))

/* setting properties requires svn_stringbuf_t; this helps out */
typedef struct {
  svn_stringbuf_t *name;
  svn_stringbuf_t *value;
} vsn_url_helper;

typedef svn_error_t * (*prop_setter_t)(void *baton,
                                       svn_stringbuf_t *name,
                                       svn_stringbuf_t *value);

typedef struct {
  void *baton;
  svn_boolean_t fetch_props;
  const char *vsn_url;
} dir_item_t;

typedef struct {
  svn_ra_session_t *ras;

  apr_file_t *tmpfile;
  svn_stringbuf_t *fname;

  svn_boolean_t is_status;
  svn_boolean_t fetch_props;

  const svn_delta_edit_fns_t *editor;
  void *edit_baton;

  apr_array_header_t *dirs;	/* stack of directory batons/vsn_urls */
#define TOP_DIR(rb) (((dir_item_t *)(rb)->dirs->elts)[(rb)->dirs->nelts - 1])
#define PUSH_BATON(rb,b) (*(void **)apr_array_push((rb)->dirs) = (b))

  void *file_baton;
  svn_stringbuf_t *namestr;
  svn_stringbuf_t *cpathstr;
  svn_stringbuf_t *href;

  vsn_url_helper vuh;

  svn_error_t *err;

} report_baton_t;

static const char report_head[] = "<S:update-report xmlns:S=\""
                                   SVN_XML_NAMESPACE
                                   "\">" DEBUG_CR;
static const char report_tail[] = "</S:update-report>" DEBUG_CR;

static const struct ne_xml_elm report_elements[] =
{
  { SVN_XML_NAMESPACE, "update-report", ELEM_update_report, 0 },
  { SVN_XML_NAMESPACE, "target-revision", ELEM_target_revision, 0 },
  { SVN_XML_NAMESPACE, "open-directory", ELEM_open_directory, 0 },
  /* ### Sat 24 Nov 2001: after all clients have upgraded, change the
     "replace-" elements here to "open-" and upgrade the server.  -kff */  
  { SVN_XML_NAMESPACE, "replace-directory", ELEM_open_directory, 0 },
  { SVN_XML_NAMESPACE, "add-directory", ELEM_add_directory, 0 },
  { SVN_XML_NAMESPACE, "open-file", ELEM_open_file, 0 },
  { SVN_XML_NAMESPACE, "replace-file", ELEM_open_file, 0 },
  { SVN_XML_NAMESPACE, "add-file", ELEM_add_file, 0 },
  { SVN_XML_NAMESPACE, "delete-entry", ELEM_delete_entry, 0 },
  { SVN_XML_NAMESPACE, "fetch-props", ELEM_fetch_props, 0 },
  { SVN_XML_NAMESPACE, "remove-prop", ELEM_remove_prop, 0 },
  { SVN_XML_NAMESPACE, "fetch-file", ELEM_fetch_file, 0 },

  { "DAV:", "checked-in", ELEM_checked_in, 0 },
  { "DAV:", "href", NE_ELM_href, NE_XML_CDATA },

  { NULL }
};


static svn_stringbuf_t *my_basename(const char *url, apr_pool_t *pool)
{
  svn_stringbuf_t *s = svn_stringbuf_create(url, pool);

  svn_path_canonicalize(s, svn_path_url_style);

  /* ### creates yet another string. let's optimize this stuff... */
  return svn_path_last_component(s, svn_path_url_style, pool);
}

/* ### fold this function into store_vsn_url; not really needed */
static const char *get_vsn_url(const svn_ra_dav_resource_t *rsrc)
{
  return apr_hash_get(rsrc->propset,
                      SVN_RA_DAV__PROP_CHECKED_IN, APR_HASH_KEY_STRING);
}

static svn_error_t *simple_store_vsn_url(const char *vsn_url,
                                         void *baton,
                                         prop_setter_t setter,
                                         vsn_url_helper *vuh)
{
  svn_error_t *err;

  /* store the version URL as a property */
  svn_stringbuf_set(vuh->value, vsn_url);
  err = (*setter)(baton, vuh->name, vuh->value);
  if (err)
    return svn_error_quick_wrap(err,
                                "could not save the URL of the "
                                "version resource");

  return NULL;
}

static svn_error_t *store_vsn_url(const svn_ra_dav_resource_t *rsrc,
                                  void *baton,
                                  prop_setter_t setter,
                                  vsn_url_helper *vuh)
{
  const char *vsn_url;

  vsn_url = get_vsn_url(rsrc);
  if (vsn_url == NULL)
    return NULL;

  return simple_store_vsn_url(vsn_url, baton, setter, vuh);
}

static void add_props(const svn_ra_dav_resource_t *r,
                      prop_setter_t setter,
                      void *baton,
                      apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  svn_stringbuf_t *skey;
  svn_stringbuf_t *sval;

  skey = svn_stringbuf_create("", pool);
  sval = svn_stringbuf_create("", pool);

  for (hi = apr_hash_first(pool, r->propset); hi; hi = apr_hash_next(hi))
    {
      const char *key;
      char *val;

      apr_hash_this(hi, (const void **)&key, NULL, (void *)&val);
      
#define NSLEN (sizeof(SVN_PROP_CUSTOM_PREFIX) - 1)
      if (strncmp(key, SVN_PROP_CUSTOM_PREFIX, NSLEN) == 0)
        {
          /* for custom props, we strip the namespace, and just use whatever
             name the user gave the property. */

          svn_stringbuf_set(skey, key + NSLEN);

          /* ### urk. this value isn't binary-safe... */
          svn_stringbuf_set(sval, val);

          (*setter)(baton, skey, sval);
        }
#undef NSLEN
#define NSLEN (sizeof(SVN_PROP_PREFIX) - 1)
      else if (strncmp(key, SVN_PROP_PREFIX, NSLEN) == 0)
        {
          /* this is one of our properties. pass it straight through. */

          /* ### oops. watch out for props that the server sets, which we
             ### don't want reflected in the WC. we should put these into
             ### a server-prop namespace. */
          if (strcmp(key + NSLEN, "baseline-relative-path") == 0)
            continue;

          svn_stringbuf_set(skey, key);

          /* ### urk. this value isn't binary-safe... */
          svn_stringbuf_set(sval, val);

          (*setter)(baton, skey, sval);
        }
#undef NSLEN
    }
}
                      

static svn_error_t * fetch_dirents(svn_ra_session_t *ras,
                                   const char *url,
                                   void *dir_baton,
                                   apr_array_header_t *subdirs,
                                   apr_array_header_t *files,
                                   prop_setter_t setter,
                                   vsn_url_helper *vuh,
                                   apr_pool_t *pool)
{
  apr_hash_t *dirents;
  struct uri parsed_url;
  apr_hash_index_t *hi;

  /* Fetch all properties so we can snarf ones out of the svn:custom
   * namspace. */
  SVN_ERR( svn_ra_dav__get_props(&dirents, ras->sess, url, NE_DEPTH_ONE, NULL,
                                 NULL /* allprop */, pool) );

  /* ### This question is from Karl:
   *
   * I don't understand the implementation of this function.  The
   * first thing we do is fetch all properties for a URL, and store
   * them in a hash named `dirents'.  That's where I get confused --
   * sure, if url is a directory, then its entries will show up as
   * properties, but couldn't there be various other properties that
   * are not entries in the dir?  Or is it guaranteed that the *only*
   * properties we'd get above are the directory entries, and no other
   * properties are possible?
   *
   * The rest of the function is just more in the same vein -- it
   * makes sense if and only if that guarantee is present.  But I'd
   * just be very surprised to learn that entries are the *only*
   * possible properties one could ever get from a successful PROPFIND
   * with depth 1 on a url.  Is it really true?
   *
   * Anyway, regardless of the above, svn_ra_dav__get_props() needs to
   * get properly documented in ra_dav.h, along with
   * svn_ra_dav__get_props_resource() and svn_ra_dav__get_one_prop().
   * :-)
   */

  uri_parse(url, &parsed_url, NULL);

  for (hi = apr_hash_first(pool, dirents); hi; hi = apr_hash_next(hi))
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
              SVN_ERR( store_vsn_url(r, dir_baton, setter, vuh) );
            }
          else
            {
              subdir_t *subdir = apr_palloc(pool, sizeof(*subdir));

              subdir->rsrc = r;
              subdir->parent_baton = dir_baton;

              PUSH_SUBDIR(subdirs, subdir);
            }
        }
      else
        {
          svn_ra_dav_resource_t **file = apr_array_push(files);
          *file = r;
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
  svn_stringbuf_t data = { (char *)buf, len, len, frc->pool };

  if (frc->err)
    {
      /* We must have gotten an error during the last read... 

         ### what we'd *really* like to do here (or actually, at the
         bottom of this function) is to somehow abort the read
         process...no sense on banging a server for 10 megs of data
         when we've already established that we, for some reason,
         can't handle that data. */
      return;
    }

  if (len == 0)
    {
      /* file is complete. */
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

  /* We can't really do anything useful if we get an error here.  Pass
     it off to someone who can. */
  frc->err = (*frc->handler)(&window, frc->handler_baton);
}

static svn_error_t *simple_fetch_file(ne_session *sess,
                                      const char *url,
                                      svn_boolean_t text_deltas,
                                      void *file_baton,
                                      const svn_delta_edit_fns_t *editor,
                                      apr_pool_t *pool)
{
  file_read_ctx_t frc = { 0 };
  svn_error_t *err;
  svn_error_t *err2;
  int rv;
  svn_string_t my_url = { url, strlen(url) };
  svn_stringbuf_t *url_str = svn_path_uri_encode (&my_url, pool);

  err = (*editor->apply_textdelta)(file_baton,
                                   &frc.handler,
                                   &frc.handler_baton);
  if (err)
    {
      return svn_error_quick_wrap(err, "could not save file");
    }

  /* Only bother with text-deltas if our caller cares. */
  if (! text_deltas)
    {
      SVN_ERR ((*frc.handler)(NULL, frc.handler_baton));
      return SVN_NO_ERROR;
    }

  frc.err = NULL;
  frc.pool = pool;

  rv = ne_read_file(sess, url_str->data, fetch_file_reader, &frc);
  if (rv != NE_OK)
    {
      /* ### other GET responses? */

      /* ### need an SVN_ERR here */
      err = svn_error_create(APR_EGENERAL, 0, NULL, pool, ne_get_error(sess));
    }
  /* else: err == NULL */

  if (frc.err)
    return frc.err;

  /* close the handler, now that the file reading is complete. */
  err2 = (*frc.handler)(NULL, frc.handler_baton);

  /* return the primary error, other return the close error (if any) */
  return err ? err : err2;
}

static svn_error_t *fetch_file(ne_session *sess,
                               const svn_ra_dav_resource_t *rsrc,
                               void *dir_baton,
                               vsn_url_helper *vuh,
                               const svn_delta_edit_fns_t *editor,
                               apr_pool_t *pool)
{
  const char *bc_url = rsrc->url;    /* url in the Baseline Collection */
  svn_error_t *err;
  svn_error_t *err2;
  svn_stringbuf_t *name;
  void *file_baton;

  name = my_basename(bc_url, pool);
  err = (*editor->add_file)(name, dir_baton,
                            NULL, SVN_INVALID_REVNUM,
                            &file_baton);
  if (err)
    return svn_error_quick_wrap(err, "could not add a file");

  err = simple_fetch_file(sess, bc_url, TRUE, file_baton, editor, pool);
  if (err)
    {
      /* ### do we really need to bother with closing the file_baton? */
      goto error;
    }

  /* Add the properties. */
  add_props(rsrc, editor->change_file_prop, file_baton, pool);

  /* store the version URL as a property */
  err = store_vsn_url(rsrc, file_baton, editor->change_file_prop, vuh);

 error:
  err2 = (*editor->close_file)(file_baton);
  return err ? err : err2;
}

static svn_error_t * begin_checkout(svn_ra_session_t *ras,
                                    svn_revnum_t revision,
                                    svn_stringbuf_t **activity_url,
                                    svn_revnum_t *target_rev,
                                    const char **bc_root)
{
  apr_pool_t *pool = ras->pool;
  svn_boolean_t is_dir;
  svn_string_t bc_url;
  svn_string_t bc_relative;
#ifdef BUSTED_CRAP
  svn_stringbuf_t *path;
#endif

  /* ### if REVISION means "get latest", then we can use an expand-property
     ### REPORT rather than two PROPFINDs to reach the baseline-collection */

  /* Fetch the activity-collection-set from the server. */
  /* ### also need to fetch/validate the DAV capabilities */
  SVN_ERR( svn_ra_dav__get_activity_url(activity_url, ras, ras->root.path,
                                        pool) );

  SVN_ERR( svn_ra_dav__get_baseline_info(&is_dir, &bc_url, &bc_relative,
                                         target_rev, ras->sess,
                                         ras->root.path, revision, pool) );
  if (!is_dir)
    {
      /* ### eek. what to do? */

      /* ### need an SVN_ERR here */
      return svn_error_create(APR_EGENERAL, 0, NULL, pool,
                              "URL does not identify a collection.");
    }

  /* The root for the checkout is the Baseline Collection root, plus the
     relative location of the public URL to its repository root. */

#ifdef BUSTED_CRAP
  path = svn_stringbuf_create_from_string(&bc_url, pool);
  svn_path_add_component_nts(path, bc_relative.data, svn_path_url_style);

  *bc_root = path->data;
#else
  /* ### this is broken cuz it assumes bc_url has a trailing slash. */
  *bc_root = apr_pstrcat(pool, bc_url.data, bc_relative.data, NULL);
#endif

  return NULL;
}


svn_error_t *svn_ra_dav__get_file(void *session_baton,
                                  svn_stringbuf_t *url,
                                  svn_revnum_t revision,
                                  svn_stream_t *stream)
{
  abort ();
  return SVN_NO_ERROR;
}


svn_error_t * svn_ra_dav__do_checkout(void *session_baton,
                                      svn_revnum_t revision,
                                      svn_boolean_t recurse,
                                      const svn_delta_edit_fns_t *editor,
                                      void *edit_baton)
{
  svn_ra_session_t *ras = session_baton;

  svn_error_t *err;
  void *root_baton;
  svn_stringbuf_t *act_url_name;
  vsn_url_helper vuh;
  svn_stringbuf_t *activity_url;
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
  SVN_ERR( (*editor->open_root)(edit_baton, SVN_IGNORED_REVNUM,
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
  act_url_name = svn_stringbuf_create(SVN_RA_DAV__LP_ACTIVITY_URL, ras->pool);

  /* prep the helper */
  vuh.name = svn_stringbuf_create(SVN_RA_DAV__LP_VSN_URL, ras->pool);
  vuh.value = MAKE_BUFFER(ras->pool);

  do
    {
      const char *url;
      svn_ra_dav_resource_t *rsrc;
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
            {
              /* Finish the edit */
              SVN_ERR( ((*editor->close_edit) (edit_baton)) );

              /* Store auth info if necessary */
              SVN_ERR( (svn_ra_dav__maybe_store_auth_info (ras)) );

              return SVN_NO_ERROR;
            }
        }

      if (strlen(url) > strlen(bc_root))
        {
          svn_stringbuf_t *name;

          /* We're not in the root, add a directory */
          name = my_basename(url, ras->pool);

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

      SVN_ERR (svn_ra_dav__get_props_resource(&rsrc,
                                              ras->sess,
                                              url,
                                              NULL,
                                              NULL,
                                              ras->pool));
      add_props(rsrc, editor->change_dir_prop, this_baton, ras->pool);

      /* add a sentinel. this will be used to signal a close_directory
         for this directory's baton. */
      subdir = apr_pcalloc(ras->pool, sizeof(*subdir));
      subdir->parent_baton = this_baton;
      PUSH_SUBDIR(subdirs, subdir);

      err = fetch_dirents(ras, url, this_baton, subdirs, files,
                          editor->change_dir_prop, &vuh, ras->pool);
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
          rsrc = ((svn_ra_dav_resource_t **)files->elts)[i];

          err = fetch_file(ras->sess, rsrc, this_baton, &vuh, editor,
                           ras->pool);
          if (err)
            /* ### should we close the dir batons first? */
            return svn_error_quick_wrap(err, "could not checkout a file");
        }
      /* reset the list of files */
      files->nelts = 0;

    } while (recurse && subdirs->nelts > 0);

  /* ### should never reach??? */

  /* Finish the edit */
  SVN_ERR( ((*editor->close_edit) (edit_baton)) );

  /* Store auth info if necessary */
  SVN_ERR( (svn_ra_dav__maybe_store_auth_info (ras)) );

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------- */

svn_error_t *svn_ra_dav__get_latest_revnum(void *session_baton,
                                           svn_revnum_t *latest_revnum)
{
  svn_ra_session_t *ras = session_baton;

  /* ### should we perform an OPTIONS to validate the server we're about
     ### to talk to? */

  /* we don't need any of the baseline URLs and stuff, but this does
     give us the latest revision number */
  SVN_ERR( svn_ra_dav__get_baseline_info(NULL, NULL, NULL, latest_revnum,
                                         ras->sess, ras->root.path,
                                         SVN_INVALID_REVNUM, ras->pool) );

  SVN_ERR( svn_ra_dav__maybe_store_auth_info(ras) );

  return NULL;
}


/* ### DUMMY FUNC.   To be marshalled over network like previous
   routine. */

svn_error_t *svn_ra_dav__get_dated_revision (void *session_baton,
                                             svn_revnum_t *revision,
                                             apr_time_t timestamp)
{
  *revision = 0;

  /* On the other side of the network, mod_dav_svn can simply call
     svn_repos_dated_revision().  */

  return SVN_NO_ERROR;
}




/* -------------------------------------------------------------------------
**
** UPDATE HANDLING
**
** ### docco...
**
** DTD of the update report:
** ### open/add file/dir. first child is always checked-in/href (vsn_url).
** ### next are subdir elems, possibly fetch-file, then fetch-prop.
*/

/* This implements the `ne_xml_validate_cb' prototype. */
static int validate_element(void *userdata,
                            ne_xml_elmid parent,
                            ne_xml_elmid child)
{
  /* We're being very strict with the validity of XML elements here. If
     something exists that we don't know about, then we might not update
     the client properly. We also make various assumptions in the element
     processing functions, and the strong validation enables those
     assumptions. */

  switch (parent)
    {
    case NE_ELM_root:
      if (child == ELEM_update_report)
        return NE_XML_VALID;
      else
        return NE_XML_INVALID;

    case ELEM_update_report:
      if (child == ELEM_target_revision
          || child == ELEM_open_directory)
        return NE_XML_VALID;
      else
        return NE_XML_INVALID;

    case ELEM_open_directory:
      if (child == ELEM_open_directory
          || child == ELEM_add_directory
          || child == ELEM_open_file
          || child == ELEM_add_file
          || child == ELEM_fetch_props
          || child == ELEM_remove_prop
          || child == ELEM_delete_entry
          || child == ELEM_checked_in)
        return NE_XML_VALID;
      else
        return NE_XML_INVALID;

    case ELEM_add_directory:
      if (child == ELEM_add_directory
          || child == ELEM_add_file
          || child == ELEM_checked_in)
        return NE_XML_VALID;
      else
        return NE_XML_INVALID;

    case ELEM_open_file:
      if (child == ELEM_checked_in
          || child == ELEM_fetch_file
          || child == ELEM_fetch_props
          || child == ELEM_remove_prop)
        return NE_XML_VALID;
      else
        return NE_XML_INVALID;

    case ELEM_add_file:
      if (child == ELEM_checked_in)
        return NE_XML_VALID;
      else
        return NE_XML_INVALID;

    case ELEM_checked_in:
      if (child == NE_ELM_href)
        return NE_XML_VALID;
      else
        return NE_XML_INVALID;

    default:
      return NE_XML_DECLINE;
    }

  /* NOTREACHED */
}

static const char *get_attr(const char **atts, const char *which)
{
  for (; *atts != NULL; atts += 2)
    if (strcmp(*atts, which) == 0)
      return atts[1];
  return NULL;
}

static void push_dir(report_baton_t *rb, void *baton)
{
  dir_item_t *di = (dir_item_t *)apr_array_push(rb->dirs);

  di->baton = baton;
  di->vsn_url = NULL;
}


/* This implements the `ne_xml_startelm_cb' prototype. */
static int start_element(void *userdata, const struct ne_xml_elm *elm,
                         const char **atts)
{
  report_baton_t *rb = userdata;
  const char *att;
  svn_revnum_t base;
  const char *name;
  svn_stringbuf_t *cpath = NULL;
  svn_revnum_t crev = SVN_INVALID_REVNUM;
  void *new_dir_baton;
  svn_error_t *err;

  switch (elm->id)
    {
    case ELEM_target_revision:
      att = get_attr(atts, "rev");
      /* ### verify we got it. punt on error. */

      CHKERR( (*rb->editor->set_target_revision)(rb->edit_baton, atol(att)) );
      break;

    case ELEM_open_directory:
      att = get_attr(atts, "rev");
      /* ### verify we got it. punt on error. */
      base = atol(att);
      if (rb->dirs->nelts == 0)
        {
          err = (*rb->editor->open_root)(rb->edit_baton, base,
                                         &new_dir_baton);
        }
      else
        {
          name = get_attr(atts, "name");
          /* ### verify we got it. punt on error. */
          svn_stringbuf_set(rb->namestr, name);

          err = (*rb->editor->open_directory)(rb->namestr,
                                              TOP_DIR(rb).baton, base,
                                              &new_dir_baton);
        }
      if (err != NULL)
        goto error;

      /* push the new baton onto the directory baton stack */
      push_dir(rb, new_dir_baton);

      /* Property fetching is NOT implied in replacement. */
      TOP_DIR(rb).fetch_props = FALSE;
      break;

    case ELEM_add_directory:
      name = get_attr(atts, "name");
      /* ### verify we got it. punt on error. */
      svn_stringbuf_set(rb->namestr, name);

      att = get_attr(atts, "copyfrom-path");
      if (att != NULL)
        {
          cpath = rb->cpathstr;
          svn_stringbuf_set(cpath, att);

          att = get_attr(atts, "copyfrom-rev");
          /* ### verify we got it. punt on error. */
          crev = atol(att);
        }

      CHKERR( (*rb->editor->add_directory)(rb->namestr, TOP_DIR(rb).baton,
                                           cpath, crev, &new_dir_baton) );

      /* push the new baton onto the directory baton stack */
      push_dir(rb, new_dir_baton);

      /* Property fetching is implied in addition. */
      TOP_DIR(rb).fetch_props = TRUE;
      break;

    case ELEM_open_file:
      att = get_attr(atts, "rev");
      /* ### verify we got it. punt on error. */
      base = atol(att);

      name = get_attr(atts, "name");
      /* ### verify we got it. punt on error. */
      svn_stringbuf_set(rb->namestr, name);

      CHKERR( (*rb->editor->open_file)(rb->namestr, TOP_DIR(rb).baton, base,
                                       &rb->file_baton) );

      /* Property fetching is NOT implied in replacement. */
      rb->fetch_props = FALSE;
      break;

    case ELEM_add_file:
      name = get_attr(atts, "name");
      /* ### verify we got it. punt on error. */
      svn_stringbuf_set(rb->namestr, name);

      att = get_attr(atts, "copyfrom-path");
      if (att != NULL)
        {
          cpath = rb->cpathstr;
          svn_stringbuf_set(cpath, att);

          att = get_attr(atts, "copyfrom-rev");
          /* ### verify we got it. punt on error. */
          crev = atol(att);
        }

      CHKERR( (*rb->editor->add_file)(rb->namestr, TOP_DIR(rb).baton,
                                      cpath, crev, &rb->file_baton) );

      /* Property fetching is implied in addition. */
      rb->fetch_props = TRUE;
      break;

    case ELEM_remove_prop:
      name = get_attr(atts, "name");

      /* Removing a prop.  */
      if (rb->file_baton == NULL)
        {
          svn_stringbuf_t *namestr = svn_stringbuf_create(name, rb->ras->pool);
          rb->editor->change_dir_prop(TOP_DIR(rb).baton, namestr, NULL);
        }
      else
        {
          svn_stringbuf_t *namestr = svn_stringbuf_create(name, rb->ras->pool);
          rb->editor->change_file_prop(rb->file_baton, namestr, NULL);
        }
      break;
      
    case ELEM_fetch_props:
      if (rb->is_status)
        {
          /* If this is just a status check, the specifics of the
             property change are uninteresting.  Simply call our
             editor function with bogus data so it registers a
             property mod. */
          svn_stringbuf_t *namestr = 
            svn_stringbuf_create(SVN_PROP_PREFIX "BOGOSITY", rb->ras->pool);
          if (rb->file_baton == NULL)
            rb->editor->change_dir_prop(TOP_DIR(rb).baton, namestr, NULL);
          else
            rb->editor->change_file_prop(rb->file_baton, namestr, NULL);
        }
      else
        {
          /* Note that we need to fetch props for this... */
          if (rb->file_baton == NULL)
            TOP_DIR(rb).fetch_props = TRUE; /* ...directory. */
          else
            rb->fetch_props = TRUE; /* ...file. */
        }
      break;

    case ELEM_fetch_file:
      /* assert: rb->href->len > 0 */
      CHKERR( simple_fetch_file(rb->ras->sess2, rb->href->data, 
                                rb->is_status ? FALSE : TRUE,
                                rb->file_baton, rb->editor, rb->ras->pool) );
      break;

    case ELEM_delete_entry:
      name = get_attr(atts, "name");
      /* ### verify we got it. punt on error. */
      svn_stringbuf_set(rb->namestr, name);

      CHKERR( (*rb->editor->delete_entry)(rb->namestr, TOP_DIR(rb).baton) );
      break;

    default:
      break;
    }

  return 0;

 error:
  rb->err = err;

  /* stop the parsing */
  return 1;
}


static svn_error_t *
add_node_props (report_baton_t *rb)
{
  svn_ra_dav_resource_t *rsrc;

  /* Do nothing for status commands.  */
  if (rb->is_status)
    return SVN_NO_ERROR;

  if (rb->file_baton)
    {
      if (! rb->fetch_props)
        return SVN_NO_ERROR;

      /* Fetch dir props. */
      SVN_ERR (svn_ra_dav__get_props_resource(&rsrc,
                                              rb->ras->sess2,
                                              rb->href->data,
                                              NULL,
                                              NULL,
                                              rb->ras->pool));
      add_props(rsrc, 
                rb->editor->change_file_prop, 
                rb->file_baton,
                rb->ras->pool);
    }
  else
    {
      if (! TOP_DIR(rb).fetch_props)
        return SVN_NO_ERROR;

      /* Fetch dir props. */
      SVN_ERR (svn_ra_dav__get_props_resource(&rsrc,
                                              rb->ras->sess2,
                                              TOP_DIR(rb).vsn_url,
                                              NULL,
                                              NULL,
                                              rb->ras->pool));
      add_props(rsrc, 
                rb->editor->change_dir_prop, 
                TOP_DIR(rb).baton, 
                rb->ras->pool);
    }
    
  return SVN_NO_ERROR;
}

/* This implements the `ne_xml_endelm_cb' prototype. */
static int end_element(void *userdata, 
                       const struct ne_xml_elm *elm,
                       const char *cdata)
{
  report_baton_t *rb = userdata;
  svn_error_t *err;

  switch (elm->id)
    {
    case ELEM_add_directory:

      /*** FALLTHRU ***/

    case ELEM_open_directory:
      /* fetch node props as necessary. */
      CHKERR (add_node_props (rb));

      /* close the topmost directory, and pop it from the stack */
      CHKERR( (*rb->editor->close_directory)(TOP_DIR(rb).baton) );
      --rb->dirs->nelts;
      break;

    case ELEM_add_file:
      /* we wait until the close element to do the work. this allows us to
         retrieve the href before fetching. */

      /* fetch file */
      CHKERR( simple_fetch_file(rb->ras->sess2, rb->href->data, 
                                rb->is_status ? FALSE : TRUE,
                                rb->file_baton, rb->editor, rb->ras->pool) );


      /*** FALLTHRU ***/

    case ELEM_open_file:
      /* fetch node props as necessary. */
      CHKERR (add_node_props (rb));

      /* close the file and mark that we are no longer operating on a file */
      CHKERR( (*rb->editor->close_file)(rb->file_baton) );
      rb->file_baton = NULL;
      break;

    case NE_ELM_href:
      /* do nothing during a status update. */
      if (rb->is_status)
        break;

      /* record the href that we just found */
      svn_ra_dav__copy_href(rb->href, cdata);

      if (rb->file_baton == NULL)
        {
          CHKERR( simple_store_vsn_url(rb->href->data, TOP_DIR(rb).baton,
                                       rb->editor->change_dir_prop,
                                       &rb->vuh) );

          /* save away the URL in case a fetch-props arrives after all of
             the subdir processing. we will need this copy of the URL to
             fetch the properties (i.e. rb->href will be toast by then). */
          TOP_DIR(rb).vsn_url = apr_pmemdup(rb->ras->pool,
                                            rb->href->data, rb->href->len + 1);
        }
      else
        {
          CHKERR( simple_store_vsn_url(rb->href->data, rb->file_baton,
                                       rb->editor->change_file_prop,
                                       &rb->vuh) );
        }
      break;

    default:
      break;
    }

  return 0;

 error:
  rb->err = err;

  /* stop the parsing */
  return 1;
}

static svn_error_t * reporter_set_path(void *report_baton,
                                       svn_stringbuf_t *path,
                                       svn_revnum_t revision)
{
  report_baton_t *rb = report_baton;
  apr_status_t status;
  const char *entry;
  svn_stringbuf_t *qpath = NULL;

  svn_xml_escape_stringbuf (&qpath, path, rb->ras->pool);
  entry = apr_psprintf(rb->ras->pool,
                       "<S:entry rev=\"%ld\">%s</S:entry>" DEBUG_CR,
                       revision, qpath->data);

  status = apr_file_write_full(rb->tmpfile, entry, strlen(entry), NULL);
  if (status)
    {
      (void) apr_file_close(rb->tmpfile);
      return svn_error_create(status, 0, NULL, rb->ras->pool,
                              "Could not write an entry to the temporary "
                              "report file.");
    }

  return SVN_NO_ERROR;
}


static svn_error_t * reporter_delete_path(void *report_baton,
                                          svn_stringbuf_t *path)
{
  report_baton_t *rb = report_baton;
  apr_status_t status;
  const char *s;

  s = apr_psprintf(rb->ras->pool,
                   "<S:missing>%s</S:missing>" DEBUG_CR,
                   path->data);

  status = apr_file_write_full(rb->tmpfile, s, strlen(s), NULL);
  if (status)
    {
      (void) apr_file_close(rb->tmpfile);
      return svn_error_create(status, 0, NULL, rb->ras->pool,
                              "Could not write a missing entry to the "
                              "temporary report file.");
    }

  return SVN_NO_ERROR;
}


static svn_error_t * reporter_abort_report(void *report_baton)
{
  report_baton_t *rb = report_baton;

  (void) apr_file_close(rb->tmpfile);

  return SVN_NO_ERROR;
}


static svn_error_t * reporter_finish_report(void *report_baton)
{
  report_baton_t *rb = report_baton;
  apr_status_t status;
  int fdesc;
  svn_error_t *err;
  apr_off_t offset = 0;

  status = apr_file_write_full(rb->tmpfile,
                               report_tail, sizeof(report_tail) - 1, NULL);
  if (status)
    {
      (void) apr_file_close(rb->tmpfile);
      return svn_error_create(status, 0, NULL, rb->ras->pool,
                              "Could not write the trailer for the temporary "
                              "report file.");
    }

  /* get the editor process prepped */
  rb->dirs = apr_array_make(rb->ras->pool, 5, sizeof(dir_item_t));
  rb->namestr = MAKE_BUFFER(rb->ras->pool);
  rb->cpathstr = MAKE_BUFFER(rb->ras->pool);
  rb->href = MAKE_BUFFER(rb->ras->pool);

  rb->vuh.name = svn_stringbuf_create(SVN_RA_DAV__LP_VSN_URL, rb->ras->pool);
  rb->vuh.value = MAKE_BUFFER(rb->ras->pool);

  /* Rewind the tmpfile. */
  status = apr_file_seek(rb->tmpfile, APR_SET, &offset);
  if (status)
    {
      (void) apr_file_close(rb->tmpfile);
      return svn_error_create(status, 0, NULL, rb->ras->pool,
                              "Couldn't rewind tmpfile.");
    }
  /* Convert the (apr_file_t *)tmpfile into a file descriptor for neon. */
  status = svn_io_fd_from_file(&fdesc, rb->tmpfile);
  if (status)
    {
      (void) apr_file_close(rb->tmpfile);
      return svn_error_create(status, 0, NULL, rb->ras->pool,
                              "Couldn't get file-descriptor of tmpfile.");
    }

  err = svn_ra_dav__parsed_request(rb->ras, "REPORT", rb->ras->root.path,
                                   NULL, fdesc,
                                   report_elements, validate_element,
                                   start_element, end_element, rb,
                                   rb->ras->pool);

  /* we're done with the file */
  (void) apr_file_close(rb->tmpfile);

  if (err != NULL)
    return err;
  if (rb->err != NULL)
    return rb->err;

  /* we got the whole HTTP response thing done. now wrap up the update
     process with a close_edit call. */
  SVN_ERR( (*rb->editor->close_edit)(rb->edit_baton) );

  /* store auth info if we can. */
  SVN_ERR( svn_ra_dav__maybe_store_auth_info (rb->ras) );

  return SVN_NO_ERROR;
}

static const svn_ra_reporter_t ra_dav_reporter = {
  reporter_set_path,
  reporter_delete_path,
  reporter_finish_report,
  reporter_abort_report
};


/* Make a generic reporter/baton for reporting the state of the
   working copy during updates or status checks. */
static svn_error_t *
make_reporter (void *session_baton,
               const svn_ra_reporter_t **reporter,
               void **report_baton,
               svn_revnum_t revision,
               svn_stringbuf_t *target,
               svn_boolean_t recurse,
               const svn_delta_edit_fns_t *editor,
               void *edit_baton,
               svn_boolean_t is_status)
{
  svn_ra_session_t *ras = session_baton;
  report_baton_t *rb;
  apr_status_t status;
  const char *s;
  const char *msg;

  /* ### create a subpool for this operation? */

  rb = apr_pcalloc(ras->pool, sizeof(*rb));
  rb->ras = ras;
  rb->editor = editor;
  rb->edit_baton = edit_baton;
  rb->is_status = is_status;

  /* Neon "pulls" request body content from the caller. The reporter is
     organized where data is "pushed" into self. To match these up, we use
     an intermediate file -- push data into the file, then let Neon pull
     from the file.

     Note: one day we could spin up a thread and use a pipe between this
     code and Neon. We write to a pipe, Neon reads from the pipe. Each
     thread can block on the pipe, waiting for the other to complete its
     work.
  */

  /* Use the client callback to create a tmpfile. */
  SVN_ERR(ras->callbacks->open_tmp_file (&rb->tmpfile, ras->callback_baton));

  /* ### register a cleanup on our (sub)pool which removes the file. this
     ### will ensure that the file always gets tossed, even if we exit
     ### with an error. */

  /* prep the file */
  status = apr_file_write_full(rb->tmpfile,
                               report_head, sizeof(report_head) - 1, NULL);
  if (status)
    {
      msg = "Could not write the header for the temporary report file.";
      goto error;
    }

  /* an invalid revnum means "latest". we can just omit the target-revision
     element in that case. */
  if (SVN_IS_VALID_REVNUM(revision))
    {
      s = apr_psprintf(ras->pool, 
                       "<S:target-revision>%ld</S:target-revision>",
                       revision);
      status = apr_file_write_full(rb->tmpfile, s, strlen(s), NULL);
      if (status)
        {
          msg = "Failed writing the target revision to the report tempfile.";
          goto error;
        }
    }

  /* A NULL target is no problem.  */
  if (target && target->data)
    {
      s = apr_psprintf(ras->pool, 
                       "<S:update-target>%s</S:update-target>",
                       target->data);
      status = apr_file_write_full(rb->tmpfile, s, strlen(s), NULL);
      if (status)
        {
          msg = "Failed writing the target to the report tempfile.";
          goto error;
        }
    }

  /* mod_dav_svn will assume recursive, unless it finds this element. */
  if (!recurse)
    {
      const char * data = "<S:recursive>no</S:recursive>";
      status = apr_file_write_full(rb->tmpfile, data, strlen(data), NULL);
      if (status)
        {
          msg = "Failed writing the target to the report tempfile.";
          goto error;
        }
    }

  *reporter = &ra_dav_reporter;
  *report_baton = rb;

  return SVN_NO_ERROR;

 error:
  (void) apr_file_close(rb->tmpfile);
  return svn_error_create(status, 0, NULL, ras->pool, msg);
}                      


svn_error_t * svn_ra_dav__do_update(void *session_baton,
                                    const svn_ra_reporter_t **reporter,
                                    void **report_baton,
                                    svn_revnum_t revision_to_update_to,
                                    svn_stringbuf_t *update_target,
                                    svn_boolean_t recurse,
                                    const svn_delta_edit_fns_t *wc_update,
                                    void *wc_update_baton)
{
  return make_reporter (session_baton,
                        reporter,
                        report_baton,
                        revision_to_update_to,
                        update_target,
                        recurse,
                        wc_update,
                        wc_update_baton,
                        FALSE); /* is_status */
}


svn_error_t * svn_ra_dav__do_status(void *session_baton,
                                    const svn_ra_reporter_t **reporter,
                                    void **report_baton,
                                    svn_stringbuf_t *status_target,
                                    svn_boolean_t recurse,
                                    const svn_delta_edit_fns_t *wc_status,
                                    void *wc_status_baton)
{
  return make_reporter (session_baton,
                        reporter,
                        report_baton,
                        SVN_INVALID_REVNUM,
                        status_target,
                        recurse,
                        wc_status,
                        wc_status_baton,
                        TRUE); /* is_status */
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
