/*
         Copyright (c) 2003, WebThing Ltd
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

/* libxml */
#include <libxml/HTMLparser.h>

/* apache */
#include <http_protocol.h>
#include <http_config.h>
#include <http_log.h>
#include <apr_strings.h>

module AP_MODULE_DECLARE_DATA proxy_html_module ;

typedef struct {
  struct urlmap* next ;
  const char* from ;
  const char* to ;
} urlmap ;
typedef struct {
  urlmap* map ;
  const char* doctype ;
} proxy_html_conf ;
typedef struct {
  htmlSAXHandlerPtr sax ;
  ap_filter_t* f ;
  urlmap* map ;
  htmlParserCtxtPtr parser ;
  apr_bucket_brigade* bb ;
} saxctxt ;

static void pstartDocument(void* ctxt) {
  saxctxt* ctx = (saxctxt*) ctxt ;

  proxy_html_conf* cfg = ap_get_module_config(ctx->f->r->per_dir_config,&proxy_html_module);
  apr_table_unset(ctx->f->r->headers_out, "Content-Length") ;
  apr_table_unset(ctx->f->r->headers_out, "ETag") ;
  ap_set_content_type(ctx->f->r, "text/html;charset=utf-8") ;
  ap_fputs(ctx->f->next, ctx->bb, cfg->doctype) ;
}
static void pendDocument(void* ctxt) {
  saxctxt* ctx = (saxctxt*) ctxt ;
  APR_BRIGADE_INSERT_TAIL(ctx->bb,
	apr_bucket_eos_create(ctx->bb->bucket_alloc) ) ;
  ap_pass_brigade(ctx->f->next, ctx->bb) ;
}
typedef struct {
	const char* name ;
	const char** attrs ;
} elt_t ;

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
	  for ( m = ctx->map ; m ; m = (urlmap*)m->next ) {
	    if ( ! strncasecmp(value, m->from, strlen(m->from) ) ) {
	      value = apr_pstrcat(ctx->f->r->pool, m->to, value+strlen(m->from) , NULL) ;
	      break ;
	    }
	  }
	}
      }
      if ( ! value )
        ap_fputstrs(ctx->f->next, ctx->bb, " ", a[0], NULL) ;
      else
	ap_fputstrs(ctx->f->next, ctx->bb, " ", a[0], "=\"", value, "\"", NULL) ;
    }
  }
  ap_fputc(ctx->f->next, ctx->bb, '>') ;
}
static void pendElement(void* ctxt, const xmlChar* name) {
  const char** p ;
  saxctxt* ctx = (saxctxt*) ctxt ;
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
	return ;
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
  sax->startDocument = pstartDocument ;
  sax->endDocument = pendDocument ;
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
  ctype = strdup(in) ;
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

  xmlCharEncoding enc
	= xmlParseCharEncoding(ctype2encoding(f->r->pool, f->r->content_type)) ;

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
  fctx->map = ap_get_module_config(f->r->per_dir_config,&proxy_html_module);

  if ( f->r->proto_num >= 1001 ) {
    if ( ! f->r->main && ! f->r->prev )
      f->r->chunked = 1 ;
  }
  fctx->parser = htmlCreatePushParserCtxt
	( fctx->sax , fctx, "    ", 4, 0, enc) ;
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
      htmlParseChunk(ctxt->parser, buf, 0, 1) ;
      htmlFreeParserCtxt(ctxt->parser) ;
    } else if ( apr_bucket_read(b, &buf, &bytes, APR_BLOCK_READ)
	      == APR_SUCCESS ) {
      htmlParseChunk(ctxt->parser, buf, bytes, 0) ;
    } else {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, f->r, "Error in bucket read") ;
    }
  }
  apr_brigade_destroy(bb) ;
  return APR_SUCCESS ;
}
static const char* DEFAULT_DOCTYPE =
	"<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01//EN\">\n" ;

static void* proxy_html_config(apr_pool_t* pool, char* x) {
  proxy_html_conf* ret = apr_pcalloc(pool, sizeof(proxy_html_conf) ) ;
  ret->doctype = DEFAULT_DOCTYPE ;
  return ret ;
}
static void* proxy_html_merge(apr_pool_t* pool, void* BASE, void* ADD) {
  proxy_html_conf* base = (proxy_html_conf*) BASE ;
  proxy_html_conf* add = (proxy_html_conf*) ADD ;
  proxy_html_conf* conf = apr_palloc(pool, sizeof(proxy_html_conf)) ;
  conf->map = add->map ? add->map : base->map ;
  if ( add->map && base->map ) {
    urlmap* newmap = add->map ;
    while ( newmap->next )
	newmap = (urlmap*)newmap->next ;
    newmap->next = (struct urlmap*) base->map ;
  }
  conf->doctype = ( add->doctype == DEFAULT_DOCTYPE )
		? base->doctype : add->doctype ;
  return conf ;
}
static const char* set_urlmap(cmd_parms* cmd, void* CFG,
	const char* from, const char* to) {
  proxy_html_conf* cfg = (proxy_html_conf*)CFG ;
  urlmap* newmap = apr_palloc(cmd->pool, sizeof(urlmap) ) ;
  newmap->from = apr_pstrdup(cmd->pool, from) ;
  newmap->to = apr_pstrdup(cmd->pool, to) ;
  newmap->next = (struct urlmap*) cfg->map ;
  cfg->map = newmap ;
  return NULL ;
}
static const char* set_doctype(cmd_parms* cmd, void* CFG, const char* t) {
  proxy_html_conf* cfg = (proxy_html_conf*)CFG ;
  cfg->doctype = apr_pstrdup(cmd->pool, t) ;
  return NULL ;
}
static const command_rec proxy_html_cmds[] = {
  AP_INIT_TAKE2("ProxyHTMLURLMap", set_urlmap, NULL, OR_ALL, "Map URL From To" ) ,
  AP_INIT_TAKE1("ProxyHTMLDoctype", set_doctype, NULL, OR_ALL, "Set Doctype for URL mapped documents" ) ,
  { NULL }
} ;
static void proxy_html_hooks(apr_pool_t* p) {
  ap_register_output_filter("proxy-html", proxy_html_filter,
	proxy_html_filter_init, AP_FTYPE_RESOURCE) ;
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
