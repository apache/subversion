/*
 * fetch.c :  routines for fetching updates and checkouts
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
#include <apr_md5.h>
#include <apr_portable.h>

#include <ne_basic.h>
#include <ne_utils.h>
#include <ne_basic.h>
#include <ne_207.h>
#include <ne_props.h>
#include <ne_xml.h>
#include <ne_request.h>
#include <ne_compress.h>

#include "svn_error.h"
#include "svn_pools.h"
#include "svn_delta.h"
#include "svn_io.h"
#include "svn_md5.h"
#include "svn_ra.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_dav.h"
#include "svn_time.h"

#include "ra_dav.h"


#define CHKERR(e)               \
do {                            \
  if ((rb->err = (e)) != NULL)  \
    return 1;                   \
} while(0)

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

  /* these two are the handler that the editor gave us */
  svn_txdelta_window_handler_t handler;
  void *handler_baton;

  /* if we're receiving an svndiff, this is a parser which places the
     resulting windows into the above handler/baton. */
  svn_stream_t *stream;

} file_read_ctx_t;

typedef struct {
  svn_boolean_t do_checksum;  /* only accumulate checksum if set */
  apr_md5_ctx_t md5_context;  /* accumulating checksum of file contents */
  svn_stream_t *stream;       /* stream to write file contents to */
} file_write_ctx_t;

typedef struct {
  svn_error_t *err;             /* propagate an error out of the reader */

  int checked_type;             /* have we processed ctype yet? */
  ne_content_type ctype;        /* the Content-Type header */

  void *subctx;
} custom_get_ctx_t;

#define POP_SUBDIR(sds) (((subdir_t **)(sds)->elts)[--(sds)->nelts])
#define PUSH_SUBDIR(sds,s) (*(subdir_t **)apr_array_push(sds) = (s))

typedef svn_error_t * (*prop_setter_t)(void *baton,
                                       const char *name,
                                       const svn_string_t *value,
                                       apr_pool_t *pool);

typedef struct {
  /* The baton returned by the editor's open_root/open_dir */
  void *baton;

  /* Should we fetch properties for this directory when the close tag
     is found? */
  svn_boolean_t fetch_props;

  /* The version resource URL for this directory. */
  const char *vsn_url;

  /* A buffer which stores the relative directory name. We also use this
     for temporary construction of relative file names. */
  svn_stringbuf_t *pathbuf;

  /* A subpool.  It's about memory.  Ya dig? */
  apr_pool_t *pool;

} dir_item_t;

typedef struct {
  svn_ra_session_t *ras;

  apr_file_t *tmpfile;

  svn_boolean_t fetch_content;
  svn_boolean_t fetch_props;

  const svn_delta_editor_t *editor;
  void *edit_baton;

  apr_array_header_t *dirs;  /* stack of directory batons/vsn_urls */
#define TOP_DIR(rb) (((dir_item_t *)(rb)->dirs->elts)[(rb)->dirs->nelts - 1])
#define PUSH_BATON(rb,b) (*(void **)apr_array_push((rb)->dirs) = (b))

  /* These items are only valid inside add- and open-file tags! */
  void *file_baton;
  apr_pool_t *file_pool;
  const char *result_checksum; /* hex md5 digest of result; may be null */

  svn_stringbuf_t *namestr;
  svn_stringbuf_t *cpathstr;
  svn_stringbuf_t *href;

  const char *current_wcprop_path;
  svn_boolean_t is_switch;

  /* Named target, or NULL if none.  For example, in 'svn up wc/foo',
     this is "wc/foo", but in 'svn up' it is NULL.  

     The target helps us determine whether a response received from
     the server should be acted on.  Take 'svn up wc/foo': the server
     may send back a new vsn-rsrc-url wcprop for 'wc' (because the
     report had to be anchored there just in case the update deletes
     wc/foo).  While this is correct behavior for the server, the
     client should ignore the new wcprop, because the client knows
     it's not really updating the top level directory. */
  const char *target;

  svn_error_t *err;

} report_baton_t;

static const char report_head[] = "<S:update-report xmlns:S=\""
                                   SVN_XML_NAMESPACE
                                   "\">" DEBUG_CR;
static const char report_tail[] = "</S:update-report>" DEBUG_CR;

static const struct ne_xml_elm report_elements[] =
{
  { SVN_XML_NAMESPACE, "update-report", ELEM_update_report, 0 },
  { SVN_XML_NAMESPACE, "resource-walk", ELEM_resource_walk, 0 },
  { SVN_XML_NAMESPACE, "resource", ELEM_resource, 0 },
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
  { SVN_XML_NAMESPACE, "prop", ELEM_prop, 0 },
  { SVN_DAV_PROP_NS_DAV, "repository-uuid",
    ELEM_repository_uuid, NE_XML_CDATA },

  { SVN_DAV_PROP_NS_DAV, "md5-checksum", ELEM_md5_checksum, NE_XML_CDATA },

  { "DAV:", "version-name", ELEM_version_name, NE_XML_CDATA },
  { "DAV:", "creationdate", ELEM_creationdate, NE_XML_CDATA },
  { "DAV:", "creator-displayname", ELEM_creator_displayname, NE_XML_CDATA },

  { "DAV:", "checked-in", ELEM_checked_in, 0 },
  { "DAV:", "href", NE_ELM_href, NE_XML_CDATA },

  { NULL }
};

/* Elements used in a dated-rev-report response */
static const struct ne_xml_elm drev_report_elements[] =
{
  { SVN_XML_NAMESPACE, "dated-rev-report", ELEM_dated_rev_report, 0 },
  { "DAV:", "version-name", ELEM_version_name, NE_XML_CDATA },
  { NULL }
};

static svn_error_t *simple_store_vsn_url(const char *vsn_url,
                                         void *baton,
                                         prop_setter_t setter,
                                         apr_pool_t *pool)
{
  /* store the version URL as a property */
  SVN_ERR_W( (*setter)(baton, SVN_RA_DAV__LP_VSN_URL, 
                       svn_string_create(vsn_url, pool), pool),
             "could not save the URL of the version resource" );

  return NULL;
}

static svn_error_t *store_vsn_url(const svn_ra_dav_resource_t *rsrc,
                                  void *baton,
                                  prop_setter_t setter,
                                  apr_pool_t *pool)
{
  const svn_string_t *vsn_url = apr_hash_get(rsrc->propset, 
                                             SVN_RA_DAV__PROP_CHECKED_IN, 
                                             APR_HASH_KEY_STRING);
  if (vsn_url == NULL)
    return NULL;

  return simple_store_vsn_url(vsn_url->data, baton, setter, pool);
}

static svn_error_t *get_delta_base(const char **delta_base,
                                   const char *relpath,
                                   svn_ra_get_wc_prop_func_t get_wc_prop,
                                   void *cb_baton,
                                   apr_pool_t *pool)
{
  const svn_string_t *value;

  if (relpath == NULL || get_wc_prop == NULL)
    {
      *delta_base = NULL;
      return SVN_NO_ERROR;
    }

  SVN_ERR( (*get_wc_prop)(cb_baton, relpath, SVN_RA_DAV__LP_VSN_URL,
                          &value, pool) );

  *delta_base = value ? value->data : NULL;
  return SVN_NO_ERROR;
}

/* helper func which maps certain DAV: properties to svn:wc:
   properties.  Used during checkouts and updates.  */
static svn_error_t *set_special_wc_prop (const char *key,
                                         const svn_string_t *val,
                                         prop_setter_t setter,
                                         void *baton,
                                         apr_pool_t *pool)
{  
  const char *name = NULL;

  if (strcmp(key, SVN_RA_DAV__PROP_VERSION_NAME) == 0)
    name = SVN_PROP_ENTRY_COMMITTED_REV;
  else if (strcmp(key, SVN_RA_DAV__PROP_CREATIONDATE) == 0)
    name = SVN_PROP_ENTRY_COMMITTED_DATE;
  else if (strcmp(key, SVN_RA_DAV__PROP_CREATOR_DISPLAYNAME) == 0)
    name = SVN_PROP_ENTRY_LAST_AUTHOR;
  else if (strcmp(key, SVN_RA_DAV__PROP_REPOSITORY_UUID) == 0)
    name = SVN_PROP_ENTRY_UUID;

  /* If we got a name we care about it, call the setter function. */
  if (name)
    SVN_ERR( (*setter)(baton, name, val, pool) );

  return SVN_NO_ERROR;
}


static void add_props(const svn_ra_dav_resource_t *r,
                      prop_setter_t setter,
                      void *baton,
                      apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, r->propset); hi; hi = apr_hash_next(hi))
    {
      const void *vkey;
      void *vval;
      const char *key;
      const svn_string_t *val;

      apr_hash_this(hi, &vkey, NULL, &vval);
      key = vkey;
      val = vval;

#define NSLEN (sizeof(SVN_DAV_PROP_NS_CUSTOM) - 1)
      if (strncmp(key, SVN_DAV_PROP_NS_CUSTOM, NSLEN) == 0)
        {
          /* for props in the 'custom' namespace, we strip the
             namespace and just use whatever name the user gave the
             property. */
          (*setter)(baton, key + NSLEN, val, pool);
          continue;
        }
#undef NSLEN

#define NSLEN (sizeof(SVN_DAV_PROP_NS_SVN) - 1)
      if (strncmp(key, SVN_DAV_PROP_NS_SVN, NSLEN) == 0)
        {
          /* This property is an 'svn:' prop, recognized by client, or
             server, or both.  Convert the URI namespace into normal
             'svn:' prefix again before pushing it at the wc. */
          (*setter)(baton, 
                    apr_pstrcat(pool, SVN_PROP_PREFIX, key + NSLEN, NULL), 
                    val, pool);
        }
#undef NSLEN

      else
        {
          /* If we get here, then we have a property that is neither
             in the 'custom' space, nor in the 'svn' space.  So it
             must be either in the 'network' space or 'DAV:' space.
             The following routine converts a handful of DAV: props
             into 'svn:wc:' or 'svn:entry:' props that libsvn_wc
             wants. */
          set_special_wc_prop (key, val, setter, baton, pool);
        }
    }
}
                      

