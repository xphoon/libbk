#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_stats.c,v 1.1 2003/03/25 04:56:23 seth Exp $";
static const char libbk__copyright[] = "Copyright (c) 2001";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright LIBBK++
 *
 * Copyright (c) 2002 The Authors.  All rights reserved.
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
 * All of the support routines for dealing with performance statistic tracking
 */

#include <libbk.h>
#include "libbk_internal.h"



#define MAXPERFINFO	8192			///< Maximum size for a performance information line



/**
 * Performance tracking list
 */
struct bk_stat_list
{
  dict_h	bsl_list;			///< List of performance tracks
  bk_flags	bsl_flags;			///< Flags for the future
};



/**
 * Performance tracking node
 */
struct bk_stat_node
{
  const char	       *bsn_name1;		///< Primary name of tracking node
  const char	       *bsn_name2;		///< Subsidiary name of tracking node
  u_int			bsn_minutime;		///< Minimum number of microseconds we have seen for this item
  u_int			bsn_maxutime;		///< Maximum number of microseconds we have seen for this item
  u_int			bsn_count;		///< Number of times we have tracked 
  bk_flags		bsn_flags;		///< Flags for the future
  u_quad_t		bsn_sumutime;		///< Sum of microseconds we have seen for this itme
  struct timeval	bsn_start;		///< Start time for current tracking
};



/**
 * @name Defines: bsl_clc
 * Performance tracking database CLC definitions
 * to hide CLC choice.
 */
// @{
#define bsl_create(o,k,f,a)	ht_create((o),(k),(f),(a))
#define bsl_destroy(h)		ht_destroy(h)
#define bsl_insert(h,o)		ht_insert((h),(o))
#define bsl_insert_uniq(h,n,o)	ht_insert_uniq((h),(n),(o))
#define bsl_append(h,o)		ht_append((h),(o))
#define bsl_append_uniq(h,n,o)	ht_append_uniq((h),(n),(o))
#define bsl_search(h,k)		ht_search((h),(k))
#define bsl_delete(h,o)		ht_delete((h),(o))
#define bsl_minimum(h)		ht_minimum(h)
#define bsl_maximum(h)		ht_maximum(h)
#define bsl_successor(h,o)	ht_successor((h),(o))
#define bsl_predecessor(h,o)	ht_predecessor((h),(o))
#define bsl_iterate(h,d)	ht_iterate((h),(d))
#define bsl_nextobj(h,i)	ht_nextobj((h),(i))
#define bsl_iterate_done(h,i)	ht_iterate_done((h),(i))
#define bsl_error_reason(h,i)	ht_error_reason((h),(i))
static int bsl_oo_cmp(struct bk_stat_node *a, struct bk_stat_node *b);
static int bsl_ko_cmp(struct bk_stat_node *a, struct bk_stat_node *b);
static ht_val bsl_obj_hash(struct bk_stat_node *a);
static ht_val bsl_key_hash(struct bk_stat_node *a);
static struct ht_args bsl_args = { 512, 1, (ht_func)bsl_obj_hash, (ht_func)bsl_key_hash };
// @}



/**
 * Create a performance statistic tracking list
 *
 *	@param B BAKA thread/global state.
 *	@param flags Fun for the future
 *	@return <i>NULL</i> on failure.<br>
 *	@return <br><i>performance list</i> on success
 */
struct bk_stat_list *bk_stat_create(bk_s B, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_stat_list *blist;

  if (!BK_CALLOC(blist))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate performance list structure: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }

  if (!(blist->bsl_list = bsl_create((dict_function)bsl_oo_cmp, (dict_function)bsl_ko_cmp, DICT_UNIQUE_KEYS, &bsl_args)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate performance list: %s\n", bsl_error_reason(NULL, NULL));
    goto error;
  }

  BK_RETURN(B, blist);

 error:
  if (blist)
    bk_stat_destroy(B, blist);
  BK_RETURN(B, NULL);
}



/**
 * Destroy a performance statistic tracking list
 *
 *	@param B BAKA thread/global state.
 *	@param blist List to destroy
 */
