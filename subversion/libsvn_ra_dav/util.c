/*
 * util.c :  utility functions for the RA/DAV library
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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

#include <apr_pools.h>

#define APR_WANT_STRFUNC
#include <apr_want.h>

#include <ne_socket.h>
#include <ne_uri.h>
#include <ne_compress.h>

#include "svn_string.h"
#include "svn_xml.h"
#include "svn_path.h"
#include "svn_config.h"

#include "ra_dav.h"




typedef struct {
  apr_pool_t *pool;                          /* pool on which this is alloc-d */
  void *original_userdata;                   /* userdata for callbacks */
  const svn_ra_dav__xml_elm_t *elements;     /* old-style elements table */
  svn_ra_dav__xml_validate_cb *validate_cb;  /* old-style validate callback */
  svn_ra_dav__xml_startelm_cb *startelm_cb;  /* old-style startelm callback */
  svn_ra_dav__xml_endelm_cb *endelm_cb;      /* old-style endelm callback */
  svn_stringbuf_t *cdata_accum;              /* stringbuffer for CDATA */
} neon_shim_baton_t;

/** Find a given element in the table of elements.
 *
 * The table of XML elements @a table is searched until element identified by
 * namespace @a nspace and name @a name is found. If no elements are found,
 * tries to find and return element identified by @c ELEM_unknown. If that is
 * not found, returns NULL pointer. */
static const svn_ra_dav__xml_elm_t *
lookup_elem(const svn_ra_dav__xml_elm_t *table, const char *nspace,
            const char *name)
{
  /* placeholder for `unknown' element if it's present */
  const svn_ra_dav__xml_elm_t *elem_unknown = NULL;
  const svn_ra_dav__xml_elm_t *elem;

  for(elem = table; elem->nspace; ++elem)
    {
      if (strcmp(elem->nspace, nspace) == 0
          && strcmp(elem->name, name) == 0)
        return elem;

      /* Use a single loop to save CPU cycles.
       *
       * Maybe this element is defined as `unknown'? */
      if (elem->id == ELEM_unknown)
        elem_unknown = elem;
    }

  /* ELEM_unknown position in the table or NULL */
  return elem_unknown;
}

/** Fill in temporary structure for ELEM_unknown element.
 *
 * Call only for element ELEM_unknown!
 * For Neon 0.23 API compatibility, we need to fill the XML element structure
 * with real namespace and element name, as "old-style" handler used to get
 * that from Neon parser. This is a hack, so don't expect it to be elegant.
 * The @a elem_pointer is a reference to element pointer which is returned
 * by lookup_elem, and supposedly points at en entry in the XML elements
 * table supplied by an "old-style" handler. @a elem_unknown_temporary is
 * a reference to XML element structure allocated on the stack. There's
 * no reason to allocate it anywhere else because it's going to use
 * @a nspace and @a name which are passed into the "new-style" handler by
 * the Neon parser, so the structure pointed at by @a elem_unknown_temporary
 * must die when the calling function completes. This function is designed
 * to be called from "new-style" startelm and endelm callbacks. */
static void
handle_unknown(const svn_ra_dav__xml_elm_t **elem_pointer,
               svn_ra_dav__xml_elm_t *elem_unknown_temporary,
               const char *nspace, const char *name)
{
  elem_unknown_temporary->nspace = nspace;
  elem_unknown_temporary->name = name;
  elem_unknown_temporary->id = (*elem_pointer)->id;
  elem_unknown_temporary->flags = (*elem_pointer)->flags;

  /* The pointer will use temporary record instead of a table record */
  *elem_pointer = elem_unknown_temporary;
}

/** (Neon 0.24) Start element parsing.
 *
 * Calls "old-style" API callbacks validate_cb and startelm_cb to emulate
 * Neon 0.23 parser. @a userdata is a @c neon_shim_baton_t instance.
 * ---- ne_xml.h ----
 * The startelm callback may return:
 *   <0 =>  abort the parse (NE_XML_ABORT)
 *    0 =>  decline this element  (NE_XML_DECLINE)
 *   >0 =>  accept this element; value is state for this element.
 * The 'parent' integer is the state returned by the handler of the
 * parent element. */
