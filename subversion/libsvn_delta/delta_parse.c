/*
 * delta_parse.c:  parse an Subversion "tree-delta" XML stream
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */

/* ==================================================================== */


/*
  This file implements one critical interface: svn_delta_parse().
  Every other routine in this file is hidden (static).

  svn_delta_parse() reads an XML stream from a specified source using
  expat, validating the XML as it goes.  Whenever an interesting event
  happens, it calls a caller-specified callback routine from an
  svn_walk_t structure.
  
*/



#include <stdio.h>
#include <string.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "apr_strings.h"
#include "xmlparse.h"
#include "delta.h"


/* We must keep this map IN SYNC with the enumerated type
   svn_delta__XML_t in delta.h!  

   It allows us to do rapid strcmp() comparisons.  We terminate with
   NULL so that we have the ability to loop over the array easily. */
static char *svn_delta__tagmap[] =
{
  "tree-delta",
  "new",
  "delete",
  "replace",
  "file",
  "dir",
  "text-delta",
  "prop-delta",
  "set",
  NULL
};





/* Return the value associated with NAME in expat attribute array ATTS,
   else return NULL.  (There could never be a NULL attribute value in
   the XML, although the empty string is possible.)
   
   ATTS is an array of c-strings: even-numbered indexes are names,
   odd-numbers hold values.  If all is right, it should end on an
   even-numbered index pointing to NULL.
*/
/* kff todo: caller is responsible for flagging errors now; make sure
   calling code is adjusted accordingly. */
static const char *
get_attribute_value (const char **atts, char *name)
{
  while (atts && (*atts))
    {
      if (strcmp (atts[0], name) == 0)
        return atts[1];
      else
        atts += 2; /* continue looping */
    }

  /* Else no such attribute name seen. */
  return NULL;
}




/* The way to officially bail out of expat. 
   
   Store ERROR in DIGGER and set all expat callbacks to NULL. (To
   understand why this works, see svn_delta_parse(). ) */
static void
signal_expat_bailout (svn_error_t *error, svn_delta__digger_t *digger)
{
  /* This will cause the current XML_Parse() call to finish quickly! */
  XML_SetElementHandler (digger->expat_parser, NULL, NULL);
  XML_SetCharacterDataHandler (digger->expat_parser, NULL);

  /* Once outside of XML_Parse(), the existence of this field will
     cause svn_delta_parse()'s main read-loop to return error. */
  digger->validation_error = error;
}



/* Return an informative error message about invalid XML.
   (Set DESTROY_P to indicate an unexpected closure tag) */
static svn_error_t *
XML_validation_error (apr_pool_t *pool,
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

  return svn_create_error (SVN_ERR_MALFORMED_XML, 0, msg, NULL, pool);
}


/* Set up FRAME's ancestry information to the degree that it is not
   already set.  Information is derived by walking backwards up from
   FRAME and examining parents, so it is important that frame has
   _already_ been linked into the digger's stack. */
static void
maybe_derive_ancestry (svn_delta__stackframe_t *dest_frame, apr_pool_t *pool)
{
  if ((dest_frame->tag != svn_delta__XML_dir) 
      && (dest_frame->tag != svn_delta__XML_file))
    {
      /* This is not the kind of frame that needs ancestry information. */
      return;
    }
  else if (dest_frame->ancestor_path
           && (dest_frame->ancestor_version >= 0))
    {
      /* It is the kind of frame that needs ancestry information, but
         all its ancestry information is already set. */
      return;
    }
  else
    {
      svn_delta__stackframe_t *p = dest_frame->previous;
      svn_string_t *derived_ancestor_path = NULL;
      svn_string_t *this_name = NULL;

      while (p)
        {
          /* Since we're walking up from youngest, we catch and hang
             onto the name attribute before seeing any ancestry. */
          if ((! this_name) && p->name)
            this_name = p->name;

          if ((p->ancestor_path) && (! dest_frame->ancestor_path))
            {
              /* Why are we setting derived_ancestry_path according to
               * the nearest previous ancestor_path, instead of
               * nearest previous name?
               *
               * I'm glad you asked.
               *
               * It's because ancestry needs to be an absolute
               * path in existing repository version.  There's no
               * guarantee that the `name' fields we've seen so far
               * are actually in the repository, and even if they
               * were, there's no guarantee that the first frame with
               * a name represents a name at the _top_ of the
               * repository.  Following ancestry solves these
               * problems.
               *
               * kff todo: sleep on above reasoning.
               *
               * Remember that if any of the directories in the
               * chain has changed its name, then we wouldn't be
               * here anyway, because the delta should have set
               * ancestry attributes explicitly for everything
               * under that change.
               */

              derived_ancestor_path = svn_string_dup (p->ancestor_path, pool);
              svn_path_add_component (derived_ancestor_path, this_name, 

                                      SVN_PATH_REPOS_STYLE, pool);
              dest_frame->ancestor_path = derived_ancestor_path;
            }

          /* If ancestor_version not set, and see it here, then set it. */
          if ((dest_frame->ancestor_version < 0) && (p->ancestor_version >= 0))
            dest_frame->ancestor_version = p->ancestor_version;

          p = p->previous;
        }

      /* We don't check that an ancestor was actually found.  It's not
         this function's job to determine if an ancestor is necessary,
         only to find and set one if available. */
      if (! dest_frame->ancestor_path)
        dest_frame->ancestor_path = derived_ancestor_path;
    }
}


