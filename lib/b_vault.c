#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_vault.c,v 1.1 2003/02/01 04:23:11 seth Exp $";
static const char libbk__copyright[] = "Copyright (c) 2001";
static const char libbk__contact[] = "<projectbaka@baka.org>";
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

#include <libbk.h>
#include "libbk_internal.h"


/**
 * @file
 * Vault for storage of values by string keys (note a great deal of functionality
 * is provided by #define wrappers in libbk.h)
 */



/**
 * @name Defines: vault_kv_clc
 * Key-value database CLC definitions
 * to hide CLC choice.
 */
// @{
#define vault_create(o,k,f,a)		ht_create((o),(k),(f),(a))
static int vault_oo_cmp(void *bck1, void *bck2);
static int vault_ko_cmp(void *a, void *bck2);
static ht_val vault_obj_hash(void *bck);
static ht_val vault_key_hash(void *a);
// @}





/**
 * Create vault
 *
 *	@param B BAKA thread/global state 
 *	@param table_entries Optional number of entries in CLC table (0 for default)
 *	@param bucket_entries Optional number of entries in each bucket (0 for default)
 *	@param flags fun for the future
 *	@return <i>NULL</i> on call failure, allocation failure, etc
 *	@return <br><i>allocated vault</i> on success.
 */
bk_vault_t bk_vault_create(bk_s B, int table_entries, int bucket_entries, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_vault_t vault = NULL;
  struct ht_args vault_args = { 512, 1, vault_obj_hash, vault_key_hash };

  if (table_entries)
    vault_args.ht_table_entries = table_entries;

  if (bucket_entries)
    vault_args.ht_bucket_entries = bucket_entries;

  if (!(vault = vault_create(vault_oo_cmp, vault_ko_cmp, DICT_HT_STRICT_HINTS|DICT_UNIQUE_KEYS, &vault_args)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create vault CLC: %s\n", bk_vault_error_reason(NULL, NULL));
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B, vault);
}



/**
 * CLC functions for vault
 */
// @{
static int vault_oo_cmp(void *bck1, void *bck2)
{
  struct bk_vault_node *a = bck1, *b = bck2;
  return(strcmp(a->key,b->key));
}
static int vault_ko_cmp(void *a, void *bck2)
{
  struct bk_vault_node *b = bck2;
  return(strcmp(a,b->key));
}
static ht_val vault_obj_hash(void *bck)
{
  struct bk_vault_node *a = bck;
  return(bk_strhash(a->key,BK_HASH_NOMODULUS));
}
static ht_val vault_key_hash(void *a)
{
  return(bk_strhash(a,BK_HASH_NOMODULUS));
}
// @}