#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_funlist.c,v 1.2 2001/11/05 20:53:06 seth Exp $";
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

#include <libbk.h>
#include "libbk_internal.h"


#define funlist_create(o,k,f,a)			dll_create(o,k,f)
#define funlist_destroy(h)			dll_destroy(h)
#define funlist_insert(h,o)			dll_insert(h,o)
#define funlist_insert_uniq(h,n,o)		dll_insert_uniq(h,n,o)
#define funlist_append(h,o)			dll_append(h,o)
#define funlist_append_uniq(h,n,o)		dll_append_uniq(h,n,o)
#define funlist_search(h,k)			dll_search(h,k)
#define funlist_delete(h,o)			dll_delete(h,o)
#define funlist_minimum(h)			dll_minimum(h)
#define funlist_maximum(h)			dll_maximum(h)
#define funlist_successor(h,o)			dll_successor(h,o)
#define funlist_predecessor(h,o)		dll_predecessor(h,o)
#define funlist_iterate(h,d)			dll_iterate(h,d)
#define funlist_nextobj(h,i)			dll_nextobj(h,i)
#define funlist_iterate_done(h,i)		dll_iterate_done(h,i)
#define funlist_error_reason(h,i)		dll_error_reason(h,i)



/*
 * Create the function list
 */
struct bk_funlist *bk_funlist_init(bk_s B, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_funlist *funlist;

  if (!(funlist = malloc(sizeof(*funlist))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate function list structure: %s\n",strerror(errno));
    BK_RETURN(B, NULL);
  }
  funlist->bf_flags = flags;

  if (!(funlist->bf_list = funlist_create(NULL, NULL, DICT_UNORDERED, NULL)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate function list list: %s\n", funlist_error_reason(NULL, NULL));
  }

  BK_RETURN(B, funlist);

 error:
  if (funlist)
  {
    if (funlist->bf_list)
      funlist_destroy(funlist->bf_list);
    free(funlist);
  }
  BK_RETURN(B, NULL);
}



/*
 * Get rid of the function list and its contents
 */
void bk_funlist_destroy(bk_s B, struct bk_funlist *funlist)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_fun *curfun;

  if (!funlist)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_VRETURN(B);
  }

  DICT_NUKE_CONTENTS(funlist->bf_list, funlist, curfun, bk_error_printf(B, BK_ERR_ERR, "Could not dictionary delete during nuke: %s\n",funlist_error_reason(funlist->bf_list, NULL)); break, free(curfun));
  funlist_destroy(funlist->bf_list);
  free(funlist);

  BK_VRETURN(B);
}



/*
 * Call all functions in the function list
 */
void bk_funlist_call(bk_s B, struct bk_funlist *funlist, u_int aux, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_fun *curfun;
  dict_iter iter;

  if (!funlist)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_VRETURN(B);
  }

  /* Iterate (to handle deletions) through the functions calling them */
  iter = funlist_iterate(funlist->bf_list, DICT_FROM_START);
  while (curfun = funlist_nextobj(funlist->bf_list, iter))
    (*curfun->bf_fun)(B, curfun->bf_args, aux);

  BK_VRETURN(B);
}



/*
 * Add a function to the end of the function list
 */
int bk_funlist_insert(bk_s B, struct bk_funlist *funlist, void (*bf_fun)(bk_s, void *, u_int), void *args, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_fun *curfun;

  if (!funlist && !bf_fun)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, -1);
  }

  if (!(curfun = malloc(sizeof(*curfun))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate function storage structure: %s\n", strerror(errno));
    BK_RETURN(B, -1);
  }
  curfun->bf_fun = bf_fun;
  curfun->bf_args = args;

  if (funlist_append(funlist->bf_list, curfun) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate function storage structure: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B,0);

 error:
  if (curfun)
    free(curfun);
  BK_RETURN(B, -1);
}



/*
 * Get rid of the first function on the list which has the same fun/args
 *
 * -1 = error
 * 0 = deleted
 * 1 = notfound
 */
int bk_funlist_delete(bk_s B, struct bk_funlist *funlist, void (*bf_fun)(bk_s, void *, u_int), void *args, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_fun *curfun;

  if (!funlist || !bf_fun)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B,-1);
  }

  for(curfun=funlist_minimum(funlist->bf_list);
      curfun && !(curfun->bf_fun == bf_fun && curfun->bf_args == args);
      curfun = funlist_successor(funlist->bf_list, curfun))
    ;

  if (curfun)
  {
    int ret = 0;
    if (funlist_delete(funlist->bf_list, curfun) != DICT_OK)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not delete requested function immediate after it was found! %s\n",funlist_error_reason(funlist->bf_list, NULL));
      ret = -1;
    }
    free(curfun);
    BK_RETURN(B, ret);
  }

  BK_RETURN(B, 1);
}