/* A validation note.  

   The strategy for validating our XML stream is simple:

   1. When we find a new "open" tag, make sure it logically follows
   the previous tag.  This is handled in do_stack_append().

   2. When we find a "close" tag, make sure the newest item on the
   stack is of the identical type.  This is handled by
   do_stack_remove().

   When these functions find invalid XML, they call signal_expat_bailout().

*/


/* Decide if it's valid XML to append NEW_FRAME to DIGGER's stack.  If
   so, append the frame and inherit the parent's baton.  If not,
   return a validity error. (TAGNAME is used for error message.) */
static svn_error_t *
do_stack_append (svn_delta__digger_t *digger, 
                 svn_delta__stackframe_t *new_frame,
                 const char *tagname)
{
  apr_pool_t *pool = digger->pool;
  svn_delta__stackframe_t *youngest_frame = digger->stack;

  if (youngest_frame == NULL)
    {
      /* The stack is empty, this is our first frame. */

      /* Make sure that it's indeed a tree-delta. */
      if (new_frame->tag != svn_delta__XML_treedelta)
        return XML_validation_error (pool, tagname, FALSE);

      digger->stack = new_frame;
      /* kff todo: parent_baton cannot be side effected, so why do we
         have it?  It will always be digger->dir_baton. */
      new_frame->baton = digger->dir_baton;
    }
  else   /* We already have a context, so check validity. */
    {
      /* <tree-delta> must follow <dir> */
      if ((new_frame->tag == svn_delta__XML_treedelta)
          && (youngest_frame->tag != svn_delta__XML_dir))
        return XML_validation_error (pool, tagname, FALSE);
      
      /* <new>, <replace> must follow <tree-delta> */
      else if ( ((new_frame->tag == svn_delta__XML_new)
                 || (new_frame->tag == svn_delta__XML_replace))
                && (youngest_frame->tag != svn_delta__XML_treedelta) )
        return XML_validation_error (pool, tagname, FALSE);

      /* <delete> must follow either <tree-delta> or <prop-delta> */
      else if ( (new_frame->tag == svn_delta__XML_delete)
                && (youngest_frame->tag != svn_delta__XML_treedelta)
                && (youngest_frame->tag != svn_delta__XML_propdelta) )
        return XML_validation_error (pool, tagname, FALSE);
      
      /* <file>, <dir> must follow either <new> or <replace> */
      else if ((new_frame->tag == svn_delta__XML_file)
               || (new_frame->tag == svn_delta__XML_dir))
        {
          if ((youngest_frame->tag != svn_delta__XML_new)
              && (youngest_frame->tag != svn_delta__XML_replace))
            return XML_validation_error (digger->pool, tagname, FALSE);
        }
      
      /* <prop-delta> must follow one of <new>, <replace> (if talking
         about a directory entry's properties) or must follow one of
         <file>, <dir> */
      else if ((new_frame->tag == svn_delta__XML_propdelta)
               && (youngest_frame->tag != svn_delta__XML_new)
               && (youngest_frame->tag != svn_delta__XML_replace)
               && (youngest_frame->tag != svn_delta__XML_file)
               && (youngest_frame->tag != svn_delta__XML_dir))
        return XML_validation_error (pool, tagname, FALSE);
      
      /* <text-delta> must follow <file> */
      else if ((new_frame->tag == svn_delta__XML_textdelta)
               && (youngest_frame->tag != svn_delta__XML_file))
        return XML_validation_error (pool, tagname, FALSE);

      /* <set> must follow <prop-delta> */
      else if ((new_frame->tag == svn_delta__XML_textdelta)
               && (youngest_frame->tag != svn_delta__XML_file))
        return XML_validation_error (pool, tagname, FALSE);

      /* ancestry information can only appear as <file> or <dir> attrs */
      else if ((new_frame->ancestor_path
                || (new_frame->ancestor_version >= 0))
               && (new_frame->tag != svn_delta__XML_file)
               && (new_frame->tag != svn_delta__XML_dir))
        return XML_validation_error (pool, tagname, FALSE);


      /* The XML is valid.  Do the append.  */
      youngest_frame->next = new_frame;

      /* Inherit parent's baton. */
      new_frame->baton = youngest_frame->baton; 

      /* Change digger's field accordingly. */
      digger->stack = new_frame;
    }
  
  /* Link backwards, too. */
  new_frame->previous = youngest_frame;

  /* Set up any unset ancestry information. */
  maybe_derive_ancestry (new_frame, pool);

  return SVN_NO_ERROR;
}