static svn_error_t * fetch_dirents(svn_ra_session_t *ras,
                                   const char *url,
                                   void *dir_baton,
                                   svn_boolean_t recurse,
                                   apr_array_header_t *subdirs,
                                   apr_array_header_t *files,
                                   prop_setter_t setter,
                                   apr_pool_t *pool)
{
  apr_hash_t *dirents;
  ne_uri parsed_url;
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

  ne_uri_parse(url, &parsed_url);

  for (hi = apr_hash_first(pool, dirents); hi; hi = apr_hash_next(hi))
    {
      void *val;
      svn_ra_dav_resource_t *r;

      apr_hash_this(hi, NULL, NULL, &val);
      r = val;

      if (r->is_collection)
        {
          if (ne_path_compare(parsed_url.path, r->url) == 0)
            {
              /* don't insert "this dir" into the set of subdirs */

              /* store the version URL for this resource */
              SVN_ERR( store_vsn_url(r, dir_baton, setter, pool) );
            }
          else
            {
              if (recurse)
                {
                  subdir_t *subdir = apr_palloc(pool, sizeof(*subdir));

                  subdir->rsrc = r;
                  subdir->parent_baton = dir_baton;

                  PUSH_SUBDIR(subdirs, subdir);
                }
            }
        }
      else
        {
          svn_ra_dav_resource_t **file = apr_array_push(files);
          *file = r;
        }
    }

  ne_uri_free(&parsed_url);

  return SVN_NO_ERROR;
}

static svn_error_t *custom_get_request(ne_session *sess,
                                       const char *url,
                                       const char *relpath,
                                       ne_block_reader reader,
                                       void *subctx,
                                       svn_ra_get_wc_prop_func_t get_wc_prop,
                                       void *cb_baton,
                                       svn_boolean_t use_base,
                                       apr_pool_t *pool)
{
  custom_get_ctx_t cgc = { 0 };
  const char *delta_base;
  ne_request *req;
  ne_decompress *decompress;
  svn_error_t *err;
  int code;
  int decompress_rv;
  svn_ra_ne_session_baton_t *sess_baton =
    ne_get_session_private(sess, SVN_RA_NE_SESSION_ID);

  if (use_base)
    {
      /* See if we can get a version URL for this resource. This will
         refer to what we already have in the working copy, thus we
         can get a diff against this particular resource. */
      SVN_ERR( get_delta_base(&delta_base, relpath,
                              get_wc_prop, cb_baton, pool) );
    }
  else
    {
      delta_base = NULL;
    }

  req = ne_request_create(sess, "GET", url);
  if (req == NULL)
    {
      return svn_error_createf(SVN_ERR_RA_DAV_CREATING_REQUEST, NULL,
                               "Could not create a GET request for '%s'",
                               url);
    }

  /* we want to get the Content-Type so that we can figure out whether
     this is an svndiff or a fulltext */
  ne_add_response_header_handler(req, "Content-Type", ne_content_type_handler,
                                 &cgc.ctype);

  if (delta_base)
    {
      /* The HTTP delta draft uses an If-None-Match header holding an
         entity tag corresponding to the copy we have. It is much more
         natural for us to use a version URL to specify what we have.
         Thus, we want to use the If: header to specify the URL. But
         mod_dav sees all "State-token" items as lock tokens. When we
         get mod_dav updated and the backend APIs expanded, then we
         can switch to using the If: header. For now, use a custom
         header to specify the version resource to use as the base. */
      ne_add_request_header(req, SVN_DAV_DELTA_BASE_HEADER, delta_base);
    }

  /* add in a reader to capture the body of the response. */
  if (sess_baton->compression) {
    decompress = ne_decompress_reader(req, ne_accept_2xx, reader, &cgc);
  }
  else {
    decompress = NULL;
    ne_add_response_body_reader(req, ne_accept_2xx, reader, &cgc);
  }

  /* complete initialization of the body reading context */
  cgc.subctx = subctx;

  /* run the request and get the resulting status code (and svn_error_t) */
  err = svn_ra_dav__request_dispatch(&code, req, sess, "GET", url,
                                     200 /* OK */,
                                     226 /* IM Used */,
                                     pool);

  if (decompress) {
    decompress_rv = ne_decompress_destroy(decompress);
  }
  else {
    decompress_rv = 0;
  }

  /* we no longer need this */
  if (cgc.ctype.value != NULL)
    free(cgc.ctype.value);

  /* if there was an error writing the contents, then return it rather
     than Neon-related errors */
  if (cgc.err)
    {
      if (err)
        svn_error_clear (err);
      return cgc.err;
    }

  if (decompress_rv != 0)
    {
       const char *msg;

       msg = apr_psprintf(pool, "GET request failed for %s", url);
       if (err)
         svn_error_clear (err);
       err = svn_ra_dav__convert_error(sess, msg, decompress_rv);
    }
  
  if (err)
    return err;

  return SVN_NO_ERROR;
}

static void fetch_file_reader(void *userdata, const char *buf, size_t len)
{
  custom_get_ctx_t *cgc = userdata;
  file_read_ctx_t *frc = cgc->subctx;

  if (cgc->err)
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

  if (!cgc->checked_type)
    {

      if (cgc->ctype.type
          && cgc->ctype.subtype
          && !strcmp(cgc->ctype.type, "application") 
          && !strcmp(cgc->ctype.subtype, "vnd.svn-svndiff"))
        {
          /* we are receiving an svndiff. set things up. */
          frc->stream = svn_txdelta_parse_svndiff(frc->handler,
                                                  frc->handler_baton,
                                                  TRUE,
                                                  frc->pool);
        }

      cgc->checked_type = 1;
    }

  if (frc->stream == NULL)
    {
      /* receiving plain text. construct a window for it. */

      svn_txdelta_window_t window = { 0 };
      svn_txdelta_op_t op;
      svn_string_t data;

      data.data = buf;
      data.len = len;

      op.action_code = svn_txdelta_new;
      op.offset = 0;
      op.length = len;

      window.tview_len = len;       /* result will be this long */
      window.num_ops = 1;
      window.ops = &op;
      window.new_data = &data;

      /* We can't really do anything useful if we get an error here.  Pass
         it off to someone who can. */
      cgc->err = (*frc->handler)(&window, frc->handler_baton);
    }
  else
    {
      /* receiving svndiff. feed it to the svndiff parser. */

      apr_size_t written = len;

      cgc->err = svn_stream_write(frc->stream, buf, &written);

      /* ### the svndiff stream parser does not obey svn_stream semantics
         ### in its write handler. it does not output the number of bytes
         ### consumed by the handler. specifically, it may decrement the
         ### number by 4 for the header, then never touch it again. that
         ### makes it appear like an incomplete write.
         ### disable this check for now. the svndiff parser actually does
         ### consume all bytes, all the time.
      */
#if 0
      if (written != len && cgc->err == NULL)
        cgc->err = svn_error_createf(SVN_ERR_INCOMPLETE_DATA, NULL,
                                     "unable to completely write the svndiff "
                                     "data to the parser stream "
                                     "(wrote " APR_SIZE_T_FMT " "
                                     "of " APR_SIZE_T_FMT " bytes)",
                                     written, len);
#endif
    }
}

static svn_error_t *simple_fetch_file(ne_session *sess,
                                      const char *url,
                                      const char *relpath,
                                      svn_boolean_t text_deltas,
                                      void *file_baton,
                                      const char *base_checksum,
                                      const svn_delta_editor_t *editor,
                                      svn_ra_get_wc_prop_func_t get_wc_prop,
                                      void *cb_baton,
                                      apr_pool_t *pool)
{
  file_read_ctx_t frc = { 0 };

  SVN_ERR_W( (*editor->apply_textdelta)(file_baton,
                                        base_checksum,
                                        pool,
                                        &frc.handler,
                                        &frc.handler_baton),
             "could not save file");

  /* If we have no handler for the windows, we can do nothing here. */
  if (! frc.handler)
    return SVN_NO_ERROR;

  /* Only bother with text-deltas if our caller cares. */
  if (! text_deltas)
    {
      SVN_ERR ((*frc.handler)(NULL, frc.handler_baton));
      return SVN_NO_ERROR;
    }

  frc.pool = pool;

  SVN_ERR( custom_get_request(sess, url, relpath,
                              fetch_file_reader, &frc,
                              get_wc_prop, cb_baton,
                              TRUE, pool) );

  /* close the handler, since the file reading completed successfully. */
  SVN_ERR( (*frc.handler)(NULL, frc.handler_baton) );

  return SVN_NO_ERROR;
}

static svn_error_t *fetch_file(ne_session *sess,
                               const svn_ra_dav_resource_t *rsrc,
                               void *dir_baton,
                               const svn_delta_editor_t *editor,
                               const char *edit_path,
                               apr_pool_t *pool)
{
  const char *bc_url = rsrc->url;    /* url in the Baseline Collection */
  svn_error_t *err;
  svn_error_t *err2;
  void *file_baton;
  const svn_string_t *checksum = apr_hash_get(rsrc->propset,
                                              SVN_RA_DAV__PROP_MD5_CHECKSUM,
                                              APR_HASH_KEY_STRING);

  SVN_ERR_W( (*editor->add_file)(edit_path, dir_baton,
                                 NULL, SVN_INVALID_REVNUM,
                                 pool, &file_baton),
             "could not add a file");

  /* fetch_file() is only used for checkout, so we just pass NULL for the
     simple_fetch_file() params related to fetching version URLs (for
     fetching deltas) */
  err = simple_fetch_file(sess, bc_url, NULL, TRUE, file_baton, 
                          NULL, editor, NULL, NULL, pool);
  if (err)
    {
      /* ### do we really need to bother with closing the file_baton? */
      goto error;
    }

  /* Add the properties. */
  add_props(rsrc, editor->change_file_prop, file_baton, pool);

  /* store the version URL as a property */
  err = store_vsn_url(rsrc, file_baton, editor->change_file_prop, pool);

 error:
  err2 = (*editor->close_file)(file_baton,
                               (checksum ? checksum->data : NULL),
                               pool);
  return err ? err : err2;
}

