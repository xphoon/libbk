#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_netutils.c,v 1.7 2001/11/28 18:24:09 seth Exp $";
static char libbk__copyright[] = "Copyright (c) 2001";
static char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright LIBBK++
 *
 * Copyright (c) 2001 The Authors.  All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Mail <projectbaka@baka.org> for further information
 *
 * --Copyright LIBBK--
 */

/**
 * @file 
 * Random network utility functions. This a sort of a catch-all file
 * (though clearly each functino should have somethig to do with network
 * code.
 */

#include <libbk.h>
#include "libbk_internal.h"



#define BK_ENDPT_SPEC_PROTO_SEPARATOR	"://"	///< XXX
#define BK_ENDPT_SPEC_SERVICE_SEPARATOR	":"	///< XXX



/** 
 * All the state which must be saved during hostname determination in order
 * to pass onto bk_net_init()
 */
struct start_service_state
{
  bk_flags			sss_flags;	///< Everyone needs flags.
  struct bk_netinfo *  		sss_lbni;	///< bk_netinfo.
  struct bk_netinfo *  		sss_rbni;	///< bk_netinfo.
  bk_bag_callback_t		sss_callback;	///< User callback.
  void *			sss_args;	///< User args.
  char *			sss_securenets;	///< Securenets info.
  int				sss_backlog;	///< Listen backlog.
  char *			sss_host;	///< Space to save a hostname.
  u_long			sss_timeout;	///< Timeout
};



static struct start_service_state *sss_create(bk_s B);
static void sss_destroy(bk_s B, struct start_service_state *sss);
static void sss_serv_gethost_complete(bk_s B, struct bk_run *run , struct hostent **h, struct bk_netinfo *bni, void *args);
static void sss_connect_rgethost_complete(bk_s B, struct bk_run *run, struct hostent **h, struct bk_netinfo *bni, void *args);
static void sss_connect_lgethost_complete(bk_s B, struct bk_run *run, struct hostent **h, struct bk_netinfo *bni, void *args);




/**
 * Get the length of a sockaddr (not every OS supports sa_len);
 *
 *	@param B BAKA thread/global state.
 *	@param sa @a sockaddr 
 *	@return <i>-1</i> on failure.<br>
 *	@return @a sockaddr <i>length</i> on success.
 */
int
bk_netutils_get_sa_len(bk_s B, struct sockaddr *sa)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int len;

  if (!sa)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  // XXX -- do this correctly
#ifdef SA_LEN
  return sa->salen
#endif /* SA_LEN */

  switch (sa->sa_family)
  {
  case AF_INET:
    len = sizeof(struct sockaddr_in);
    break;

  case AF_INET6:
    len = sizeof(struct sockaddr_in6);
    break;

  case AF_LOCAL:
    len = bk_strnlen(B, ((struct sockaddr_un *)sa)->sun_path, sizeof(struct sockaddr_un));
    break;

  default:
    bk_error_printf(B, BK_ERR_ERR, "Address family %d is not suppored\n", sa->sa_family);
    goto error;
    break;
  }

  BK_RETURN(B, len);

 error:
  BK_RETURN(B,-1);
}



/**
 * Parse a BAKA URL endpoint specifier. Copy out host string, service string
 * and protocol string in *allocated* memory (use free(2) to free). If no
 * entry is found for one of the copyout values the supplied default (if
 * any) is returned, otherwise NULL. Hostspecifiers/BAKA endpoints appear very much like
 * URL's. [PROTO://][HOST][:SERVICE]. You will note that in theory the empty
 * string is legal and this function will not *complain* if it finds no
 * data. It is assumed that the caller will have specified at least a
 * default host string (like BK_ADDR_ANY) or default service. NB If the
 * host string is empty @a hoststr will be *NULL* (not empty).
 * NB: This is a @a friendly function not public.
 *
 *	@param B BAKA thread/global state.
 *	@param url Host specifier ("url" is shorter :-))
// XXX - supply alternate default url???
 *	@param hoststr Copy out host string.
 *	@param defhoststr Default host string.
 *	@param serivcestr Copy out service string.
 *	@param defservicestr Default service string.
 *	@param protostr Copy out protocol string.
 *	@param defprotostr Default protocol string.
 *	@return <i>-1</i> with @a hoststr, @a servicestr, and @a protostr
 *	set to NULL on failure.<br>
 *	@return <i>0</i> with @a hoststr, @a servicestr, and @a protostr
 *	appropriately set on success.
 */