/* Decide if an xml closure TAGNAME is valid, by examining the
   youngest frame in DIGGER's stack.  If so, remove YOUNGEST_FRAME
   from the stack.  If not, return a validity error. */
static svn_error_t *
do_stack_remove (svn_delta__digger_t *digger, const char *tagname)
{
  apr_pool_t *pool = digger->pool;
  svn_delta__stackframe_t *youngest_frame = digger->stack;

  if (youngest_frame == NULL)
    return XML_validation_error (pool, tagname, TRUE);

  /* Validity check: Make sure the kind of object we're removing (due
     to an XML TAGNAME closure) actually agrees with the type of frame
     at the top of the stack.  This also filters out bogus values of
     TAGNAME. */
  if (strcmp (tagname, svn_delta__tagmap[youngest_frame->tag]))
    return XML_validation_error (pool, tagname, TRUE);
  
  /* Do the removal: lose the pointer to the youngest frame. */
  if (youngest_frame->previous) {
    digger->stack = youngest_frame->previous;
    digger->stack->next = NULL;
  }
  

  else  /* we must be removing the only frame in the stack */
    digger->stack = NULL;
      
  return SVN_NO_ERROR;
}



/* Utility:  set FRAME's tag field to an svn_delta__XML_t, according to NAME */
static svn_error_t *
set_tag_type (svn_delta__stackframe_t *frame,
              const char *name,
              svn_delta__digger_t *digger)
{
  int tag;
  
  for (tag = 0; svn_delta__tagmap[tag]; tag++)
    if (! strcmp (name, svn_delta__tagmap[tag]))
      {
        frame->tag = tag;
        return SVN_NO_ERROR;
      }
  
  return XML_validation_error (digger->pool, name, TRUE);
}




/* Called when we get a <dir> tag preceeded by either a <new> or
   <replace> tag; calls the appropriate callback inside
   DIGGER->WALKER, depending on the value of REPLACE_P. */
static svn_error_t *
do_directory_callback (svn_delta__digger_t *digger, 
                       svn_delta__stackframe_t *youngest_frame,
                       const char **atts,
                       svn_boolean_t replace_p)
{
  svn_error_t *err;
  void *child_baton;
  /* kff todo: should be (svn_string_t *) here?  I'm beginning to
     wonder if it's such a good idea to use svn_string_t in situations
     where (char *) is so much more natural, though... */
  const char *ancestor, *ver;
  svn_string_t *dir_name = NULL;

  /* Only proceed if the walker callback exists. */
  if (replace_p && (! digger->walker->replace_directory))
    return SVN_NO_ERROR;
  if ((! replace_p) && (! digger->walker->add_directory))
    return SVN_NO_ERROR;

  /* Retrieve the "name" field from the previous <new> or <replace> tag */
  dir_name = youngest_frame->previous->name;
  if (dir_name == NULL)
    return
      svn_create_error 
      (SVN_ERR_MALFORMED_XML, 0,
       "do_directory_callback: <dir>'s parent tag has no 'name' field.",
       NULL, digger->pool);
                             
  /* Search through ATTS, looking for any "ancestor" or "ver"
     attributes of the current <dir> tag. */
  ancestor = get_attribute_value (atts, "ancestor");
  if (ancestor)
    youngest_frame->ancestor_path = svn_string_create (ancestor, digger->pool);
  ver = get_attribute_value (atts, "ver");
  if (ver)
    youngest_frame->ancestor_version = atoi (ver);

  /* Call our walker's callback. */
  if (replace_p)
    err = (* (digger->walker->replace_directory)) 
      (dir_name,
       digger->walk_baton,
       youngest_frame->baton,
       youngest_frame->ancestor_path,
       youngest_frame->ancestor_version,
       &child_baton);
  else
    err = (* (digger->walker->add_directory)) 
      (dir_name,
       digger->walk_baton,
       youngest_frame->baton,
       youngest_frame->ancestor_path,
       youngest_frame->ancestor_version,
       &child_baton);

  if (err) 
    return err;

  /* Use the new value of CHILD_BATON as our future parent baton. */
  youngest_frame->baton = child_baton;

  /* Store in the digger, too, for safekeeping. */
  digger->dir_baton = child_baton;

  return SVN_NO_ERROR;
}



