/* 
   This file began as James Clark's simple demonstration of how to use
   the expat library.  :)
*/


#include <stdio.h>
#include "svn_types.h"
#include "apr_pools.h"
#include "xmlparse.h"


int main()
{
  char buf[BUFSIZ];
  int done;
  svn_delta_digger_t my_digger;

  /* Create a parser for XML deltas. */
  XML_Parser parser = svn_delta_make_xml_parser (&my_digger);

  /* TODO:  create an apr pool, palloc() space for the digger structure */

  do {
    /* Buffer a big chunk of stream */
    size_t len = fread (buf, 1, sizeof(buf), stdin);
    done = len < sizeof(buf);

    /* Parse the buffer */
    if (! XML_Parse (parser, buf, len, done)) {
      fprintf (stderr,
               "%s at line %d\n",
               XML_ErrorString (XML_GetErrorCode (parser)),
               XML_GetCurrentLineNumber (parser));
      return 1;
    }
    /* Lather.  Rinse.  Repeat. */
  } while (!done);

  XML_ParserFree (parser);
  return 0;
}
