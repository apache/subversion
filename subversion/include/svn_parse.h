/* svn_parse:  shared parsing routines for reading config files
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
 * This software may consist of voluntary contributions made by many
 * individuals on behalf of Collab.Net.
 */



#ifndef SVN_PARSE_H
#define SVN_PARSE_H

#include <stdio.h>
#include <svn_error.h>
#include <svn_types.h>


/* Subversion uses a single syntax for many of its config files,
   internal administrative files, and so on.  This syntax is a subset
   of the syntax for Scheme data; when we get a real, integral
   extension language, we may substitute the interpreter's parser for
   this little home-grown parser.

   The syntax we recognize here is as follows:

     object:              list | symbol | string | number ;
     list:                '(' object_list_opt ')' ;
     object_list_opt:     (nothing) | object_list ;
     object_list:         object | object object_list ;

   The lexical details are as follows:

     token:               '(' | ')' | symbol | string | number ;
     symbol:              (any string made from the characters
                          '!$%&*:/<=>?~_^.+-', letters, or digits, that
                          does not start with any of the characters '.+-'
                          or a digit) ;
     string:              '"' (any sequence of characters, in which '"'
                          and backslash are escaped with a backslash) '"' ;
     number:              digits | '+' digits | '-' digits ;
     digits:              (any sequence of digits) ;
     intertoken_space:    ' ' | '\t' | '\n' | '\r' | '\f' | comment ;
     comment:             ';' (and then all subsequent characters up to 
                                  the next newline) ;

   intertoken_space may occur on either side of any token, but not
   within a token.  */



/* A data structure representing a parsed `object', as defined above.  */
typedef struct svn_parsed {

  /* What kind of object does this structure represent?  The
     possibilities are given in the `object' production of the grammar
     above.  */
  enum svn_parsed_kind {
    svn_parsed_list,
    svn_parsed_symbol,
    svn_parsed_string,
    svn_parsed_number
  } kind;

  /* The `kind' field indicates which member of the union is valid.  */
  union {
    
    struct svn_parsed_list {
      /* The number of elements in this list.  */
      int n;

      /* A null-terminated array of pointers to the list's elements.  */
      struct svn_parsed *elt;
    } list;

    svn_string_t string;
    svn_string_t symbol;
    long number;
  } u;
} svn_parsed_t;


/* Parse one object from STREAM, and set *OBJECT_P to an svn_parsed_t
   object, allocated from POOL.  If we reach EOF, set OBJECT to 0.  */
extern svn_error_t *svn_parse (svn_parsed_t **object_p,
                               FILE *stream,
                               ap_pool_t *pool);
   
/* Print the external representation of OBJECT on STREAM.  The resulting
   output could be parsed by `svn_parse'.  */
extern svn_error_t *svn_parse_print (FILE *stream, svn_parsed_t *object);

/* Given OBJECT, which must be a list of lists, set *ELT_P the element
   of OBJECT whose first element is a symbol whose name is NAME.  */
extern svn_error_t *svn_parse_ref (svn_parsed_t *elt_p,
                                   svn_parsed_t *object,
                                   char *name);

#endif /* SVN_PARSE_H */


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
