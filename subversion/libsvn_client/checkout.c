/*
 * checkout.c:  wrappers around wc checkout functionality
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

/* ==================================================================== */



/*** Includes. ***/

#include <assert.h>
#include "svn_wc.h"
#include "svn_delta.h"
#include "svn_client.h"
#include "svn_ra.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "client.h"



/* Check out the external items described by EXTERNALS into PATH.
 * Use POOL for any temporary allocation.
 *
 * EXTERNALS is a series of lines, such as:
 *
 *   localdir1   http://url.for.external.source/etc/
 *   localdir2   http://another.url/blah/blah/blah
 *   localdir3   http://and.so.on/and/so/forth
 *
 * (I.e., the format for values of the directory property
 * SVN_PROP_EXTERNALS.)
 */
static svn_error_t *
handle_externals_description (const char *externals,
                              const char *path,
                              apr_pool_t *pool)
{
  apr_array_header_t *description_lines
    = svn_cstring_split (externals, "\n\r", TRUE, pool);
  int i;
  
  for (i = 0; i < description_lines->nelts; i++)
    {
#if 0      
      /* ### in progress */

      const char *target_dir;
      const char *url;

      const char *this_line
        = APR_ARRAY_IDX (description_lines, i, (const char *));
#endif /* 0 */
      
    }

  return SVN_NO_ERROR;
}


/* Walk newly checked-out tree PATH looking for directories that have
   the "svn:externals" property set.  For each one, read the external
   items from in the property value, and check them out as subdirs
   of the directory that had the property.

   Use POOL for temporary allocation.
   
   Notes: This is done _after_ the entire initial checkout is complete
   so that fetching external items (and any errors therefrom) doesn't
   delay the primary checkout.  */
static svn_error_t *
process_externals (svn_stringbuf_t *path, apr_pool_t *pool)
{
  const svn_string_t *externals;

  SVN_ERR (svn_wc_prop_get (&externals, SVN_PROP_EXTERNALS, path->data, pool));

  if (externals)
    SVN_ERR (handle_externals_description (externals->data, path->data, pool));

  /* Recurse. */
  {
    apr_hash_t *entries;
    apr_hash_index_t *hi;
    
    SVN_ERR (svn_wc_entries_read (&entries, path, FALSE, pool));
    for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
      {
        const void *key;
        apr_ssize_t klen;
        void *val;
        svn_wc_entry_t *ent;
        
        apr_hash_this (hi, &key, &klen, &val);
        ent = (svn_wc_entry_t *) val;

        if ((ent->kind == svn_node_dir)
            && (strcmp (ent->name->data, SVN_WC_ENTRY_THIS_DIR) != 0))
          {
            svn_path_add_component (path, ent->name);
            SVN_ERR (process_externals (path, pool));
            svn_path_remove_component (path);
          }
      }
  }

  return SVN_NO_ERROR;
}



/*** Public Interfaces. ***/


svn_error_t *
svn_client_checkout (const svn_delta_editor_t *before_editor,
                     void *before_edit_baton,
                     const svn_delta_editor_t *after_editor,
                     void *after_edit_baton,
                     svn_client_auth_baton_t *auth_baton,
                     svn_stringbuf_t *URL,
                     svn_stringbuf_t *path,
                     const svn_client_revision_t *revision,
                     svn_boolean_t recurse,
                     svn_stringbuf_t *xml_src,
                     apr_pool_t *pool)
{
  const svn_delta_editor_t *checkout_editor;
  void *checkout_edit_baton;
  svn_error_t *err;
  svn_revnum_t revnum;

  /* Sanity check.  Without these, the checkout is meaningless. */
  assert (path != NULL);
  assert (URL != NULL);

  /* Get revnum set to something meaningful, so we can fetch the
     checkout editor. */
  if (revision->kind == svn_client_revision_number)
    revnum = revision->value.number; /* do the trivial conversion manually */
  else
    revnum = SVN_INVALID_REVNUM; /* no matter, do real conversion later */

  /* Canonicalize the URL. */
  svn_path_canonicalize (URL);

  /* Fetch the checkout editor.  If REVISION is invalid, that's okay;
     either the RA or XML driver will call editor->set_target_revision
     later on. */
  SVN_ERR (svn_wc_get_checkout_editor (path,
                                       URL,
                                       revnum,
                                       recurse,
                                       &checkout_editor,
                                       &checkout_edit_baton,
                                       pool));

  /* Wrap it up with outside editors. */
  svn_delta_wrap_editor (&checkout_editor, &checkout_edit_baton,
                         before_editor, before_edit_baton,
                         checkout_editor, checkout_edit_baton,
                         after_editor, after_edit_baton, pool);

  /* if using an RA layer */
  if (! xml_src)
    {
      void *ra_baton, *session;
      svn_ra_plugin_t *ra_lib;

      /* Get the RA vtable that matches URL. */
      SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
      SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL->data, pool));

      /* Open an RA session to URL. Note that we do not have an admin area
         for storing temp files.  We do, however, want to store auth data
         after the checkout builds the WC. */
      SVN_ERR (svn_client__open_ra_session (&session, ra_lib, URL, path,
                                            NULL, TRUE, FALSE, TRUE,
                                            auth_baton, pool));

      SVN_ERR (svn_client__get_revision_number
               (&revnum, ra_lib, session, revision, path->data, pool));

      /* Tell RA to do a checkout of REVISION; if we pass an invalid
         revnum, that means RA will fetch the latest revision.  */
      err = ra_lib->do_checkout (session,
                                 revnum,
                                 recurse,
                                 checkout_editor,
                                 checkout_edit_baton);
      /* Sleep for one second to ensure timestamp integrity. */
      apr_sleep (APR_USEC_PER_SEC * 1);
      
      if (err)
        return err;

      /* Close the RA session. */
      SVN_ERR (ra_lib->close (session));
    }      
  
  /* else we're checking out from xml */
  else
    {
      apr_status_t apr_err;
      apr_file_t *in = NULL;
      void *wrap_edit_baton;
      const svn_delta_edit_fns_t *wrap_editor;

      /* Open xml file. */
      apr_err = apr_file_open (&in, xml_src->data, (APR_READ | APR_CREATE),
                               APR_OS_DEFAULT, pool);
      if (apr_err)
        return svn_error_createf (apr_err, 0, NULL, pool,
                                  "unable to open %s", xml_src->data);

      /* ### todo:  This is a TEMPORARY wrapper around our editor so we
         can use it with an old driver. */
      svn_delta_compat_wrap (&wrap_editor, &wrap_edit_baton,
                             checkout_editor, checkout_edit_baton, pool);

      /* Do a checkout by xml-parsing the stream.  An invalid revnum
         means that there will be a revision number in the <delta-pkg>
         tag.  Otherwise, a valid revnum will be stored in the wc,
         assuming there's no <delta-pkg> tag to override it. */
      err = svn_delta_xml_auto_parse (svn_stream_from_aprfile (in, pool),
                                      wrap_editor,
                                      wrap_edit_baton,
                                      URL->data,
                                      revnum,
                                      pool);

      /* Sleep for one second to ensure timestamp integrity. */
      apr_sleep (APR_USEC_PER_SEC * 1);
      
      if (err)
        return err;

      /* Close XML file. */
      apr_file_close (in);
    }

  SVN_ERR (process_externals (path, pool));

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