static svn_error_t * begin_checkout(svn_ra_session_t *ras,
                                    svn_revnum_t revision,
                                    const svn_string_t **activity_coll,
                                    svn_revnum_t *target_rev,
                                    const char **bc_root)
{
  apr_pool_t *pool = ras->pool;
  svn_boolean_t is_dir;
  svn_string_t bc_url;
  svn_string_t bc_relative;

  /* ### if REVISION means "get latest", then we can use an expand-property
     ### REPORT rather than two PROPFINDs to reach the baseline-collection */

  SVN_ERR( svn_ra_dav__get_baseline_info(&is_dir, &bc_url, &bc_relative,
                                         target_rev, ras->sess,
                                         ras->root.path, revision, pool) );
  if (!is_dir)
    {
      /* ### eek. what to do? */

      /* ### need an SVN_ERR here */
      return svn_error_create(APR_EGENERAL, NULL,
                              "URL does not identify a collection.");
    }

  /* The root for the checkout is the Baseline Collection root, plus the
     relative location of the public URL to its repository root. */
  *bc_root = svn_path_url_add_component(bc_url.data, bc_relative.data, pool);

  /* Fetch the activity-collection-set from the server, by running an
     OPTIONS request against the bc_url, which we know is guaranteed
     to exist in HEAD.  */
  /* ### also need to fetch/validate the DAV capabilities */
  SVN_ERR( svn_ra_dav__get_activity_collection(activity_coll, ras,
                                               bc_url.data, pool) );
  
  /* ### ben sez: whoa, this is majorly in violation of the deltaV
     rfc.  Our ra_dav module assumes that the activity url is *global*
     to the server, and happily caches it in every single working copy
     directory.  But rfc 3253 (section 13.7) states that OPTIONS
     requests on different resources may return *different* activity
     urls -- heck, they might be urls to complete different hosts! */

  return NULL;
}


/* Helper (neon callback) for svn_ra_dav__get_file. */
static void get_file_reader(void *userdata, const char *buf, size_t len)
{
  custom_get_ctx_t *cgc = userdata;
  apr_size_t wlen;

  /* The stream we want to push data at. */
  file_write_ctx_t *fwc = cgc->subctx; 
  svn_stream_t *stream = fwc->stream;

  if (fwc->do_checksum)
    apr_md5_update(&(fwc->md5_context), buf, len);

  /* Write however many bytes were passed in by neon. */
  wlen = len;
  svn_stream_write(stream, buf, &wlen);

#if 0
  /* Neon's callback won't let us return error.  Joe knows this is a
     bug in his API, so this section can be reactivated someday. */

  if (wlen != len)
    {
      /* Uh oh, didn't write as many bytes as neon gave us. */
      return 
        svn_error_create(SVN_ERR_STREAM_UNEXPECTED_EOF, NULL,
                         "Error writing to svn_stream.");
    }
#endif
      
}


/* minor helper for svn_ra_dav__get_file, of type prop_setter_t */
static svn_error_t * 
add_prop_to_hash (void *baton,
                  const char *name,
                  const svn_string_t *value,
                  apr_pool_t *pool)
{
  apr_hash_t *ht = (apr_hash_t *) baton;
  apr_hash_set(ht, name, APR_HASH_KEY_STRING, value);
  return SVN_NO_ERROR;
}


