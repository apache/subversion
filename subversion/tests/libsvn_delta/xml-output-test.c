/*
 * xml-output-test.c:  simple XML-generation test
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
 * software developed by CollabNet (http://www.Collab.Net/)."
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

#include <stdio.h>
#include "svn_types.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "apr_pools.h"

/* This is a really rough first-cut test program just to exercise the
 * code and see if it works.  It doesn't verify the output and can't
 * be hooked into the test framework. */

/* NOTE: Does no error-checking.  */
static svn_error_t *
write_to_file (void *baton, const char *data, apr_size_t *len,
               apr_pool_t *pool)
{
  FILE *fp = baton;

  *len = fwrite (data, 1, *len, fp);
  return SVN_NO_ERROR;
}

int main(int argc, char **argv)
{
  apr_pool_t *pool;
  const svn_delta_edit_fns_t *editor;
  svn_txdelta_window_handler_t *handler;
  svn_txdelta_window_t window;
  void *edit_baton, *root_baton, *dir_baton, *file_baton, *handler_baton;
  svn_string_t *foo_string;
  svn_string_t *bar_string;
  svn_string_t *baz_string;
  svn_string_t *aaa_string;
  svn_string_t *bbb_string;
  svn_string_t *ccc_string;

  apr_initialize();
  pool = svn_pool_create (NULL);
  foo_string = svn_string_create ("foo", pool);
  bar_string = svn_string_create ("bar", pool);
  baz_string = svn_string_create ("baz", pool);
  aaa_string = svn_string_create ("aaa", pool);
  bbb_string = svn_string_create ("bbb", pool);
  ccc_string = svn_string_create ("ccc", pool);

  window.sview_offset = 0;
  window.sview_len = 0;
  window.tview_len = 10;
  window.num_ops = 1;
  window.ops_size = 1;
  window.ops = apr_palloc (pool, sizeof (*window.ops));
  window.ops[0].action_code = svn_txdelta_new;
  window.ops[0].offset = 0;
  window.ops[0].length = 10;
  window.new = svn_string_create ("test delta", pool);

  svn_delta_get_xml_editor (write_to_file, stdout, &editor, &edit_baton, pool);
  editor->replace_root (edit_baton, &root_baton);
  editor->replace_directory (foo_string, root_baton, aaa_string, 2,
			     &dir_baton);
  editor->replace_file (bar_string, dir_baton, NULL, 0, &file_baton);
  editor->apply_textdelta (file_baton, &handler, &handler_baton);
  handler (&window, handler_baton);
  handler (NULL, handler_baton);
  editor->close_file (file_baton);
  editor->replace_file (baz_string, dir_baton, NULL, 0, &file_baton);
  editor->change_file_prop (file_baton, bbb_string, ccc_string);
  editor->change_file_prop (file_baton, aaa_string, NULL);
  editor->change_dir_prop (dir_baton, ccc_string, bbb_string);
  editor->close_directory (dir_baton);
  editor->close_directory (root_baton);
  editor->apply_textdelta (file_baton, &handler, &handler_baton);
  handler (NULL, handler_baton);
  editor->close_file (file_baton);
  editor->close_edit (edit_baton);
  return 0;
}
