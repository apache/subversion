/*
 * xml_parse.c:  parse an Subversion "tree-delta" XML stream
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


/*
  This file implements a few public interfaces :

     svn_delta_make_xml_parser()   -- create a custom svn/expat parser
     svn_delta_free_xml_parser()   -- free it
 
     svn_delta_xml_parsebytes()    -- push some svn xml stream at the parser
     svn_delta_xml_auto_parse()    -- automated `pull interface' wrapper

  As the parser receives xml, calls are made into an svn_delta_edit_fns_t.

  See the bottom of this file for these routines.
*/



#include <stdio.h>
#include <string.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_xml.h"
#include "svn_base64.h"
#include "svn_quoprint.h"
#include "apr_strings.h"
#ifdef SVN_HAVE_OLD_EXPAT
#include "xmlparse.h"
#else
#include "expat.h"
#endif
#include "delta.h"


/* We must keep this map IN SYNC with the enumerated type
   svn_delta__XML_t in delta.h!  

   It allows us to do rapid strcmp() comparisons.  We terminate with
   NULL so that we have the ability to loop over the array easily. */
static const char * const svn_delta__tagmap[] =
{
  SVN_DELTA__XML_TAG_DELTA_PKG,
  SVN_DELTA__XML_TAG_TREE_DELTA,
  SVN_DELTA__XML_TAG_ADD,
  SVN_DELTA__XML_TAG_DELETE,
  SVN_DELTA__XML_TAG_OPEN,
  SVN_DELTA__XML_TAG_FILE,
  SVN_DELTA__XML_TAG_DIR,
  SVN_DELTA__XML_TAG_TEXT_DELTA,
  SVN_DELTA__XML_TAG_TEXT_DELTA_REF,
  SVN_DELTA__XML_TAG_PROP_DELTA,
  SVN_DELTA__XML_TAG_SET,
  NULL
};





/* Return an informative error message about invalid XML.
   (Set DESTROY_P to indicate an unexpected closure tag) */
static svn_error_t *
xml_validation_error (apr_pool_t *pool,
                      const char *name,
                      svn_boolean_t destroy_p)
{
  char *msg;

  if (destroy_p)
    msg = apr_psprintf (pool,
                        "XML validation error: got unexpected </%s>",
                        name);

  else
    msg = apr_psprintf (pool,
                        "XML validation error: got unexpected <%s>",
                        name);

  return svn_error_create (SVN_ERR_MALFORMED_XML, 0, NULL, pool, msg);
}


/* Set up FRAME's ancestry information to the degree that it is not
   already set.  Information is derived by walking backwards up from
   FRAME and examining parents, so it is important that frame has
   _already_ been linked into the digger's stack. */
static svn_error_t *
maybe_derive_ancestry (svn_xml__stackframe_t *frame,
                       apr_pool_t *pool)
{
  if ((frame->tag != svn_delta__XML_dir) 
      && (frame->tag != svn_delta__XML_file))
    {
      /* This is not the kind of frame that needs ancestry information. */
      return SVN_NO_ERROR;
    }
  else if (frame->ancestor_path
           && (frame->ancestor_revision >= 0))
    {
      /* It is the kind of frame that needs ancestry information, but
         all its ancestry information is already set. */
      return SVN_NO_ERROR;
    }
  else
    {
      svn_xml__stackframe_t *p = frame->previous;
      svn_stringbuf_t *this_name = NULL;

      while (p)
        {
          /* Since we're walking up from youngest, we catch and hang
             onto the name attribute before seeing any ancestry. */
          if ((! this_name) && p->name)
            this_name = p->name;

          if (p->ancestor_path && (! frame->ancestor_path))
            {
              /* Why are we setting derived_ancestry_path according to
               * the nearest previous ancestor_path, instead of
               * nearest previous name?
               *
               * I'm glad you asked.
               *
               * It's because ancestry needs to be an absolute
               * path in existing repository revision.  There's no
               * guarantee that the `name' fields we've seen so far
               * are actually in the repository, and even if they
               * were, there's no guarantee that the first frame with
               * a name represents a name at the _top_ of the
               * repository.  Following ancestry solves these
               * problems.
               *
               * Remember that if any of the directories in the
               * chain has changed its name, then we wouldn't be
               * here anyway, because the delta should have set
               * ancestry attributes explicitly for everything
               * under that change.
               */

              frame->ancestor_path
                = svn_stringbuf_dup (p->ancestor_path, pool);
              svn_path_add_component (frame->ancestor_path, this_name);
            }

          /* If ancestor_revision not set, and see it here, then set it. */
          if ((frame->ancestor_revision < 0) && (p->ancestor_revision >= 0))
            frame->ancestor_revision = p->ancestor_revision;

          /* If we have all the ancestry information we need, then
             stop the search. */
          if ((frame->ancestor_revision >= 0) && frame->ancestor_path)
            break;

          /* Else, keep searching. */
          p = p->previous;
        }

      if ((frame->ancestor_path == NULL)
          || (frame->ancestor_revision == SVN_INVALID_REVNUM))
        return svn_error_create (SVN_ERR_XML_MISSING_ANCESTRY,
                                 0,
                                 NULL,
                                 pool,
                                 "unable to derive ancestry");
    }

  return SVN_NO_ERROR;
}


/* Return true iff the youngest stack frame in the digger
   represents the outermost </tree-delta> in the xml form. 
   Although this function could do some minor validation, it does
   not.  It answers one question and nothing more. */
static svn_boolean_t
outermost_tree_delta_close_p (svn_xml__digger_t *digger)
{
  if (! digger->stack)
    return FALSE;
  else if (! digger->stack->previous)
    return FALSE;
  else if ((digger->stack->tag == svn_delta__XML_treedelta)
           && (digger->stack->previous->tag == svn_delta__XML_deltapkg))
    return TRUE;
  else
    return FALSE;
}


/* A validation note.  

   The strategy for validating our XML stream is simple:

   1. When we find a new "open" tag, make sure it logically follows
   the previous tag.  This is handled in do_stack_append().  (If valid,
   this same routines does the append.)

   2. When we find a "close" tag, make sure the newest item on the
   stack is of the identical type.  This is handled by
   do_stack_check_remove().  (If valid, the removal is done at the end
   of xml_handle_end().)

   When these functions find invalid XML, they call signal_expat_bailout().
*/


