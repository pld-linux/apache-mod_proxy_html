/*
         Copyright (c) 2003-4, WebThing Ltd
         Author: Nick Kew <nick@webthing.com>
                 
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
      
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
        
You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
                   
*/
/*	Note to Users
 
	You are requested to register as a user, at
	http://apache.webthing.com/registration.html
 
	This entitles you to support from the developer
	(see the webpage for details).
	I'm unlikely to reply to help/support requests from
	non-registered users, unless you're paying and/or offering
	constructive feedback such as bug reports or sensible
	suggestions for further development.
 
	It also makes a small contribution to the effort
	that's gone into developing this work.
*/

/* libxml */
#include <libxml/HTMLparser.h>

/* apache */
#include <http_protocol.h>
#include <http_config.h>
#include <http_log.h>
#include <apr_strings.h>

module AP_MODULE_DECLARE_DATA proxy_html_module ;

typedef struct urlmap {
  struct urlmap* next ;
  const char* from ;
  const char* to ;
} urlmap ;
typedef struct {
  urlmap* map ;
  const char* doctype ;
  const char* etag ;
  unsigned int flags ;
} proxy_html_conf ;
typedef struct {
  htmlSAXHandlerPtr sax ;
  ap_filter_t* f ;
  proxy_html_conf* cfg ;
  htmlParserCtxtPtr parser ;
  apr_bucket_brigade* bb ;
  xmlCharEncoding enc ;
} saxctxt ;

static int is_empty_elt(const char* name) {
  const char** p ;
  static const char* empty_elts[] = {
    "br" ,
    "link" ,
    "img" ,
    "hr" ,
    "input" ,
    "meta" ,
    "base" ,
    "area" ,
    "param" ,
    "col" ,
    "frame" ,
    "isindex" ,
    "basefont" ,
    NULL
  } ;
  for ( p = empty_elts ; *p ; ++p )
    if ( !strcmp( *p, name) )
      return 1 ;
  return 0 ;
}

typedef struct {
	const char* name ;
	const char** attrs ;
} elt_t ;

#define NORM_LC 0x1
#define NORM_MSSLASH 0x2
#define NORM_RESET 0x4

static char* normalise(unsigned int flags, char* str) {
  xmlChar* p ;
  if ( flags & NORM_LC )
    for ( p = str ; *p ; ++p )
      if ( isupper(*p) )
	*p = tolower(*p) ;

  if ( flags & NORM_MSSLASH )
    for ( p = strchr(str, '\\') ; p ; p = strchr(p+1, '\\') )
      *p = '/' ;

  return str ;
}

