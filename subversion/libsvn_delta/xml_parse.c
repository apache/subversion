/*
 * delta_parse.c:  parse an Subversion "tree-delta" XML stream
 *
 * ================================================================
 * Copyright (c) 2000 Collab.Net.  All rights reserved.
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
 * software developed by Collab.Net (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of Collab.Net.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLAB.NET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
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
 * individuals on behalf of Collab.Net.
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
#include "svn_error.h"
#include "svn_delta.h"
#include "apr_strings.h"
#include "xmlparse.h"






/* Search for NAME in expat ATTS array, place into VALUE.
   If NAME does not exist in ATTS, return simple error. */
static svn_error_t *
get_attribute_value (const char **atts, char *name, char **value)
{
  /* TODO */

  return SVN_ERR_XML_ATTRIB_NOT_FOUND;

  return SVN_NO_ERROR;
}



/* Return the newest frame of an XML stack. (The "top" of the stack.) */
static svn_delta_stackframe_t *
find_stack_newest (svn_delta_stackframe_t *frame)
{
  if (frame)
    {
      if (frame->next == NULL)
        return frame;
      else
        return find_stack_newest (frame->next);
    }
  else
    return NULL;
}



/* The way to officially bail out of expat. 
   
   Store ERROR in DIGGER and set all expat callbacks to NULL. (To
   understand why this works, see svn_delta_parse(). ) */
static void
signal_expat_bailout (svn_error_t *error, svn_delta_digger_t *digger)
{
  /* This will cause the current XML_Parse() call to finish quickly! */
  XML_SetElementHandler (digger->expat_parser, NULL, NULL);
  XML_SetCharacterDataHandler (digger->expat_parser, NULL);

  /* Once outside of XML_Parse(), the existence of this field will
     cause svn_delta_parse()'s main read-loop to return error. */
  digger->validation_error = err;
}



/* Return an informative error message about invalid XML.
   (Set DESTROY_P to indicate an unexpected closure tag) */