/* If FRAME represents an <add> or <open> command, check if the
   "name" attribute conflicts with an preexisting dirent name in the
   parent (tree-delta) frame.  If so, return error.  If not, store the
   dirent name in parent's "namespace" hash. 

   Assumes that FRAME has not yet been appended to DIGGER->STACK.
*/
static svn_error_t *
check_dirent_namespace (svn_xml__digger_t *digger,
                        svn_xml__stackframe_t *frame)
{
  apr_hash_t *namespace = NULL;
  void *dirent_exists = NULL;

  /* Sanity: check frame's type.  If we're not looking at directory
     entries, just leave. */
  if ((frame->tag != svn_delta__XML_add)
       && (frame->tag != svn_delta__XML_open))
    return SVN_NO_ERROR;

  namespace= digger->stack->namespace;
  
  if (namespace == NULL)
    return 
      svn_error_create 
      (SVN_ERR_MALFORMED_XML, 0, NULL, digger->pool,
       "check_dirent_namespace: parent frame has no namespace hash.");

  if ((frame->name == NULL) || (svn_stringbuf_isempty (frame->name)))
    return
      svn_error_create
      (SVN_ERR_MALFORMED_XML, 0,
       NULL, digger->pool,
       "check_dirent_namespace: <add> or <open> has no `name' attribute.");
  
  /* Is "name" in the namespace already? */
  dirent_exists = apr_hash_get (namespace,
                                frame->name->data,
                                frame->name->len);
  if (dirent_exists)
    return 
      svn_error_createf
      (SVN_ERR_MALFORMED_XML, 0, NULL, digger->pool,
       frame->name->data,
       "check_dirent_namespace: non-unique dirent name '%s'");

  else /* dirent_exists == NULL, so this is a unique dirent name */
    apr_hash_set (namespace, frame->name->data, frame->name->len, (void *) 1);

  return SVN_NO_ERROR;
}




/* Decide if it's valid XML to append NEW_FRAME to DIGGER's stack.  If
   so, append the frame and inherit the parent's baton.  If not,
   return a validity error. (TAGNAME is used for error message.) */
