/*
 * adm_crawler.c:  report local WC mods to an Editor.
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


#include <string.h>

#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_hash.h>

#include <assert.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_io.h"
#include "svn_sorts.h"
#include "svn_delta.h"

#include "wc.h"
#include "adm_files.h"
#include "props.h"
#include "translate.h"


/* Helper for report_revisions().
   
   Perform an atomic restoration of the file FILE_PATH; that is, copy
   the file's text-base to the administrative tmp area, and then move
   that file to FILE_PATH with possible translations/expansions.  */
static svn_error_t *
restore_file (const char *file_path,
              svn_wc_adm_access_t *adm_access,
              apr_pool_t *pool)
{
  const char *text_base_path, *tmp_text_base_path;
  svn_subst_keywords_t *keywords;
  const char *eol;

  text_base_path = svn_wc__text_base_path (file_path, FALSE, pool);
  tmp_text_base_path = svn_wc__text_base_path (file_path, TRUE, pool);

  SVN_ERR (svn_io_copy_file (text_base_path, tmp_text_base_path,
                             FALSE, pool));

  SVN_ERR (svn_wc__get_eol_style (NULL, &eol, file_path, pool));
  SVN_ERR (svn_wc__get_keywords (&keywords,
                                 file_path, adm_access, NULL, pool));
  
  /* When copying the tmp-text-base out to the working copy, make
     sure to do any eol translations or keyword substitutions,
     as dictated by the property values.  If these properties
     are turned off, then this is just a normal copy. */
  SVN_ERR (svn_subst_copy_and_translate (tmp_text_base_path,
                                         file_path,
                                         eol, FALSE, /* don't repair */
                                         keywords,
                                         TRUE, /* expand keywords */
                                         pool));
  
  SVN_ERR (svn_io_remove_file (tmp_text_base_path, pool));

  /* If necessary, tweak the new working file's executable bit. */
  SVN_ERR (svn_wc__maybe_set_executable (NULL, file_path, pool));

  /* Remove any text conflict */
  SVN_ERR (svn_wc_resolve_conflict (file_path, adm_access, TRUE, FALSE, FALSE,
                                    NULL, NULL, pool));

  /* ### hey guys, shouldn't we recording the 'restored'
     working-file's timestamp in its entry?  Right now, every time we
     restore a file, the front-line-timestamp-check-for-modifiedness
     is being destroyed. */

  return SVN_NO_ERROR;
}


/* The recursive crawler that describes a mixed-revision working
   copy to an RA layer.  Used to initiate updates.

   This is a depth-first recursive walk of DIR_PATH under ADM_ACCESS.
   Look at each entry and check if its revision is different than
   DIR_REV.  If so, report this fact to REPORTER.  If an entry is
   missing from disk, report its absence to REPORTER.  

   If TRAVERSAL_INFO is non-null, record this directory's
   value of svn:externals in both TRAVERSAL_INFO->externals_old and
   TRAVERSAL_INFO->externals_new, using wc_path + dir_path as the key,
   and the raw (unparsed) value of the property as the value.  NOTE:
   We set the value in both places, because its absence in just one or
   the other place signals that the property was added or deleted;
   thus, storing it in both places signals that it is present and, by
   default, unchanged.

   If RESTORE_FILES is set, then unexpectedly missing working files
   will be restored from text-base and NOTIFY_FUNC/NOTIFY_BATON
   will be called to report the restoration. */