static int
shim_startelm(void *userdata, int parent_state, const char *nspace,
              const char *name, const char **attrs)
{
  neon_shim_baton_t *baton = userdata;
  svn_ra_dav__xml_elm_t elem_unknown_temporary;
  const svn_ra_dav__xml_elm_t *elem = lookup_elem(baton->elements, nspace,
                                                  name);
  int rc;

  if (!elem)
    return NE_XML_DECLINE; /* Let Neon handle this */

  /* TODO: explore an option of keeping element pointer in the baton
   * to cut one loop in endelm */

  /* 'parent' here actually means a parent element's id as opposed
   * to 'parent' parameter passed to the startelm() function */
  rc = baton->validate_cb(baton->original_userdata, parent_state, elem->id);
  if (rc != SVN_RA_DAV__XML_VALID) {
    return (rc == SVN_RA_DAV__XML_DECLINE) ? NE_XML_DECLINE : NE_XML_ABORT;
  }

  if (elem->id == ELEM_unknown)
    handle_unknown(&elem, &elem_unknown_temporary, nspace, name);

  rc = baton->startelm_cb(baton->original_userdata, elem, attrs);
  if (rc != SVN_RA_DAV__XML_VALID) {
    return (rc == SVN_RA_DAV__XML_DECLINE) ? NE_XML_DECLINE : NE_XML_ABORT;
  }

  if (baton->cdata_accum != NULL)
    svn_stringbuf_setempty(baton->cdata_accum);
  else
    baton->cdata_accum = svn_stringbuf_create("", baton->pool);

  /* @a parent in the pre-Neon 0.24 interface was a parent's element
   * id but now it's the status returned by parent's startelm(), so we need to
   * bridge this by returning this element's id as a status.
   * We also need to ensure that element ids start with 1, because
   * zero is `decline'. See ra_dav.h definition of ELEM_* values.
   */
  return elem->id;
}

/** (Neon 0.24) Collect element's contents.
 *
 * Collects element's contents into @a userdata string buffer. @a userdata is a
 * @c neon_shim_baton_t instance.
 * May return non-zero to abort the parse. */
static int shim_cdata(void *userdata, int state, const char *cdata, size_t len)
{
  const neon_shim_baton_t *baton = userdata;

  svn_stringbuf_appendbytes(baton->cdata_accum, cdata, len);
  return 0; /* no error */
}

/** (Neon 0.24) Finish parsing element.
 *
 * Calls "old-style" endelm_cb callback. @a userdata is a @c neon_shim_baton_t
 * instance.
 * May return non-zero to abort the parse. */
static int shim_endelm(void *userdata, int state, const char *nspace,
                       const char *name)
{
  const neon_shim_baton_t *baton = userdata;
  svn_ra_dav__xml_elm_t elem_unknown_temporary;
  const svn_ra_dav__xml_elm_t *elem = lookup_elem(baton->elements, nspace,
                                                  name);
  int rc;

  if (!elem)
    return -1; /* shouldn't be here if startelm didn't abort the parse */

  if (elem->id == ELEM_unknown)
    handle_unknown(&elem, &elem_unknown_temporary, nspace, name);

  rc = baton->endelm_cb(baton->original_userdata,
                        elem,
                        baton->cdata_accum->data);
  if (rc != SVN_RA_DAV__XML_VALID)
    return -1; /* abort the parse */

  return 0; /* no error */
}

void svn_ra_dav__xml_push_handler(ne_xml_parser *p,
                                  const svn_ra_dav__xml_elm_t *elements,
                                  svn_ra_dav__xml_validate_cb validate_cb,
                                  svn_ra_dav__xml_startelm_cb startelm_cb,
                                  svn_ra_dav__xml_endelm_cb endelm_cb, 
                                  void *userdata,
                                  apr_pool_t *pool)
{
  neon_shim_baton_t *baton = apr_pcalloc(pool, sizeof(neon_shim_baton_t));
  baton->pool = pool;
  baton->original_userdata = userdata;
  baton->elements = elements;
  baton->validate_cb = validate_cb;
  baton->startelm_cb = startelm_cb;
  baton->endelm_cb = endelm_cb;
  baton->cdata_accum = NULL; /* don't create until startelm is called */

  ne_xml_push_handler(p, shim_startelm, shim_cdata, shim_endelm, baton);
}




void svn_ra_dav__copy_href(svn_stringbuf_t *dst, const char *src)
{
  ne_uri parsed_url;

  /* parse the PATH element out of the URL and store it.

     ### do we want to verify the rest matches the current session?

     Note: mod_dav does not (currently) use an absolute URL, but simply a
     server-relative path (i.e. this uri_parse is effectively a no-op).
  */
  (void) ne_uri_parse(src, &parsed_url);
  svn_stringbuf_set(dst, parsed_url.path);
  ne_uri_free(&parsed_url);
}

