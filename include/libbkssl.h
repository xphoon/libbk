/*
 * $Id: libbkssl.h,v 1.2 2003/06/03 17:47:37 lindauer Exp $
 *
 * ++Copyright LIBBK++
 * 
 * Copyright (c) 2001,2002 The Authors. All rights reserved.
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
 * Public APIs for libbkssl
 */

#ifndef _LIBBKSSL_h_
#define _LIBBKSSL_h_



/**
 * Forward (and incomplete) declarations
 */
struct bk_ssl_ctx;
struct bk_ssl;



extern int bk_ssl_env_init(bk_s B);
extern int bk_ssl_env_destroy(bk_s B);
struct bk_ssl_ctx *bk_ssl_create_context(bk_s B, const char *cert_path, const char *key_path, const char *dhparam_path, const char *cafile, bk_flags flags);
#define BK_SSL_REJECT_V2	0x01		///< Reject SSL v2 clients
#define BK_SSL_NOCERT		0x02		///< Don't use a certificate
extern void bk_ssl_destroy_context(bk_s B, struct bk_ssl_ctx *ssl_ctx);
extern int bk_ssl_start_service_verbose(bk_s B, struct bk_run *run, struct bk_ssl_ctx *ssl_ctx, char *url, char *defhoststr, char *defservstr, char *defprotostr, char *securenets, bk_bag_callback_f callback, void *args, int backlog, bk_flags flags);
extern int bk_ssl_make_conn_verbose(bk_s B, struct bk_run *run, struct bk_ssl_ctx *ssl_ctx, char *rurl, char *defrhost, char *defrserv, char *lurl, char *deflhost, char *deflserv, char *defproto, u_long timeout, bk_bag_callback_f callback, void *args, bk_flags flags );
struct bk_ioh *bk_ssl_ioh_init(bk_s B, struct bk_ssl *ssl, int fdin, int fdout, bk_iohhandler_f handler, void *opaque, u_int32_t inbufhint, u_int32_t inbufmax, u_int32_t outbufmax, struct bk_run *run, bk_flags flags);
extern void bk_ssl_destroy(bk_s B, struct bk_ssl *ssl, bk_flags flags);
#define BK_SSL_DESTROY_DONTCLOSEFDS	0x1	///< Don't close underlying fds on destroy



#endif /* _LIBBKSSL_h_ */