static svn_error_t *
report_revisions (svn_wc_adm_access_t *adm_access,
                  const char *dir_path,
                  svn_revnum_t dir_rev,
                  const svn_ra_reporter_t *reporter,
                  void *report_baton,
                  svn_wc_notify_func_t notify_func,
                  void *notify_baton,
                  svn_boolean_t restore_files,
                  svn_boolean_t recurse,
                  svn_wc_traversal_info_t *traversal_info,
                  apr_pool_t *pool)
{
  apr_hash_t *entries, *dirents;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create (pool);
  const svn_wc_entry_t *dot_entry;
  const char *this_url, *this_path, *this_full_path;
  svn_wc_adm_access_t *dir_access;

  /* Construct the actual 'fullpath' = wc_path + dir_path */
  const char *full_path
    = svn_path_join (svn_wc_adm_access_path (adm_access), dir_path, subpool);

  /* Get both the SVN Entries and the actual on-disk entries.   Also
     notice that we're picking up 'deleted' entries too. */
  SVN_ERR (svn_wc_adm_retrieve (&dir_access, adm_access, full_path, subpool));
  SVN_ERR (svn_wc_entries_read (&entries, dir_access, TRUE, subpool));
  SVN_ERR (svn_io_get_dirents (&dirents, full_path, subpool));
  
  /* Do the real reporting and recursing. */
  
  /* First, look at "this dir" to see what its URL is. */
  dot_entry = apr_hash_get (entries, SVN_WC_ENTRY_THIS_DIR, 
                            APR_HASH_KEY_STRING);
  this_url = apr_pstrdup (pool, dot_entry->url);
  this_path = apr_pstrdup (subpool, dir_path);
  this_full_path = apr_pstrdup (subpool, full_path);

  /* If "this dir" has "svn:externals" property set on it, store its name
     in traversal_info. */
  if (traversal_info)
    {
      const svn_string_t *val_s;
      SVN_ERR (svn_wc_prop_get
               (&val_s, SVN_PROP_EXTERNALS, this_full_path, pool));

      if (val_s)
        {
          const char *dup_path = apr_pstrdup (traversal_info->pool,
                                              this_full_path);
          const char *dup_val = apr_pstrmemdup (traversal_info->pool,
                                                val_s->data,
                                                val_s->len);

          apr_hash_set (traversal_info->externals_old,
                        dup_path, APR_HASH_KEY_STRING, dup_val);

          apr_hash_set (traversal_info->externals_new,
                        dup_path, APR_HASH_KEY_STRING, dup_val);
        }
    }

  /* Looping over current directory's SVN entries: */
  for (hi = apr_hash_first (subpool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      const svn_wc_entry_t *current_entry; 
      svn_node_kind_t *dirent_kind;
      svn_boolean_t missing = FALSE;

      /* Get the next entry */
      apr_hash_this (hi, &key, &klen, &val);
      current_entry = val;

      /* Compute the name of the entry.  Skip THIS_DIR altogether. */
      if (! strcmp (key, SVN_WC_ENTRY_THIS_DIR))
        continue;

      /* Compute the complete path of the entry, relative to dir_path,
         and the calculated URL of the entry.  */
      {
        /* ### This block was heavily dependent on stringbuf code.
           It needs a more thorough rewrite, but for now we paper it
           over with a terrifyingly ugly kluge. */

        svn_stringbuf_t *this_path_s = svn_stringbuf_create (this_path, pool);
        svn_stringbuf_t *dir_path_s = svn_stringbuf_create (dir_path, pool);
        svn_stringbuf_t *full_path_s = svn_stringbuf_create (full_path, pool);
        svn_stringbuf_t *this_full_path_s
          = svn_stringbuf_create (this_full_path, pool);
        svn_stringbuf_t *this_url_s = svn_stringbuf_create (this_url, pool);
        svn_stringbuf_t *dot_entry_url_s
          = svn_stringbuf_create (dot_entry->url, pool);

        if (this_path_s->len > dir_path_s->len)
          svn_stringbuf_chop (this_path_s, this_path_s->len - dir_path_s->len);
        if (this_full_path_s->len > full_path_s->len)
          svn_stringbuf_chop (this_full_path_s, 
                              this_full_path_s->len - full_path_s->len);
        if (this_url_s->len > dot_entry_url_s->len)
          svn_stringbuf_chop (this_url_s,
                              this_url_s->len - dot_entry_url_s->len);
        svn_path_add_component (this_path_s, key);
        svn_path_add_component (this_full_path_s, key);
        svn_path_add_component (this_url_s, 
                                    svn_path_uri_encode (key, pool));

        this_path = this_path_s->data;
        this_full_path = this_full_path_s->data;
        this_url = this_url_s->data;
      }
      
      /* The Big Tests: */

      /* If the entry is 'deleted', make sure the server knows its missing. */
      if (current_entry->deleted)
        {
          SVN_ERR (reporter->delete_path (report_baton, this_path));
          continue;
        }
      
      /* Is the entry on disk?  Set a flag if not. */
      dirent_kind = (svn_node_kind_t *) apr_hash_get (dirents, key, klen);
      if (! dirent_kind)
        missing = TRUE;
      
      /* From here on out, ignore any entry scheduled for addition */
      if (current_entry->schedule == svn_wc_schedule_add)
        continue;
      
      if (current_entry->kind == svn_node_file) 
        {
          if (dirent_kind && (*dirent_kind != svn_node_file))
            {
              /* If the dirent changed kind, report it as missing and
                 move on to the next entry.  Later on, the update
                 editor will return an 'obstructed update' error.  :) */
              SVN_ERR (reporter->delete_path (report_baton,
                                              this_path));
              continue;
            }

          if (missing 
              && restore_files 
              && (current_entry->schedule != svn_wc_schedule_delete)
              && (current_entry->schedule != svn_wc_schedule_replace))
            {
              /* Recreate file from text-base. */
              SVN_ERR (restore_file (this_full_path, dir_access, pool));
              
              /* Report the restoration to the caller. */
              if (notify_func != NULL)
                (*notify_func) (notify_baton, 
                                this_full_path,
                                svn_wc_notify_restore,
                                svn_node_file,
                                NULL,
                                svn_wc_notify_state_unknown,
                                svn_wc_notify_state_unknown,
                                SVN_INVALID_REVNUM);

            }

          /* Possibly report a disjoint URL... */
          if ((current_entry->schedule != svn_wc_schedule_add)
              && (current_entry->schedule != svn_wc_schedule_replace)
              && (strcmp (current_entry->url, this_url) != 0))
            SVN_ERR (reporter->link_path (report_baton,
                                          this_path,
                                          current_entry->url,
                                          current_entry->revision));
          /* ... or perhaps just a differing revision. */
          else if (current_entry->revision !=  dir_rev)
            SVN_ERR (reporter->set_path (report_baton,
                                         this_path,
                                         current_entry->revision));
        }
      
      else if (current_entry->kind == svn_node_dir && recurse)
        {
          svn_wc_adm_access_t *subdir_access;
          const svn_wc_entry_t *subdir_entry;

          if (missing)
            {
              /* We can't recreate dirs locally, so report as missing,
                 and move on to the next entry.  */
              SVN_ERR (reporter->delete_path (report_baton, this_path));
              continue;
            }
          
          if (dirent_kind && (*dirent_kind != svn_node_dir))
            /* No excuses here.  If the user changed a versioned
               directory into something else, the working copy is
               hosed.  It can't receive updates within this dir
               anymore.  Throw a real error. */
            return svn_error_createf
              (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
               "The entry '%s' is no longer a directory,\n"
               "which prevents proper updates.\n"
               "Please remove this entry and try updating again.",
               this_path);
          
          /* We need to read the full entry of the directory from its
             own "this dir", if available. */
          SVN_ERR (svn_wc_adm_retrieve (&subdir_access, adm_access,
                                        this_full_path, subpool));
          SVN_ERR (svn_wc_entry (&subdir_entry, this_full_path, subdir_access,
                                 TRUE, subpool));

          /* Possibly report a disjoint URL... */
          if (strcmp (subdir_entry->url, this_url) != 0)
            SVN_ERR (reporter->link_path (report_baton,
                                          this_path,
                                          subdir_entry->url,
                                          subdir_entry->revision));
          /* ... or perhaps just a differing revision. */
          else if (subdir_entry->revision != dir_rev)
            SVN_ERR (reporter->set_path (report_baton,
                                         this_path,
                                         subdir_entry->revision));

          /* Recurse. */
          SVN_ERR (report_revisions (adm_access, this_path,
                                     subdir_entry->revision,
                                     reporter, report_baton,
                                     notify_func, notify_baton,
                                     restore_files, recurse,
                                     traversal_info,
                                     subpool));
        } /* end directory case */

    } /* end main entries loop */

  /* We're done examining this dir's entries, so free everything. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}


/*------------------------------------------------------------------*/
/*** Public Interfaces ***/


/* This is the main driver of the working copy state "reporter", used
   for updates. */
svn_error_t *
svn_wc_crawl_revisions (const char *path,
                        svn_wc_adm_access_t *adm_access,
                        const svn_ra_reporter_t *reporter,
                        void *report_baton,
                        svn_boolean_t restore_files,
                        svn_boolean_t recurse,
                        svn_wc_notify_func_t notify_func,
                        void *notify_baton,
                        svn_wc_traversal_info_t *traversal_info,
                        apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  const svn_wc_entry_t *entry;
  svn_revnum_t base_rev = SVN_INVALID_REVNUM;
  svn_boolean_t missing = FALSE;
  const svn_wc_entry_t *parent_entry = NULL;

  /* The first thing we do is get the base_rev from the working copy's
     ROOT_DIRECTORY.  This is the first revnum that entries will be
     compared to. */
  SVN_ERR (svn_wc_entry (&entry, path, adm_access, FALSE, pool));
  base_rev = entry->revision;
  if (base_rev == SVN_INVALID_REVNUM)
    {
      SVN_ERR (svn_wc_entry (&parent_entry, 
                             svn_path_dirname (path, pool),
                             adm_access,
                             FALSE, pool));
      base_rev = parent_entry->revision;
    }

  /* The first call to the reporter merely informs it that the
     top-level directory being updated is at BASE_REV.  Its PATH
     argument is ignored. */
  SVN_ERR (reporter->set_path (report_baton, "", base_rev));

  if (entry->schedule != svn_wc_schedule_delete)
    {
      apr_finfo_t info;
      err = svn_io_stat (&info, path, APR_FINFO_MIN, pool);
      if (err)
        {
          if (APR_STATUS_IS_ENOENT(err->apr_err))
            missing = TRUE;
          svn_error_clear (err);
        }
    }

  if (entry->kind == svn_node_dir)
    {
      if (missing)
        {
          /* Always report directories as missing;  we can't recreate
             them locally. */
          err = reporter->delete_path (report_baton, "");
          if (err)
            goto abort_report;
        }
      else 
        {
          /* Recursively crawl ROOT_DIRECTORY and report differing
             revisions. */
          err = report_revisions (adm_access,
                                  "",
                                  base_rev,
                                  reporter, report_baton,
                                  notify_func, notify_baton,
                                  restore_files, recurse,
                                  traversal_info,
                                  pool);
          if (err)
            goto abort_report;
        }
    }

  else if (entry->kind == svn_node_file)
    {
      const char *pdir, *bname;

      if (missing && restore_files)
        {
          /* Recreate file from text-base. */
          err = restore_file (path, adm_access, pool);
          if (err)
            goto abort_report;

          /* Report the restoration to the caller. */
          if (notify_func != NULL)
            (*notify_func) (notify_baton, path, svn_wc_notify_restore,
                            svn_node_file,
                            NULL,
                            svn_wc_notify_state_unknown,
                            svn_wc_notify_state_unknown,
                            SVN_INVALID_REVNUM);
        }
      
      /* Split PATH into parent PDIR and basename BNAME. */
      svn_path_split (path, &pdir, &bname, pool);
      if (! parent_entry)
        SVN_ERR (svn_wc_entry (&parent_entry, pdir, adm_access, FALSE, pool));
      
      if (parent_entry 
          && parent_entry->url 
          && entry->url
          && strcmp (entry->url, 
                     svn_path_url_add_component (parent_entry->url, 
                                                 bname, pool)))
        {
          /* This file is disjoint with respect to its parent
             directory.  Since we are looking at the actual target of
             the report (not some file in a subdirectory of a target
             directory), and that target is a file, we need to pass an
             empty string to link_path. */
          SVN_ERR (reporter->link_path (report_baton,
                                        "",
                                        entry->url,
                                        entry->revision));
        }
      else if (entry->revision != base_rev)
        {
          /* If this entry is a file node, we just want to report that
             node's revision.  Since we are looking at the actual target
             of the report (not some file in a subdirectory of a target
             directory), and that target is a file, we need to pass an
             empty string to set_path. */
          err = reporter->set_path (report_baton, "", base_rev);
          if (err)
            goto abort_report;
        }
    }

  /* Finish the report, which causes the update editor to be driven. */
  SVN_ERR (reporter->finish_report (report_baton));

 abort_report:
  if (err)
    {
      /* Clean up the fs transaction. */
      svn_error_t *fserr;
      fserr = reporter->abort_report (report_baton);
      if (fserr)
        return svn_error_quick_wrap (fserr, "Error aborting report.");
      else
        return err;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_transmit_text_deltas (const char *path,
                             svn_wc_adm_access_t *adm_access,
                             svn_boolean_t fulltext,
                             const svn_delta_editor_t *editor,
                             void *file_baton,
                             const char **tempfile,
                             apr_pool_t *pool)
{
  const char *tmpf, *tmp_base;
  apr_status_t status;
  svn_txdelta_window_handler_t handler;
  void *wh_baton;
  svn_txdelta_stream_t *txdelta_stream;
  apr_file_t *localfile = NULL;
  apr_file_t *basefile = NULL;
  
  /* Tell the editor that we're about to apply a textdelta to the
     file baton; the editor returns to us a window consumer routine
     and baton.  If there is no handler provided, just close the file
     and get outta here.  */
  SVN_ERR (editor->apply_textdelta (file_baton, pool, &handler, &wh_baton));
  if (! handler)
    return editor->close_file (file_baton, pool);

  /* Make an untranslated copy of the working file in the
     adminstrative tmp area because a) we want this to work even if
     someone changes the working file while we're generating the
     txdelta, b) we need to detranslate eol and keywords anyway, and
     c) after the commit, we're going to copy the tmp file to become
     the new text base anyway.

     Note that since the translation routine doesn't let you choose
     the filename, we have to do one extra copy.  But what the heck,
     we're about to generate an svndiff anyway. */
  SVN_ERR (svn_wc_translated_file (&tmpf, path, adm_access, FALSE, pool));
  tmp_base = svn_wc__text_base_path (path, TRUE, pool);
  SVN_ERR (svn_io_copy_file (tmpf, tmp_base, FALSE, pool));

  /* Alert the caller that we have created a temporary file that might
     need to be cleaned up. */
  if (tempfile)
    *tempfile = tmp_base;

  /* If the translation step above actually created a new file, delete
     the old one. */
  if (tmpf != path)
    SVN_ERR (svn_io_remove_file (tmpf, pool));
      
  /* If we're not sending fulltext, we'll be sending diffs against the
     text-base. */
  if (! fulltext)
    {
      /* Before we set up an svndiff stream against the old text base,
         make sure the old text base still matches its checksum.
         Otherwise we could send corrupt data and never know it. */ 

      svn_stringbuf_t *checksum;
      const char *tb = svn_wc__text_base_path (path, FALSE, pool);
      const svn_wc_entry_t *ent;
      
      SVN_ERR (svn_wc_entry (&ent, path, adm_access, FALSE, pool));
      SVN_ERR (svn_io_file_checksum (&checksum, tb, pool));
      
      /* For backwards compatibility, no checksum means assume a match. */
      if (ent->checksum && (strcmp (checksum->data, ent->checksum) != 0))
        {
          /* There is an entry checksum, but it does not match the
             actual text base checksum.  Extreme badness.  Of course,
             theoretically we could just switch to fulltext
             transmission here, and everything would work fine; after
             all, we're going to replace the text base with a new one
             in a moment anyway, and we'd fix the checksum then.
             But it's better to error out.  People should know that
             their text bases are getting corrupted, so they can
             investigate.  Other commands could be affected, too, such
             as `svn diff'.  */
          
          /* Deliberately ignore error here; the error about the
             checksum mismatch is more important to return. */
          svn_io_remove_file (tmp_base, pool);
          
          if (tempfile)
            *tempfile = NULL;
          
          return svn_error_createf
            (SVN_ERR_WC_CORRUPT_TEXT_BASE, NULL,
             "svn_wc_transmit_text_deltas: checksum mismatch for '%s':\n"
             "   recorded checksum: %s\n"
             "   actual checksum:   %s\n",
             tb, ent->checksum, checksum->data);
        }
      else
        SVN_ERR (svn_wc__open_text_base (&basefile, path, APR_READ, pool));
    }

  /* Open a filehandle for tmp text-base. */
  SVN_ERR_W (svn_io_file_open (&localfile, tmp_base,
                               APR_READ, APR_OS_DEFAULT, pool),
             "do_apply_textdelta: error opening local file");

  /* Create a text-delta stream object that pulls data out of the two
     files. */
  svn_txdelta (&txdelta_stream,
               svn_stream_from_aprfile (basefile, pool),
               svn_stream_from_aprfile (localfile, pool),
               pool);
  
  /* Pull windows from the delta stream and feed to the consumer. */
  SVN_ERR (svn_txdelta_send_txstream (txdelta_stream, handler, 
                                      wh_baton, pool));
    
  /* Close the two files */
  if ((status = apr_file_close (localfile)))
    return svn_error_create (status, NULL,
                             "error closing local file");
  
  if (basefile)
    SVN_ERR (svn_wc__close_text_base (basefile, path, 0, pool));

  /* Close the file baton, and get outta here. */
  return editor->close_file (file_baton, pool);
}


svn_error_t *
svn_wc_transmit_prop_deltas (const char *path,
                             const svn_wc_entry_t *entry,
                             const svn_delta_editor_t *editor,
                             void *baton,
                             const char **tempfile,
                             apr_pool_t *pool)
{
  int i;
  const char *props, *props_base, *props_tmp;
  apr_array_header_t *propmods;
  apr_hash_t *localprops = apr_hash_make (pool);
  apr_hash_t *baseprops = apr_hash_make (pool);
  
  /* First, get the prop_path from the original path */
  SVN_ERR (svn_wc__prop_path (&props, path, 0, pool));
  
  /* Get the full path of the prop-base `pristine' file */
  if ((entry->schedule == svn_wc_schedule_replace)
      || (entry->schedule == svn_wc_schedule_add))
    {
      /* do nothing: baseprop hash should be -empty- for comparison
         purposes.  if they already exist on disk, they're "leftover"
         from the old file that was replaced. */
      props_base = NULL;
    }
  else
    /* the real prop-base hash */
    SVN_ERR (svn_wc__prop_base_path (&props_base, path, 0, pool));

  /* Copy the local prop file to the administrative temp area */
  SVN_ERR (svn_wc__prop_path (&props_tmp, path, 1, pool));
  SVN_ERR (svn_io_copy_file (props, props_tmp, FALSE, pool));

  /* Alert the caller that we have created a temporary file that might
     need to be cleaned up. */
  if (tempfile)
    *tempfile = props_tmp;

  /* Load all properties into hashes */
  SVN_ERR (svn_wc__load_prop_file (props_tmp, localprops, pool));
  if (props_base)
    SVN_ERR (svn_wc__load_prop_file (props_base, baseprops, pool));
  
  /* Get an array of local changes by comparing the hashes. */
  SVN_ERR (svn_wc_get_local_propchanges (&propmods, localprops, 
                                         baseprops, pool));

  /* Apply each local change to the baton */
  for (i = 0; i < propmods->nelts; i++)
    {
      const svn_prop_t *p = &APR_ARRAY_IDX (propmods, i, svn_prop_t);
      if (entry->kind == svn_node_file)
        SVN_ERR (editor->change_file_prop (baton, p->name, p->value, pool));
      else
        SVN_ERR (editor->change_dir_prop (baton, p->name, p->value, pool));
    }

  return SVN_NO_ERROR;
}