/* Called when we find a <delete> tag after a <tree-delta> tag. */
static svn_error_t *
do_delete_dirent (svn_delta__digger_t *digger, 
                  svn_delta__stackframe_t *youngest_frame)
{
  svn_string_t *dir_name = NULL;
  svn_error_t *err;

  /* Only proceed if the walker callback exists. */
  if (! (digger->walker->delete))
    return SVN_NO_ERROR;
  
  /* Retrieve the "name" field from the current <delete> tag */
  dir_name = youngest_frame->name;
  if (dir_name == NULL)
    return 
      svn_create_error 
      (SVN_ERR_MALFORMED_XML, 0,
       "do_delete_dirent: <delete> tag has no 'name' field.",
       NULL, digger->pool);

  /* Call our walker's callback */
  err = (* (digger->walker->delete)) (dir_name, 
                                      digger->walk_baton,
                                      youngest_frame->baton);
  if (err)
    return err;

  return SVN_NO_ERROR;
}



/* Called when we find a <delete> tag after a <prop-delta> tag. */
static svn_error_t *
do_delete_prop (svn_delta__digger_t *digger, 
                svn_delta__stackframe_t *youngest_frame)
{
  svn_string_t *dir_name = NULL;
  svn_error_t *err;
  
  /* If there's no pdelta_parser, then that means the caller just
     doesn't care about the particular <prop-delta> in progress.  Just
     go home. */
  if (! digger->pdelta_parser)
    return SVN_NO_ERROR;
      
  /* Retrieve the "name" field from the current <delete> tag */
  dir_name = youngest_frame->name;
  if (dir_name == NULL)
    return 
      svn_create_error 
      (SVN_ERR_MALFORMED_XML, 0,
       "do_delete_prop: <delete> tag has no 'name' field.",
       NULL, digger->pool);
  
  /* Finish filling out the propchange object in the parser: */
  digger->pdelta_parser->propchange->kind = svn_prop_delete;
  digger->pdelta_parser->propchange->name = 
    svn_string_dup (dir_name, digger->pdelta_parser->subpool);
  digger->pdelta_parser->propchange->value = NULL;
  
  err = (*(digger->pdelta_parser->handler)) (digger->pdelta_parser->propchange,
                                             digger->pdelta_parser->baton);
  if (err)
    return err;
  
  /* Now deallocate the parser's subpool, the one which was holding
     the propchange, and then create a new subpool for the next
     propchange. */
  svn_delta__reset_parser_subpool (digger->pdelta_parser);


  return SVN_NO_ERROR;
}





/* Called when we get <new> followed by <file>: the caller wants to know, call appropriate callback in our walk structure. */


static svn_error_t *
do_file_callback (svn_delta__digger_t *digger,
                  svn_delta__stackframe_t *youngest_frame,
                  const char **atts,
                  svn_boolean_t replace_p)
{
  svn_error_t *err;
  const char *ancestor, *ver;
  svn_string_t *dir_name = NULL;

  /* Only proceed if the walker callback exists. */
  if (replace_p && (! digger->walker->replace_file))
    return SVN_NO_ERROR;
  if ((! replace_p) && (! digger->walker->add_file))
    return SVN_NO_ERROR;

  /* Retrieve the "name" field from the previous <new> or <replace> tag */
  dir_name = youngest_frame->previous->name;
  if (dir_name == NULL)
    return 
      svn_create_error 
      (SVN_ERR_MALFORMED_XML, 0,
       "do_file_callback: <file>'s parent tag has no 'name' field.",
       NULL, digger->pool);
                             
  /* Search through ATTS, looking for any "ancestor" or "ver"
     attributes of the current <dir> tag. */
  ancestor = get_attribute_value (atts, "ancestor");
  if (ancestor)
    youngest_frame->ancestor_path = svn_string_create (ancestor, digger->pool);
  ver = get_attribute_value (atts, "ver");
  if (ver)
    youngest_frame->ancestor_version = atoi (ver);

  /* Call our walker's callback, and get back a window handler & baton. */
  if (replace_p)
    err = (* (digger->walker->replace_file)) 
      (dir_name,
       digger->walk_baton,
       youngest_frame->baton,
       youngest_frame->ancestor_path,
       youngest_frame->ancestor_version);
  else
    err = (* (digger->walker->add_file)) 
      (dir_name,
       digger->walk_baton,
       youngest_frame->baton,
       youngest_frame->ancestor_path,
       youngest_frame->ancestor_version);
  
  if (err)
    return err;
  
  return SVN_NO_ERROR;
}




/* Called when we get a </dir> tag */
static svn_error_t *
do_finish_directory (svn_delta__digger_t *digger)
{
  svn_error_t *err;

  /* Only proceed if the walker callback exists. */
  if (! (digger->walker->finish_directory))
    return SVN_NO_ERROR;

  /* Now: xml_handle_end() has just called do_stack_remove() on the
     youngest frame in the stack, which means the only place to find
     the old dir_baton is stashed inside digger.  Good thinking! */

  /* Nothing to do but caller the walker's callback, methinks. */
  err = (* (digger->walker->finish_directory)) (digger->dir_baton);
  if (err)
    return err;

  return SVN_NO_ERROR;
}