int
bk_parse_endpt_spec(bk_s B, char *urlstr, char **hoststr, char *defhoststr, char **servicestr,  char *defservicestr, char **protostr, char *defprotostr)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *host, *service, *proto;	/* Temporary versions */
  char *protoend;
  char *url = NULL;

  if (!urlstr || !hoststr || !servicestr || !protostr)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  /* Init. copyout args (so we don't free random stuff in error case) */
  *hoststr = NULL;
  *servicestr = NULL;
  *protostr = NULL;

  /* Make modifiable copy */
  if (!(url = strdup(urlstr)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup urlstr: %s\n", strerror(errno));
    goto error;
  }

  proto = url;

  /* Look for protocol specifier and set host to point *after* it. */
  if ((protoend = strstr(proto, BK_ENDPT_SPEC_PROTO_SEPARATOR)))
  {
    *protoend = '\0';
    host = protoend + strlen(BK_ENDPT_SPEC_PROTO_SEPARATOR);
  }
  else
  {
    proto = NULL;
    host = url;
  }

  /* If not found or empty, use default */
  if (!proto || BK_STREQ(proto,""))
  {
    proto = defprotostr;
  }

  /* Copyout proto */
  if (proto && !(*protostr = strdup(proto)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup protostr: %s\n", strerror(errno));
    goto error;
  }

  /* Find service info */
  if ((service = strstr(host, BK_ENDPT_SPEC_SERVICE_SEPARATOR)))
  {
    *service++ = '\0';
  }

  /* If not found, or empty use default */
  if (!service || BK_STREQ(service,""))
  {
    service = defservicestr;
  }

  /* Copy service if available. */
  if (service && !(*servicestr = strdup(service)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup service: %s\n", strerror(errno));
    goto error;
  }

  /* Host should be pointing to right thing */

  /* If host exists (which it *always* should) but is empty, use default */
  if (host && BK_STREQ(host, ""))
  {
    host = defhoststr;
  }

  /* Copy host if available. */
  if (host && !(*hoststr = strdup(host)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup host: %s\n", strerror(errno));
    goto error;
  }

  if (url) free (url);

  BK_RETURN(B,0);

 error:
  if (url) free(url);
  if (*hoststr) free (*hoststr);
  *hoststr = NULL;
  if (*servicestr) free (*servicestr);
  *servicestr = NULL;
  if (*protostr) free (*protostr);
  *protostr = NULL;
  BK_RETURN(B,-1);
}



/**
 * Make a service with a user friendly string based interface. 
 *
// XXX Create _short version "url" and "defaulturl" w/o securenets
 *
 *	@param B BAKA thread/global state.
// XXX documentation
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_netutils_start_service(bk_s B, struct bk_run *run, char *url, char *defhoststr, char *defservstr, char *defprotostr, char *securenets, bk_bag_callback_t callback, void *args, int backlog, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *hoststr = NULL;
  char *servstr = NULL;
  char *protostr = NULL;
  struct bk_netinfo *bni = NULL;
  struct start_service_state *sss = NULL;
  void *ghbf_info;

  if (!run || !url || !callback)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }


// XXX - default any host, TCP service, ANY port (or use the bk_parse_local_endpt_spec)

  if (bk_parse_endpt_spec(B, url, &hoststr, defhoststr?defhoststr:BK_ADDR_ANY, &servstr, defservstr, &protostr, defprotostr?defprotostr:"tcp")<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not convert endpoint specifier\n");
    goto error;
  }

  if (securenets)
  {
    bk_error_printf(B, BK_ERR_WARN, "Securenets are not yet implemented, Caveat emptor.\n");
  }
  
  if (!hoststr || !servstr || !protostr)
  {
    bk_error_printf(B, BK_ERR_ERR, "Must specify all three of host/service/protocol\n");
    goto error;
  }

  if (!(bni = bk_netinfo_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create bk_netinfo\n");
    goto error;
  }

  // XXX - in theory, getservbyfoo should be the only necessary--this is redundant
  if (bk_getprotobyfoo(B,protostr, NULL, bni, 0)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set the protocol information\n");
    goto error;
  }

  if (bk_getservbyfoo(B, servstr, bni->bni_bpi->bpi_protostr, NULL, bni, 0)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set the service information\n");
    goto error;
  }

  if (!(sss = sss_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create sss\n");
    goto error;
  }

  sss->sss_flags = flags;
  sss->sss_lbni = bni;
  sss->sss_callback = callback;
  sss->sss_args = args;
  sss->sss_securenets = securenets;
  sss->sss_backlog = backlog;

  if (!(ghbf_info = bk_gethostbyfoo(B, hoststr, 0, NULL, bni, run, sss_serv_gethost_complete, sss, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not begin the hostname lookup process\n");
    goto error;
  }

  // <TODO>Return ghbf_info to the user somehow here point once there is a start_service_preservice_cancel--sss is not helpful.</TODO>

  if (hoststr) free(hoststr);
  if (servstr) free(servstr);
  if (protostr) free(protostr);
  BK_RETURN(B,0);
  
 error:
  if (hoststr) free(hoststr);
  if (servstr) free(servstr);
  if (protostr) free(protostr);
  if (bni) bk_netinfo_destroy(B, bni);
  if (sss) sss_destroy(B, sss);
  BK_RETURN(B,-1);
  
}



/**
 * Continue trying to set up a service following hostname determination.
 *	@param B BAKA thread/global state.
XXX - documentation failure
 */
static void
sss_serv_gethost_complete(bk_s B, struct bk_run *run , struct hostent **h, struct bk_netinfo *bni, void *args)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct start_service_state *sss;

  /* h is supposed to be NULL */
  if (!run || h || !bni || !(sss = args))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

  // XXX - pass securenets here
  if (bk_net_init(B, run, sss->sss_lbni, NULL, 0, sss->sss_flags, sss->sss_callback, sss->sss_args, sss->sss_backlog) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open service\n");
    goto error;
  }
  
  sss_destroy(B, sss);
  BK_VRETURN(B);

 error:  
  sss_destroy(B, sss);
  BK_VRETURN(B);
}



/**
 * Create a start_service_state structure.
 *	@param B BAKA thread/global state.
XXX - documentation failure
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new sss on success.
 */
static struct start_service_state *
sss_create(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct start_service_state *sss;
  
  if (!(BK_CALLOC(sss)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate sss: %s\n", strerror(errno));
    goto error;
  }
  
  BK_RETURN(B,sss);

 error:
  BK_RETURN(B,NULL);
}



/**
 * Destroy a @a start_service_state
 *	@param B BAKA thread/global state.
 *	@param sss The @a start_service_state to destroy.
 */
static void
sss_destroy(bk_s B, struct start_service_state *sss)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!sss)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }
  
  if (sss->sss_lbni) bk_netinfo_destroy(B,sss->sss_lbni);
  if (sss->sss_rbni) bk_netinfo_destroy(B,sss->sss_rbni);
  free(sss);

  BK_VRETURN(B);
}