static void pstartElement(void* ctxt, const xmlChar* name,
		const xmlChar** attrs ) {

  saxctxt* ctx = (saxctxt*) ctxt ;

  static const char* href[] = { "href", NULL } ;
  static const char* cite[] = { "cite", NULL } ;
  static const char* action[] = { "action", NULL } ;
  static const char* imgattr[] = { "src", "longdesc", "usemap", NULL } ;
  static const char* inputattr[] = { "src", "usemap", NULL } ;
  static const char* scriptattr[] = { "src", "for", NULL } ;
  static const char* frameattr[] = { "src", "longdesc", NULL } ;
  static const char* objattr[] = { "classid", "codebase", "data", "usemap", NULL } ;
  static const char* profile[] = { "profile", NULL } ;
  static const char* background[] = { "background", NULL } ;
  static const char* codebase[] = { "codebase", NULL } ;

  static elt_t linked_elts[] = {
    { "a" , href } ,
    { "form", action } ,
    { "base" , href } ,
    { "area" , href } ,
    { "link" , href } ,
    { "img" , imgattr } ,
    { "input" , inputattr } ,
    { "script" , scriptattr } ,
    { "frame", frameattr } ,
    { "iframe", frameattr } ,
    { "object", objattr } ,
    { "q" , cite } ,
    { "blockquote" , cite } ,
    { "ins" , cite } ,
    { "del" , cite } ,
    { "head" , profile } ,
    { "body" , background } ,
    { "applet", codebase } ,
    { NULL, NULL }
  } ;

  ap_fputc(ctx->f->next, ctx->bb, '<') ;
  ap_fputs(ctx->f->next, ctx->bb, name) ;

  if ( attrs ) {
    const char** linkattrs = 0 ;
    const xmlChar** a ;
    elt_t* elt ;
    for ( elt = linked_elts;  elt->name != NULL ; ++elt )
      if ( !strcmp(elt->name, name) ) {
	linkattrs = elt->attrs ;
	break ;
      }
    for ( a = attrs ; *a ; a += 2 ) {
      const xmlChar* value = a[1] ;
      if ( linkattrs && value ) {
	int is_uri = 0 ;
	const char** linkattr = linkattrs ;
	do {
	  if ( !strcmp(*linkattr, *a) ) {
	    is_uri = 1 ;
	    break ;
	  }
	} while ( *++linkattr ) ;
	if ( is_uri ) {
	  urlmap* m ;
	  for ( m = ctx->cfg->map ; m ; m = m->next ) {
	    if ( ! strncasecmp(value, m->from, strlen(m->from) ) ) {
	      value = apr_pstrcat(ctx->f->r->pool, m->to, value+strlen(m->from) , NULL) ;
	      break ;
	    }
	  }
	}
      }
      if ( ! value )
	ap_fputstrs(ctx->f->next, ctx->bb, " ", a[0], NULL) ;
      else {
	if ( ctx->cfg->flags != 0 )
	  value = normalise(ctx->cfg->flags,
		apr_pstrdup(ctx->f->r->pool, value ) ) ;
	ap_fputstrs(ctx->f->next, ctx->bb, " ", a[0], "=\"",
		 value, "\"", NULL) ;
      }
    }
  }
  if ( is_empty_elt(name) )
    ap_fputs(ctx->f->next, ctx->bb, ctx->cfg->etag) ;
  else
    ap_fputc(ctx->f->next, ctx->bb, '>') ;
}
static void pendElement(void* ctxt, const xmlChar* name) {
  saxctxt* ctx = (saxctxt*) ctxt ;
  if ( ! is_empty_elt(name) )
    ap_fprintf(ctx->f->next, ctx->bb, "</%s>", name) ;
}
#define FLUSH ap_fwrite(ctx->f->next, ctx->bb, (chars+begin), (i-begin)) ; begin = i+1
static void pcharacters(void* ctxt, const xmlChar *chars, int length) {
  saxctxt* ctx = (saxctxt*) ctxt ;
  int i ;
  int begin ;
  for ( begin=i=0; i<length; i++ ) {
    switch (chars[i]) {
      case '&' : FLUSH ; ap_fputs(ctx->f->next, ctx->bb, "&amp;") ; break ;
      case '<' : FLUSH ; ap_fputs(ctx->f->next, ctx->bb, "&lt;") ; break ;
      case '>' : FLUSH ; ap_fputs(ctx->f->next, ctx->bb, "&gt;") ; break ;
      case '"' : FLUSH ; ap_fputs(ctx->f->next, ctx->bb, "&quot;") ; break ;
      default : break ;
    }
  }
  FLUSH ;
}
static void pcdata(void* ctxt, const xmlChar *chars, int length) {
  saxctxt* ctx = (saxctxt*) ctxt ;
  ap_fwrite(ctx->f->next, ctx->bb, chars, length) ;
}
static void pcomment(void* ctxt, const xmlChar *chars) {
  saxctxt* ctx = (saxctxt*) ctxt ;
  ap_fputstrs(ctx->f->next, ctx->bb, "<!--", chars, "-->", NULL) ;
}
static htmlSAXHandlerPtr setupSAX(apr_pool_t* pool) {
  htmlSAXHandlerPtr sax = apr_pcalloc(pool, sizeof(htmlSAXHandler) ) ;
  sax->startDocument = NULL ;
  sax->endDocument = NULL ;
  sax->startElement = pstartElement ;
  sax->endElement = pendElement ;
  sax->characters = pcharacters ;
  sax->comment = pcomment ;
  sax->cdataBlock = pcdata ;
  return sax ;
}
static char* ctype2encoding(apr_pool_t* pool, const char* in) {
  char* x ;
  char* ptr ;
  char* ctype ;
  if ( ! in )
    return 0 ;
  if ( ctype = strdup(in) , ! ctype )
    return 0 ;
  for ( ptr = ctype ; *ptr; ++ptr)
    if ( isupper(*ptr) )
      *ptr = tolower(*ptr) ;

  if ( ptr = strstr(ctype, "charset=") , ptr > 0 ) {
    ptr += 8 ;  // jump over "charset=" and chop anything that follows charset
    if ( x = strchr(ptr, ' ') , x )
      *x = 0 ;
    if ( x = strchr(ptr, ';') , x )
      *x = 0 ;
  }
  x =  ptr ? apr_pstrdup(pool, ptr) : 0 ;
  free (ctype ) ;
  return x ;
}