/* Helper for svn_ra_dav__get_file(), svn_ra_dav__get_dir(), and
   svn_ra_dav__rev_proplist().

   Loop over the properties in RSRC->propset, examining namespaces and
   such to filter Subversion, custom, etc. properties.  

   User-visible props get added to the PROPS hash (alloced in POOL).

   If ADD_ENTRY_PROPS is true, then "special" working copy entry-props
   are added to the hash by set_special_wc_prop().
*/
static svn_error_t *
filter_props (apr_hash_t *props,
              svn_ra_dav_resource_t *rsrc,
              svn_boolean_t add_entry_props,
              apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, rsrc->propset); hi; hi = apr_hash_next(hi)) 
    {
      const void *key;
      const char *name;
      void *val;
      const svn_string_t *value;

      apr_hash_this(hi, &key, NULL, &val);
      name = key;
      value = svn_string_dup(val, pool);

      /* If the property is in the 'custom' namespace, then it's a
         normal user-controlled property coming from the fs.  Just
         strip off this prefix and add to the hash. */
#define NSLEN (sizeof(SVN_DAV_PROP_NS_CUSTOM) - 1)
      if (strncmp(name, SVN_DAV_PROP_NS_CUSTOM, NSLEN) == 0)
        {
          apr_hash_set(props, name + NSLEN, APR_HASH_KEY_STRING, value);
          continue;
        }
#undef NSLEN

      /* If the property is in the 'svn' namespace, then it's a
         normal user-controlled property coming from the fs.  Just
         strip off the URI prefix, add an 'svn:', and add to the hash. */
#define NSLEN (sizeof(SVN_DAV_PROP_NS_SVN) - 1)
      if (strncmp(name, SVN_DAV_PROP_NS_SVN, NSLEN) == 0)
        {
          apr_hash_set(props, 
                       apr_pstrcat(pool, SVN_PROP_PREFIX, name + NSLEN, NULL),
                       APR_HASH_KEY_STRING, 
                       value);
          continue;
        }
#undef NSLEN
      else if (strcmp(name, SVN_RA_DAV__PROP_CHECKED_IN) == 0)
        {
          /* For files, we currently only have one 'wc' prop. */
          apr_hash_set(props, SVN_RA_DAV__LP_VSN_URL,
                       APR_HASH_KEY_STRING, value);
        }
      else
        {
          /* If it's one of the 'entry' props, this func will
             recognize the DAV: name & add it to the hash mapped to a
             new name recognized by libsvn_wc. */
          if (add_entry_props)
            SVN_ERR(set_special_wc_prop (name, value, add_prop_to_hash, 
                                         props, pool));
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_dav__get_file(void *session_baton,
                                  const char *path,
                                  svn_revnum_t revision,
                                  svn_stream_t *stream,
                                  svn_revnum_t *fetched_rev,
                                  apr_hash_t **props,
                                  apr_pool_t *pool)
{
  svn_ra_dav_resource_t *rsrc;
  const char *final_url;
  svn_ra_session_t *ras = (svn_ra_session_t *) session_baton;
  const char *url = svn_path_url_add_component (ras->url, path, pool);

  /* If the revision is invalid (head), then we're done.  Just fetch
     the public URL, because that will always get HEAD. */
  if ((! SVN_IS_VALID_REVNUM(revision)) && (fetched_rev == NULL))
    final_url = url;

  /* If the revision is something specific, we need to create a bc_url. */
  else
    {
      svn_revnum_t got_rev;
      svn_string_t bc_url, bc_relative;

      SVN_ERR (svn_ra_dav__get_baseline_info(NULL,
                                             &bc_url, &bc_relative,
                                             &got_rev,
                                             ras->sess,
                                             url, revision,
                                             pool));
      final_url = svn_path_url_add_component(bc_url.data,
                                             bc_relative.data,
                                             pool);
      if (fetched_rev != NULL)
        *fetched_rev = got_rev;
    }

  if (stream)
    {
      svn_error_t *err;
      const svn_string_t *expected_checksum = NULL;
      file_write_ctx_t fwc;
      ne_propname md5_propname = { SVN_DAV_PROP_NS_DAV, "md5-checksum" };
      unsigned char digest[MD5_DIGESTSIZE];
      const char *hex_digest;

      /* Only request a checksum if we're getting the file contents. */
      /* ### We should arrange for the checksum to be returned in the
         svn_ra_dav__get_baseline_info() call above; that will prevent
         the extra round trip, at least some of the time. */
      err = svn_ra_dav__get_one_prop(&expected_checksum,
                                     ras->sess,
                                     final_url,
                                     NULL,
                                     &md5_propname,
                                     pool);

      /* Older servers don't serve this prop, but that's okay. */
      /* ### temporary hack for 0.17. if the server doesn't have the prop,
         ### then __get_one_prop returns an empty string. deal with it.  */
      if ((err && (err->apr_err == SVN_ERR_RA_DAV_PROPS_NOT_FOUND))
          || (expected_checksum && (*expected_checksum->data == '\0')))
        {
          fwc.do_checksum = FALSE;
          svn_error_clear(err);
        }
      else if (err)
        return err;
      else
        fwc.do_checksum = TRUE;

      fwc.stream = stream;

      if (fwc.do_checksum)
        apr_md5_init(&(fwc.md5_context));

      /* Fetch the file, shoving it at the provided stream. */
      SVN_ERR( custom_get_request(ras->sess, final_url, path,
                                  get_file_reader, &fwc,
                                  ras->callbacks->get_wc_prop,
                                  ras->callback_baton,
                                  FALSE, pool) );

      if (fwc.do_checksum)
        {
          apr_md5_final(digest, &(fwc.md5_context));
          hex_digest = svn_md5_digest_to_cstring(digest, pool);

          if (strcmp (hex_digest, expected_checksum->data) != 0)
            return svn_error_createf
              (SVN_ERR_CHECKSUM_MISMATCH, NULL,
               "svn_ra_dav__get_file: checksum mismatch for '%s':\n"
               "   expected checksum:  %s\n"
               "   actual checksum:    %s\n",
               path, expected_checksum->data, hex_digest);
        }
    }

  if (props)
    {
      SVN_ERR( svn_ra_dav__get_props_resource(&rsrc, ras->sess, final_url, 
                                              NULL, NULL /* all props */, 
                                              pool) ); 
      *props = apr_hash_make(pool);
      SVN_ERR (filter_props (*props, rsrc, TRUE, pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *svn_ra_dav__get_dir(void *session_baton,
                                 const char *path,
                                 svn_revnum_t revision,
                                 apr_hash_t **dirents,
                                 svn_revnum_t *fetched_rev,
                                 apr_hash_t **props,
                                 apr_pool_t *pool)
{
  svn_ra_dav_resource_t *rsrc;
  apr_hash_index_t *hi;
  int len;
  apr_hash_t *resources;
  const char *final_url;
  char *stripped_final_url;
  svn_ra_session_t *ras = (svn_ra_session_t *) session_baton;
  const char *url = svn_path_url_add_component (ras->url, path, pool);

  /* If the revision is invalid (head), then we're done.  Just fetch
     the public URL, because that will always get HEAD. */
  if ((! SVN_IS_VALID_REVNUM(revision)) && (fetched_rev == NULL))
    final_url = url;

  /* If the revision is something specific, we need to create a bc_url. */
  else
    {
      svn_revnum_t got_rev;
      svn_string_t bc_url, bc_relative;

      SVN_ERR (svn_ra_dav__get_baseline_info(NULL,
                                             &bc_url, &bc_relative,
                                             &got_rev,
                                             ras->sess,
                                             url, revision,
                                             pool));
      final_url = svn_path_url_add_component(bc_url.data,
                                             bc_relative.data,
                                             pool);
      if (fetched_rev != NULL)
        *fetched_rev = got_rev;
    }

  if (dirents)
    {
      /* Just like Nautilus, Cadaver, or any other browser, we do a
         PROPFIND on the directory of depth 1. */
      SVN_ERR( svn_ra_dav__get_props(&resources, ras->sess,
                                     final_url, NE_DEPTH_ONE,
                                     NULL, NULL /* all props */, pool) );
      
      /* Clean up any trailing slashes on final_url, creating
         stripped_final_url */
      stripped_final_url = apr_pstrdup(pool, final_url);
      len = strlen(final_url);
      if (len > 1 && final_url[len - 1] == '/')
        stripped_final_url[len - 1] = '\0';
      
      /* Now we have a hash that maps a bunch of url children to resource
         objects.  Each resource object contains the properties of the
         child.   Parse these resources into svn_dirent_t structs. */
      *dirents = apr_hash_make (pool);
      for (hi = apr_hash_first (pool, resources);
           hi;
           hi = apr_hash_next (hi))
        {
          const void *key;
          void *val;
          const char *childname;
          svn_ra_dav_resource_t *resource;
          const svn_string_t *propval;
          apr_hash_index_t *h;
          svn_dirent_t *entry;
          
          apr_hash_this (hi, &key, NULL, &val);
          childname =  key;
          resource = val;
          
          /* Skip the effective '.' entry that comes back from NE_DEPTH_ONE */
          if (strcmp(resource->url, stripped_final_url) == 0)
            continue;
          
          entry = apr_pcalloc (pool, sizeof(*entry));
          
          /* node kind */
          entry->kind = resource->is_collection ? svn_node_dir : svn_node_file;
          
          /* size */
          propval = apr_hash_get(resource->propset,
                                 SVN_RA_DAV__PROP_GETCONTENTLENGTH,
                                 APR_HASH_KEY_STRING);
          if (propval == NULL)
            entry->size = 0;
          else
            entry->size = apr_atoui64(propval->data);
          
          /* does this resource contain any 'svn' or 'custom' properties,
             i.e.  ones actually created and set by the user? */
          for (h = apr_hash_first (pool, resource->propset);
               h; h = apr_hash_next (h))
            {
              const void *kkey;
              void *vval;
              apr_hash_this (h, &kkey, NULL, &vval);
              
              if (strncmp((const char *)kkey, SVN_DAV_PROP_NS_CUSTOM,
                          sizeof(SVN_DAV_PROP_NS_CUSTOM)) == 0)
                entry->has_props = TRUE;
              
              else if (strncmp((const char *)kkey, SVN_DAV_PROP_NS_SVN,
                               sizeof(SVN_DAV_PROP_NS_SVN)) == 0)
                entry->has_props = TRUE;
            }
          
          /* created_rev & friends */
          propval = apr_hash_get(resource->propset,
                                 SVN_RA_DAV__PROP_VERSION_NAME,
                                 APR_HASH_KEY_STRING);
          if (propval != NULL)
            entry->created_rev = SVN_STR_TO_REV(propval->data);
          
          propval = apr_hash_get(resource->propset,
                                 SVN_RA_DAV__PROP_CREATIONDATE,
                                 APR_HASH_KEY_STRING);
          if (propval != NULL)
            SVN_ERR( svn_time_from_cstring(&(entry->time),
                                           propval->data, pool) );
          
          propval = apr_hash_get(resource->propset,
                                 SVN_RA_DAV__PROP_CREATOR_DISPLAYNAME,
                                 APR_HASH_KEY_STRING);
          if (propval != NULL)
            entry->last_author = propval->data;
          
          apr_hash_set(*dirents, svn_path_basename(childname, pool),
                       APR_HASH_KEY_STRING, entry);
        }
    }

  if (props)                    
    {
      SVN_ERR( svn_ra_dav__get_props_resource(&rsrc, ras->sess, final_url, 
                                              NULL, NULL /* all props */, 
                                              pool) ); 

      *props = apr_hash_make(pool);
      SVN_ERR (filter_props (*props, rsrc, TRUE, pool));
    }

  return SVN_NO_ERROR;
}



svn_error_t * svn_ra_dav__do_checkout(void *session_baton,
                                      svn_revnum_t revision,
                                      svn_boolean_t recurse,
                                      const svn_delta_editor_t *editor,
                                      void *edit_baton,
                                      apr_pool_t *pool)
{
  svn_ra_session_t *ras = session_baton;
  svn_error_t *err;
  void *root_baton;
  const svn_string_t *activity_coll;
  svn_revnum_t target_rev;
  const char *bc_root;
  subdir_t *subdir;
  apr_array_header_t *subdirs;  /* subdirs to scan (subdir_t *) */
  apr_array_header_t *files;    /* files to grab (svn_ra_dav_resource_t *) */
  svn_stringbuf_t *edit_path 
    = svn_stringbuf_create ("", pool); /* telescopic path */
  apr_pool_t *subpool;

  /* ### use quick_wrap rather than SVN_ERR on some of these? */

  /* this subpool will be used during various iteration loops, and cleared
     each time. long-lived stuff should go into ras->pool. */
  subpool = svn_pool_create(pool);

  /* begin the checkout process by fetching some basic information */
  SVN_ERR( begin_checkout(ras, revision, &activity_coll, &target_rev,
                          &bc_root) );

  /* all the files we checkout will have TARGET_REV for the revision */
  SVN_ERR( (*editor->set_target_revision)(edit_baton, target_rev, pool) );

  /* In the checkout case, we don't really have a base revision, so
     pass SVN_IGNORED_REVNUM. */
  SVN_ERR( (*editor->open_root)(edit_baton, SVN_IGNORED_REVNUM,
                                pool, &root_baton) );

  /* store the subdirs into an array for processing, rather than recursing */
  subdirs = apr_array_make(pool, 5, sizeof(subdir_t *));
  files = apr_array_make(pool, 10, sizeof(svn_ra_dav_resource_t *));

  /* Build a directory resource for the root. We'll pop this off and fetch
     the information for it. */
  subdir = apr_palloc(pool, sizeof(*subdir));
  subdir->parent_baton = root_baton;

  subdir->rsrc = apr_pcalloc(pool, sizeof(*subdir->rsrc));
  subdir->rsrc->url = bc_root;

  PUSH_SUBDIR(subdirs, subdir);
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
          svn_path_remove_component(edit_path);
          err = (*editor->close_directory) (parent_baton, subpool);
          if (err)
            return svn_error_quick_wrap(err, "could not finish directory");

          if (subdirs->nelts == 0)
            {
              /* Finish the edit */
              SVN_ERR( ((*editor->close_edit) (edit_baton, subpool)) );

              /* Store auth info if necessary */
              SVN_ERR( (svn_ra_dav__maybe_store_auth_info (ras)) );

              return SVN_NO_ERROR;
            }
        }

      if (strlen(url) > strlen(bc_root))
        {
          const char *comp;
          comp = svn_path_uri_decode(svn_path_basename(url, subpool), 
                                     subpool);
          svn_path_add_component(edit_path, comp);
                                     
          SVN_ERR_W( (*editor->add_directory) (edit_path->data, parent_baton,
                                               NULL, SVN_INVALID_REVNUM,
                                               pool, &this_baton),
                     "could not add directory");
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
                                              subpool));
      add_props(rsrc, editor->change_dir_prop, this_baton, subpool);

      /* finished processing the directory. clear out the gunk. */
      svn_pool_clear(subpool);

      /* add a sentinel. this will be used to signal a close_directory
         for this directory's baton. */
      subdir = apr_pcalloc(pool, sizeof(*subdir));
      subdir->parent_baton = this_baton;
      PUSH_SUBDIR(subdirs, subdir);

      err = fetch_dirents(ras, url, this_baton, recurse, subdirs, files,
                          editor->change_dir_prop, pool);
      if (err)
        return svn_error_quick_wrap(err, "could not fetch directory entries");

      /* ### use set_wc_dir_prop() */

      /* store the activity URL as a property */
      /* ### should we close the dir batons before returning?? */
      SVN_ERR_W( (*editor->change_dir_prop)(this_baton, 
                                            SVN_RA_DAV__LP_ACTIVITY_COLL,
                                            activity_coll, pool),
                 "could not save the URL to indicate "
                 "where to create activities");

      /* process each of the files that were found */
      for (i = files->nelts; i--; )
        {
          apr_size_t edit_len = edit_path->len;
          const char *comp;
          rsrc = ((svn_ra_dav_resource_t **)files->elts)[i];
          comp = svn_path_uri_decode(svn_path_basename(rsrc->url, subpool), 
                                     subpool);
          svn_path_add_component(edit_path, comp);

          /* ### should we close the dir batons first? */
          SVN_ERR_W( fetch_file(ras->sess, rsrc, this_baton,
                                editor, edit_path->data, subpool),
                     "could not checkout a file");
          svn_stringbuf_chop(edit_path, edit_path->len - edit_len);

          /* trash the per-file stuff. */
          svn_pool_clear(subpool);
        }

      /* reset the list of files */
      files->nelts = 0;

    } while (subdirs->nelts > 0);

  /* ### should never reach??? */

  /* Finish the edit */
  SVN_ERR( ((*editor->close_edit) (edit_baton, pool)) );

  /* Store auth info if necessary */
  SVN_ERR( (svn_ra_dav__maybe_store_auth_info (ras)) );

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------- */

svn_error_t *svn_ra_dav__get_latest_revnum(void *session_baton,
                                           svn_revnum_t *latest_revnum,
                                           apr_pool_t *pool)
{
  svn_ra_session_t *ras = session_baton;

  /* ### should we perform an OPTIONS to validate the server we're about
     ### to talk to? */

  /* we don't need any of the baseline URLs and stuff, but this does
     give us the latest revision number */
  SVN_ERR( svn_ra_dav__get_baseline_info(NULL, NULL, NULL, latest_revnum,
                                         ras->sess, ras->root.path,
                                         SVN_INVALID_REVNUM, pool) );

  SVN_ERR( svn_ra_dav__maybe_store_auth_info(ras) );

  return NULL;
}


/* -------------------------------------------------------------------------
**
** DATED REV REPORT HANDLING
**
** DeltaV provides no mechanism for mapping a date to a revision, so
** we use a custom report, S:dated-rev-report.  The request contains a
** DAV:creationdate element giving the requested date; the response
** contains a DAV:version-name element giving the most recent revision
** as of that date.
**
** Since this report is so simple, we don't bother with validation or
** baton structures or anything; we just set the revision number in
** the end-element handler for DAV:version-name.
*/

/* This implements the `ne_xml_validate_cb' prototype. */
static int drev_validate_element(void *userdata, ne_xml_elmid parent,
                                 ne_xml_elmid child)
{
  return NE_XML_VALID;
}

/* This implements the `ne_xml_startelm_cb' prototype. */
static int drev_start_element(void *userdata, const struct ne_xml_elm *elm,
                              const char **atts)
{
  return 0;
}

/* This implements the `ne_xml_endelm_cb' prototype. */
static int drev_end_element(void *userdata, const struct ne_xml_elm *elm,
                            const char *cdata)
{
  if (elm->id == ELEM_version_name)
    *((svn_revnum_t *) userdata) = SVN_STR_TO_REV(cdata);

  return 0;
}

svn_error_t *svn_ra_dav__get_dated_revision (void *session_baton,
                                             svn_revnum_t *revision,
                                             apr_time_t timestamp,
                                             apr_pool_t *pool)
{
  svn_ra_session_t *ras = session_baton;
  const char *body;
  svn_error_t *err;

  body = apr_psprintf(pool,
                      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                      "<S:dated-rev-report xmlns:S=\"" SVN_XML_NAMESPACE "\" "
                      "xmlns:D=\"DAV:\">"
                      "<D:creationdate>%s</D:creationdate>"
                      "</S:dated-rev-report>",
                      svn_time_to_cstring(timestamp, pool));

  *revision = SVN_INVALID_REVNUM;
  err = svn_ra_dav__parsed_request(ras->sess, "REPORT",
                                   ras->root.path, body, NULL, NULL,
                                   drev_report_elements,
                                   drev_validate_element,
                                   drev_start_element, drev_end_element,
                                   revision, NULL, pool);
  if (err && err->apr_err == SVN_ERR_UNSUPPORTED_FEATURE)
    return svn_error_quick_wrap(err, "Server does not support date-based "
                                "operations.");
  else if (err)
    return err;

  if (*revision == SVN_INVALID_REVNUM)
    return svn_error_create(SVN_ERR_INCOMPLETE_DATA, NULL,
                            "Invalid server response to dated-rev request.");

  return SVN_NO_ERROR;
}


/* Populate the members of ne_propname structure *PROP for the
   Subversion property NAME.  */
static void
make_ne_propname (ne_propname *prop,
                  const char *name)
{
  if (strncmp(name, SVN_PROP_PREFIX, (sizeof(SVN_PROP_PREFIX) - 1)) == 0)
    {
      prop->nspace = SVN_DAV_PROP_NS_SVN;
      prop->name = name + sizeof(SVN_PROP_PREFIX) - 1;
    }
  else
    {
      prop->nspace = SVN_DAV_PROP_NS_CUSTOM;
      prop->name = name;
    }
}

svn_error_t *svn_ra_dav__change_rev_prop (void *session_baton,
                                          svn_revnum_t rev,
                                          const char *name,
                                          const svn_string_t *value,
                                          apr_pool_t *pool)
{
  svn_ra_session_t *ras = session_baton;
  svn_ra_dav_resource_t *baseline;
  apr_hash_t *prop_changes = NULL;
  apr_array_header_t *prop_deletes = NULL;
  static const ne_propname wanted_props[] =
    {
      { "DAV:", "auto-version" },
      { NULL }
    };

  /* Main objective: do a PROPPATCH (allprops) on a baseline object */  

  /* ### A Word From Our Sponsor:  see issue #916.

     Be it heretofore known that this Subversion behavior is
     officially in violation of WebDAV/DeltaV.  DeltaV has *no*
     concept of unversioned properties, anywhere.  If you proppatch
     something, some new version of *something* is created.

     In particular, we've decided that a 'baseline' maps to an svn
     revision; if we attempted to proppatch a baseline, a *normal*
     DeltaV server would do an auto-checkout, patch the working
     baseline, auto-checkin, and create a new baseline.  But
     mod_dav_svn just changes the baseline destructively.
  */

  /* Get the baseline resource. */
  SVN_ERR (svn_ra_dav__get_baseline_props(NULL, &baseline,
                                          ras->sess, 
                                          ras->url,
                                          rev,
                                          wanted_props, /* DAV:auto-version */
                                          pool));

  /* ### TODO: if we got back some value for the baseline's
         'DAV:auto-version' property, interpret it.  We *don't* want
         to attempt the PROPPATCH if the deltaV server is going to do
         auto-versioning and create a new baseline! */

  if (value)
    {
      prop_changes = apr_hash_make(pool);
      apr_hash_set(prop_changes, name, APR_HASH_KEY_STRING, value);
    }
  else
    {
      prop_deletes = apr_array_make(pool, 1, sizeof(const char *));
      (*((const char **) apr_array_push(prop_deletes))) = name;
    }

  return svn_ra_dav__do_proppatch(ras, baseline->url, prop_changes,
                                  prop_deletes, pool);
}


svn_error_t *svn_ra_dav__rev_proplist (void *session_baton,
                                       svn_revnum_t rev,
                                       apr_hash_t **props,
                                       apr_pool_t *pool)
{
  svn_ra_session_t *ras = session_baton;
  svn_ra_dav_resource_t *baseline;

  *props = apr_hash_make (pool);

  /* Main objective: do a PROPFIND (allprops) on a baseline object */  
  SVN_ERR (svn_ra_dav__get_baseline_props(NULL, &baseline,
                                          ras->sess, 
                                          ras->url,
                                          rev,
                                          NULL, /* get ALL properties */
                                          pool));

  /* Build a new property hash, based on the one in the baseline
     resource.  In particular, convert the xml-property-namespaces
     into ones that the client understands.  Strip away the DAV:
     liveprops as well. */
  SVN_ERR (filter_props(*props, baseline, FALSE, pool));

  return SVN_NO_ERROR;
}


svn_error_t *svn_ra_dav__rev_prop (void *session_baton,
                                   svn_revnum_t rev,
                                   const char *name,
                                   svn_string_t **value,
                                   apr_pool_t *pool)
{
  svn_ra_session_t *ras = session_baton;
  svn_ra_dav_resource_t *baseline;
  apr_hash_t *filtered_props;

  /* E-Z initialization */
  ne_propname wanted_props[] =
    {
      { "", "" },
      { NULL }
    };

  /* Decide on the namespace and propname for XML marshalling. */
  make_ne_propname(&(wanted_props[0]), name);

  /* Main objective: do a PROPFIND (allprops) on a baseline object */  
  SVN_ERR (svn_ra_dav__get_baseline_props(NULL, &baseline,
                                          ras->sess, 
                                          ras->url,
                                          rev,
                                          wanted_props,
                                          pool));

  /* Build a new property hash, based on the one in the baseline
     resource.  In particular, convert the xml-property-namespaces
     into ones that the client understands.  Strip away the DAV:
     liveprops as well. */
  filtered_props = apr_hash_make(pool);
  SVN_ERR (filter_props (filtered_props, baseline, FALSE, pool));

  *value = apr_hash_get(filtered_props, name, APR_HASH_KEY_STRING);

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
          || child == ELEM_open_directory
          || child == ELEM_resource_walk)
        return NE_XML_VALID;
      else
        return NE_XML_INVALID;

    case ELEM_resource_walk:
      if (child == ELEM_resource)
        return NE_XML_VALID;
      else
        return NE_XML_INVALID;

    case ELEM_resource:
      if (child == ELEM_checked_in)
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
          || child == ELEM_prop
          || child == ELEM_checked_in)
        return NE_XML_VALID;
      else
        return NE_XML_INVALID;

    case ELEM_add_directory:
      if (child == ELEM_add_directory
          || child == ELEM_add_file
          || child == ELEM_prop
          || child == ELEM_checked_in)
        return NE_XML_VALID;
      else
        return NE_XML_INVALID;

    case ELEM_open_file:
      if (child == ELEM_checked_in
          || child == ELEM_fetch_file
          || child == ELEM_prop
          || child == ELEM_fetch_props
          || child == ELEM_remove_prop)
        return NE_XML_VALID;
      else
        return NE_XML_INVALID;

    case ELEM_add_file:
      if (child == ELEM_checked_in
          || child == ELEM_prop)
        return NE_XML_VALID;
      else
        return NE_XML_INVALID;

    case ELEM_checked_in:
      if (child == NE_ELM_href)
        return NE_XML_VALID;
      else
        return NE_XML_INVALID;

    case ELEM_prop:
      /*      if (child == ELEM_version_name
              || child == ELEM_creationdate
              || child == ELEM_creator_displayname
              || child == ELEM_md5_checksum
              || child == ELEM_repository_uuid
              || child == ELEM_remove_prop)
              return NE_XML_VALID;
              else
              return NE_XML_DECLINE;
      */
      /* ### TODO:  someday uncomment the block above, and make the
         else clause return NE_XML_IGNORE.  But first, neon needs to
         define that value.  :-) */
      return NE_XML_VALID;

    default:
      return NE_XML_DECLINE;
    }

  /* NOTREACHED */
}

static const char *get_attr(const char **atts, const char *which)
{
  for (; atts && *atts; atts += 2)
    if (strcmp(*atts, which) == 0)
      return atts[1];
  return NULL;
}

static void push_dir(report_baton_t *rb, 
                     void *baton, 
                     svn_stringbuf_t *pathbuf,
                     apr_pool_t *pool)
{
  dir_item_t *di = (dir_item_t *)apr_array_push(rb->dirs);

  memset(di, 0, sizeof(*di));
  di->baton = baton;
  di->pathbuf = pathbuf;
  di->pool = pool;
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
  dir_item_t *parent_dir;
  void *new_dir_baton;
  svn_stringbuf_t *pathbuf;
  apr_pool_t *subpool;
  const char *base_checksum = NULL;

  switch (elm->id)
    {
    case ELEM_target_revision:
      att = get_attr(atts, "rev");
      /* ### verify we got it. punt on error. */

      CHKERR( (*rb->editor->set_target_revision)(rb->edit_baton,
                                                 SVN_STR_TO_REV(att),
                                                 rb->ras->pool) );
      break;

    case ELEM_resource:
      att = get_attr(atts, "path");
      /* ### verify we got it. punt on error. */
      rb->current_wcprop_path = apr_pstrdup(rb->ras->pool, att);
      break;

    case ELEM_open_directory:
      att = get_attr(atts, "rev");
      /* ### verify we got it. punt on error. */
      base = SVN_STR_TO_REV(att);
      if (rb->dirs->nelts == 0)
        {
          /* pathbuf has to live for the whole edit! */
          pathbuf = svn_stringbuf_create("", rb->ras->pool);

          /* During switch operations, we need to invalidate the
             tree's version resource URLs in case something goes
             wrong. */
          if (rb->is_switch && rb->ras->callbacks->invalidate_wc_props)
            {
              CHKERR( rb->ras->callbacks->invalidate_wc_props
                      (rb->ras->callback_baton,
                       "", SVN_RA_DAV__LP_VSN_URL, rb->ras->pool) );
            }

          subpool = svn_pool_create(rb->ras->pool);
          CHKERR( (*rb->editor->open_root)(rb->edit_baton, base,
                                           subpool, &new_dir_baton) );

          /* push the new baton onto the directory baton stack */
          push_dir(rb, new_dir_baton, pathbuf, subpool);
        }
      else
        {
          name = get_attr(atts, "name");
          /* ### verify we got it. punt on error. */
          svn_stringbuf_set(rb->namestr, name);

          parent_dir = &TOP_DIR(rb);
          subpool = svn_pool_create(parent_dir->pool);

          pathbuf = svn_stringbuf_dup(parent_dir->pathbuf, subpool);
          svn_path_add_component(pathbuf, rb->namestr->data);

          CHKERR( (*rb->editor->open_directory)(pathbuf->data,
                                                parent_dir->baton, base,
                                                subpool, 
                                                &new_dir_baton) );

          /* push the new baton onto the directory baton stack */
          push_dir(rb, new_dir_baton, pathbuf, subpool);
        }

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
          crev = SVN_STR_TO_REV(att);
        }

      parent_dir = &TOP_DIR(rb);
      subpool = svn_pool_create(parent_dir->pool);

      pathbuf = svn_stringbuf_dup(parent_dir->pathbuf, subpool);
      svn_path_add_component(pathbuf, rb->namestr->data);

      CHKERR( (*rb->editor->add_directory)(pathbuf->data, parent_dir->baton,
                                           cpath ? cpath->data : NULL, 
                                           crev, subpool,
                                           &new_dir_baton) );

      /* push the new baton onto the directory baton stack */
      push_dir(rb, new_dir_baton, pathbuf, subpool);

      /* Property fetching is implied in addition. */
      TOP_DIR(rb).fetch_props = TRUE;
      break;

    case ELEM_open_file:
      att = get_attr(atts, "rev");
      /* ### verify we got it. punt on error. */
      base = SVN_STR_TO_REV(att);

      name = get_attr(atts, "name");
      /* ### verify we got it. punt on error. */
      svn_stringbuf_set(rb->namestr, name);

      parent_dir = &TOP_DIR(rb);
      rb->file_pool = svn_pool_create(rb->ras->pool);
      rb->result_checksum = NULL;

      /* Add this file's name into the directory's path buffer. It will be
         removed in end_element() */
      svn_path_add_component(parent_dir->pathbuf, rb->namestr->data);

      CHKERR( (*rb->editor->open_file)(parent_dir->pathbuf->data, 
                                       parent_dir->baton, base,
                                       rb->file_pool,
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
          crev = SVN_STR_TO_REV(att);
        }

      parent_dir = &TOP_DIR(rb);
      rb->file_pool = svn_pool_create(rb->ras->pool);
      rb->result_checksum = NULL;

      /* Add this file's name into the directory's path buffer. It will be
         removed in end_element() */
      svn_path_add_component(parent_dir->pathbuf, rb->namestr->data);

      CHKERR( (*rb->editor->add_file)(parent_dir->pathbuf->data,
                                      parent_dir->baton,
                                      cpath ? cpath->data : NULL, 
                                      crev, rb->file_pool,
                                      &rb->file_baton) );

      /* Property fetching is implied in addition. */
      rb->fetch_props = TRUE;

      break;

    case ELEM_remove_prop:
      name = get_attr(atts, "name");
      /* ### verify we got it. punt on error. */
      svn_stringbuf_set(rb->namestr, name);

      /* Removing a prop.  */
      if (rb->file_baton == NULL)
        rb->editor->change_dir_prop(TOP_DIR(rb).baton, rb->namestr->data, 
                                    NULL, TOP_DIR(rb).pool);
      else
        rb->editor->change_file_prop(rb->file_baton, rb->namestr->data, 
                                     NULL, rb->file_pool);
      break;
      
    case ELEM_fetch_props:
      if (!rb->fetch_content)
        {
          /* If this is just a status check, the specifics of the
             property change are uninteresting.  Simply call our
             editor function with bogus data so it registers a
             property mod. */
          svn_stringbuf_set(rb->namestr, SVN_PROP_PREFIX "BOGOSITY");

          if (rb->file_baton == NULL)
            rb->editor->change_dir_prop(TOP_DIR(rb).baton, rb->namestr->data, 
                                        NULL, TOP_DIR(rb).pool);
          else
            rb->editor->change_file_prop(rb->file_baton, rb->namestr->data, 
                                         NULL, rb->file_pool);
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
      base_checksum = get_attr(atts, "base-checksum");
      rb->result_checksum = NULL;

      /* assert: rb->href->len > 0 */
      CHKERR( simple_fetch_file(rb->ras->sess2, 
                                rb->href->data,
                                TOP_DIR(rb).pathbuf->data,
                                rb->fetch_content,
                                rb->file_baton,
                                base_checksum,
                                rb->editor,
                                rb->ras->callbacks->get_wc_prop,
                                rb->ras->callback_baton,
                                rb->file_pool) );
      break;

    case ELEM_delete_entry:
      name = get_attr(atts, "name");
      /* ### verify we got it. punt on error. */
      svn_stringbuf_set(rb->namestr, name);

      parent_dir = &TOP_DIR(rb);
      subpool = parent_dir->pool;

      pathbuf = svn_stringbuf_dup(parent_dir->pathbuf, subpool);
      svn_path_add_component(pathbuf, rb->namestr->data);

      CHKERR( (*rb->editor->delete_entry)(pathbuf->data,
                                          SVN_INVALID_REVNUM,
                                          TOP_DIR(rb).baton,
                                          subpool) );
      break;

    default:
      break;
    }

  return 0;
}


static svn_error_t *
add_node_props (report_baton_t *rb, apr_pool_t *pool)
{
  svn_ra_dav_resource_t *rsrc;

  /* Do nothing if we aren't fetching content.  */
  if (!rb->fetch_content)
    return SVN_NO_ERROR;

  if (rb->file_baton)
    {
      if (! rb->fetch_props)
        return SVN_NO_ERROR;

      /* Fetch file props. */
      SVN_ERR(svn_ra_dav__get_props_resource(&rsrc,
                                             rb->ras->sess2,
                                             rb->href->data,
                                             NULL,
                                             NULL,
                                             pool));
      add_props(rsrc, 
                rb->editor->change_file_prop, 
                rb->file_baton,
                pool);
    }
  else
    {
      if (! TOP_DIR(rb).fetch_props)
        return SVN_NO_ERROR;

      /* Fetch dir props. */
      SVN_ERR(svn_ra_dav__get_props_resource(&rsrc,
                                             rb->ras->sess2,
                                             TOP_DIR(rb).vsn_url,
                                             NULL,
                                             NULL,
                                             pool));
      add_props(rsrc, 
                rb->editor->change_dir_prop, 
                TOP_DIR(rb).baton, 
                pool);
    }
    
  return SVN_NO_ERROR;
}

/* This implements the `ne_xml_endelm_cb' prototype. */
static int end_element(void *userdata, 
                       const struct ne_xml_elm *elm,
                       const char *cdata)
{
  report_baton_t *rb = userdata;
  const svn_delta_editor_t *editor = rb->editor;

  switch (elm->id)
    {
    case ELEM_resource:
      rb->current_wcprop_path = NULL;
      break;

    case ELEM_add_directory:
    case ELEM_open_directory:

      /* fetch node props, unless this is the top dir and the real
         target of the operation is not the top dir. */
      if (! ((rb->dirs->nelts == 1) && rb->target))
        CHKERR( add_node_props(rb, TOP_DIR(rb).pool));

      /* Close the directory on top of the stack, and pop it.  Also,
         destroy the subpool used exclusive by this directory and its
         children.  */
      CHKERR( (*rb->editor->close_directory)(TOP_DIR(rb).baton, 
                                             TOP_DIR(rb).pool) );
      svn_pool_destroy(TOP_DIR(rb).pool);
      apr_array_pop(rb->dirs);

      /* If we just popped the last directory from the stack, we can
         close the edit. */
      if (rb->dirs->nelts == 0)
        {
          /* we got the whole HTTP response thing done. now wrap up the update
             process with a close_edit call. */
          CHKERR( (*rb->editor->close_edit)(rb->edit_baton, rb->ras->pool) );
          rb->edit_baton = NULL;
        }
      break;

    case ELEM_add_file:
      /* we wait until the close element to do the work. this allows us to
         retrieve the href before fetching. */

      /* fetch file */
      CHKERR( simple_fetch_file(rb->ras->sess2,
                                rb->href->data,
                                TOP_DIR(rb).pathbuf->data,
                                rb->fetch_content,
                                rb->file_baton,
                                NULL,  /* no base checksum in an add */
                                rb->editor,
                                rb->ras->callbacks->get_wc_prop,
                                rb->ras->callback_baton,
                                rb->file_pool) );

      /* fetch node props as necessary. */
      CHKERR( add_node_props(rb, rb->file_pool) );

      /* close the file and mark that we are no longer operating on a file */
      CHKERR( (*rb->editor->close_file)(rb->file_baton,
                                        rb->result_checksum,
                                        rb->file_pool) );
      rb->file_baton = NULL;

      /* Yank this file out of the directory's path buffer. */
      svn_path_remove_component(TOP_DIR(rb).pathbuf);
      svn_pool_destroy(rb->file_pool);
      rb->file_pool = NULL;
      break;

    case ELEM_open_file:
      /* fetch node props as necessary. */
      CHKERR( add_node_props(rb, rb->file_pool) );

      /* close the file and mark that we are no longer operating on a file */
      CHKERR( (*rb->editor->close_file)(rb->file_baton,
                                        rb->result_checksum,
                                        rb->file_pool) );
      rb->file_baton = NULL;

      /* Yank this file out of the directory's path buffer. */
      svn_path_remove_component(TOP_DIR(rb).pathbuf);
      svn_pool_destroy(rb->file_pool);
      rb->file_pool = NULL;
      break;

    case NE_ELM_href:
      /* do nothing if we aren't fetching content. */
      if (!rb->fetch_content)
        break;

      /* record the href that we just found */
      svn_ra_dav__copy_href(rb->href, cdata);
      
      /* if we're within a <resource> tag, then just call the generic
         RA set_wcprop_callback directly;  no need to use the
         update-editor.  */
      if (rb->current_wcprop_path != NULL)
        {
          svn_string_t href_val;
          href_val.data = rb->href->data;
          href_val.len = rb->href->len;

          if (rb->ras->callbacks->set_wc_prop != NULL)
            CHKERR( rb->ras->callbacks->set_wc_prop(rb->ras->callback_baton,
                                                    rb->current_wcprop_path,
                                                    SVN_RA_DAV__LP_VSN_URL,
                                                    &href_val,
                                                    rb->ras->pool) );
        }
      /* else we're setting a wcprop in the context of an editor drive. */
      else if (rb->file_baton == NULL)
        {
          /* Update the wcprop here, unless this is the top directory
             and the real target of this operation is something other
             than the top directory. */
          if (! ((rb->dirs->nelts == 1) && rb->target))
            {
              CHKERR( simple_store_vsn_url(rb->href->data, TOP_DIR(rb).baton,
                                           rb->editor->change_dir_prop,
                                           TOP_DIR(rb).pool) );
              
              /* save away the URL in case a fetch-props arrives after all of
                 the subdir processing. we will need this copy of the URL to
                 fetch the properties (i.e. rb->href will be toast by then). */
              TOP_DIR(rb).vsn_url = apr_pmemdup(TOP_DIR(rb).pool,
                                                rb->href->data,
                                                rb->href->len + 1);
            }
        }
      else
        {
          CHKERR( simple_store_vsn_url(rb->href->data, rb->file_baton,
                                       rb->editor->change_file_prop,
                                       rb->file_pool) );
        }
      break;

    case ELEM_md5_checksum:
      /* We only care about file checksums. */
      if (rb->file_baton)
        rb->result_checksum = apr_pstrdup(rb->file_pool, cdata);
      break;

    case ELEM_version_name:
    case ELEM_creationdate:
    case ELEM_creator_displayname:
      {
        /* The name of the xml tag is the property that we want to set. */
        apr_pool_t *pool = 
          rb->file_baton ? rb->file_pool : TOP_DIR(rb).pool;
        prop_setter_t setter =
          rb->file_baton ? editor->change_file_prop : editor->change_dir_prop;
        const char *name = apr_pstrcat(pool, elm->nspace, elm->name, NULL);
        void *baton = rb->file_baton ? rb->file_baton : TOP_DIR(rb).baton;
        svn_string_t valstr;

        valstr.data = cdata;
        valstr.len = strlen(cdata);
        CHKERR( set_special_wc_prop(name, &valstr, setter, baton, pool) );
      }
      break;
  
    default:
      break;
    }

  return 0;
}


static svn_error_t * reporter_set_path(void *report_baton,
                                       const char *path,
                                       svn_revnum_t revision,
                                       svn_boolean_t start_empty,
                                       apr_pool_t *pool)
{
  report_baton_t *rb = report_baton;
  apr_status_t status;
  const char *entry;
  svn_stringbuf_t *qpath = NULL;

  svn_xml_escape_cdata_cstring (&qpath, path, pool);
  if (start_empty)
    entry = apr_psprintf(pool,
                         "<S:entry rev=\"%"
                         SVN_REVNUM_T_FMT
                         "\" start-empty=\"true\">%s</S:entry>" DEBUG_CR,
                         revision, qpath->data);
  else
    entry = apr_psprintf(pool,
                         "<S:entry rev=\"%"
                         SVN_REVNUM_T_FMT
                         "\">%s</S:entry>" DEBUG_CR,
                         revision, qpath->data);

  status = apr_file_write_full(rb->tmpfile, entry, strlen(entry), NULL);
  if (status)
    {
      (void) apr_file_close(rb->tmpfile);
      return svn_error_create(status, NULL,
                              "Could not write an entry to the temporary "
                              "report file.");
    }

  return SVN_NO_ERROR;
}


static svn_error_t * reporter_link_path(void *report_baton,
                                        const char *path,
                                        const char *url,
                                        svn_revnum_t revision,
                                        svn_boolean_t start_empty,
                                        apr_pool_t *pool)
{
  report_baton_t *rb = report_baton;
  apr_status_t status;
  const char *entry;
  svn_stringbuf_t *qpath = NULL, *qlinkpath = NULL;
  svn_string_t bc_relative;

  /* Convert the copyfrom_* url/rev "public" pair into a Baseline
     Collection (BC) URL that represents the revision -- and a
     relative path under that BC.  */
  SVN_ERR( svn_ra_dav__get_baseline_info(NULL, NULL, &bc_relative, NULL,
                                         rb->ras->sess,
                                         url, revision,
                                         pool));
  
  
  svn_xml_escape_cdata_cstring (&qpath, path, pool);
  svn_xml_escape_attr_cstring (&qlinkpath, bc_relative.data, pool);
  if (start_empty)
    entry = apr_psprintf(pool,
                         "<S:entry rev=\"%" SVN_REVNUM_T_FMT
                         "\" linkpath=\"/%s\" start-empty=\"true\""
                         ">%s</S:entry>" DEBUG_CR,
                         revision, qlinkpath->data, qpath->data);
  else
    entry = apr_psprintf(pool,
                         "<S:entry rev=\"%" SVN_REVNUM_T_FMT
                         "\" linkpath=\"/%s\">%s</S:entry>" DEBUG_CR,
                         revision, qlinkpath->data, qpath->data);
    
  status = apr_file_write_full(rb->tmpfile, entry, strlen(entry), NULL);
  if (status)
    {
      (void) apr_file_close(rb->tmpfile);
      return svn_error_create(status, NULL,
                              "Could not write an entry to the temporary "
                              "report file.");
    }

  return SVN_NO_ERROR;
}


static svn_error_t * reporter_delete_path(void *report_baton,
                                          const char *path,
                                          apr_pool_t *pool)
{
  report_baton_t *rb = report_baton;
  apr_status_t status;
  const char *s;
  svn_stringbuf_t *qpath = NULL;

  svn_xml_escape_cdata_cstring (&qpath, path, pool);
  s = apr_psprintf(pool,
                   "<S:missing>%s</S:missing>" DEBUG_CR,
                   qpath->data);

  status = apr_file_write_full(rb->tmpfile, s, strlen(s), NULL);
  if (status)
    {
      (void) apr_file_close(rb->tmpfile);
      return svn_error_create(status, NULL,
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
  svn_error_t *err;
  const char *vcc;

  status = apr_file_write_full(rb->tmpfile,
                               report_tail, sizeof(report_tail) - 1, NULL);
  if (status)
    {
      (void) apr_file_close(rb->tmpfile);
      return svn_error_create(status, NULL,
                              "Could not write the trailer for the temporary "
                              "report file.");
    }

  /* get the editor process prepped */
  rb->dirs = apr_array_make(rb->ras->pool, 5, sizeof(dir_item_t));
  rb->namestr = MAKE_BUFFER(rb->ras->pool);
  rb->cpathstr = MAKE_BUFFER(rb->ras->pool);
  rb->href = MAKE_BUFFER(rb->ras->pool);

  err = svn_ra_dav__get_vcc(&vcc, rb->ras->sess, rb->ras->url, rb->ras->pool);
  if (err)
    {
      (void) apr_file_close(rb->tmpfile);
      return err;
    }

  err = svn_ra_dav__parsed_request(rb->ras->sess, "REPORT", vcc,
                                   NULL, rb->tmpfile, NULL,
                                   report_elements, validate_element,
                                   start_element, end_element, rb,
                                   NULL, rb->ras->pool);

  if (err)
    {
      /* If running the update-report on the VCC failed, it's probably
         an older server.  Fall back to the old-style, by requesting
         the report on the src-url itself.  This runs a risk of
         choking on issue #891 ("REPORT fails on item not in HEAD"),
         but hey, it's better than nothing.  */

      /* ### someday, remove this fallback code. */

      svn_error_clear (err);
      err = svn_ra_dav__parsed_request(rb->ras->sess, "REPORT",
                                       rb->ras->root.path,
                                       NULL, rb->tmpfile, NULL,
                                       report_elements, validate_element,
                                       start_element, end_element, rb,
                                       NULL, rb->ras->pool);
    }

  /* we're done with the file */
  (void) apr_file_close(rb->tmpfile);

  if (err != NULL)
    return err;
  if (rb->err != NULL)
    return rb->err;

  /* We got the whole HTTP response thing done.  *Whew*.  Our edit
     baton should have been closed by now, so return a failure if it
     hasn't been. */
  if (rb->edit_baton)
    {
      return svn_error_createf 
        (SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
         "REPORT response handling failed to complete the editor drive");
    }

  /* store auth info if we can. */
  SVN_ERR( svn_ra_dav__maybe_store_auth_info (rb->ras) );

  return SVN_NO_ERROR;
}

static const svn_ra_reporter_t ra_dav_reporter = {
  reporter_set_path,
  reporter_delete_path,
  reporter_link_path,
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
               const char *target,
               const char *dst_path,
               svn_boolean_t recurse,
               svn_boolean_t ignore_ancestry,
               svn_boolean_t resource_walk,
               const svn_delta_editor_t *editor,
               void *edit_baton,
               svn_boolean_t fetch_content,
               apr_pool_t *pool)
{
  svn_ra_session_t *ras = session_baton;
  report_baton_t *rb;
  apr_status_t status;
  const char *s;
  const char *msg;

  /* ### create a subpool for this operation? */

  rb = apr_pcalloc(pool, sizeof(*rb));
  rb->ras = ras;
  rb->editor = editor;
  rb->edit_baton = edit_baton;
  rb->fetch_content = fetch_content;
  rb->is_switch = dst_path ? TRUE : FALSE;
  rb->target = target;

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

  /* always write the original source path.  this is part of the "new
     style" update-report syntax.  if the tmpfile is used in an "old
     style' update-report request, older servers will just ignore this
     unknown xml element. */
  s = apr_psprintf(pool, "<S:src-path>%s</S:src-path>", ras->url);
  status = apr_file_write_full(rb->tmpfile, s, strlen(s), NULL);
  if (status)
    {
      msg = "Failed writing the src-path to the report tempfile.";
      goto error;
    }

  /* an invalid revnum means "latest". we can just omit the target-revision
     element in that case. */
  if (SVN_IS_VALID_REVNUM(revision))
    {
      s = apr_psprintf(pool, 
                       "<S:target-revision>%" SVN_REVNUM_T_FMT
                       "</S:target-revision>", revision);
      status = apr_file_write_full(rb->tmpfile, s, strlen(s), NULL);
      if (status)
        {
          msg = "Failed writing the target revision to the report tempfile.";
          goto error;
        }
    }

  /* A NULL target is no problem.  */
  if (target)
    {
      s = apr_psprintf(pool, 
                       "<S:update-target>%s</S:update-target>",
                       target);
      status = apr_file_write_full(rb->tmpfile, s, strlen(s), NULL);
      if (status)
        {
          msg = "Failed writing the target to the report tempfile.";
          goto error;
        }
    }


  /* A NULL dst_path is also no problem;  this is only passed during a
     'switch' operation.  If NULL, we don't mention it in the custom
     report, and mod_dav_svn automatically runs dir_delta() on two
     identical paths. */
  if (dst_path)
    {
      svn_stringbuf_t *dst_path_str = NULL;
      svn_xml_escape_cdata_cstring (&dst_path_str, dst_path, pool);

      s = apr_psprintf(pool, "<S:dst-path>%s</S:dst-path>",
                       dst_path_str->data);
      status = apr_file_write_full(rb->tmpfile, s, strlen(s), NULL);
      if (status)
        {
          msg = "Failed writing the dst-path to the report tempfile.";
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
          msg = "Failed writing the recurse flag to the report tempfile.";
          goto error;
        }
    }

  /* mod_dav_svn will use ancestry in diffs unless it finds this element. */
  if (ignore_ancestry)
    {
      const char * data = "<S:ignore-ancestry>yes</S:ignore-ancestry>";
      status = apr_file_write_full(rb->tmpfile, data, strlen(data), NULL);
      if (status)
        {
          msg = "Failed writing the ignore_ancestry flag to the "
                "report tempfile.";
          goto error;
        }
    }
  /* If we want a resource walk to occur, note that now. */
  if (resource_walk)
    {
      const char * data = "<S:resource-walk>yes</S:resource-walk>";
      status = apr_file_write_full(rb->tmpfile, data, strlen(data), NULL);
      if (status)
        {
          msg = "Failed writing the resource-walk flag to the report tempfile.";
          goto error;
        }
    }

  *reporter = &ra_dav_reporter;
  *report_baton = rb;

  return SVN_NO_ERROR;

 error:
  (void) apr_file_close(rb->tmpfile);
  return svn_error_create(status, NULL, msg);
}                      


svn_error_t * svn_ra_dav__do_update(void *session_baton,
                                    const svn_ra_reporter_t **reporter,
                                    void **report_baton,
                                    svn_revnum_t revision_to_update_to,
                                    const char *update_target,
                                    svn_boolean_t recurse,
                                    const svn_delta_editor_t *wc_update,
                                    void *wc_update_baton,
                                    apr_pool_t *pool)
{
  return make_reporter (session_baton,
                        reporter,
                        report_baton,
                        revision_to_update_to,
                        update_target,
                        NULL,
                        recurse,
                        FALSE,
                        FALSE,
                        wc_update,
                        wc_update_baton,
                        TRUE, /* fetch_content */
                        pool);
}


svn_error_t * svn_ra_dav__do_status(void *session_baton,
                                    const svn_ra_reporter_t **reporter,
                                    void **report_baton,
                                    const char *status_target,
                                    svn_boolean_t recurse,
                                    const svn_delta_editor_t *wc_status,
                                    void *wc_status_baton,
                                    apr_pool_t *pool)
{
  return make_reporter (session_baton,
                        reporter,
                        report_baton,
                        SVN_INVALID_REVNUM,
                        status_target,
                        NULL,
                        recurse,
                        FALSE,
                        FALSE,
                        wc_status,
                        wc_status_baton,
                        FALSE, /* fetch_content */
                        pool);
}


svn_error_t * svn_ra_dav__do_switch(void *session_baton,
                                    const svn_ra_reporter_t **reporter,
                                    void **report_baton,
                                    svn_revnum_t revision_to_update_to,
                                    const char *update_target,
                                    svn_boolean_t recurse,
                                    const char *switch_url,
                                    const svn_delta_editor_t *wc_update,
                                    void *wc_update_baton,
                                    apr_pool_t *pool)
{
  return make_reporter (session_baton,
                        reporter,
                        report_baton,
                        revision_to_update_to,
                        update_target,
                        switch_url,
                        recurse,
                        FALSE,
                        TRUE,
                        wc_update,
                        wc_update_baton,
                        TRUE, /* fetch_content */
                        pool);
}


svn_error_t * svn_ra_dav__do_diff(void *session_baton,
                                  const svn_ra_reporter_t **reporter,
                                  void **report_baton,
                                  svn_revnum_t revision,
                                  const char *diff_target,
                                  svn_boolean_t recurse,
                                  svn_boolean_t ignore_ancestry,
                                  const char *versus_url,
                                  const svn_delta_editor_t *wc_diff,
                                  void *wc_diff_baton,
                                  apr_pool_t *pool)
{
  return make_reporter (session_baton,
                        reporter,
                        report_baton,
                        revision,
                        diff_target,
                        versus_url,
                        recurse,
                        ignore_ancestry,
                        FALSE,
                        wc_diff,
                        wc_diff_baton,
                        TRUE, /* fetch_content */
                        pool);
}