static svn_error_t *
do_stack_append (svn_xml__digger_t *digger, 
                 svn_xml__stackframe_t *new_frame,
                 const char *tagname)
{
  svn_error_t *err;
  apr_pool_t *pool = digger->pool;
  svn_xml__stackframe_t *youngest_frame = digger->stack;

  if (digger->stack == NULL)
    {
      /* First time appending. */

      /* Make sure that it's indeed a delta-pkg we're appending. */
      if (new_frame->tag != svn_delta__XML_deltapkg)
        return xml_validation_error (pool, tagname, FALSE);

      /* Do the append and get out */
      digger->stack = new_frame;

      return SVN_NO_ERROR;
    }

  /* <tree-delta> must follow either <dir> or <delta-pkg> */
  else if ((new_frame->tag == svn_delta__XML_treedelta)
           && ((youngest_frame->tag != svn_delta__XML_dir) 
               && (youngest_frame->tag != svn_delta__XML_deltapkg)))
    return xml_validation_error (pool, tagname, FALSE);
  
  /* <add>, <open> must follow <tree-delta> */
  else if ( ((new_frame->tag == svn_delta__XML_add)
             || (new_frame->tag == svn_delta__XML_open))
            && (youngest_frame->tag != svn_delta__XML_treedelta) )
    return xml_validation_error (pool, tagname, FALSE);
  
  /* <delete> must follow either <tree-delta> or <prop-delta> */
  else if ( (new_frame->tag == svn_delta__XML_delete)
            && (youngest_frame->tag != svn_delta__XML_treedelta)
            && (youngest_frame->tag != svn_delta__XML_propdelta) )
    return xml_validation_error (pool, tagname, FALSE);
  
  /* <file>, <dir> must follow either <add> or <open> */
  else if ((new_frame->tag == svn_delta__XML_file)
           || (new_frame->tag == svn_delta__XML_dir))
    {
      if ((youngest_frame->tag != svn_delta__XML_add)
              && (youngest_frame->tag != svn_delta__XML_open))
        return xml_validation_error (digger->pool, tagname, FALSE);
    }
  
  /* <prop-delta> must follow either <file> or <dir> */
  else if ((new_frame->tag == svn_delta__XML_propdelta)
           && (youngest_frame->tag != svn_delta__XML_file)
           && (youngest_frame->tag != svn_delta__XML_dir))
    return xml_validation_error (pool, tagname, FALSE);
  
  /* <text-delta> must follow either <file> or <delta-pkg> */
  else if ((new_frame->tag == svn_delta__XML_textdelta)
           && ((youngest_frame->tag != svn_delta__XML_file)
               && (youngest_frame->tag != svn_delta__XML_deltapkg)))
    return xml_validation_error (pool, tagname, FALSE);
  
  /* <set> must follow <prop-delta> */
  else if ((new_frame->tag == svn_delta__XML_set)
           && (youngest_frame->tag != svn_delta__XML_propdelta))
    return xml_validation_error (pool, tagname, FALSE);
  
  /* ancestry information can only appear as <file> or <dir> attrs */
  else if (new_frame->ancestor_path
           && (new_frame->tag != svn_delta__XML_file)
           && (new_frame->tag != svn_delta__XML_dir))
    return xml_validation_error (pool, tagname, FALSE);
  
  /* revisions can only appears in <file>, <dir>, and <delete> tags. */
  else if (SVN_IS_VALID_REVNUM(new_frame->ancestor_revision)
           && (new_frame->tag != svn_delta__XML_delete)
           && (new_frame->tag != svn_delta__XML_file)
           && (new_frame->tag != svn_delta__XML_dir))
    return xml_validation_error (pool, tagname, FALSE);

  /* Final check: if this is an <add> or <open>, make sure the
     "name" attribute is unique within the parent <tree-delta>. */
  
  err = check_dirent_namespace (digger, new_frame);
  if (err)
    return err;

  /* The XML is valid.  Do the append.  */
  youngest_frame->next = new_frame;
  
  /* Inherit parent's baton. */
  new_frame->baton = youngest_frame->baton; 
  
  /* Digger should now point to the youngest stackframe. */
  digger->stack = new_frame;

  /* Link backwards, too. */
  new_frame->previous = youngest_frame;
  
  /* Set up any unset ancestry information. */
  err = maybe_derive_ancestry (new_frame, pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}




/* Decide if an xml closure TAGNAME is valid, by examining the
   youngest frame in DIGGER's stack. */
static svn_error_t *
do_stack_check_remove (svn_xml__digger_t *digger, const char *tagname)
{
  apr_pool_t *pool = digger->pool;
  svn_xml__stackframe_t *youngest_frame = digger->stack;

  /* Validity check: Make sure the kind of object we're removing (due
     to an XML TAGNAME closure) actually agrees with the type of frame
     at the top of the stack.  This also filters out bogus values of
     TAGNAME. */
  if (strcmp (tagname, svn_delta__tagmap[youngest_frame->tag]))
    return xml_validation_error (pool, tagname, TRUE);
        
  return SVN_NO_ERROR;
}



/* Utility:  set FRAME's tag field to an svn_delta__XML_t, according to NAME */
static svn_error_t *
set_tag_type (svn_xml__stackframe_t *frame,
              const char *name,
              svn_xml__digger_t *digger)
{
  int tag;
  
  for (tag = 0; svn_delta__tagmap[tag]; tag++)
    if (! strcmp (name, svn_delta__tagmap[tag]))
      {
        frame->tag = tag;
        return SVN_NO_ERROR;
      }
  
  return xml_validation_error (digger->pool, name, TRUE);
}




/* Called when we get a <dir> tag preceeded by either a <new> or
   <open> tag; calls the appropriate callback inside
   DIGGER->EDITOR, depending on the value of OPEN_P. */
static svn_error_t *
do_directory_callback (svn_xml__digger_t *digger, 
                       svn_xml__stackframe_t *youngest_frame,
                       const char **atts,
                       svn_boolean_t open_p)
{
  svn_error_t *err;
  const char *ancestor, *ver;
  svn_stringbuf_t *dir_name = NULL;

  /* Retrieve the "name" field from the previous <new> or <open> tag */
  dir_name = youngest_frame->previous->name;
  if (dir_name == NULL)
    return
      svn_error_create 
      (SVN_ERR_MALFORMED_XML, 0,
       NULL, digger->pool,
       "do_directory_callback: <dir>'s parent tag has no 'name' field.");
                             
  /* Search through ATTS, looking for any "ancestor" or "ver"
     attributes of the current <dir> tag. */
  ancestor = svn_xml_get_attr_value (SVN_DELTA__XML_ATTR_BASE_PATH, atts);
  if (ancestor)
    youngest_frame->ancestor_path = svn_stringbuf_create (ancestor, digger->pool);
  ver = svn_xml_get_attr_value (SVN_DELTA__XML_ATTR_BASE_REV, atts);
  if (ver)
    youngest_frame->ancestor_revision = SVN_STR_TO_REV (ver);

  /* Call our editor's callback. */
  if (open_p)
    err = digger->editor->open_directory
      (dir_name,
       youngest_frame->baton,
       youngest_frame->ancestor_revision,
       &(youngest_frame->baton));
  else
    err = digger->editor->add_directory
      (dir_name,
       youngest_frame->baton,
       NULL, SVN_INVALID_REVNUM,
       /* We no longer use these args:      
             youngest_frame->ancestor_path,
             youngest_frame->ancestor_revision,
          ...unless we're doing some *crazy* optimizations! */
       &(youngest_frame->baton));

  if (err) 
    return err;

  /* Store CHILD_BATON in the digger, too, for safekeeping. */
  digger->dir_baton = youngest_frame->baton;

  return SVN_NO_ERROR;
}



/* Called when we find a <delete> tag after a <tree-delta> tag. */
static svn_error_t *
do_delete_dirent (svn_xml__digger_t *digger, 
                  svn_xml__stackframe_t *youngest_frame,
                  const char **atts)
{
  svn_stringbuf_t *dirent_name = NULL;
  svn_error_t *err;
  const char *ver;
  svn_revnum_t revision = SVN_INVALID_REVNUM;

  /* Retrieve the "name" field from the current <delete> tag */
  dirent_name = youngest_frame->name;
  if (dirent_name == NULL)
    return 
      svn_error_create 
      (SVN_ERR_MALFORMED_XML, 0,
       NULL, digger->pool,
       "do_delete_dirent: <delete> tag has no 'name' field.");

  /* Get the revision from the tag attributes. */
  ver = svn_xml_get_attr_value (SVN_DELTA__XML_ATTR_BASE_REV, atts);
  if (ver)
    revision = SVN_STR_TO_REV (ver);

  /* Call our editor's callback */
  err = digger->editor->delete_entry (dirent_name, 
                                      revision,
                                      youngest_frame->baton);
  if (err)
    return err;

  return SVN_NO_ERROR;
}




/* Called when we get <file> after a <new> or <open>. */
static svn_error_t *
do_file_callback (svn_xml__digger_t *digger,
                  svn_xml__stackframe_t *youngest_frame,
                  const char **atts,
                  svn_boolean_t open_p)
{
  svn_error_t *err;
  const char *ancestor, *ver;
  svn_stringbuf_t *filename = NULL;

  /* Retrieve the "name" field from the previous <new> or <open> tag */
  filename = youngest_frame->previous->name;
  if (filename == NULL)
    return 
      svn_error_create 
      (SVN_ERR_MALFORMED_XML, 0,
       NULL, digger->pool,
       "do_file_callback: <file>'s parent tag has no 'name' field.");
                             
  /* Search through ATTS, looking for any "ancestor" or "ver"
     attributes of the current <dir> tag. */
  ancestor = svn_xml_get_attr_value (SVN_DELTA__XML_ATTR_BASE_PATH, atts);
  if (ancestor)
    youngest_frame->ancestor_path = svn_stringbuf_create (ancestor, digger->pool);
  ver = svn_xml_get_attr_value (SVN_DELTA__XML_ATTR_BASE_REV, atts);
  if (ver)
    youngest_frame->ancestor_revision = SVN_STR_TO_REV (ver);

  /* Call our editor's callback, and get back a window handler & baton. */
  if (open_p)
    err = digger->editor->open_file
      (filename,
       youngest_frame->baton,
       youngest_frame->ancestor_revision,
       &(youngest_frame->file_baton));
  else
    err = digger->editor->add_file 
      (filename,
       youngest_frame->baton,
       youngest_frame->ancestor_path,
       youngest_frame->ancestor_revision,
       &(youngest_frame->file_baton));
  
  if (err)
    return err;

  /* Store FILE_BATON in the digger, too, for safekeeping. */
  digger->file_baton = youngest_frame->file_baton;

  return SVN_NO_ERROR;
}




/* Called when we get a </dir> tag */
static svn_error_t *
do_close_directory (svn_xml__digger_t *digger)
{
  svn_error_t *err;

  /* Nothing to do but invoke the editor's callback, methinks. */
  err = digger->editor->close_directory (digger->stack->baton);
  if (err)
    return err;
  
  /* Drop the current directory baton */
  digger->dir_baton = NULL;

  return SVN_NO_ERROR;
}


/* Called when we get a </file> tag */
static svn_error_t *
do_close_file (svn_xml__digger_t *digger)
{
  svn_error_t *err;

  /* Call the editor's callback ONLY IF the frame's file_baton isn't
     stored in a hashtable!! */
  if (! digger->stack->hashed)
    {
      err = digger->editor->close_file (digger->stack->file_baton);
      if (err)
        return err;
    }

  /* Drop the current parsers and file_baton. */
  digger->svndiff_parser = NULL;
  digger->file_baton = NULL;

  return SVN_NO_ERROR;
}



/* Given a REF_ID key, return FILE_BATON from DIGGER's
   postfix-hashtable. */
static svn_error_t *
lookup_file_baton (void **file_baton,
                   svn_xml__digger_t *digger,
                   svn_stringbuf_t *ref_id)
{
  *file_baton = apr_hash_get (digger->postfix_hash,
                              ref_id->data,
                              ref_id->len);

  if (! *file_baton)
    {
      char *msg = apr_psprintf 
        (digger->pool,
         "lookup_file_baton: ref_id `%s' has no associated file",
         ref_id->data);
        
      return svn_error_create (SVN_ERR_MALFORMED_XML, 0, NULL,
                               digger->pool, msg);
    }
  
  return SVN_NO_ERROR;
}



/* When we find a new text-delta, a editor callback returns to us an
   svndiff-window-consumption routine that we use to create a unique
   svndiff parser.  (The svndiff parser knows how to "push" windows of
   svndiff to the consumption routine.)  */
static svn_error_t *
do_begin_textdelta (svn_xml__digger_t *digger, svn_stringbuf_t *encoding)
{
  svn_error_t *err;
  svn_txdelta_window_handler_t window_consumer = NULL;
  svn_stream_t *intermediate;
  void *file_baton = NULL;
  void *consumer_baton = NULL;

  /* Error check: if this is an "in-line" text-delta, it should NOT
     have a ref_id field.  */
  if (digger->stack->previous)
    if ((digger->stack->previous->tag == svn_delta__XML_file)
        && (digger->stack->ref_id))
      return 
        svn_error_create (SVN_ERR_MALFORMED_XML, 0,
                          NULL, digger->pool,
                          "do_begin_textdelta: in-line text-delta has ID.");

  /* Error check: if this is a "postfix" text-delta, is MUST have a
     ref_id field. */
  if (digger->stack->previous)
    if ((digger->stack->previous->tag == svn_delta__XML_deltapkg)
        && (! digger->stack->ref_id))
      return 
        svn_error_create (SVN_ERR_MALFORMED_XML, 0,
                          NULL, digger->pool,
                          "do_begin_textdelta: postfix text-delta lacks ID.");
  
  /* Now fetch the appropriate file_baton. */
  if (digger->stack->ref_id) 
    {
      /* postfix: look it up in hashtable. */
      err = lookup_file_baton (&file_baton, digger, digger->stack->ref_id);
      if (err)
        return err;
      /* for later convenience, store it inside the text-delta frame. */
      digger->stack->file_baton = file_baton;
    }
  else
    {
      /* in-line: use file_baton from the parent <file> frame. 
         Luckily, it happens to be stashed directly in digger, too. */
      file_baton = digger->file_baton;
    }



  /* Get a window consumer & baton! */
  err = digger->editor->apply_textdelta (file_baton,
                                         &window_consumer,
                                         &consumer_baton);
  if (err)
    return err;

  /* Now create an svndiff parser based on the consumer/baton we got. */
  intermediate = svn_txdelta_parse_svndiff (window_consumer, consumer_baton,
                                            TRUE, digger->pool);
  if (encoding == NULL || strcmp(encoding->data, "base64") == 0)
    digger->svndiff_parser = svn_base64_decode (intermediate, digger->pool);
  else if (encoding != NULL && strcmp(encoding->data, "quoted-printable") == 0)
    digger->svndiff_parser = svn_quoprint_decode (intermediate, digger->pool);
  else
    return svn_error_createf (SVN_ERR_XML_UNKNOWN_ENCODING, 0, NULL,
                              digger->pool,
                              "do_begin_textdelta: unknown encoding %s.",
                              encoding->data);
  return SVN_NO_ERROR;
}



/* When we find a new <text-delta-ref> */
static svn_error_t *
do_begin_textdeltaref (svn_xml__digger_t *digger)
{

  /* Error check:  there *must* be a ref_id field in this frame. */
  if (! digger->stack->ref_id)
    return svn_error_create (SVN_ERR_MALFORMED_XML, 0,
                             NULL, digger->pool,
                             "do_begin_textdeltaref:  reference has no `id'.");

  /* Store the parent <file> frame's `file_baton' in a hash table,
     keyed by the ref_id string. */
  apr_hash_set (digger->postfix_hash,
                digger->stack->ref_id->data,
                digger->stack->ref_id->len,
                digger->stack->previous->file_baton);
  
  /* Mark the parent <file> tag, which lets us know (later on) that
     its file_baton is stored in a hash. */
  digger->stack->previous->hashed = TRUE;

  return SVN_NO_ERROR;
}




/* When we find a new <prop-delta> */
static svn_error_t *
do_begin_propdelta (svn_xml__digger_t *digger)
{
  svn_xml__stackframe_t *youngest_frame;

  /* First, allocate a new propdelta object in our digger (if there's
     already one there, we lose the pointer to it, which is fine.) */
  digger->current_propdelta
    = (svn_delta__propdelta_t *)
    apr_pcalloc (digger->pool,
                 sizeof (*(digger->current_propdelta)));

  digger->current_propdelta->name  = svn_stringbuf_create ("", digger->pool);
  digger->current_propdelta->value = svn_stringbuf_create ("", digger->pool);

  /* Now figure out our context.  Is this a propdelta on a file, dir,
     or dirent? */
  youngest_frame = digger->stack;
  if (!youngest_frame->previous)
    return 
      svn_error_create 
      (SVN_ERR_MALFORMED_XML, 0, NULL, digger->pool,
       "do_begin_propdelta: <prop-delta> tag has no parent context");
  
  switch (youngest_frame->previous->tag)
    {
    case svn_delta__XML_file:
      {
        digger->current_propdelta->kind = svn_propdelta_file;
        /* Get the name of the file, too. */
        if (youngest_frame->previous->previous)
          digger->current_propdelta->entity_name = 
            svn_stringbuf_dup (youngest_frame->previous->previous->name,
                            digger->pool);
        break;
      }
    case svn_delta__XML_dir:
      {
        digger->current_propdelta->kind = svn_propdelta_dir;
        /* Get the name of the dir, too. */
        if (youngest_frame->previous->previous)
          digger->current_propdelta->entity_name = 
            svn_stringbuf_dup (youngest_frame->previous->previous->name,
                            digger->pool);
        break;
      }
    default:
      return 
        svn_error_create 
        (SVN_ERR_MALFORMED_XML, 0,
         NULL, digger->pool,
         "do_begin_propdelta: <prop-delta> tag has unknown context!");
    }
   

  return SVN_NO_ERROR;
}



/* When we get a <set>, add the "name" field to our propdelta in-progress */
static svn_error_t *
do_begin_setprop (svn_xml__digger_t *digger,
                  svn_xml__stackframe_t *youngest_frame)
{
  if (digger->current_propdelta)
    digger->current_propdelta->name =
      svn_stringbuf_dup (youngest_frame->name, digger->pool);
  
  return SVN_NO_ERROR;
}




/* Called when we find a <delete> tag after a <prop-delta> tag. */
static svn_error_t *
do_delete_prop (svn_xml__digger_t *digger, 
                svn_xml__stackframe_t *youngest_frame)
{
  svn_stringbuf_t *dir_name = NULL;
        
  if (! digger->current_propdelta)
    return SVN_NO_ERROR;

  /* Retrieve the "name" field from the current <delete> tag */
  dir_name = youngest_frame->name;
  if (dir_name == NULL)
    return 
      svn_error_create 
      (SVN_ERR_MALFORMED_XML, 0,
       NULL, digger->pool,
       "do_delete_prop: <delete> tag has no 'name' field.");
  
  /* Finish filling out current propdelta. */
  digger->current_propdelta->name =
    svn_stringbuf_dup (dir_name, digger->pool);


  return SVN_NO_ERROR;
}


/* When we get a </set>, or when we get the implicit closure at the
   end of <delete />, we send off the prop-delta to the appropriate
   editor callback.  Then blank the current prop-delta's name and
   value. */
static svn_error_t *
do_prop_delta_callback (svn_xml__digger_t *digger)
{
  svn_stringbuf_t *value_string;
  svn_error_t *err = SVN_NO_ERROR;

  if (! digger->current_propdelta)
    return SVN_NO_ERROR;

  if (svn_stringbuf_isempty(digger->current_propdelta->value))
    value_string = NULL;
  else
    value_string = digger->current_propdelta->value;

  switch (digger->current_propdelta->kind)
    {
    case svn_propdelta_file:
      {
        err = digger->editor->change_file_prop
          (digger->file_baton,
           digger->current_propdelta->name,
           value_string);
        break;
      }
    case svn_propdelta_dir:
      {
        err = digger->editor->change_dir_prop
          (digger->dir_baton,
           digger->current_propdelta->name,
           value_string);
        break;
      }
    default:
      {
        return svn_error_create 
          (SVN_ERR_MALFORMED_XML, 0, NULL, digger->pool,
           "do_prop_delta_callback: unknown digger->current_propdelta->kind");
      }
    }

  if (err)
    return err;

  /* Now that the change has been sent, clear its NAME and VALUE
     fields -- but not the KIND field, because more changes may be
     coming inside this <prop-delta> ! */
  svn_stringbuf_setempty(digger->current_propdelta->name);
  svn_stringbuf_setempty(digger->current_propdelta->value);

  return SVN_NO_ERROR;
}





/*-----------------------------------------------------------------------*/

/* The Three Main Expat Callbacks */

/* Callback:  called whenever expat finds a new "open" tag.

   NAME contains the name of the tag.
   ATTS is a dumb list of tag attributes;  a list of name/value pairs, all
   null-terminated cstrings, and ending with an extra final NULL.
*/  
static void
xml_handle_start (void *userData, const char *name, const char **atts)
{
  svn_error_t *err;
  const char *value;

  /* Resurrect our digger structure */
  svn_xml__digger_t *my_digger = (svn_xml__digger_t *) userData;

  /* -------- Create and fill in new stackframe ------------ */

  svn_xml__stackframe_t *new_frame
    = apr_pcalloc (my_digger->pool, sizeof (*new_frame));

  /* Initialize the ancestor revision to a recognizably invalid value. */
  new_frame->ancestor_revision = SVN_INVALID_REVNUM;

  /* Set the tag field */
  err = set_tag_type (new_frame, name, my_digger);
  if (err)
    {
      /* Uh-oh, unrecognized tag, bail out. */
      svn_xml_signal_bailout (err, my_digger->svn_parser);
      return;
    }

  /* kff todo: wonder if we shouldn't use make_attr_hash here instead? */

  /* Set "name" field in frame, if there's any such attribute in ATTS */
  value = svn_xml_get_attr_value (SVN_DELTA__XML_ATTR_NAME, atts);
  if (value)
    new_frame->name = svn_stringbuf_create (value, my_digger->pool);

  /* If this is an add tag, it might contain copyfrom_path and
     copyfrom_revision attributes.  Otherwise, it might just have the
     logical equivalents of these, named base_path and
     base_revision. */
  if (new_frame->tag == svn_delta__XML_add)
    {
      /* Set copyfrom path in frame, if there's any such attribute in
         ATTS */
      value = svn_xml_get_attr_value (SVN_DELTA__XML_ATTR_COPYFROM_PATH, atts);
      if (value)
        new_frame->ancestor_path = svn_stringbuf_create (value, my_digger->pool);

      /* Set copyfrom revision in frame, if there's any such attribute
         in ATTS */
      value = svn_xml_get_attr_value (SVN_DELTA__XML_ATTR_COPYFROM_REV, atts);
      if (value)
        new_frame->ancestor_revision = SVN_STR_TO_REV (value);
    }
  else
    {
      /* Set ancestor path in frame, if there's any such attribute in
         ATTS */
      value = svn_xml_get_attr_value (SVN_DELTA__XML_ATTR_BASE_PATH, atts);
      if (value)
        new_frame->ancestor_path = svn_stringbuf_create (value, my_digger->pool);

      /* Set ancestor revision in frame, if there's any such attribute
         in ATTS */
      value = svn_xml_get_attr_value (SVN_DELTA__XML_ATTR_BASE_REV, atts);
      if (value)
        new_frame->ancestor_revision = SVN_STR_TO_REV (value);
    }  
  
  /* Set "id" in frame, if there's any such attribute in ATTS */
  value = svn_xml_get_attr_value (SVN_DELTA__XML_ATTR_ID, atts);
  if (value)
    new_frame->ref_id = svn_stringbuf_create (value, my_digger->pool);

  /* Set "encoding" in frame, if there's any such attribute in ATTS */
  value = svn_xml_get_attr_value (SVN_DELTA__XML_ATTR_ENCODING, atts);
  if (value)
    new_frame->encoding = svn_stringbuf_create (value, my_digger->pool);

  /* If this frame is a <delta-pkg>, it's the top-most frame, which
     holds the "base" ancestry info */
  if (new_frame->tag == svn_delta__XML_deltapkg)
    {
      svn_revnum_t target_rev = SVN_INVALID_REVNUM;

      /* If no target revision was provided to us via the digger, then
         it is assumed the caller is wanting to operate on the head of
         the tree, which from the perspective of an xml-based
         repository has a current revision of the target_rev we
         hopefully will attain from the attributes of the delta-pkg
         tag.  However, if we *were* provided a target revision, we
         will (for now) allow that value to override the value read in
         from the delta-pkg tag. [todo] Consider the banishment of
         this exercise once a real filesystem is in place. */

      if (! SVN_IS_VALID_REVNUM(my_digger->base_revision))
        {
          /* Set target revision, if there's any such attribute in ATTS */
          value = svn_xml_get_attr_value (SVN_DELTA__XML_ATTR_TARGET_REV, 
                                          atts);
          if (value)
            target_rev = SVN_STR_TO_REV (value);
        }
      else
        target_rev = my_digger->base_revision;

      /* Set the global target revision by calling into the editor */
      if (SVN_IS_VALID_REVNUM(target_rev))
        {
          my_digger->base_revision = target_rev;
          err = my_digger->editor->set_target_revision
            (my_digger->edit_baton, target_rev);
        }
      else
        {
          err = svn_error_create (
                  SVN_ERR_XML_MISSING_ANCESTRY, 0,
                  NULL, my_digger->pool,
                  "xml_handle_start: no valid target revision provided!");
          svn_xml_signal_bailout (err, my_digger->svn_parser);
          return;
        }

      new_frame->ancestor_path = svn_stringbuf_create (my_digger->base_path,
                                                       my_digger->pool);
      new_frame->ancestor_revision = my_digger->base_revision;
    }

  /* If this frame represents a new tree-delta, we need to fill in its
     hashtable and possibly store a root_dir_baton in the parent
     <delta-pkg> frame, too.  */
  if (new_frame->tag == svn_delta__XML_treedelta)
    {
      /* Always create frame's hashtable to hold dirent names. */
      new_frame->namespace = apr_hash_make (my_digger->pool);
      
      /* If this is the FIRST tree-delta we've ever seen... */
      if (my_digger->stack->tag == svn_delta__XML_deltapkg)
        {
          /* Fetch the rootdir_baton by calling into the editor */
          void *rootdir_baton;

          err = my_digger->editor->open_root
            (my_digger->edit_baton, new_frame->ancestor_revision, 
             &rootdir_baton);
          if (err)
            svn_xml_signal_bailout (err, my_digger->svn_parser);

          /* Place this rootdir_baton into the parent of the whole
             stack, our <delta-pkg> tag.  Then, when we push the
             <tree-delta> frame to the stack, it will automatically
             "inherit" the baton as well.  We end up with our top two
             stackframes both containing the root_baton, but that's
             harmless.  */
          my_digger->stack->baton = rootdir_baton;
        }
    }

  /* ---------- Append the new stackframe to the stack ------- */

  /*  Append new frame to stack, validating in the process. 
      If successful, new frame will automatically inherit parent's baton. */
  err = do_stack_append (my_digger, new_frame, name);
  if (err) {
    /* Uh-oh, invalid XML, bail out. */
    svn_xml_signal_bailout (err, my_digger->svn_parser);
    return;
  }

  /* ---------- Interpret the stackframe to the editor ---- */

  /* Now look for special events that the uber-caller (of
     svn_delta_parse()) might want to know about.  */

  /* EVENT:  Are we adding a new directory?  */
  if (new_frame->previous)
    if ((new_frame->previous->tag == svn_delta__XML_add) 
        && (new_frame->tag == svn_delta__XML_dir))
      {
        err = do_directory_callback (my_digger, new_frame, atts, FALSE);
        if (err)
          svn_xml_signal_bailout (err, my_digger->svn_parser);
        return;
      }

  /* EVENT:  Are we replacing a directory?  */
  if (new_frame->previous)
    if ((new_frame->previous->tag == svn_delta__XML_open) 
        && (new_frame->tag == svn_delta__XML_dir))
      {
        err = do_directory_callback (my_digger, new_frame, atts, TRUE);
        if (err)
          svn_xml_signal_bailout (err, my_digger->svn_parser);
        return;
      }  

  /* EVENT:  Are we deleting a directory entry?  */
  if (new_frame->previous)
    if ( (new_frame->tag == svn_delta__XML_delete) &&
         (new_frame->previous->tag == svn_delta__XML_treedelta) )
      {
        err = do_delete_dirent (my_digger, new_frame, atts);
        if (err)
          svn_xml_signal_bailout (err, my_digger->svn_parser);
        return;
      }

  /* EVENT:  Are we adding a new file?  */
  if (new_frame->previous)
    if ((new_frame->previous->tag == svn_delta__XML_add) 
        && (new_frame->tag == svn_delta__XML_file))
      {
        err = do_file_callback (my_digger, new_frame, atts, FALSE);
        if (err)
          svn_xml_signal_bailout (err, my_digger->svn_parser);
        return;
      }

  /* EVENT:  Are we replacing a file?  */
  if (new_frame->previous)
    if ((new_frame->previous->tag == svn_delta__XML_open) 
        && (new_frame->tag == svn_delta__XML_file))
      {
        err = do_file_callback (my_digger, new_frame, atts, TRUE);
        if (err)
          svn_xml_signal_bailout (err, my_digger->svn_parser);
        return;
      }

  /* EVENT:  Are we starting a new text-delta?  */
  if (new_frame->tag == svn_delta__XML_textdelta) 
    {
      err = do_begin_textdelta (my_digger, new_frame->encoding);
      if (err)
        svn_xml_signal_bailout (err, my_digger->svn_parser);
      return;
    }

  /* EVENT:  Are we starting a new text-delta?  */
  if (new_frame->tag == svn_delta__XML_textdeltaref) 
    {
      err = do_begin_textdeltaref (my_digger);
      if (err)
        svn_xml_signal_bailout (err, my_digger->svn_parser);
      return;
    }

  /* EVENT:  Are we starting a new prop-delta?  */
  if (new_frame->tag == svn_delta__XML_propdelta) 
    {
      err = do_begin_propdelta (my_digger);
      if (err)
        svn_xml_signal_bailout (err, my_digger->svn_parser);
      return;
    }

  /* EVENT:  Are we setting a proeprty?  */
  if (new_frame->tag == svn_delta__XML_set) 
    {
      err = do_begin_setprop (my_digger, new_frame);
      if (err)
        svn_xml_signal_bailout (err, my_digger->svn_parser);
      return;
    }

  /* EVENT:  Are we deleting a property?  */
  if (new_frame->previous)
    if ( (new_frame->tag == svn_delta__XML_delete) &&
         (new_frame->previous->tag == svn_delta__XML_propdelta) )
      {
        err = do_delete_prop (my_digger, new_frame);
        if (err)
          svn_xml_signal_bailout (err, my_digger->svn_parser);
        return;
      }

  /* This is a void expat callback, don't return anything. */
}



/*  Callback:  called whenever we find a close tag (close paren) */

static void 
xml_handle_end (void *userData, const char *name)
{
  svn_error_t *err;

  /* Resurrect our digger structure */
  svn_xml__digger_t *digger = (svn_xml__digger_t *) userData;
  svn_xml__stackframe_t *youngest_frame = digger->stack;

  /* Validity check: is it going to be ok to remove the youngest frame
      in our stack?  */
  err = do_stack_check_remove (digger, name);
  if (err) {
    /* Uh-oh, invalid XML, bail out */
    svn_xml_signal_bailout (err, digger->svn_parser);
    return;
  }
  
  /* Now look for special events that the uber-caller (of
     svn_delta_parse()) might want to know about.  */

  /* EVENT: When we get a </dir> pass back the dir_baton and call
     editor. */
  if (strcmp (name, SVN_DELTA__XML_TAG_DIR) == 0)
    {
      err = do_close_directory (digger);
      if (err)
        svn_xml_signal_bailout (err, digger->svn_parser);
    }      

  /* EVENT: when we get a </file>, drop our digger's parsers and call
     editor. */
  if (strcmp (name, SVN_DELTA__XML_TAG_FILE) == 0)
    {
      /* closes digger->stack->file_baton, which is good. */
      err = do_close_file (digger);
      if (err)
        svn_xml_signal_bailout (err, digger->svn_parser);
    }

  /* EVENT: when we get a </text-delta>, do major cleanup.  */
  if (strcmp (name, SVN_DELTA__XML_TAG_TEXT_DELTA) == 0)
    {
      if (digger->svndiff_parser != NULL)
        {     
          /* Close the svndiff stream. */
          err = svn_stream_close (digger->svndiff_parser);
          if (err)
            svn_xml_signal_bailout (err, digger->svn_parser);
        }

      /* If we're finishing a "postfix" text-delta, we must
         deliberately close the file_baton, since no </file> tag will
         make this happen automatically. */
      if (digger->stack->ref_id)
        {
          /* closes digger->stack->file_baton, which is good. */
          err = do_close_file (digger); 
          if (err)
            svn_xml_signal_bailout (err, digger->svn_parser);
        }
    }


  /* EVENT: when we get a </set>, send off the prop-delta. */
  if (strcmp (name, SVN_DELTA__XML_TAG_SET) == 0)
    {
      err = do_prop_delta_callback (digger);
      if (err)
        svn_xml_signal_bailout (err, digger->svn_parser);
    }

  /* EVENT: when we get a prop-delta </delete>, send it off. */
  if (digger->stack->previous)
    if ( (strcmp (name, SVN_DELTA__XML_TAG_DELETE) == 0)
         && (digger->stack->previous->tag == svn_delta__XML_propdelta) )
      {
        err = do_prop_delta_callback (digger);
        if (err)
          svn_xml_signal_bailout (err, digger->svn_parser);
      }

  /* EVENT: is this the final </tree-delta>?  If so, we have to
     close_directory(root_baton), because there won't be any </dir>
     tag for the root of the change. */
  if (strcmp (name, SVN_DELTA__XML_TAG_TREE_DELTA) == 0)
    {
      if (outermost_tree_delta_close_p (digger))
        {
          err = do_close_directory (digger);
          if (err)
            svn_xml_signal_bailout (err, digger->svn_parser);
        }
    }


  /* After checking for above events, do the stackframe removal. */

  /* Lose the pointer to the youngest frame. */
  if (youngest_frame->previous) 
    {
      digger->stack = youngest_frame->previous;
      digger->stack->next = NULL;
    }
  else
    digger->stack = NULL;
  
  /* This is a void expat callback, don't return anything. */
}




/* Callback: called whenever expat finds data _between_ an open/close
   tagpair. */
static void 
xml_handle_data (void *userData, const char *data, int len)
{
  apr_size_t length = (apr_size_t) len;

  /* Resurrect digger structure */
  svn_xml__digger_t *digger = (svn_xml__digger_t *) userData;

  /* Figure out the context of this callback.  If we're currently
     inside a <text-delta> or <prop-delta>, that's great.  If not,
     then we've got some erroneous data flying around our XML, and we
     should return an error. */

  svn_xml__stackframe_t *youngest_frame = digger->stack;

  if (youngest_frame == NULL) {
    svn_error_t *err = svn_error_create (SVN_ERR_MALFORMED_XML, 0,
                                         NULL, digger->pool,
                                         "xml_handle_data: no XML context!");
    svn_xml_signal_bailout (err, digger->svn_parser);
    return;
  }

  if (youngest_frame->tag == svn_delta__XML_textdelta)
    {
      svn_error_t *err;
      
      /* Check that we have an svndiff parser to deal with this data. */
      if (digger->svndiff_parser == NULL)
        return;

      /* Pass the data to our current svndiff parser.  When the parser
         has received enough bytes to make a "window", it pushes the
         window to the uber-caller's own window-consumer routine. */
      err = svn_stream_write (digger->svndiff_parser, data, &length);
      if (err)
        {
          svn_xml_signal_bailout
            (svn_error_quick_wrap
             (err, "xml_handle_data: svndiff parser choked."),
             digger->svn_parser);
          return;
        }                          
    }

  else if (youngest_frame->tag == svn_delta__XML_set)
    {
      /* We're about to receive some amount of "value" data for a
         prop-delta `set' command.  (The "name" data is already stored
         in the current stackframe's "name" field, since expat gave us
         the whole thing as an XML attribute.) 

         So just append the new data to the current_propdelta's
         "value" buffer.  Easy.
      */

      if (digger->current_propdelta)
        svn_stringbuf_appendbytes (digger->current_propdelta->value,
                                data, length);
    }

  else
    {
      /* The data must be outside the bounds of a <text-delta> or a
      <prop-delta> -- so we ignore it.  (It's probably whitespace --
      expat sends us whitespace frequently.) */
    }

  /* This is a void expat callback, don't return anything. */
}


/*------------------------------------------------------------------*/

/* Public interfaces (see svn_delta.h)  */


/* Given a precreated svn_delta_edit_fns_t EDITOR, return a custom xml
   PARSER that will call into it (and feed EDIT_BATON to its
   callbacks.)  Additionally, this XML parser will use BASE_PATH and
   BASE_REVISION as default "context variables" when computing ancestry
   within a tree-delta. */
svn_error_t *
svn_delta_make_xml_parser (svn_delta_xml_parser_t **parser,
                           const svn_delta_edit_fns_t *editor,
                           void *edit_baton,
                           const char *base_path, 
                           svn_revnum_t base_revision,
                           apr_pool_t *pool)
{
  svn_delta_xml_parser_t *delta_parser;
  svn_xml_parser_t *svn_parser;
  svn_xml__digger_t *digger;
  apr_pool_t *main_subpool;

  /* Create a subpool to contain *everything*.  That way,
     svn_delta_free_xml_parser() has an easy target to destroy.  :) */
  main_subpool = svn_pool_create (pool);
      
  /* Create a new digger structure and fill it out*/
  digger = apr_pcalloc (main_subpool, sizeof (*digger));

  digger->pool             = main_subpool;
  digger->stack            = NULL;
  digger->editor           = editor;
  digger->base_path        = apr_pstrdup (main_subpool, base_path);
  digger->base_revision    = base_revision;
  digger->edit_baton       = edit_baton;
  digger->rootdir_baton    = NULL;
  digger->dir_baton        = NULL;
  digger->validation_error = SVN_NO_ERROR;
  digger->svndiff_parser   = NULL;
  digger->postfix_hash     = apr_hash_make (main_subpool);

  /* Create an expat parser */
  svn_parser = svn_xml_make_parser (digger,
                                    xml_handle_start,
                                    xml_handle_end,
                                    xml_handle_data,
                                    main_subpool);

  /* Store the parser in the digger too, so that our expat callbacks
     can magically set themselves to NULL in the case of an error. */
  digger->svn_parser = svn_parser;

  /* Create a new subversion xml parser and put everything inside it. */
  delta_parser = apr_pcalloc (main_subpool, sizeof (*delta_parser));
  
  delta_parser->my_pool      = main_subpool;
  delta_parser->svn_parser   = svn_parser;
  delta_parser->digger       = digger;

  /* Return goodness. */
  *parser = delta_parser;
  return SVN_NO_ERROR;
}



void 
svn_delta_free_xml_parser (svn_delta_xml_parser_t *parser)
{
  svn_pool_destroy (parser->my_pool);
}



svn_error_t *
svn_delta_xml_parsebytes (const char *buffer, apr_size_t len, int isFinal, 
                          svn_delta_xml_parser_t *delta_parser)
{
  svn_error_t *err;

  err = svn_xml_parse (delta_parser->svn_parser,
                       buffer,
                       len,
                       isFinal);

  if (err)
    return err;

  /* Call `close_edit' callback if this is the final push */
  if (isFinal)
    {
      err = delta_parser->digger->editor->close_edit
        (delta_parser->digger->edit_baton);

      if (err)
        return err;        
    }
  
  return SVN_NO_ERROR;
}



svn_error_t *
svn_delta_xml_auto_parse (svn_stream_t *source,
                          const svn_delta_edit_fns_t *editor,
                          void *edit_baton,
                          const char *base_path,
                          svn_revnum_t base_revision,
                          apr_pool_t *pool)
{
  char buf[BUFSIZ];
  apr_size_t len;
  int done;
  svn_error_t *err;
  svn_delta_xml_parser_t *delta_parser;

  /* Create a custom Subversion XML parser */
  err =  svn_delta_make_xml_parser (&delta_parser,
                                    editor,
                                    edit_baton,
                                    base_path,
                                    base_revision,
                                    pool);
  if (err)
    return err;

  /* Repeatedly pull data from SOURCE and feed it to the parser,
     until there's no more data (or we get an error). */
  
  do {
    /* Read BUFSIZ bytes into buf using the supplied read function. */
    len = BUFSIZ;
    err = svn_stream_read (source, buf, &len);
    if (err)
      return svn_error_quick_wrap (err, "svn_delta_parse: can't read source");

    /* We're done if the source stream returned zero bytes read. */
    done = (len == 0);

    /* Push these bytes at the parser */
    err = svn_delta_xml_parsebytes (buf, len, done, delta_parser);
    if (err)
      return err;

  } while (! done);

  svn_delta_free_xml_parser (delta_parser);
  return SVN_NO_ERROR;
}




/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