static int proxy_html_filter_init(ap_filter_t* f) {
  saxctxt* fctx ;

/* remove content-length filter */
  ap_filter_rec_t* clf = ap_get_output_filter_handle("CONTENT_LENGTH") ;
  ap_filter_t* ff = f->next ;

  do {
    ap_filter_t* fnext = ff->next ;
    if ( ff->frec == clf )
      ap_remove_output_filter(ff) ;
    ff = fnext ;
  } while ( ff ) ;

  fctx = f->ctx = apr_pcalloc(f->r->pool, sizeof(saxctxt)) ;
  fctx->sax = setupSAX(f->r->pool) ;
  fctx->f = f ;
  fctx->bb = apr_brigade_create(f->r->pool, f->r->connection->bucket_alloc) ;
  fctx->cfg = ap_get_module_config(f->r->per_dir_config,&proxy_html_module);

/* Note the encoding now, before updating content-type */
  fctx->enc = xmlParseCharEncoding
		(ctype2encoding(f->r->pool, f->r->content_type)) ;

  if ( f->r->proto_num >= 1001 ) {
    if ( ! f->r->main && ! f->r->prev )
      f->r->chunked = 1 ;
  }
  apr_table_unset(f->r->headers_out, "Content-Length") ;
  apr_table_unset(f->r->headers_out, "ETag") ;
  ap_set_content_type(f->r, "text/html;charset=utf-8") ;
  ap_fputs(f->next, fctx->bb, fctx->cfg->doctype) ;
  return OK ;
}
static saxctxt* check_filter_init (ap_filter_t* f) {

  if ( f->r->proxyreq && f->r->content_type ) {
    if ( strncasecmp(f->r->content_type, "text/html", 9) &&
	strncasecmp(f->r->content_type, "application/xhtml+xml", 21) ) {
      ap_remove_output_filter(f) ;
      return NULL ;
    }
  }

  if ( ! f->ctx )
    proxy_html_filter_init(f) ;
  return f->ctx ;
}
static int proxy_html_filter(ap_filter_t* f, apr_bucket_brigade* bb) {
  apr_bucket* b ;
  const char* buf = 0 ;
  apr_size_t bytes = 0 ;

  saxctxt* ctxt = check_filter_init(f) ;
  if ( ! ctxt )
    return ap_pass_brigade(f->next, bb) ;

  for ( b = APR_BRIGADE_FIRST(bb) ;
	b != APR_BRIGADE_SENTINEL(bb) ;
	b = APR_BUCKET_NEXT(b) ) {
    if ( APR_BUCKET_IS_EOS(b) ) {
      if ( ctxt->parser != NULL ) {
	htmlParseChunk(ctxt->parser, buf, 0, 1) ;
	htmlFreeParserCtxt(ctxt->parser) ;
      }
      APR_BRIGADE_INSERT_TAIL(ctxt->bb,
	apr_bucket_eos_create(ctxt->bb->bucket_alloc) ) ;
      ap_pass_brigade(ctxt->f->next, ctxt->bb) ;
    } else if ( apr_bucket_read(b, &buf, &bytes, APR_BLOCK_READ)
	      == APR_SUCCESS ) {
      if ( ctxt->parser == NULL )
	ctxt->parser = htmlCreatePushParserCtxt(ctxt->sax, ctxt,
		buf, bytes, 0, ctxt->enc) ;
      else
	htmlParseChunk(ctxt->parser, buf, bytes, 0) ;
    } else {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, f->r, "Error in bucket read") ;
    }
  }
  //ap_fflush(ctxt->f->next, ctxt->bb) ;	// uncomment for debug
  apr_brigade_destroy(bb) ;
  return APR_SUCCESS ;
}
static const char* fpi_html =
	"<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01//EN\">\n" ;
static const char* fpi_html_legacy =
	"<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\">\n" ;
static const char* fpi_xhtml =
	"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n" ;
static const char* fpi_xhtml_legacy =
	"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n" ;
static const char* html_etag = ">" ;
static const char* xhtml_etag = " />" ;
#define DEFAULT_DOCTYPE fpi_html
#define DEFAULT_ETAG html_etag