/* Called when we get a </file> tag */
static svn_error_t *
do_finish_file (svn_delta__digger_t *digger)
{
  svn_error_t *err;

  /* Drop the current parsers! */
  digger->vcdiff_parser = NULL;
  digger->pdelta_parser = NULL;

  /* Only proceed further if the walker callback exists. */
  if (! (digger->walker->finish_file))
    return SVN_NO_ERROR;

  /* Now: xml_handle_end() has just called do_stack_remove() on the
     youngest frame in the stack, which means the only place to find
     the old dir_baton is stashed inside digger.  Good thinking! */

  /* Call the walker's callback. */
  err = (* (digger->walker->finish_file)) (digger->dir_baton);
  if (err)
    return err;

  return SVN_NO_ERROR;
}



/* When we find a new text-delta, a walker callback returns to us a
   vcdiff-window-consumption routine that we use to create a unique
   vcdiff parser.  (The vcdiff parser knows how to "push" windows of
   vcdata to the consumption routine.)  */
static svn_error_t *
do_begin_textdelta (svn_delta__digger_t *digger)
{
  svn_error_t *err;
  svn_text_delta_window_handler_t *window_consumer;
  void *consumer_baton = NULL;

  if (digger->walker->begin_textdelta == NULL)
    return SVN_NO_ERROR;

  /* Get a window consumer & baton! */
  err = (* (digger->walker->begin_textdelta)) (digger->walk_baton,
                                               digger->dir_baton,
                                               &window_consumer,
                                               &consumer_baton);

  /* Now create a vcdiff parser based on the consumer/baton we got. */  
  digger->vcdiff_parser = svn_delta__make_vcdiff_parser (window_consumer,
                                                  consumer_baton,
                                                  digger->pool);
  return SVN_NO_ERROR;
}



/* When we find a new prop-delta */
static svn_error_t *
do_begin_propdelta (svn_delta__digger_t *digger)
{
  svn_error_t *err;
  svn_delta__stackframe_t *youngest_frame;
  svn_propchange_location_t location;
  svn_propchange_handler_t *consumer;
  void *consumer_baton = NULL;

  if (digger->walker->begin_propdelta == NULL)
    return SVN_NO_ERROR;

  /* First, figure out our context.  Is this a propdelta on a file,
     dir, or dirent? */

  youngest_frame = digger->stack;
  if (!youngest_frame->previous)
    return 
      svn_create_error 
      (SVN_ERR_MALFORMED_XML, 0,
       "do_begin_propdelta: <prop-delta> tag has no parent context",
       NULL, digger->pool);
  
  switch (youngest_frame->previous->tag)
    {
    case svn_delta__XML_file:
      {
        location = svn_prop_file;
        break;
      }
    case svn_delta__XML_dir:
      {
        location = svn_prop_dir;
        break;
      }
    case svn_delta__XML_new:
      {
        location = svn_prop_dirent;
        break;
      }
    case svn_delta__XML_replace:
      {
        location = svn_prop_dirent;
        break;
      }
    default:
      return 
        svn_create_error 
        (SVN_ERR_MALFORMED_XML, 0,
         "do_begin_propdelta: <prop-delta> tag has wonky context",
         NULL, digger->pool);
    }
   

  /* Get a propdelta consumer & baton! */
  err = (* (digger->walker->begin_propdelta)) (digger->walk_baton,
                                               digger->dir_baton,
                                               location,
                                               &consumer,
                                               &consumer_baton);

  /* Now create a pdelta parser based on the consumer/baton we got. */
  digger->pdelta_parser = svn_delta__make_pdelta_parser (consumer,
                                                         consumer_baton,
                                                         digger->pool);

  /* Fill out what fields we can in the parser's propchange object: */
  digger->pdelta_parser->loc = location;
  digger->pdelta_parser->propchange->loc = location;


  return SVN_NO_ERROR;
}



/* Clean up after finishing receiving a text-delta */
static svn_error_t *
do_finish_textdelta (svn_delta__digger_t *digger)
{
  svn_error_t *err;

  /* First, don't forget to flush out any remaining bytes sitting in
     the buffer of our vcdiff parser!  */
  err = svn_delta__vcdiff_flush_buffer (digger->vcdiff_parser);

  /* Now call the walker callback, if it exists */
  if (digger->walker->finish_textdelta)
    {
      err = digger->walker->finish_textdelta
        (digger->walk_baton,
         digger->dir_baton,
         digger->vcdiff_parser->consumer_baton);

      if (err)
        return err;
    }

  return SVN_NO_ERROR;
}



/* Clean up after finishing receiving a prop-delta */
static svn_error_t *
do_finish_propdelta (svn_delta__digger_t *digger)
{
  svn_error_t *err;

  if (!digger->pdelta_parser)
    return SVN_NO_ERROR;

  /* Call the walker callback, if it exists */
  if (digger->walker->finish_propdelta)
    {
      err = digger->walker->finish_propdelta (digger->walk_baton,
                                              digger->dir_baton,
                                              digger->pdelta_parser->baton,
                                              digger->pdelta_parser->loc);
      if (err)
        return err;
    }

  return SVN_NO_ERROR;
}