void bk_stat_destroy(bk_s B, struct bk_stat_list *blist)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_stat_node *bnode;

  if (!blist)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  if (blist->bsl_list)
  {
    DICT_NUKE(blist->bsl_list, bsl, bnode, bk_error_printf(B, BK_ERR_ERR, "Could not delete what we just minimized: %s\n", bsl_error_reason(blist->bsl_list, NULL)); break, bk_stat_node_destroy(B, bnode));
  }
  free(blist);

  BK_VRETURN(B);
}



/**
 * Create a performance statistic tracking node, attach to blist
 *
 *	@param B BAKA thread/global state.
 *	@param blist Performance list
 *	@param name1 Primary name
 *	@param name2 Secondary name
 *	@param flags Fun for the future
 *	@return <i>NULL</i> on failure.<br>
 *	@return <br><i>performance node</i> on success
 */
struct bk_stat_node *bk_stat_nodelist_create(bk_s B, struct bk_stat_list *blist, const char *name1, const char *name2, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_stat_node *bnode;

  if (!blist || !name1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(bnode = bk_stat_node_create(B, name1, name2, flags)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create node for list\n");
    BK_RETURN(B, NULL);
  }

  if (bsl_insert(blist->bsl_list, bnode) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert autocreated performance node %s/%s, pressing on\n",name1, name2?name2:"");
    bk_stat_node_destroy(B, bnode);
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B, bnode);
}



/**
 * Create a performance statistic tracking node
 *
 *	@param B BAKA thread/global state.
 *	@param name1 Primary name
 *	@param name2 Secondary name
 *	@param flags Fun for the future
 *	@return <i>NULL</i> on failure.<br>
 *	@return <br><i>performance node</i> on success
 */
struct bk_stat_node *bk_stat_node_create(bk_s B, const char *name1, const char *name2, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_stat_node *bnode;

  if (!name1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!BK_CALLOC(bnode))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate performance node: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }

  if (!(bnode->bsn_name1 = strdup(name1)) || (name2 && !(bnode->bsn_name2 = strdup(name2))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not duplicate performance node name: %s\n", strerror(errno));
    goto error;
  }
  bnode->bsn_minutime = UINT_MAX;

  BK_RETURN(B, bnode);

 error:
  if (bnode)
    bk_stat_node_destroy(B, bnode);
  BK_RETURN(B, NULL);
}



/**
 * Destroy a performance statistic tracking node
 *
 *	@param B BAKA thread/global state.
 *	@param bnode Node to destroy
 */
void bk_stat_node_destroy(bk_s B, struct bk_stat_node *bnode)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bnode)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  if (bnode->bsn_name1)
    free((void *)bnode->bsn_name1);

  if (bnode->bsn_name2)
    free((void *)bnode->bsn_name2);

  free(bnode);

  BK_VRETURN(B);
}



/**
 * Start a performance interval, by name
 *
 * @param B BAKA thread/global environment
 * @param blist Performance list
 * @param name1 Primary name
 * @param name2 Secondary name
 * @param flags Fun for the future
 */