svn_error_t *svn_ra_dav__convert_error(ne_session *sess,
                                       const char *context,
                                       int retcode)
{
  int errcode = SVN_ERR_RA_DAV_REQUEST_FAILED;
  const char *msg;

  /* Convert the return codes. */
  switch (retcode) 
    {
    case NE_AUTH:
      errcode = SVN_ERR_RA_NOT_AUTHORIZED;
      msg = "authorization failed";
      break;
      
    case NE_CONNECT:
      msg = "could not connect to server";
      break;

    case NE_TIMEOUT:
      msg = "timed out waiting for server";
      break;

    default:
      /* Get the error string from neon. */
      msg = ne_get_error (sess);
      break;
    }

  return svn_error_createf (errcode, NULL, "%s: %s (%s://%s)", 
                            context, msg, ne_get_scheme(sess), 
                            ne_get_server_hostport(sess));
  
}


/** Error parsing **/


/* Custom function of type ne_accept_response. */
static int ra_dav_error_accepter(void *userdata,
                                 ne_request *req,
                                 const ne_status *st)
{
  /* Only accept the body-response if the HTTP status code is *not* 2XX. */
  return (st->klass != 2);
}


static const svn_ra_dav__xml_elm_t error_elements[] =
{
  { "DAV:", "error", ELEM_error, 0 },
  { "svn:", "error", ELEM_svn_error, 0 },
  { "http://apache.org/dav/xmlns", "human-readable", 
    ELEM_human_readable, SVN_RA_DAV__XML_CDATA },

  /* ### our validator doesn't yet recognize the rich, specific
         <D:some-condition-failed/> objects as defined by DeltaV.*/

  { NULL }
};


static int validate_error_elements(void *userdata,
                                   svn_ra_dav__xml_elmid parent,
                                   svn_ra_dav__xml_elmid child)
{
  switch (parent)
    {
    case ELEM_root:
      if (child == ELEM_error)
        return SVN_RA_DAV__XML_VALID;
      else
        return SVN_RA_DAV__XML_INVALID;

    case ELEM_error:
      if (child == ELEM_svn_error
          || child == ELEM_human_readable)
        return SVN_RA_DAV__XML_VALID;
      else
        return SVN_RA_DAV__XML_DECLINE;  /* ignore if something else
                                            was in there */

    default:
      return SVN_RA_DAV__XML_DECLINE;
    }

  /* NOTREACHED */
}


static int start_err_element(void *userdata, const svn_ra_dav__xml_elm_t *elm,
                             const char **atts)
{
  svn_error_t **err = userdata;

  switch (elm->id)
    {
    case ELEM_svn_error:
      {
        /* allocate the svn_error_t.  Hopefully the value will be
           overwritten by the <human-readable> tag, or even someday by
           a <D:failed-precondition/> tag. */
        *err = svn_error_create(APR_EGENERAL, NULL,
                                "General svn error from server");
        break;
      }
    case ELEM_human_readable:
      {
        /* get the errorcode attribute if present */
        const char *errcode_str = 
          svn_xml_get_attr_value("errcode", /* ### make constant in
                                               some mod_dav header? */
                                 atts);

        if (errcode_str && *err) 
          (*err)->apr_err = atoi(errcode_str);

        break;
      }

    default:
      break;
    }

  return SVN_RA_DAV__XML_VALID;
}

static int end_err_element(void *userdata, const svn_ra_dav__xml_elm_t *elm,
                           const char *cdata)
{
  svn_error_t **err = userdata;

  switch (elm->id)
    {
    case ELEM_human_readable:
      {
        if (cdata && *err)
          (*err)->message = apr_pstrdup((*err)->pool, cdata);
        break;
      }

    default:
      break;
    }

  return SVN_RA_DAV__XML_VALID;
}


/* A body provider for ne_set_request_body_provider that pulls data
 * from an APR file. See ne_request.h for a description of the
 * interface.
 */