/* When we get a <set>, add the "name" field to the pdelta_parser's
   propchange object. */
static svn_error_t *
do_begin_setprop (svn_delta__digger_t *digger,
                  svn_delta__stackframe_t *youngest_frame)
{
  if (!digger->pdelta_parser)
    return SVN_NO_ERROR;

  digger->pdelta_parser->propchange->name =
    svn_string_dup (youngest_frame->name, digger->pdelta_parser->subpool);
  
  return SVN_NO_ERROR;
}



/* When we get a </set>, send off the buffered name/value within
   digger->pdelta_parser->propchange */
static svn_error_t *
do_finish_setprop (svn_delta__digger_t *digger)
{
  svn_error_t *err;

  if (!digger->pdelta_parser)
    return SVN_NO_ERROR;

  /* Our pdelta_parser must have buffered a complete "value" string.
     Let's send it off! */

  /* Finish filling out the propchange object: */
  digger->pdelta_parser->propchange->kind = svn_prop_set;
  /* propchange->value has been growing all along */

  err = (*(digger->pdelta_parser->handler)) (digger->pdelta_parser->propchange,
                                             digger->pdelta_parser->baton);

  if (err)
    return err;

  /* Now deallocate the parser's subpool, the one which was holding
     the propchange, and then create a new subpool for the next
     propchange. */
  svn_delta__reset_parser_subpool (digger->pdelta_parser);

  return SVN_NO_ERROR;
}