static void* proxy_html_config(apr_pool_t* pool, char* x) {
  proxy_html_conf* ret = apr_pcalloc(pool, sizeof(proxy_html_conf) ) ;
  ret->doctype = DEFAULT_DOCTYPE ;
  ret->etag = DEFAULT_ETAG ;
  return ret ;
}
static void* proxy_html_merge(apr_pool_t* pool, void* BASE, void* ADD) {
  proxy_html_conf* base = (proxy_html_conf*) BASE ;
  proxy_html_conf* add = (proxy_html_conf*) ADD ;
  proxy_html_conf* conf = apr_palloc(pool, sizeof(proxy_html_conf)) ;

  if ( add->map && base->map ) {
    urlmap* a ;
    conf->map = NULL ;
    for ( a = base->map ; a ; a = a->next ) {
      urlmap* save = conf->map ;
      conf->map = apr_pmemdup(pool, a, sizeof(urlmap)) ;
      conf->map->next = save ;
    }
    for ( a = add->map ; a ; a = a->next ) {
      urlmap* save = conf->map ;
      conf->map = apr_pmemdup(pool, a, sizeof(urlmap)) ;
      conf->map->next = save ;
    }
  } else
    conf->map = add->map ? add->map : base->map ;

  conf->doctype = ( add->doctype == DEFAULT_DOCTYPE )
		? base->doctype : add->doctype ;
  conf->etag = ( add->etag == DEFAULT_ETAG ) ? base->etag : add->etag ;
  if ( add->flags & NORM_RESET )
    conf->flags = add->flags ^ NORM_RESET ;
  else
    conf->flags = base->flags | add->flags ;
  return conf ;
}
static const char* set_urlmap(cmd_parms* cmd, void* CFG,
	const char* from, const char* to) {
  proxy_html_conf* cfg = (proxy_html_conf*)CFG ;
  urlmap* oldmap = cfg->map ;
  urlmap* newmap = apr_palloc(cmd->pool, sizeof(urlmap) ) ;
  newmap->from = apr_pstrdup(cmd->pool, from) ;
  newmap->to = apr_pstrdup(cmd->pool, to) ;
  newmap->next = NULL ;
  if ( oldmap ) {
    while ( oldmap->next )
      oldmap = oldmap->next ;
    oldmap->next = newmap ;
  } else
    cfg->map = newmap ;
  return NULL ;
}
static const char* set_doctype(cmd_parms* cmd, void* CFG, const char* t,
	const char* l) {
  proxy_html_conf* cfg = (proxy_html_conf*)CFG ;
  if ( !strcasecmp(t, "xhtml") ) {
    cfg->etag = xhtml_etag ;
    if ( l && !strcasecmp(l, "legacy") )
      cfg->doctype = fpi_xhtml_legacy ;
    else
      cfg->doctype = fpi_xhtml ;
  } else if ( !strcasecmp(t, "html") ) {
    cfg->etag = html_etag ;
    if ( l && !strcasecmp(l, "legacy") )
      cfg->doctype = fpi_html_legacy ;
    else
      cfg->doctype = fpi_html ;
  } else {
    cfg->doctype = apr_pstrdup(cmd->pool, t) ;
    if ( l && ( ( l[0] == 'x' ) || ( l[0] == 'X' ) ) )
      cfg->etag = xhtml_etag ;
  }
  return NULL ;
}
static void set_param(proxy_html_conf* cfg, const char* arg) {
  if ( arg && *arg )
    if ( !strcmp(arg, "lowercase") )
      cfg->flags |= NORM_LC ;
    else if ( !strcmp(arg, "dospath") )
      cfg->flags |= NORM_MSSLASH ;
    else if ( !strcmp(arg, "reset") )
      cfg->flags |= NORM_RESET ;
}
static const char* set_flags(cmd_parms* cmd, void* CFG, const char* arg1,
	const char* arg2, const char* arg3) {
  set_param( (proxy_html_conf*)CFG, arg1) ;
  set_param( (proxy_html_conf*)CFG, arg2) ;
  set_param( (proxy_html_conf*)CFG, arg3) ;
  return NULL ;
}
static const command_rec proxy_html_cmds[] = {
  AP_INIT_TAKE2("ProxyHTMLURLMap", set_urlmap, NULL, OR_ALL, "Map URL From To" ) ,
  AP_INIT_TAKE12("ProxyHTMLDoctype", set_doctype, NULL, OR_ALL, "(HTML|XHTML) [Legacy]" ) ,
  AP_INIT_TAKE123("ProxyHTMLFixups", set_flags, NULL, OR_ALL, "Options are lowercase, dospath" ) ,
  { NULL }
} ;
static void proxy_html_hooks(apr_pool_t* p) {
  ap_register_output_filter("proxy-html", proxy_html_filter,
	NULL, AP_FTYPE_RESOURCE) ;
}
module AP_MODULE_DECLARE_DATA proxy_html_module = {
	STANDARD20_MODULE_STUFF,
	proxy_html_config,
	proxy_html_merge,
	NULL,
	NULL,
	proxy_html_cmds,
	proxy_html_hooks
} ;