/**
 * Start up a connectino with a user friendly interface.
 *
// XXX Create a _short version "rurl" and "defaultrurl" and no "lurl" and friends
 *
 *	@param B BAKA thread/global state.
// XXX Document this.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_netutils_make_conn(bk_s B, struct bk_run *run,
		      char *rurl, char *defrhost, char *defrserv,
		      char *lurl, char *deflhost, char *deflserv,
		      char *defproto, u_long timeout, bk_bag_callback_t callback, void *args, bk_flags flags )
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *rhoststr = NULL;
  char *rservstr = NULL;
  char *rprotostr = NULL;
  char *lhoststr = NULL;
  char *lservstr = NULL;
  char *lprotostr = NULL;
  struct bk_netinfo *rbni = NULL;
  struct bk_netinfo *lbni = NULL;
  struct start_service_state *sss = NULL;
  void *ghbf_info;


  if (!run || !rurl || !callback)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  /* Parse out the remote "URL" */
  if (bk_parse_endpt_spec(B, rurl, &rhoststr, defrhost, &rservstr, defrserv, &rprotostr, defproto?defproto:"tcp")<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not convert remote endpoint specify\n");
    goto error;
  }

  /* If anything is unset, die */
  if (!rhoststr || !rservstr || !rprotostr)
  {
    bk_error_printf(B, BK_ERR_ERR, "Must specify all three of remote host/service/protocol\n");
    goto error;
  }
  
  if (!deflhost) deflhost = BK_ADDR_ANY;
  if (!deflserv) deflserv = "0";
  if (!defproto) defproto = "tcp";

  /* Parse out the local side "URL" */
  if (bk_parse_endpt_spec(B, lurl?lurl:"", &lhoststr, deflhost?deflhost:BK_ADDR_ANY, &lservstr, deflserv, &lprotostr, defproto)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not convert remote endpoint specify\n");
    goto error;
  }

  /* If the protocol is unset, use the remote protocol */
  if (!lprotostr && !(lprotostr = strdup(rprotostr)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup protocol from remote: %s\n", strerror(errno));
    goto error;
  }

  /* Die on unset things */
  if (!lhoststr || !lservstr || !lprotostr)
  {
    bk_error_printf(B, BK_ERR_ERR, "Must specify all three of remote host/service/protocol\n");
    goto error;
  }

  /* Create remote netinfo */
  if (!(rbni = bk_netinfo_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create remote bni: %s\n", strerror(errno));
    goto error;
  }

  /* Create local netinfo */
  if (!(lbni = bk_netinfo_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create local bni: %s\n", strerror(errno));
    goto error;
  }

  // XXX - should be part of getserv
  /* Set remote protocol info */
  if (bk_getprotobyfoo(B, rprotostr, NULL, rbni, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set the remote protocol information\n");
    goto error;
  }

  // XXX - should be part of getserv
  /* Set local protocol info */
  if (bk_getprotobyfoo(B, lprotostr, NULL, lbni, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set the local protocol information\n");
    goto error;
  }

  /* If remote and local protocol info aren't the same, die */
  if (rbni->bni_bpi->bpi_proto != lbni->bni_bpi->bpi_proto)
  {
    bk_error_printf(B, BK_ERR_ERR, "Local and remote protocol specifications must match ([local proto num]%d != [remote proto num]%d\n", rbni->bni_bpi->bpi_proto, lbni->bni_bpi->bpi_proto);
    goto error;
  }

  /* Set remote service information */
  if (bk_getservbyfoo(B, rservstr, rbni->bni_bpi->bpi_protostr, NULL, rbni, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set the remote service information\n");
    goto error;
  }

  /* Set local service information */
  if (lservstr && bk_getservbyfoo(B, lservstr, lbni->bni_bpi->bpi_protostr, NULL, lbni, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set the local service information\n");
    goto error;
  }

  if (!(sss = sss_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate sss: %s\n", strerror(errno));
    goto error;
  }

  sss->sss_flags = flags;
  sss->sss_rbni = rbni;
  sss->sss_lbni = lbni;
  sss->sss_callback = callback;
  sss->sss_args = args;
  sss->sss_host = lhoststr;
  sss->sss_timeout = timeout;

  if (!(ghbf_info = bk_gethostbyfoo(B, rhoststr, 0, NULL, rbni, run, sss_connect_rgethost_complete, sss, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not start search for remote hostname\n");
    goto error;
  }

  // <TODO>ghbf_info needs to be returned, though some other structure (other than sss) to user at some point, so that make_conn_preconn_cancel can do something, when it is written</TODO>
    
  BK_RETURN(B,0);
  
 error:
  /* XXXXX Need to call back user */
  if (rhoststr) free(rhoststr);
  if (rservstr) free(rservstr);
  if (rprotostr) free(rprotostr);
  if (lhoststr) free(lhoststr);
  if (lservstr) free(lservstr);
  if (lprotostr) free(lprotostr);
  if (lbni) bk_netinfo_destroy(B, lbni);
  if (rbni) bk_netinfo_destroy(B, rbni);
  if (sss) sss_destroy(B,sss);
  BK_RETURN(B,-1);
  
}



/**
 * Finish up the first the hostname search
 *
 *	@param B BAKA thread/global state.
XXX - document this
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static void
sss_connect_rgethost_complete(bk_s B, struct bk_run *run, struct hostent **h, struct bk_netinfo *bni, void *args)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct start_service_state *sss = args;

  /* h is supposed to be NULL */
  if (!run || h || !bni || !sss)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    goto error;
  }

  if (!bk_gethostbyfoo(B, sss->sss_host, 0, NULL, sss->sss_lbni, run, sss_connect_lgethost_complete, sss, 0))
  {
    // XXX - some reason?
    // Intentionally not goto error;
    if (sss) sss_destroy(B,sss);
    BK_VRETURN(B);
  }

  BK_VRETURN(B);
  
 error:
  /* This *really* sucks, we have to call back the user */
  if (sss->sss_callback)
  {
    (*(sss->sss_callback))(B, sss->sss_args, -1, NULL, NULL, BK_ADDRGROUP_STATE_WIRE_ERROR);
    sss->sss_callback=NULL;
  }
  if (sss) sss_destroy(B,sss);
  BK_VRETURN(B);
}




/**
 * Finish up the second the hostname search
 *
 *	@param B BAKA thread/global state.
 * XXX - documentation
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static void
sss_connect_lgethost_complete(bk_s B, struct bk_run *run, struct hostent **h, struct bk_netinfo *bni, void *args)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct start_service_state *sss = args;

  /* h is supposed to be NULL */
  if (!run || h || !bni || !sss)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    goto error;
  }

  if (bk_net_init(B, run, sss->sss_lbni, sss->sss_rbni, sss->sss_timeout, sss->sss_flags, sss->sss_callback, sss->sss_args, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not start connection\n");
    if (sss) sss_destroy(B,sss);
    // Intentionally not goto error
    BK_VRETURN(B);
  }

  sss_destroy(B,sss);
  BK_VRETURN(B);

 error:  
  // XXX - call user callback
  if (sss) sss_destroy(B,sss);
  BK_VRETURN(B);
}
