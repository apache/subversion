/* delta-window-test.h -- utilities for delta window output
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#ifndef SVN_DELTA_WINDOW_TEST_H
#define SVN_DELTA_WINDOW_TEST_H

#define APR_WANT_STDIO
#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_lib.h>

#include "svn_delta.h"

static apr_off_t
delta_window_size_estimate(const svn_txdelta_window_t *window)
{
  apr_off_t len;
  int i;

  if (!window)
    return 0;

  /* Try to estimate the size of the delta. */
  for (i = 0, len = 0; i < window->num_ops; ++i)
    {
      apr_size_t const offset = window->ops[i].offset;
      apr_size_t const length = window->ops[i].length;
      if (window->ops[i].action_code == svn_txdelta_new)
        {
          len += 1;             /* opcode */
          len += (length > 255 ? 2 : 1);
          len += length;
        }
      else
        {
          len += 1;             /* opcode */
          len += (offset > 255 ? 2 : 1);
          len += (length > 255 ? 2 : 1);
        }
    }

  return len;
}


static apr_off_t
delta_window_print(const svn_txdelta_window_t *window,
                   const char *tag, FILE *stream)
{
  const apr_off_t len = delta_window_size_estimate(window);
  apr_off_t op_offset = 0;
  int i;

  if (!window)
    return 0;

  fprintf(stream, "%s: (WINDOW %" APR_OFF_T_FMT, tag, len);
  fprintf(stream,
          " (%" SVN_FILESIZE_T_FMT
          " %" APR_SIZE_T_FMT " %" APR_SIZE_T_FMT ")",
          window->sview_offset, window->sview_len, window->tview_len);
  for (i = 0; i < window->num_ops; ++i)
    {
      apr_size_t const offset = window->ops[i].offset;
      apr_size_t const length = window->ops[i].length;
      apr_size_t tmp;
      switch (window->ops[i].action_code)
        {
        case svn_txdelta_source:
          fprintf(stream, "\n%s:   (%" APR_OFF_T_FMT " SRC %" APR_SIZE_T_FMT
                  " %" APR_SIZE_T_FMT ")", tag, op_offset, offset, length);
          break;
        case svn_txdelta_target:
          fprintf(stream, "\n%s:   (%" APR_OFF_T_FMT " TGT %" APR_SIZE_T_FMT
                  " %" APR_SIZE_T_FMT ")", tag, op_offset, offset, length);
          break;
        case svn_txdelta_new:
          fprintf(stream, "\n%s:   (%" APR_OFF_T_FMT " NEW %"
                  APR_SIZE_T_FMT " \"", tag, op_offset, length);
          for (tmp = offset; tmp < offset + length; ++tmp)
            {
              int const dat = window->new_data->data[tmp];
              if (apr_iscntrl(dat) || !apr_isascii(dat))
                fprintf(stream, "\\%3.3o", dat & 0xff);
              else if (dat == '\\')
                fputs("\\\\", stream);
              else
                putc(dat, stream);
            }
          fputs("\")", stream);
          break;
        default:
          fprintf(stream, "\n%s:   (BAD-OP)", tag);
        }

      op_offset += length;
    }
  fputs(")\n", stream);
  return len;
}


#endif /* SVN_DELTA_WINDOW_TEST_H */
