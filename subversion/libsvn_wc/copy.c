/*
 * copy.c:  wc 'copy' functionality.
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

/* ==================================================================== */



/*** Includes. ***/

#include <string.h>
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "wc.h"



/*** Code. ***/


/* This function effectively creates and schedules a file for
   addition, but does extra administrative things to allow it to
   function as a 'copy'.

   ASSUMPTIONS:

     - src_path points to a file under version control
     - dst_parent points to a dir under version control, in the same
                  working copy.
     - dst_basename will be the 'new' name of the copied file in dst_parent
 */
static svn_error_t *
copy_file_administratively (svn_stringbuf_t *src_path, 
                            svn_stringbuf_t *dst_parent,
                            svn_stringbuf_t *dst_basename,
                            apr_pool_t *pool)
{
  enum svn_node_kind dst_kind;
  apr_hash_t *atts;
  svn_wc_entry_t *src_entry;
  svn_stringbuf_t *copyfrom_url, *copyfrom_rev;

  /* The 'dst_path' is simply dst_parent/dst_basename */
  svn_stringbuf_t *dst_path = svn_stringbuf_dup (dst_parent, pool);
  svn_path_add_component (dst_path, dst_basename, svn_path_local_style);

  /* Sanity check:  if dst file exists already, don't allow overwrite. */
  SVN_ERR (svn_io_check_path (dst_path, &dst_kind, pool));
  if (dst_kind != svn_node_none)
    return svn_error_createf (SVN_ERR_WC_ENTRY_EXISTS, 0, NULL, pool,
                              "'%s' already exists and is in the way.",
                              dst_path->data);

  /* Now, make an actual copy of the working file. */
  SVN_ERR (svn_io_copy_file (src_path, dst_path, pool));

  /* Schedule the copied file for addition; this should create an new
     entry in the parent. */
  SVN_ERR (svn_wc_add_file (dst_path, pool));


  /* Copy the pristine text-base over.  Why?  Because it's the *only*
     way we can detect any upcoming local mods on the copy.

     In other words, we're talking about the scenario where somebody
     makes local mods to 'foo.c', then does an 'svn cp foo.c bar.c'.
     In this case, bar.c should still be locally modified too.
     
     Why do we want the copy to have local mods?  Even though the user
     will only see an 'A' instead of an 'M', local mods means that the
     client doesn't have to send anything but a small delta during
     commit; the server can make efficient use of the copyfrom args. 

     As long as we're copying the text-base over, we should copy the
     working and pristine propfiles over too. */
  {
    apr_status_t status;
    enum svn_node_kind kind;
    apr_file_t *log_fp = NULL;
    svn_stringbuf_t *entry_accum = svn_stringbuf_create ("", pool);
    svn_stringbuf_t *src_wprop, *src_bprop, *dst_wprop, *dst_bprop;

    /* Discover the paths to the two text-base files */
    svn_stringbuf_t *src_txtb = svn_wc__text_base_path (src_path, FALSE, pool);
    svn_stringbuf_t *dst_txtb = svn_wc__text_base_path (dst_path, FALSE, pool);

    /* Discover the paths to the four prop files */
    SVN_ERR (svn_wc__prop_path (&src_wprop, src_path, 0, pool));
    SVN_ERR (svn_wc__prop_base_path (&src_bprop, src_path, 0, pool));
    SVN_ERR (svn_wc__prop_path (&dst_wprop, dst_path, 0, pool));
    SVN_ERR (svn_wc__prop_base_path (&dst_bprop, dst_path, 0, pool));

    /* Copy the text-base over unconditionally. */
    svn_xml_make_open_tag (&entry_accum, pool, svn_xml_self_closing,
                           SVN_WC__LOG_CP,
                           SVN_WC__LOG_ATTR_NAME, src_txtb,
                           SVN_WC__LOG_ATTR_DEST, dst_txtb, NULL);

    /* Copy the props over if they exist. */
    SVN_ERR (svn_io_check_path (src_wprop, &kind, pool));
    if (kind == svn_node_file)
      svn_xml_make_open_tag (&entry_accum, pool, svn_xml_self_closing,
                             SVN_WC__LOG_CP,
                             SVN_WC__LOG_ATTR_NAME, src_wprop,
                             SVN_WC__LOG_ATTR_DEST, dst_wprop, NULL);
      
    /* Copy the base-props over if they exist */
    SVN_ERR (svn_io_check_path (src_bprop, &kind, pool));
    if (kind == svn_node_file)
      svn_xml_make_open_tag (&entry_accum, pool, svn_xml_self_closing,
                             SVN_WC__LOG_CP,
                             SVN_WC__LOG_ATTR_NAME, src_bprop,
                             SVN_WC__LOG_ATTR_DEST, dst_bprop, NULL);

    /* Lock the destination parent dir;  that's where we're making a log. */
    SVN_ERR (svn_wc__lock (dst_parent, 0, pool));

    /* Open, write, and close a logfile. */
    SVN_ERR (svn_wc__open_adm_file (&log_fp, dst_parent,
                                    SVN_WC__ADM_LOG,
                                    (APR_WRITE | APR_CREATE), /* not excl */
                                    pool));

    status = apr_file_write_full (log_fp, entry_accum->data, 
                                  entry_accum->len, NULL);
    if (status)
      return svn_error_createf (status, 0, NULL, pool,
                                "can't write logfile in directory '%s'",
                                dst_parent->data);
    
    SVN_ERR (svn_wc__close_adm_file (log_fp, dst_parent, 
                                     SVN_WC__ADM_LOG, 1, pool));

    /* Run the log. */
    SVN_ERR (svn_wc__run_log (dst_parent, pool));
             
    /* Unlock. */
    SVN_ERR (svn_wc__unlock (dst_parent, pool));
  }

  /* Get the working URL and revision of the src_path file. */
  SVN_ERR (svn_wc_entry (&src_entry, src_path, pool));
  copyfrom_url = svn_stringbuf_dup (src_entry->ancestor, pool);
  copyfrom_rev = svn_stringbuf_createf (pool, "%ld", src_entry->revision);

  /* Make a hash that contains two new attributes. */
  atts = apr_hash_make (pool);
  apr_hash_set (atts, 
                SVN_WC_ENTRY_ATTR_COPYFROM_URL, APR_HASH_KEY_STRING,
                copyfrom_url);
  apr_hash_set (atts, 
                SVN_WC_ENTRY_ATTR_COPYFROM_REV, APR_HASH_KEY_STRING,
                copyfrom_rev);

  /* Tweak the new entry;  merge in the 'copyfrom' args.  */
  SVN_ERR (svn_wc__entry_modify (dst_parent, dst_basename,
                                 SVN_WC__ENTRY_MODIFY_ATTRIBUTES,
                                 SVN_INVALID_REVNUM, svn_node_none,
                                 0, 0, 0, 0, 0,
                                 atts, /* the only unignored part! */
                                 pool, NULL));

  return SVN_NO_ERROR;
}