static ssize_t ra_dav_body_provider (void *userdata,
                                     char *buffer,
                                     size_t buflen)
{
  apr_file_t *body_file = userdata;
  apr_status_t status;

  if (buflen == 0)
    {
      /* This is the beginning of a new body pull. Rewind the file. */
      apr_off_t offset = 0;
      status = apr_file_seek(body_file, APR_SET, &offset);
      return (status ? -1 : 0);
    }
  else
    {
      apr_size_t nbytes = buflen;
      status = apr_file_read(body_file, buffer, &nbytes);
      if (status)
        return (APR_STATUS_IS_EOF(status) ? 0 : -1);
      else
        return nbytes;
    }
}


svn_error_t *svn_ra_dav__set_neon_body_provider(ne_request *req,
                                                apr_file_t *body_file)
{
  apr_status_t status;
  apr_finfo_t finfo;

  /* ### APR bug? apr_file_info_get won't always return the correct
         size for buffered files. */
  status = apr_file_info_get(&finfo, APR_FINFO_SIZE, body_file);
  if (status)
    return svn_error_create(status, NULL,
                            "Could not calculate the request body size");

  ne_set_request_body_provider(req, (size_t) finfo.size,
                               ra_dav_body_provider, body_file);
  return SVN_NO_ERROR;
}



svn_error_t *
svn_ra_dav__parsed_request(ne_session *sess,
                           const char *method,
                           const char *url,
                           const char *body,
                           apr_file_t *body_file,
                           void set_parser (ne_xml_parser *parser,
                                            void *baton),
                           const svn_ra_dav__xml_elm_t *elements, 
                           svn_ra_dav__xml_validate_cb validate_cb,
                           svn_ra_dav__xml_startelm_cb startelm_cb, 
                           svn_ra_dav__xml_endelm_cb endelm_cb,
                           void *baton,
                           apr_hash_t *extra_headers,
                           int *status_code,
                           apr_pool_t *pool)
{
  ne_request *req;
  ne_decompress *decompress_main;
  ne_decompress *decompress_err;
  ne_xml_parser *success_parser;
  ne_xml_parser *error_parser;
  int rv;
  int decompress_rv;
  int code;
  int expected_code;
  const char *msg;
  svn_error_t *err = SVN_NO_ERROR;
  svn_ra_session_t *ras = ne_get_session_private(sess, SVN_RA_NE_SESSION_ID);

  /* create/prep the request */
  req = ne_request_create(sess, method, url);

  if (body != NULL)
    ne_set_request_body_buffer(req, body, strlen(body));
  else
    {
      err = svn_ra_dav__set_neon_body_provider(req, body_file);
      if (err)
        {
          ne_request_destroy(req);
          return err;
        }
    }

  /* ### use a symbolic name somewhere for this MIME type? */
  ne_add_request_header(req, "Content-Type", "text/xml");

  /* add any extra headers passed in by caller. */
  if (extra_headers != NULL)
    {
      apr_hash_index_t *hi;
      for (hi = apr_hash_first (pool, extra_headers);
           hi; hi = apr_hash_next (hi))
        {
          const void *key;
          void *val;
          apr_hash_this (hi, &key, NULL, &val);
          ne_add_request_header(req, (const char *) key, (const char *) val); 
        }
    }

  /* create a parser to read the normal response body */
  success_parser = ne_xml_create();
  svn_ra_dav__xml_push_handler(success_parser, elements,
                               validate_cb, startelm_cb, endelm_cb,
                               baton, pool);

  /* ### HACK: Set the parser's error to the empty string.  Someday we
     hope neon will let us have an easy way to tell the difference
     between XML parsing errors, and errors that occur while handling
     the XML tags that we get.  Until then, trust that whenever neon
     has an error somewhere below the API, it sets its own error to
     something non-empty (the API promises non-NULL, at least). */
  ne_xml_set_error(success_parser, "");

  /* if our caller is interested in having access to this parser, call
     the SET_PARSER callback with BATON. */
  if (set_parser != NULL)
    set_parser(success_parser, baton);

  /* create a parser to read the <D:error> response body */
  error_parser = ne_xml_create();
  svn_ra_dav__xml_push_handler(error_parser, error_elements,
                               validate_error_elements, start_err_element,
                               end_err_element, &err, pool); 

  /* Register the "main" accepter and body-reader with the request --
     the one to use when the HTTP status is 2XX */
  if (ras->compression)
    {
      decompress_main = ne_decompress_reader(req, ne_accept_2xx,
                                             ne_xml_parse_v, success_parser);
    }
  else
    {
      decompress_main = NULL;
      ne_add_response_body_reader(req, ne_accept_2xx, ne_xml_parse_v,
                                  success_parser);
    }

  /* Register the "error" accepter and body-reader with the request --
     the one to use when HTTP status is *not* 2XX */
  if (ras->compression)
    {
      decompress_err = ne_decompress_reader(req, ra_dav_error_accepter,
                                            ne_xml_parse_v, error_parser);
    }
  else
    {
      decompress_err = NULL;
      ne_add_response_body_reader(req, ra_dav_error_accepter, ne_xml_parse_v,
                                  error_parser);
    }

  /* run the request and get the resulting status code. */
  rv = ne_request_dispatch(req);

  if (decompress_main)
    {
      decompress_rv = ne_decompress_destroy(decompress_main);
      if (decompress_rv != 0)
        {
          rv = decompress_rv;
        }
    }

  if (decompress_err)
    {
      decompress_rv = ne_decompress_destroy(decompress_err);
      if (decompress_rv != 0)
        {
          rv = decompress_rv;
        }
    }
  
  code = ne_get_status(req)->code;
  if (status_code)
    *status_code = code;
  ne_request_destroy(req);

  if (err) /* If the error parser had a problem */
    goto cleanup;

  /* Set the expected code based on the method. */
  expected_code = 200;
  if (strcmp(method, "PROPFIND") == 0)
    expected_code = 207;

  if ((code != expected_code) 
      || (rv != NE_OK))
    {
      if (code == 404)
      {
        msg = apr_psprintf(pool, "'%s' path not found", url);
        err = svn_error_create(SVN_ERR_RA_DAV_PATH_NOT_FOUND, NULL, msg);
      }
      else
      {
        msg = apr_psprintf(pool, "%s of '%s'", method, url);
        err = svn_ra_dav__convert_error(sess, msg, rv);
      }
      goto cleanup;
    }

  /* was there an XML parse error somewhere? */
  msg = ne_xml_get_error(success_parser);
  if (msg != NULL && *msg != '\0')
    {
      err = svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                              "The %s request returned invalid XML "
                              "in the response: %s. (%s)",
                              method, msg, url);
      goto cleanup;
    }

  /* ### necessary?  be paranoid for now. */
  err = SVN_NO_ERROR;

 cleanup:
  ne_xml_destroy(success_parser);
  ne_xml_destroy(error_parser);
  if (err)
    return svn_error_createf(err->apr_err, err,
                             "%s request failed on '%s'", method, url );
  return SVN_NO_ERROR;
}



