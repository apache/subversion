/*
 * xml.c:  xml helper code shared among the Subversion libraries.
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



#include <string.h>
#include <assert.h>
#include "apr_pools.h"
#include "svn_xml.h"




/*** XML escaping. ***/

/* Return an xml-safe version of STRING. */
static svn_string_t *
escape_string (svn_string_t *string, apr_pool_t *pool)
{
  const char *start = string->data, *end = start + string->len;
  const char *p = start, *q;
  svn_string_t *newstr = svn_string_create ("", pool);

  while (1)
    {
      /* Find a character which needs to be quoted and append bytes up
         to that point.  Strictly speaking, '>' only needs to be
         quoted if it follows "]]", but it's easier to quote it all
         the time.  */
      q = p;
      while (q < end && *q != '&' && *q != '<' && *q != '>'
             && *q != '"' && *q != '\'')
        q++;
      svn_string_appendbytes (newstr, p, q - p, pool);

      /* We may already be a winner.  */
      if (q == end)
        break;

      /* Append the entity reference for the character.  */
      if (*q == '&')
        svn_string_appendcstr (newstr, "&amp;", pool);
      else if (*q == '<')
        svn_string_appendcstr (newstr, "&lt;", pool);
      else if (*q == '>')
        svn_string_appendcstr (newstr, "&gt;", pool);
      else if (*q == '"')
        svn_string_appendcstr (newstr, "&quot;", pool);
      else if (*q == '\'')
        svn_string_appendcstr (newstr, "&apos;", pool);

      p = q + 1;
    }
  return newstr;
}




/*** Making a parser. ***/

svn_xml_parser_t *
svn_xml_make_parser (void *userData,
                     XML_StartElementHandler start_handler,
                     XML_EndElementHandler end_handler,
                     XML_CharacterDataHandler data_handler,
                     apr_pool_t *pool)
{
  svn_xml_parser_t *svn_parser;
  apr_pool_t *subpool;

  XML_Parser parser = XML_ParserCreate (NULL);

  XML_SetUserData (parser, userData);
  XML_SetElementHandler (parser, start_handler, end_handler); 
  XML_SetCharacterDataHandler (parser, data_handler);

  subpool = svn_pool_create (pool, NULL);

  svn_parser = apr_pcalloc (subpool, sizeof (svn_xml_parser_t));

  svn_parser->parser = parser;
  svn_parser->pool   = subpool;

  return svn_parser;
}


/* Free a parser */
void
svn_xml_free_parser (svn_xml_parser_t *svn_parser)
{
  /* Free the expat parser */
  XML_ParserFree (svn_parser->parser);        

  /* Free the subversion parser */
  apr_destroy_pool (svn_parser->pool);
}




/* Push LEN bytes of xml data in BUF at SVN_PARSER.  If this is the
   final push, IS_FINAL must be set.  */
svn_error_t *
svn_xml_parse (svn_xml_parser_t *svn_parser,
               const char *buf,
               apr_ssize_t len,
               svn_boolean_t is_final)
{
  svn_error_t *err;
  int success;

  /* Parse some xml data */
  success = XML_Parse (svn_parser->parser, buf, len, is_final);

  /* If expat choked internally, return its error. */
  if (! success)
    {
      err = svn_error_createf
        (SVN_ERR_MALFORMED_XML, 0, NULL, svn_parser->pool, 
         "%s at line %d",
         XML_ErrorString (XML_GetErrorCode (svn_parser->parser)),
         XML_GetCurrentLineNumber (svn_parser->parser));
      
      /* Kill all parsers and return the expat error */
      svn_xml_free_parser (svn_parser);
      return err;
    }

  /* Did an an error occur somewhere *inside* the expat callbacks? */
  if (svn_parser->error)
    {
      err = svn_parser->error;
      svn_xml_free_parser (svn_parser);
      return err;
    }
  
  return SVN_NO_ERROR;
}



/* The way to officially bail out of xml parsing.
   Store ERROR in SVN_PARSER and set all expat callbacks to NULL. */
void svn_xml_signal_bailout (svn_error_t *error,
                             svn_xml_parser_t *svn_parser)
{
  /* This will cause the current XML_Parse() call to finish quickly! */
  XML_SetElementHandler (svn_parser->parser, NULL, NULL);
  XML_SetCharacterDataHandler (svn_parser->parser, NULL);
  
  /* Once outside of XML_Parse(), the existence of this field will
     cause svn_delta_parse()'s main read-loop to return error. */
  svn_parser->error = error;
}








/*** Attribute walking. ***/

/* See svn_xml.h for details. */
const char *
svn_xml_get_attr_value (const char *name, const char **atts)
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



/*** Printing XML ***/

