/*
 * $Id: test_printbuf.c,v 1.4 2003/09/05 18:41:46 seth Exp $
 *
 * ++Copyright LIBBK++
 *
 * Copyright (c) 2003 The Authors. All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Mail <projectbaka@baka.org> for further information
 *
 * --Copyright LIBBK--
 */

#include <libbk.h>

int main(int argc, char **argv, char **envp)
{
  bk_vptr buf;
  int x;
  char intro[64];
  char *prefix = NULL;

  if (argv[1])
    prefix = argv[1];

  buf.ptr = malloc(256);
  for(x=0;x<256;x++)
    ((char *)buf.ptr)[x] = x;

  for(x=0;x<=64;x++)
  {
    char *ret;

    buf.len = x;
    snprintf(intro,sizeof(intro),"Testing buffer w/size %d",x);
    ret = bk_string_printbuf(NULL, intro, prefix, &buf, 0);
    fputs(ret, stdout);
    free(ret);
    printf("\n");
  }
  exit(0);
  return(255);					/* Insight is stupid */
}