svn_error_t *
svn_ra_dav__maybe_store_auth_info(svn_ra_session_t *ras)
{
  /* No auth_baton?  Never mind. */
  if (! ras->callbacks->auth_baton)
    return SVN_NO_ERROR;

  /* If we ever got credentials, ask the iter_baton to save them.  */
  SVN_ERR (svn_auth_save_credentials(ras->auth_iterstate,
                                     ras->pool));
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_ra_dav__request_dispatch(int *code_p,
                             ne_request *request,
                             ne_session *session,
                             const char *method,
                             const char *url,
                             int okay_1,
                             int okay_2,
                             apr_pool_t *pool)
{
  ne_xml_parser *error_parser;
  int rv;
  const ne_status *statstruct;
  const char *code_desc;
  const char *msg;
  svn_error_t *err = SVN_NO_ERROR;

  /* attach a standard <D:error> body parser to the request */
  error_parser = ne_xml_create();
  svn_ra_dav__xml_push_handler(error_parser, error_elements,
                               validate_error_elements, start_err_element,
                               end_err_element, &err, pool);
  ne_add_response_body_reader(request, ra_dav_error_accepter,
                              ne_xml_parse_v, error_parser);

  /* run the request, see what comes back. */
  rv = ne_request_dispatch(request);

  statstruct = ne_get_status(request);
  code_desc = apr_pstrdup(pool, statstruct->reason_phrase);
  if (code_p)
     *code_p = statstruct->code;

  ne_request_destroy(request);
  ne_xml_destroy(error_parser);

  /* If the status code was one of the two that we expected, then go
     ahead and return now. IGNORE any marshalled error. */
  if (rv == NE_OK
      && (statstruct->code == okay_1 || statstruct->code == okay_2))
    return SVN_NO_ERROR;

  /* next, check to see if a <D:error> was discovered */
  if (err)
    return err;

  /* We either have a neon error, or some other error
     that we didn't expect. */
  msg = apr_psprintf(pool, "%s of %s", method, url);
  return svn_ra_dav__convert_error(session, msg, rv);
}