svn_error_t *
svn_xml_write_header (apr_file_t *file, apr_pool_t *pool)
{
  apr_status_t apr_err;
  const char header[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";

  apr_err = apr_full_write (file, header, (sizeof (header) - 1), NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL, pool,
                             "svn_xml_write_header: file write error.");
  
  return SVN_NO_ERROR;
}



/*** Creating attribute hashes. ***/

/* Combine an existing attribute list ATTS with a HASH that itself
   represents an attribute list.  Iff PRESERVE is true, then no value
   already in HASH will be changed, else values from ATTS will
   override previous values in HASH. */
static void
amalgamate (const char **atts,
            apr_hash_t *ht,
            svn_boolean_t preserve,
            apr_pool_t *pool)
{
  const char *key;

  if (atts)
    for (key = *atts; key; key = *(++atts))
      {
        const char *val = *(++atts);
        size_t keylen;
        assert (key != NULL);
        /* kff todo: should we also insist that val be non-null here? 
           Probably. */

        keylen = strlen (key);
        if (preserve && ((apr_hash_get (ht, key, keylen)) != NULL))
          continue;
        else
          apr_hash_set (ht, key, keylen, 
                        val ? svn_string_create (val, pool) : NULL);
      }
}


apr_hash_t *
svn_xml_ap_to_hash (va_list ap, apr_pool_t *pool)
{
  apr_hash_t *ht = apr_make_hash (pool);
  const char *key;
  
  while ((key = va_arg (ap, char *)) != NULL)
    {
      svn_string_t *val = va_arg (ap, svn_string_t *);
      apr_hash_set (ht, key, strlen (key), val);
    }

  return ht;
}


apr_hash_t *
svn_xml_make_att_hash (const char **atts, apr_pool_t *pool)
{
  apr_hash_t *ht = apr_make_hash (pool);
  amalgamate (atts, ht, 0, pool);  /* third arg irrelevant in this case */
  return ht;
}


void
svn_xml_hash_atts_overlaying (const char **atts,
                              apr_hash_t *ht,
                              apr_pool_t *pool)
{
  amalgamate (atts, ht, 0, pool);
}


void
svn_xml_hash_atts_preserving (const char **atts,
                              apr_hash_t *ht,
                              apr_pool_t *pool)
{
  amalgamate (atts, ht, 1, pool);
}



/*** Writing out tags. ***/


svn_error_t *
svn_xml_write_tag_hash (apr_file_t *file,
                        apr_pool_t *pool,
                        const int tagtype,
                        const char *tagname,
                        apr_hash_t *attributes)
{
  apr_status_t status;
  apr_size_t bytes_written;
  svn_string_t *xmlstring;
  apr_hash_index_t *hi;

  apr_pool_t *subpool = svn_pool_create (pool, NULL);

  xmlstring = svn_string_create ("<", subpool);

  if (tagtype == svn_xml__close_tag)
    svn_string_appendcstr (xmlstring, "/", subpool);

  svn_string_appendcstr (xmlstring, tagname, subpool);

  for (hi = apr_hash_first (attributes); hi; hi = apr_hash_next (hi))
    {
      const char *key;
      size_t keylen;
      svn_string_t *val;
      
      apr_hash_this (hi, (const void **) &key, &keylen, (void **) &val);
      assert (val != NULL);
      
      svn_string_appendcstr (xmlstring, "\n   ", subpool);
      svn_string_appendcstr (xmlstring, key, subpool);
      svn_string_appendcstr (xmlstring, "=\"", subpool);
      svn_string_appendstr  (xmlstring, escape_string (val, subpool), subpool);
      svn_string_appendcstr (xmlstring, "\"", subpool);
    }

  if (tagtype == svn_xml__self_close_tag)
    svn_string_appendcstr (xmlstring, "/", subpool);

  svn_string_appendcstr (xmlstring, ">\n", subpool);

  /* Do the write */
  status = apr_full_write (file, xmlstring->data, xmlstring->len,
                           &bytes_written);
  if (status)
    return svn_error_create (status, 0, NULL, subpool,
                             "svn_xml_write_tag:  file write error.");

  apr_destroy_pool (subpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_xml_write_tag_v (apr_file_t *file,
                     apr_pool_t *pool,
                     const int tagtype,
                     const char *tagname,
                     va_list ap)
{
  apr_hash_t *ht = svn_xml_ap_to_hash (ap, pool);
  return svn_xml_write_tag_hash (file, pool, tagtype, tagname, ht);
}



svn_error_t *
svn_xml_write_tag (apr_file_t *file,
                   apr_pool_t *pool,
                   const int tagtype,
                   const char *tagname,
                   ...)
{
  svn_error_t *err;
  va_list ap;

  va_start (ap, tagname);
  err = svn_xml_write_tag_v (file, pool, tagtype, tagname, ap);
  va_end (ap);

  return err;
}



/*** XML output via a tree delta `editor'. ***/

/* FIXME: below is a skeleton of a tree-editor to output xml.  It can
   be completed right now, it doesn't depend on anything else that's
   incomplete. */

struct edit_baton
{
  svn_boolean_t postfix_text_deltas;
  apr_pool_t *pool;
  /* FIXME: write constructor/destructor? */
};


struct dir_baton
{
  apr_file_t *outfile;
  struct edit_baton *edit_baton;
  apr_pool_t *pool;
  /* FIXME: write constructor/destructor? */
};


struct file_baton
{
  struct dir_baton *dir_baton;
  apr_pool_t *pool;
  /* FIXME: write constructor/destructor? */
};

static svn_error_t *
replace_root (void *edit_baton,
              void **dir_baton)
{
#if 0
  struct edit_baton *eb = (struct edit_baton *) edit_baton;
  struct dir_baton *d;
#endif /* 0 */

  /* FIXME */

  return SVN_NO_ERROR;
}


static svn_error_t *
delete (svn_string_t *name, void *parent_baton)
{
#if 0
  struct dir_baton *parent_dir_baton = (struct dir_baton *) parent_baton;
#endif /* 0 */

  /* FIXME */

  return SVN_NO_ERROR;
}


static svn_error_t *
add_directory (svn_string_t *name,
               void *parent_baton,
               svn_string_t *ancestor_path,
               svn_vernum_t ancestor_version,
               void **child_baton)
{
#if 0
  svn_error_t *err;
  struct dir_baton *parent_dir_baton = (struct dir_baton *) parent_baton;
#endif /* 0 */

  /* FIXME */

  return SVN_NO_ERROR;
}


static svn_error_t *
replace_directory (svn_string_t *name,
                   void *parent_baton,
                   svn_string_t *ancestor_path,
                   svn_vernum_t ancestor_version,
                   void **child_baton)
{
#if 0
  struct dir_baton *parent_dir_baton = (struct dir_baton *) parent_baton;
#endif /* 0 */

  /* FIXME */

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *dir_baton,
                 svn_string_t *name,
                 svn_string_t *value)
{
#if 0
  struct dir_baton *this_dir_baton = (struct dir_baton *) dir_baton;
#endif /* 0 */

  /* FIXME */

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dirent_prop (void *dir_baton,
                    svn_string_t *entry,
                    svn_string_t *name,
                    svn_string_t *value)
{
#if 0
  struct dir_baton *this_dir_baton = (struct dir_baton *) dir_baton;
#endif /* 0 */

  /* FIXME */

  return SVN_NO_ERROR;
}


static svn_error_t *
close_directory (void *dir_baton)
{
#if 0
  struct dir_baton *this_dir_baton = (struct dir_baton *) dir_baton;
#endif /* 0 */

  /* FIXME */

  return SVN_NO_ERROR;
}


static svn_error_t *
add_file (svn_string_t *name,
          void *parent_baton,
          svn_string_t *ancestor_path,
          svn_vernum_t ancestor_version,
          void **file_baton)
{
#if 0
  struct dir_baton *parent_dir_baton = (struct dir_baton *) parent_baton;
  struct file_baton *fb;
#endif /* 0 */

  /* FIXME */

  return SVN_NO_ERROR;
}


static svn_error_t *
replace_file (svn_string_t *name,
              void *parent_baton,
              svn_string_t *ancestor_path,
              svn_vernum_t ancestor_version,
              void **file_baton)
{
  svn_error_t *err;

#if 0
  struct dir_baton *parent_dir_baton = (struct dir_baton *) parent_baton;
#endif /* 0 */

  /* FIXME */

  return err;
}


static svn_error_t *
apply_textdelta (void *file_baton, 
                 svn_txdelta_window_handler_t **handler,
                 void **handler_baton)
{
  /* FIXME */
  
  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  svn_string_t *name,
                  svn_string_t *value)
{
#if 0
  struct file_baton *fb = (struct file_baton *) file_baton;
#endif /* 0 */

  /* FIXME */

  return SVN_NO_ERROR;
}


static svn_error_t *
close_file (void *file_baton)
{
#if 0
  struct file_baton *fb = (struct file_baton *) file_baton;
#endif /* 0 */

  /* FIXME */

  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton)
{
#if 0
  struct edit_baton *eb = (struct edit_baton *) edit_baton;
#endif /* 0 */

  /* FIXME */

  return SVN_NO_ERROR;
}


static const svn_delta_edit_fns_t tree_editor =
{
  replace_root,
  delete,
  add_directory,
  replace_directory,
  change_dir_prop,
  change_dirent_prop,
  close_directory,
  add_file,
  replace_file,
  apply_textdelta,
  change_file_prop,
  close_file,
  close_edit
};


svn_error_t *
svn_xml_get_editor (apr_file_t *outfile,
                    const svn_delta_edit_fns_t **editor,
                    void **edit_baton,
                    apr_pool_t *pool)
{
  struct edit_baton *eb;
  apr_pool_t *subpool = svn_pool_create (pool, NULL);

  /* FIXME: this function may need more arguments, and use an
     edit_baton constructor, etc. */

  *editor = &tree_editor;
  eb = apr_pcalloc (subpool, sizeof (*edit_baton));
  eb->postfix_text_deltas = 1;   /* Or 0? */
  eb->pool           = subpool;

  *edit_baton = eb;

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