/* This function effectively creates and schedules a dir for
   addition, but does extra administrative things to allow it to
   function as a 'copy'.

   ASSUMPTIONS:

     - src_path points to a dir under version control
     - dst_parent points to a dir under version control, in the same
                  working copy.
     - dst_basename will be the 'new' name of the copied dir in dst_parent
 */
static svn_error_t *
copy_dir_administratively (svn_stringbuf_t *src_path, 
                           svn_stringbuf_t *dst_parent,
                           svn_stringbuf_t *dst_basename,
                           apr_pool_t *pool)
{
  enum svn_node_kind dst_kind;
  apr_hash_t *atts;
  svn_wc_entry_t *src_entry;
  svn_stringbuf_t *copyfrom_url, *copyfrom_rev;

  /* The 'dst_path' is simply dst_parent/dst_basename */
  svn_stringbuf_t *dst_path = svn_stringbuf_dup (dst_parent, pool);
  svn_path_add_component (dst_path, dst_basename, svn_path_local_style);

  return SVN_NO_ERROR;  /* ### NOT YET READY */

  /* Sanity check:  if dst file exists already, don't allow overwrite. */
  SVN_ERR (svn_io_check_path (dst_path, &dst_kind, pool));
  if (dst_kind != svn_node_none)
    return svn_error_createf (SVN_ERR_WC_ENTRY_EXISTS, 0, NULL, pool,
                              "'%s' already exists and is in the way.",
                              dst_path->data);

  /* Now, create the new directory. */
  /* ### todo */

  /* Schedule the empty directory for addition; this should give it an
     administrative area and mark "this_dir" for addition. */
  /* ### todo */

  /* Copy the dir-props and dir-base-props files over, if they exist.
     Use a log to do so. */
  {
    apr_status_t status;
    enum svn_node_kind kind;
    apr_file_t *log_fp = NULL;
    svn_stringbuf_t *entry_accum = svn_stringbuf_create ("", pool);
    svn_stringbuf_t *src_wprop, *src_bprop, *dst_wprop, *dst_bprop;

    /* Discover the paths to the four prop files */
    SVN_ERR (svn_wc__prop_path (&src_wprop, src_path, 0, pool));
    SVN_ERR (svn_wc__prop_base_path (&src_bprop, src_path, 0, pool));
    SVN_ERR (svn_wc__prop_path (&dst_wprop, dst_path, 0, pool));
    SVN_ERR (svn_wc__prop_base_path (&dst_bprop, dst_path, 0, pool));

    /* Copy the props over if they exist. */
    SVN_ERR (svn_io_check_path (src_wprop, &kind, pool));
    if (kind == svn_node_file)
      svn_xml_make_open_tag (&entry_accum, pool, svn_xml_self_closing,
                             SVN_WC__LOG_CP,
                             SVN_WC__LOG_ATTR_NAME, src_wprop,
                             SVN_WC__LOG_ATTR_DEST, dst_wprop, NULL);
      
    /* Copy the base-props over if they exist */
    SVN_ERR (svn_io_check_path (src_bprop, &kind, pool));
    if (kind == svn_node_file)
      svn_xml_make_open_tag (&entry_accum, pool, svn_xml_self_closing,
                             SVN_WC__LOG_CP,
                             SVN_WC__LOG_ATTR_NAME, src_bprop,
                             SVN_WC__LOG_ATTR_DEST, dst_bprop, NULL);

    /* Lock the destination dir;  that's where we're making a log. */
    SVN_ERR (svn_wc__lock (dst_path, 0, pool));

    /* Open, write, and close a logfile. */
    SVN_ERR (svn_wc__open_adm_file (&log_fp, dst_path,
                                    SVN_WC__ADM_LOG,
                                    (APR_WRITE | APR_CREATE), /* not excl */
                                    pool));

    status = apr_file_write_full (log_fp, entry_accum->data, 
                                  entry_accum->len, NULL);
    if (status)
      return svn_error_createf (status, 0, NULL, pool,
                                "can't write logfile in directory '%s'",
                                dst_parent->data);
    
    SVN_ERR (svn_wc__close_adm_file (log_fp, dst_path, 
                                     SVN_WC__ADM_LOG, 1, pool));

    /* Run the log. */
    SVN_ERR (svn_wc__run_log (dst_path, pool));
             
    /* Unlock. */
    SVN_ERR (svn_wc__unlock (dst_path, pool));
  }


  /* Get the working URL and revision of the src_path dir. */
  SVN_ERR (svn_wc_entry (&src_entry, src_path, pool));
  copyfrom_url = svn_stringbuf_dup (src_entry->ancestor, pool);
  copyfrom_rev = svn_stringbuf_createf (pool, "%ld", src_entry->revision);

  /* Make a hash that contains two new attributes. */
  atts = apr_hash_make (pool);
  apr_hash_set (atts, 
                SVN_WC_ENTRY_ATTR_COPYFROM_URL, APR_HASH_KEY_STRING,
                copyfrom_url);
  apr_hash_set (atts, 
                SVN_WC_ENTRY_ATTR_COPYFROM_REV, APR_HASH_KEY_STRING,
                copyfrom_rev);

  /* Tweak the new entry;  merge in the 'copyfrom' args.  */
  SVN_ERR (svn_wc__entry_modify (dst_path, 
                                 svn_stringbuf_create (SVN_WC_ENTRY_THIS_DIR,
                                                       pool),
                                 SVN_WC__ENTRY_MODIFY_ATTRIBUTES,
                                 SVN_INVALID_REVNUM, svn_node_none,
                                 0, 0, 0, 0, 0,
                                 atts, /* the only unignored part! */
                                 pool, NULL));


  /* ----- ### todo:  The Recursive Tree Walk --- */

  /* Loop over each entry in src_path:
         if (entry is a file)
           copy_file_administratively()
         if (entry is a file)
           copy_dir_administrativel()     
   */
 
  return SVN_NO_ERROR;
}






/* Public Interface */

svn_error_t *
svn_wc_copy (svn_stringbuf_t *src_path,
             svn_stringbuf_t *dst_parent,
             svn_stringbuf_t *dst_basename,
             apr_pool_t *pool)
{
  enum svn_node_kind src_kind;

  SVN_ERR (svn_io_check_path (src_path, &src_kind, pool));
  
  if (src_kind == svn_node_file)
    SVN_ERR (copy_file_administratively (src_path, dst_parent,
                                         dst_basename, pool));

  else if (src_kind == svn_node_dir)

    SVN_ERR (copy_dir_administratively (src_path, dst_parent,
                                        dst_basename, pool));

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