/* Callback:  called whenever expat finds a new "open" tag.

   NAME contains the name of the tag.
   ATTS is a dumb list of tag attributes;  a list of name/value pairs, all
   null-terminated Cstrings, and ending with an extra final NULL.
*/  
static void
xml_handle_start (void *userData, const char *name, const char **atts)
{
  svn_error_t *err;
  const char *value;

  /* Resurrect our digger structure */
  svn_delta__digger_t *my_digger = (svn_delta__digger_t *) userData;

  /* Create new stackframe */
  svn_delta__stackframe_t *new_frame
    = apr_pcalloc (my_digger->pool, sizeof (svn_delta__stackframe_t));

  /* Initialize the ancestor version to a recognizably invalid value. */
  new_frame->ancestor_version = -1;

  /* Set the tag field */
  err = set_tag_type (new_frame, name, my_digger);
  if (err)
    {
      /* Uh-oh, unrecognized tag, bail out. */
      signal_expat_bailout (err, my_digger);
      return;
    }

  /* Set "name" field in frame, if there's any such attribute in ATTS */
  value = get_attribute_value (atts, "name");
  if (value)
    new_frame->name = svn_string_create (value, my_digger->pool);
  
  /* Set ancestor path in frame, if there's any such attribute in ATTS */
  value = get_attribute_value (atts, "ancestor");
  if (value)
    new_frame->ancestor_path = svn_string_create (value, my_digger->pool);
  
  /* Set ancestor version in frame, if there's any such attribute in ATTS */
  value = get_attribute_value (atts, "ver");
  if (value)
    new_frame->ancestor_version = atoi (value);
  
  /*  Append new frame to stack, validating in the process. 
      If successful, new frame will automatically inherit parent's baton. */
  err = do_stack_append (my_digger, new_frame, name);
  if (err) {
    /* Uh-oh, invalid XML, bail out. */
    signal_expat_bailout (err, my_digger);
    return;
  }

  /* Now look for special events that the uber-caller (of
     svn_delta_parse()) might want to know about.  */

  /* EVENT:  Are we adding a new directory?  */
  if (new_frame->previous)
    if ((new_frame->previous->tag == svn_delta__XML_new) 
        && (new_frame->tag == svn_delta__XML_dir))
      {
        err = do_directory_callback (my_digger, new_frame, atts, FALSE);
        if (err)
          signal_expat_bailout (err, my_digger);
        return;
      }

  /* EVENT:  Are we replacing a directory?  */
  if (new_frame->previous)
    if ((new_frame->previous->tag == svn_delta__XML_replace) 
        && (new_frame->tag == svn_delta__XML_dir))
      {
        err = do_directory_callback (my_digger, new_frame, atts, TRUE);
        if (err)
          signal_expat_bailout (err, my_digger);
        return;
      }  

  /* EVENT:  Are we deleting a directory entry?  */
  if (new_frame->previous)
    if ( (new_frame->tag == svn_delta__XML_delete) &&
         (new_frame->previous->tag == svn_delta__XML_treedelta) )
      {
        err = do_delete_dirent (my_digger, new_frame);
        if (err)
          signal_expat_bailout (err, my_digger);
        return;
      }

  /* EVENT:  Are we deleting a property?  */
  if (new_frame->previous)
    if ( (new_frame->tag == svn_delta__XML_delete) &&
         (new_frame->previous->tag == svn_delta__XML_propdelta) )
      {
        err = do_delete_prop (my_digger, new_frame);
        if (err)
          signal_expat_bailout (err, my_digger);
        return;
      }


  /* EVENT:  Are we adding a new file?  */
  if (new_frame->previous)
    if ((new_frame->previous->tag == svn_delta__XML_new) 
        && (new_frame->tag == svn_delta__XML_file))
      {
        err = do_file_callback (my_digger, new_frame, atts, FALSE);
        if (err)
          signal_expat_bailout (err, my_digger);
        return;
      }

  /* EVENT:  Are we replacing a file?  */
  if (new_frame->previous)
    if ((new_frame->previous->tag == svn_delta__XML_replace) 
        && (new_frame->tag == svn_delta__XML_file))
      {
        err = do_file_callback (my_digger, new_frame, atts, TRUE);
        if (err)
          signal_expat_bailout (err, my_digger);
        return;
      }

  /* EVENT:  Are we starting a new text-delta?  */
  if (new_frame->tag == svn_delta__XML_textdelta) 
    {
      err = do_begin_textdelta (my_digger);
      if (err)
        signal_expat_bailout (err, my_digger);
      return;
    }

  /* EVENT:  Are we starting a new prop-delta?  */
  if (new_frame->tag == svn_delta__XML_propdelta) 
    {
      err = do_begin_propdelta (my_digger);
      if (err)
        signal_expat_bailout (err, my_digger);
      return;
    }

  /* EVENT:  Are we starting a new prop-delta?  */
  if (new_frame->tag == svn_delta__XML_set) 
    {
      err = do_begin_setprop (my_digger, new_frame);
      if (err)
        signal_expat_bailout (err, my_digger);
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
  svn_delta__digger_t *my_digger = (svn_delta__digger_t *) userData;

  /*  Remove youngest frame from stack, validating in the process. */
  err = do_stack_remove (my_digger, name);
  if (err) {
    /* Uh-oh, invalid XML, bail out */
    signal_expat_bailout (err, my_digger);
    return;
  }
  
  /* Now look for special events that the uber-caller (of
     svn_delta_parse()) might want to know about.  */

  /* EVENT:  When we get a </dir> pass back the dir_baton. */
  if (strcmp (name, "dir") == 0)
    {
      err = do_finish_directory (my_digger);
      if (err)
        signal_expat_bailout (err, my_digger);
      return;
    }      

  /* EVENT: when we get a </file>, drop our digger's parsers. */
  if (strcmp (name, "file") == 0)
    {
      err = do_finish_file (my_digger);
      if (err)
        signal_expat_bailout (err, my_digger);
      return;
    }

  /* EVENT: when we get a </text-delta> */
  if (strcmp (name, "text-delta") == 0)
    {
      err = do_finish_textdelta (my_digger);
      if (err)
        signal_expat_bailout (err, my_digger);
    }

  /* EVENT: when we get a </prop-delta> */
  if (strcmp (name, "prop-delta") == 0)
    {
      err = do_finish_propdelta (my_digger);
      if (err)
        signal_expat_bailout (err, my_digger);
    }


  /* EVENT: when we get a </set>, send off the propchange. */
  if (strcmp (name, "set") == 0)
    {
      err = do_finish_setprop (my_digger);
      if (err)
        signal_expat_bailout (err, my_digger);
    }



  /* This is a void expat callback, don't return anything. */
}




/* Callback: called whenever expat finds data _between_ an open/close
   tagpair. */
static void 
xml_handle_data (void *userData, const char *data, int len)
{
  apr_off_t numbytes = len;

  /* Resurrect digger structure */
  svn_delta__digger_t *digger = (svn_delta__digger_t *) userData;

  /* Figure out the context of this callback.  If we're currently
     inside a <text-delta> or <prop-delta>, that's great.  If not,
     then we've got some erroneous data flying around our XML, and we
     should return an error. */

  svn_delta__stackframe_t *youngest_frame = digger->stack;

  if (youngest_frame == NULL) {
    svn_error_t *err = svn_create_error (SVN_ERR_MALFORMED_XML, 0,
                                         "xml_handle_data: no XML context!",
                                         NULL, digger->pool);
    signal_expat_bailout (err, digger);
    return;
  }

  if (youngest_frame->tag == svn_delta__XML_textdelta)
    {
      svn_error_t *err;
      
      /* Check that we have a vcdiff parser to deal with this data. */
      if (! digger->vcdiff_parser)
        return;

      /* Pass the data to our current vcdiff parser.  When the vcdiff
         parser has received enough bytes to make a "window", it
         pushes the window to the uber-caller's own window-consumer
         routine. */
      err = svn_delta__vcdiff_parse (digger->vcdiff_parser, data, &numbytes);
      if (err)
        {
          signal_expat_bailout
            (svn_quick_wrap_error
             (err, "xml_handle_data: vcdiff parser choked."),
             digger);
          return;
        }                          
    }

  else if (youngest_frame->tag == svn_delta__XML_set)
    {
      /* We're about to receive the "value" data for a prop-delta
         `set' command.  (The "name" data is already stored in the
         current stackframe's "name" field, since expat gave us the
         whole thing as an XML attribute.) */
      svn_error_t *err;
      
      /* Check that we have a pdelta parser to deal with the incoming
         data. */
      if (! digger->pdelta_parser)
        return;

      /* Pass the data to our current pdelta parser, so it can buffer
         up the whole value in RAM. */
      err = svn_delta__pdelta_parse (digger, data, &numbytes);
      if (err)
        {
          signal_expat_bailout
            (svn_quick_wrap_error
             (err, "xml_handle_data: pdelta parser choked."),
             digger);
          return;
        }                          
    }

  else
    {
      /* the data must be outside the bounds of a
      <text-delta></text-delta> or a <prop-delta></prop-delta>.  Just
      ignore it.  (It's probably whitespace -- expat sends us
      whitespace frequently.)*/
    }

  /* This is a void expat callback, don't return anything. */
}



/* Return an expat parser object which uses our svn_xml_* routines
   above as callbacks.  */

static XML_Parser
make_xml_parser (svn_delta__digger_t *diggy)
{
  /* Create the parser */
  XML_Parser parser = XML_ParserCreate (NULL);

  /* All callbacks should receive the digger structure. */
  XML_SetUserData (parser, diggy);

  /* Register subversion-specific callbacks with the parser */
  XML_SetElementHandler (parser,
                         xml_handle_start,
                         xml_handle_end); 
  XML_SetCharacterDataHandler (parser, xml_handle_data);

  return parser;
}


/*------------------------------------------------------------------*/
/* The _one_ public interface in this file.  (see svn_delta.h)

  svn_delta_parse() reads an XML stream from a specified source using
  expat, validating the XML as it goes.  Whenever an interesting event
  happens, it calls a caller-specified callback routine from an
  svn_walk_t structure.

*/

svn_error_t *
svn_delta_parse (svn_delta_read_fn_t *source_fn,
                 void *source_baton,
                 svn_delta_walk_t *walker,
                 void *walk_baton,
                 void *dir_baton,
                 apr_pool_t *pool)
{
  char buf[BUFSIZ];
  apr_off_t len;
  int done;
  svn_error_t *err = NULL;
  XML_Parser expat_parser;

  /* Create a digger structure */
  svn_delta__digger_t *digger
    = apr_pcalloc (pool, sizeof (svn_delta__digger_t));

  digger->pool             = pool;
  digger->stack            = NULL;
  digger->walker           = walker;
  digger->walk_baton       = walk_baton;
  digger->dir_baton        = dir_baton;
  digger->validation_error = SVN_NO_ERROR;
  digger->vcdiff_parser    = NULL;
  digger->pdelta_parser    = NULL;

  /* Create a custom expat parser (uses our own svn callbacks and
     hands them the digger on each XML event) */
  expat_parser = make_xml_parser (digger);

  /* Store the parser in the digger too, so that our expat callbacks
     can magically set themselves to NULL in the case of an error. */

  digger->expat_parser = expat_parser;


  /* Our main parse-loop */

  do {
    /* Read BUFSIZ bytes into buf using the supplied read function. */
    len = BUFSIZ;
    err = (*(source_fn)) (source_baton, buf, &len, digger->pool);
    if (err)
      {
        err = svn_quick_wrap_error (err,
                                    "svn_delta_parse: can't read data source");
        goto error;
      }

    /* How many bytes were actually read into buf?  According to the
       definition of an svn_delta_read_fn_t, we should keep reading
       until the reader function says that 0 bytes were read. */
    done = (len == 0);
    
    /* Parse the chunk of stream. */
    if (! XML_Parse (expat_parser, buf, len, done))
    {
      /* Uh oh, expat *itself* choked somehow.  Return its message. */
      err = svn_create_error
        (SVN_ERR_MALFORMED_XML, 0,
         apr_psprintf (pool, "%s at line %d",
                       XML_ErrorString (XML_GetErrorCode (expat_parser)),
                       XML_GetCurrentLineNumber (expat_parser)),
         NULL, pool);
      goto error;
    }

    /* After parsing our chunk, check to see if anybody called
       signal_expat_bailout() */
    if (digger->validation_error)
      {
        err = digger->validation_error;
        goto error;
      }

  } while (! done);


 error:
  /* Done parsing entire SRC_BATON stream, so clean up and return. */
  XML_ParserFree (expat_parser);

  return err;
}




/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