static svn_error_t *
XML_type_error (apr_pool_t *pool, const char *name, svn_boolean_t destroy_p)
{
  if (destroy_p)
    char *msg = 
      apr_psprintf (pool, "XML validation error: got unexpected </%s>", name);

  else
    char *msg = 
      apr_psprintf (pool, "XML validation error: got unexpected <%s>", name);

  return svn_create_error (SVN_ERR_MALFORMED_XML, 0, msg, NULL, pool);
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
do_stack_append (svn_delta_digger_t *digger, 
                 svn_delta_stackframe_t *new_frame,
                 const char *tagname)
{
  apr_pool_t *pool = digger->pool;

  /* Get a grip on the youngest frame of the stack */
  svn_delta_stackframe_t *youngest_frame = find_stack_newest (digger->stack);

  if (youngest_frame == NULL)
    {
      /* The stack is empty, this is our first frame append. */
      digger->stack = new_frame;
      return SVN_NO_ERROR;
    }
  
  /* Validity checks */

  /* <tree-delta> must follow <dir> */
  if ((new_frame->tag == svn_XML_treedelta)
      && (youngest_frame->tag != svn_XML_dir))
    return XML_type_error (pool, tagname, FALSE);

  /* <new>, <replace>, <delete> must all follow <tree-delta> */
  else if ( ((new_frame->tag == svn_XML_new)
             || (new_frame->tag == svn_XML_replace)
             || (new_frame->tag == svn_XML_delete))
            && (youngest_frame->tag != svn_XML_treedelta) )
        return XML_type_error (pool, tagname, FALSE);

  /* <file>, <dir> must follow either <new> or <replace> */
  else if ((new_frame->tag == svn_XML_file)
           || (new_frame->tag == svn_XML_dir))
    {
      if ((youngest_frame->tag != svn_XML_new)
          && (youngest_frame->tag != svn_XML_replace))
        return XML_type_error (digger->pool, tagname, FALSE);
    }

  /* <prop-delta> must follow one of <new>, <replace> (if talking
     about a directory entry's properties) or must follow one of
     <file>, <dir> */
  else if ((new_frame->tag == svn_XML_propdelta)
           && (youngest_frame->tag != svn_XML_new)
           && (youngest_frame->tag != svn_XML_replace)
           && (youngest_frame->tag != svn_XML_file)
           && (youngest_frame->tag != svn_XML_dir))
    return XML_type_error (pool, tagname, FALSE);

  /* <text-delta> must follow <file> */
  else if ((new_frame->tag == svn_XML_textdelta)
           && (youngest_frame->tag != svn_XML_file))
    return XML_type_error (pool, tagname, FALSE);


  /* Validity passed, do the actual append. */
  
  youngest_frame->next = new_frame;
  new_frame->previous = youngest_frame;

  /* Child inherits parent's baton.  */
  new_frame->baton = youngest_frame->baton;

  return SVN_NO_ERROR;
}




/* Decide if an xml closure TAGNAME is valid, by examining the
   youngest frame in DIGGER's stack.  If so, remove YOUNGEST_FRAME
   from the stack.  If not, return a validity error. */
static svn_error_t *
do_stack_remove (svn_delta_digger_t *digger, const char *tagname)
{
  apr_pool_t *pool = digger->pool;

  /* Get a grip on the youngest frame of the stack */
  svn_delta_stackframe_t *youngest_frame = find_stack_newest (digger->stack);

  if (youngest_frame == NULL)
    return XML_type_error (pool, tagname, TRUE);

  /* Validity check: Make sure the kind of object we're removing (due
     to an XML TAGNAME closure) actually agrees with the type of frame
     at the bottom of the stack! */
  
  if ((strcmp (tagname, "tree-delta") == 0)
      && (youngest_frame->tag != svn_XML_treedelta))
    return XML_type_error (pool, tagname, TRUE);

  else if ((strcmp (tagname, "new") == 0)
           && (youngest_frame->tag != svn_XML_new))
    return XML_type_error (pool, tagname, TRUE);

  else if ((strcmp (tagname, "delete") == 0)
           && (youngest_frame->tag != svn_XML_del))
    return XML_type_error (pool, tagname, TRUE);

  else if ((strcmp (tagname, "replace") == 0)
           && (youngest_frame->tag != svn_XML_replace))
    return XML_type_error (pool, tagname, TRUE);

  else if ((strcmp (tagname, "file") == 0)
           && (youngest_frame->tag != svn_XML_file))
    return XML_type_error (pool, tagname, TRUE);

  else if ((strcmp (tagname, "dir") == 0)
           && (youngest_frame->tag != svn_XML_dir))
    return XML_type_error (pool, tagname, TRUE);

  else if ((strcmp (tagname, "textdelta") == 0)
           && (youngest_frame->tag != svn_XML_textdelta))
    return XML_type_error (pool, tagname, TRUE);

  else if ((strcmp (tagname, "propdelta") == 0)
           && (youngest_frame->tag != svn_XML_propdelta))
    return XML_type_error (pool, tagname, TRUE);


  /* Passed validity check, do the removal. */

  /* Lose the pointer to the youngest frame. */
  if (youngest_frame->previous)
    youngest_frame->previous->next = NULL;

  else  /* we must be removing the only frame in the stack */
    digger->stack = NULL;
      
  return SVN_NO_ERROR;
}



/* Utility:  set FRAME's tag field to an svn_XML_t, according to NAME */
static svn_error_t *
set_tag_type (svn_delta_stackframe_t *frame, char *name);
{
  if (strcmp (name, "tree-delta") == 0)
    new_frame->tag = svn_XML_treedelta;
  else if (strcmp (name, "new") == 0)
    new_frame->tag = svn_XML_new;
  else if (strcmp (name, "replace") == 0)
    new_frame->tag = svn_XML_replace;
  else if (strcmp (name, "delete") == 0)
    new_frame->tag = svn_XML_delete;
  else if (strcmp (name, "file") == 0)
    new_frame->tag = svn_XML_file;
  else if (strcmp (name, "dir") == 0)
    new_frame->tag = svn_XML_dir;
  else if (strcmp (name, "text-delta") == 0)
    new_frame->tag = svn_XML_textdelta;
  else if (strcmp (name, "prop-delta") == 0)
    new_frame->tag = svn_XML_propdelta;
  else 
    return XML_type_error (my_digger->pool, name, TRUE);

  return SVN_NO_ERROR;
}




/* Called when we get a <dir> tag preceeded by either a <new> or
   <replace> tag; calls the appropriate callback inside
   DIGGER->WALKER, depending on the value of REPLACE_P. */
static svn_error_t *
do_directory_callback (svn_delta_digger_t *digger, 
                       svn_delta_stackframe_t *youngest_frame,
                       const char **atts,
                       svn_boolean_t replace_p)
{
  svn_error_t *err;
  void *child_baton;
  char *ancestor, *ver;
  svn_pdelta_t *pdelta = NULL;
  svn_string_t *base_path = NULL;
  svn_version_t base_version = 0;

  /* Retrieve the "name" field from the previous <new> or <replace> tag */
  svn_string_t *dir_name = youngest_frame->previous->name;
  if (dir_name == NULL)
    return 
      svn_create_error 
      (SVN_ERR_MALFORMED_XML, 0,
       "do_directory_callback: <dir>'s parent tag has no 'name' field."
       NULL, digger->pool);
                             
  /* Search through ATTS, looking for any "ancestor" or "ver"
     attributes of the current <dir> tag. */
  err = get_attribute_value (atts, "ancestor", &ancestor);
  if (! err)
    base_path = svn_string_create (ancestor, digger->pool);
  err = get_attribute_value (atts, "ver", &ver);
  if (! err)
    base_version = atoi (ver);

  /* Call our walker's callback. */
  if (replace_p)
    err = (* (digger->walker->replace_directory)) (dir_name,
                                                   digger->walk_baton,
                                                   youngest_frame->baton,
                                                   base_path,
                                                   base_version,
                                                   pdelta,
                                                   &child_baton);
  else
    err = (* (digger->walker->add_directory)) (dir_name,
                                               digger->walk_baton,
                                               youngest_frame->baton,
                                               base_path,
                                               base_version,
                                               pdelta,
                                               &child_baton);
  if (err) 
    return err;

  /* Use the new value of CHILD_BATON as our future parent baton. */
  youngest_frame->baton = child_baton;

  return SVN_NO_ERROR;
}



/* Called when we find a <delete> tag */
static svn_error_t *
do_delete_dirent (svn_delta_digger_t *digger, 
                  svn_delta_stackframe_t *youngest_frame)
{
  /* Retrieve the "name" field from the current <delete> tag */
  svn_string_t *dir_name = youngest_frame->previous->name;
  if (dir_name == NULL)
    return 
      svn_create_error 
      (SVN_ERR_MALFORMED_XML, 0,
       "do_delete_dirent: <delete> tag has no 'name' field."
       NULL, digger->pool);

  /* Call our walker's callback */
  err = (* (digger->walker->delete)) (dir_name, 
                                      digger->walk_baton,
                                      youngest_frame->baton);
  if (err)
    return err;

  return SVN_NO_ERROR;
}




/* Called when we get a <new> tag followed by a <file> tag */
static svn_error_t *
do_add_file (svn_delta_digger_t *digger, svn_delta_stackframe_t *frame)
{
  /* We're assuming that FRAME is of type <file>.  See if it has any
     ancestry attributes attached to it.  */

  /* Now call (* (digger->walker->add_file)) and get back a
     handler/baton combo */

  /* Now call svn_make_vcdiff_parser() with the handler/baton, and
     store the new parser in digger->vcdiff_parser */

  /* TODO: still ignoring the question of add_file()'s *pdelta field.
     Do we need a parser for this too? */

}




/* Called when we get a </dir> tag */
static svn_error_t *
do_finish_directory (svn_delta_digger_t *digger)
{
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
  char *value;

  /* Resurrect our digger structure */
  svn_delta_digger_t *my_digger = (svn_delta_digger_t *) userData;

  /* Create new stackframe */
  svn_delta_stackframe_t *new_frame 
    = apr_pcalloc (my_digger->pool, sizeof (svn_delta_stackframe_t));

  /* Set the tag field */
  err = set_tag_type (new_frame, name);
  if (err)
    {
      /* Uh-oh, unrecognized tag, bail out. */
      signal_expat_bailout (err, my_digger);
      return;
    }

  /* Set "name" field in frame, if there's any such attribute in ATTS */
  err = get_attribute_value (atts, "name", &value);
  if (! err)
    frame->name = svn_string_create (value, digger->pool);
  
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
    if ((new_frame->previous->tag == svn_XML_new) 
        && (new_frame->tag == svn_XML_dir))
      {
        err = do_directory_callback (my_digger, new_frame, atts, FALSE);
        if (err)
          signal_expat_bailout (err, my_digger);
        return;
      }

  /* EVENT:  Are we replacing a directory?  */
  if (new_frame->previous)
    if ((new_frame->previous->tag == svn_XML_replace) 
        && (new_frame->tag == svn_XML_dir))
      {
        err = do_directory_callback (my_digger, new_frame, atts, TRUE);
        if (err)
          signal_expat_bailout (err, my_digger);
        return;
      }

  /* EVENT:  Are we adding a new file?  */
  if (new_frame->previous)
    if ((new_frame->previous->tag == svn_XML_new) 
        && (new_frame->tag == svn_XML_file))
      do_add_file (my_digger, new_frame);

  /* EVENT:  Are we replacing a file?  */
  if (new_frame->previous)
    if ((new_frame->previous->tag == svn_XML_replace) 
        && (new_frame->tag == svn_XML_file))
      do_replace_file (my_digger, new_frame);
  

  /* EVENT:  Are we deleting a directory entry?  */
  if (new_frame->tag == svn_XML_delete)
    {
      err = do_delete_dirent (my_digger, new_frame);
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
  svn_delta_digger_t *my_digger = (svn_delta_digger_t *) userData;

  /*  Remove youngest frame from stack, validating in the process. */
  err = do_stack_remove (my_digger, name);
  if (err) {
    /* Uh-oh, invalid XML, bail out */
    signal_expat_bailout (err, my_digger);
    return;
  }
  
  /* TODO:  remove this line! */
  printf ("Got tag: </%s>\n", name);

  /* Now look for special events that the uber-caller (of
     svn_delta_parse()) might want to know about.  */

  /* EVENT:  Are we finished processing a directory? */
  if (strcmp (name, "dir") == 0)
    do_finish_directory (my_digger);


  /* This is a void expat callback, don't return anything. */
}




/* Callback: called whenever we find data within a tag.  
   (Of course, we only care about data within the "text-delta" tag.)  */

static void 
svn_xml_handle_data (void *userData, const char *data, int len)
{
  /* Resurrect digger structure */
  svn_delta_digger_t *my_digger = (svn_delta_digger_t *) userData;

  /* TODO: check to see that we're either inside a text-delta or
     prop-delta stackframe right now.  Then pass DATA to
     digger->vcdiff_parser */

}



/* Return an expat parser object which uses our svn_xml_* routines
   above as callbacks.  */

static XML_Parser
svn_delta_make_xml_parser (svn_delta_digger_t *diggy)
{
  /* Create the parser */
  XML_Parser parser = XML_ParserCreate (NULL);

  /* All callbacks should receive the digger structure. */
  XML_SetUserData (parser, diggy);

  /* Register subversion-specific callbacks with the parser */
  XML_SetElementHandler (parser,
                         xml_handle_start,
                         xml_handle_end); 
  XML_SetCharacterDataHandler (parser, svn_xml_handle_data);

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
  int len;
  int done;
  svn_error_t *err;
  XML_Parser expat_parser;

  /* Create a digger structure */
  svn_digger_t *digger = apr_pcalloc (pool, sizeof (svn_digger_t));

  digger->pool             = pool;
  digger->stack            = NULL;
  digger->walker           = walker;
  digger->walk_baton       = walk_baton;
  digger->dir_baton        = dir_baton;
  digger->validation_error = SVN_NO_ERROR;

  /* Create a custom expat parser (uses our own svn callbacks and
     hands them the digger on each XML event) */
  expat_parser = svn_delta_make_xml_parser (digger);

  /* Store the parser in the digger too, so that our expat callbacks
     can magically set themselves to NULL in the case of an error. */

  digger->expat_parser = parser;


  /* Our main parse-loop */

  do {
    /* Read BUFSIZ bytes into buf using the supplied read function. */
    len = BUFSIZ;
    err = (*(source_fn)) (source_baton, buf, &len);
    if (err)
      return 
        svn_quick_wrap_error (err, "svn_delta_parse: can't read data source");

    /* How many bytes were actually read into buf?  According to the
       definition of an svn_delta_read_fn_t, we should keep reading
       until the reader function says that 0 bytes were read. */
    done = (len == 0);
    
    /* Parse the chunk of stream. */
    if (! XML_Parse (expat_parser, buf, len, done))
    {
      /* Uh oh, expat *itself* choked somehow.  Return its message. */
      svn_error_t *err
        = svn_create_error
        (SVN_ERR_MALFORMED_XML, 0,
         apr_psprintf (pool, "%s at line %d",
                       XML_ErrorString (XML_GetErrorCode (parsimonious)),
                       XML_GetCurrentLineNumber (parsimonious)),
         NULL, pool);
      XML_ParserFree (expat_parser);
      return err;
    }

    /* After parsing our chunk, check to see if anybody called
       signal_expat_bailout() */
    if (digger->validation_error)
      return digger->validation_error;

  } while (! done);


  /* Done parsing entire SRC_BATON stream, so clean up and return. */
  XML_ParserFree (expat_parser);

  return SVN_NO_ERROR;
}






/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