void bk_stat_start(bk_s B, struct bk_stat_list *blist, const char *name1, const char *name2, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_stat_node *bnode;
  struct bk_stat_node searchnode;

  if (!blist || !name1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  searchnode.bsn_name1 = name1;
  searchnode.bsn_name2 = name2;

  if (!(bnode = bsl_search(blist->bsl_list, &searchnode)))
  {
    // New node, start tracking
    if (!(bnode = bk_stat_nodelist_create(B, blist, name1, name2, 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not autocreate performance node %s/%s, pressing on\n",name1, name2?name2:"");
      BK_VRETURN(B);
    }
  }
  bk_stat_node_start(B, bnode, 0);

  BK_VRETURN(B);
}



/**
 * End a performance interval, by name
 *
 * @param B BAKA thread/global environment
 * @param blist Performance list
 * @param name1 Primary name
 * @param name2 Secondary name
 * @param flags Fun for the future
 */
void bk_stat_end(bk_s B, struct bk_stat_list *blist, const char *name1, const char *name2, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_stat_node *bnode;
  struct bk_stat_node searchnode;

  if (!blist || !name1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  searchnode.bsn_name1 = name1;
  searchnode.bsn_name2 = name2;

  if (!(bnode = bsl_search(blist->bsl_list, &searchnode)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Tried to end an performance interval %s/%s which did not appear in list\n", name1, name2?name2:name2);
    BK_VRETURN(B);
  }
  bk_stat_node_end(B, bnode, 0);

  BK_VRETURN(B);
}



/**
 * Start a performance interval
 *
 * @param B BAKA thread/global environment
 * @param bnode Node to start interval
 * @param flags Fun for the future
 */
void bk_stat_node_start(bk_s B, struct bk_stat_node *bnode, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bnode)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }


  if (bnode->bsn_start.tv_sec != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Performance interval %s/%s has already been started\n", bnode->bsn_name1,bnode->bsn_name2?bnode->bsn_name2:"");
    BK_VRETURN(B);
  }

  gettimeofday(&bnode->bsn_start, NULL);

  BK_VRETURN(B);
}



/**
 * End a performance interval
 *
 * @param B BAKA thread/global environment
 * @param bnode Node to end interval
 * @param flags Fun for the future
 */
void bk_stat_node_end(bk_s B, struct bk_stat_node *bnode, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct timeval end, sum;
  u_int thisus = 0;

  if (!bnode)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  if (bnode->bsn_start.tv_sec == 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Performance interval %s/%s was ended but not started (perhaps double-started?)\n", bnode->bsn_name1,bnode->bsn_name2?bnode->bsn_name2:"");
    BK_VRETURN(B);
  }

  gettimeofday(&end, NULL);

  BK_TV_SUB(&sum, &end, &bnode->bsn_start);

  if (sum.tv_sec >= 4294)
  {
    bk_error_printf(B, BK_ERR_WARN, "Performance tracking only good for intervals <= 2^32/10^6 seconds\n");
    thisus = UINT_MAX;
  }
  else
    thisus = BK_SECSTOUSEC(sum.tv_sec) + sum.tv_usec;

  if (thisus < bnode->bsn_minutime)
    bnode->bsn_minutime = thisus;

  if (thisus > bnode->bsn_maxutime)
    bnode->bsn_maxutime = thisus;

  bnode->bsn_sumutime += thisus;
  bnode->bsn_count++;

  bnode->bsn_start.tv_sec = 0;

  BK_VRETURN(B);
}



/**
 * Return a string containing all performance information (which you must free)
 *
 * @param B BAKA thread/global environment
 * @param blist Performance list
 * @param flags BK_STAT_DUMP_HTML
 * @param <i>NULL</i> on call failure, allocation failure
 * @param <br><i>string you must free</i> on success
 */
char *bk_stat_dump(bk_s B, struct bk_stat_list *blist, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_stat_node *bnode;
  char perfbuf[MAXPERFINFO];
  bk_vstr ostring;

  if (!blist)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  ostring.ptr = malloc(MAXPERFINFO);
  ostring.max = MAXPERFINFO;
  ostring.cur = 0;

  if (BK_FLAG_ISSET(flags, BK_STAT_DUMP_HTML))
  {
    if (bk_vstr_cat(B, 0, &ostring, "<table summary=\"Performance Information\"><caption><em>Program Performance Statistics</em></caption><tr><th>Primary Name</th><th>Secondary Name</th><th>Minimum time (usec)</th><th>Average time (usec)</th><th>Maximum time (usec)</th><th>Count</th></tr>\n") < 0)
      goto error;
  }

  for (bnode = bsl_minimum(blist->bsl_list); bnode; bnode = bsl_successor(blist->bsl_list, bnode))
  {
    if (BK_FLAG_ISSET(flags, BK_STAT_DUMP_HTML))
    {
      snprintf(perfbuf, sizeof(perfbuf), "<tr><td>%s</td><td>%s</td><td>%u</td><td>%.3f</td><td>%u</td><td>%u</td></tr>\n",bnode->bsn_name1, bnode->bsn_name2, bnode->bsn_minutime, bnode->bsn_count?(float)bnode->bsn_sumutime/bnode->bsn_count:0.0, bnode->bsn_maxutime, bnode->bsn_count);
    }
    else
    {
      snprintf(perfbuf, sizeof(perfbuf), "\"%s\",\"%s\",\"%u\",\"%.3f\",\"%u\",\"%u\"\n",bnode->bsn_name1, bnode->bsn_name2, bnode->bsn_minutime, bnode->bsn_count?(float)bnode->bsn_sumutime/bnode->bsn_count:0.0, bnode->bsn_maxutime, bnode->bsn_count);
    }
    if (bk_vstr_cat(B, 0, &ostring, perfbuf) < 0)
      goto error;
  }

  if (BK_FLAG_ISSET(flags, BK_STAT_DUMP_HTML))
  {
    if (bk_vstr_cat(B, 0, &ostring, "</table>\n") < 0)
      goto error;
  }

  if (0)
  {
  error:
    bk_error_printf(B, BK_ERR_ERR, "Allocation failure: %s\n", strerror(errno));
    if (ostring.ptr)
      free(ostring.ptr);
    ostring.ptr = NULL;
  }

  BK_RETURN(B, ostring.ptr);
}



/**
 * Return a performance interval
 *
 * @param B BAKA thread/global environment
 * @param blist Performance list
 * @param name1 Primary name
 * @param name2 Secondary name
 * @param minusec Copy-out for node information
 * @param maxusec Copy-out for node information
 * @param sumusec Copy-out for node information
 * @param count Copy-out for node information
 * @param flags Fun for the future
 */
void bk_stat_info(bk_s B, struct bk_stat_list *blist, const char *name1, const char *name2, u_int *minusec, u_int *maxusec, u_quad_t *sumutime, u_int *count, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_stat_node *bnode;
  struct bk_stat_node searchnode;

  if (!blist || !name1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  searchnode.bsn_name1 = name1;
  searchnode.bsn_name2 = name2;

  if (!(bnode = bsl_search(blist->bsl_list, &searchnode)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Tried to dump a performance interval %s/%s which did not appear in list\n", name1, name2?name2:name2);
    BK_VRETURN(B);
  }

  if (minusec)
    *minusec = bnode->bsn_minutime;

  if (maxusec)
    *maxusec = bnode->bsn_maxutime;

  if (sumutime)
    *sumutime = bnode->bsn_sumutime;

  if (count)
    *count = bnode->bsn_count;

  BK_VRETURN(B);
}



/**
 * Return a performance interval
 *
 * @param B BAKA thread/global environment
 * @param bnode Node to return information for
 * @param minusec Copy-out for node information
 * @param maxusec Copy-out for node information
 * @param sumusec Copy-out for node information
 * @param count Copy-out for node information
 * @param flags Fun for the future
 */
void bk_stat_node_info(bk_s B, struct bk_stat_node *bnode, u_int *minusec, u_int *maxusec, u_quad_t *sumutime, u_int *count, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bnode)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  if (minusec)
    *minusec = bnode->bsn_minutime;

  if (maxusec)
    *maxusec = bnode->bsn_maxutime;

  if (sumutime)
    *sumutime = bnode->bsn_sumutime;

  if (count)
    *count = bnode->bsn_count;

  BK_VRETURN(B);
}



static int bsl_oo_cmp(struct bk_stat_node *a, struct bk_stat_node *b)
{
  int ret;

  if (ret = strcmp(a->bsn_name1, b->bsn_name1))
    return(ret);
  if (a->bsn_name2 && b->bsn_name2)
    return(strcmp(a->bsn_name2, b->bsn_name2));
  else
    return(a->bsn_name2 - b->bsn_name2);
}
static int bsl_ko_cmp(struct bk_stat_node *a, struct bk_stat_node *b)
{
  int ret;

  if (ret = strcmp(a->bsn_name1, b->bsn_name1))
    return(ret);
  if (a->bsn_name2 && b->bsn_name2)
    return(strcmp(a->bsn_name2, b->bsn_name2));
  else
    return(a->bsn_name2 - b->bsn_name2);
}
static ht_val bsl_obj_hash(struct bk_stat_node *a)
{
  u_int ret = bk_strhash(a->bsn_name1, BK_HASH_NOMODULUS);

  if (a->bsn_name2)
    ret += bk_strhash(a->bsn_name2, BK_HASH_NOMODULUS);
  return(ret);
}
static ht_val bsl_key_hash(struct bk_stat_node *a)
{
  u_int ret = bk_strhash(a->bsn_name1, BK_HASH_NOMODULUS);

  if (a->bsn_name2)
    ret += bk_strhash(a->bsn_name2, BK_HASH_NOMODULUS);
  return(ret);
}