#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_strcode.c,v 1.2 2002/09/06 20:36:06 jtt Exp $";
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

/**
 * @file
 * String conversion functions
 */

#include <libbk.h>
#include "libbk_internal.h"



/**
 * @name MIME routines
 *
 * Convert raw memory coding to and from an efficient portable representation
 * (MIMEB64) a la RFC2045
 *
 * These routines are from the perl module MIME::Base64 (i.e. not covered
 * under LGPL) converted into C by the Authors (e.g. a derivative work).
 *
 * Copyright (c) 1991 Bell Communications Research, Inc. (Bellcore)
 *
 * Permission to use, copy, modify, and distribute this material 
 * for any purpose and without fee is hereby granted, provided 
 * that the above copyright notice and this permission notice 
 * appear in all copies, and that the name of Bellcore not be 
 * used in advertising or publicity pertaining to this 
 * material without the specific, prior written permission 
 * of an authorized representative of Bellcore.	BELLCORE 
 * MAKES NO REPRESENTATIONS ABOUT THE ACCURACY OR SUITABILITY 
 * OF THIS MATERIAL FOR ANY PURPOSE.  IT IS PROVIDED "AS IS", 
 * WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES.
 */
// @{

#define MAX_LINE  76				///< size of encoded lines

/// The characters used for encoding, in order of their usage
static char basis_64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

#define XX      255				///< illegal base64 char
#define EQ      254				///< padding
#define INVALID XX				///< illegal base64 char

/**
 * The mime decode lookup table
 */
static unsigned char index_64[256] = {
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,62, XX,XX,XX,63,
    52,53,54,55, 56,57,58,59, 60,61,XX,XX, XX,EQ,XX,XX,
    XX, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
    15,16,17,18, 19,20,21,22, 23,24,25,XX, XX,XX,XX,XX,
    XX,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
    41,42,43,44, 45,46,47,48, 49,50,51,XX, XX,XX,XX,XX,

    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
};



/**
 * Encode a memory buffer into a base64 string.  The buffer will be broken
 * in to a series of lines 76 characters long, with the eol string used to
 * separate each line ("\n" used if the eol string is NULL).  If the eol
 * sequence is non-NULL, it will additionally be used to terminate the last
 * line. This function allocates memory which must be freed with free(3).
 *
 *	@param B BAKA Thread/Global state
 *	@param src Source memory to convert
 *	@param eolseq End of line sequence.
 *	@return <i>NULL</i> on error
 *	@return <br><i>encoded string</i> on success which caller must free
 */
char *bk_encode_base64(bk_s B, const bk_vptr *src, const char *eolseq)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *str;					/* string to encode */
  ssize_t len;					/* length of the string */
  const char *eol;				/* the end-of-line sequence to use */
  ssize_t eollen;				/* length of the EOL sequence */
  char *r, *ret;				/* result string */
  ssize_t rlen;					/* length of result string */
  unsigned char c1, c2, c3;
  int chunk;

  if (!src)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, NULL);
  }

  str = src->ptr;
  len = src->len;

  /* set up EOL from the second argument if present, default to "\n" */
  if (eolseq)
  {
    eol = eolseq;
  }
  else
  {
    eol = "\n";
  }
  eollen = strlen(eol);

  /* calculate the length of the result */
  rlen = (len+2) / 3 * 4;			/* encoded bytes */
  if (rlen)
  {
    /* add space for EOL */
    rlen += ((rlen-1) / MAX_LINE + 1) * eollen;
  }
  rlen++;					// Add space for null termination

  /* allocate a result buffer */
  if (!(ret = BK_MALLOC_LEN(r, rlen)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate memory(%d) for result: %s\n", (int) rlen, strerror(errno));
    BK_RETURN(B, NULL);
  }

  /* encode */
  for (chunk=0; len > 0; len -= 3, chunk++)
  {
    if (chunk == (MAX_LINE/4))
    {
      const char *c = eol;
      const char *e = eol + eollen;
      while (c < e)
	*r++ = *c++;
      chunk = 0;
    }

    c1 = *str++;
    *r++ = basis_64[c1>>2];

    if (len == 1)
    {
      *r++ = basis_64[(c1 & 0x3) << 4];
      *r++ = '=';
      *r++ = '=';
    }
    else
    {
      c2 = *str++;
      *r++ = basis_64[((c1 & 0x3) << 4) | ((c2 & 0xF0) >> 4)];

      if (len == 2)
      {
	*r++ = basis_64[(c2 & 0xF) << 2];
	*r++ = '=';
      }
      else
      {
	c3 = *str++;
	*r++ = basis_64[((c2 & 0xF) << 2) | ((c3 & 0xC0) >>6)];
	*r++ = basis_64[c3 & 0x3F];
      }
    }
  }

  if (rlen)
  {						/* append eol to the result string */
    const char *c = eol;
    const char *e = eol + eollen;
    while (c < e)
      *r++ = *c++;
  }
  *r = '\0';					/* NULL terminate */

  BK_RETURN(B, ret);
}



/**
 * Decode a base64 encoded buffer into memory buffer.
 *
 *	@param B BAKA Thread/Global state
 *	@param src Source memory to convert
 *	@param eolseq End of line sequence.
 *	@return <i>NULL</i> on error
 *	@return <br><i>decoded buffer</i> on success which caller must free
 *	(both bk_vptr and allocated buffer ptr) 
 */
bk_vptr *bk_decode_base64(bk_s B, const char *str)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  ssize_t len;
  const char *end;
  char *r;
  ssize_t rlen;
  unsigned char c[4];
  bk_vptr *ret;

  if (!str)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!BK_MALLOC(ret))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate return structure\n");
    BK_RETURN(B, NULL);
  }

  len = strlen(str);
  rlen = len * 3 / 4;				// Might be too much, but always enough
  rlen++;					// Add space for null termination
  if (!BK_MALLOC_LEN(ret->ptr, rlen))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate return structure\n");
    free(ret);
    BK_RETURN(B, NULL);
  }

  len = strlen(str);
  end = str + len;
  r = ret->ptr;
  ret->len = 0;

  while (str < end)
  {
    int i = 0;

    do
    {
      unsigned char uc = index_64[(int)*str++];
      if (uc != INVALID)
	c[i++] = uc;

      if (str == end)
      {
	if (i < 4)
	{
	  if (i)
	    bk_error_printf(B, BK_ERR_WARN, "Premature end of base64 data\n");

	  if (i < 2)
	    goto done;				// break outer loop

	  if (i == 2)
	    c[2] = EQ;

	  c[3] = EQ;
	}
	break;
      }
    } while (i < 4);
	    
    if (c[0] == EQ || c[1] == EQ)
    {
      bk_error_printf(B, BK_ERR_WARN, "Premature padding of base64 data\n");
      break;
    }

    bk_debug_printf_and(B, 1, "c0=%d,c1=%d,c2=%d,c3=%d\n", c[0],c[1],c[2],c[3]);

    *r++ = (c[0] << 2) | ((c[1] & 0x30) >> 4);
    ret->len++;

    if (c[2] == EQ)
      break;
    *r++ = ((c[1] & 0x0F) << 4) | ((c[2] & 0x3C) >> 2);
    ret->len++;

    if (c[3] == EQ)
      break;
    *r++ = ((c[2] & 0x03) << 6) | c[3];
    ret->len++;
  }

 done:
  BK_RETURN(B, ret);
}
// @}



#define CHUNK_LEN(cur_len, chunk_len) ((((cur_len) / (chunk_len)) + 1) * (chunk_len))
#define XML_LT_STR		"&lt;"
#define XML_GT_STR		"&gt;"
#define XML_AMP_STR		"&amp;"

/**
 * Convert a string to a valid xml string. The function allocates memory
 * which must be freed with free(3).
 *
 *	@param B BAKA thread/global state.
 *	@param str The string to convert
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>xml string</i> on success.
 */
char *
bk_string_str2xml(bk_s B, const char *str, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *xml = NULL;
  char *p;
  int len, l;
  char c;
  char *tmp;
  
  len = CHUNK_LEN(strlen(str), 1024);
  if (!(xml = malloc(len)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate xml string: %s\n", strerror(errno));
    goto error;
  }
  
  
  p = xml;
  while((c = *str))
  {
    /* <TODO> 
     * Perhaps we should all the caller to be more selective as to *which*
     * non-printables he wishes to permit. NEWLINEs and TABs for instance
     * make sense, but CTL-A perhas does not.
     * </TODOO>
     */
    if (!isprint(c) && BK_FLAG_ISCLEAR(flags, BK_STRING_STR2XML_FLAG_ALLOW_NON_PRINT))
    {
      char scratch[100];
      snprintf(scratch, 100,  "#x%x", c);
      l = strlen(scratch);
      memcpy(p, scratch, l);
      len -= l;
      p += l;
    }
    else
    {
      switch (c)
      {
      case '<':
	l = strlen(XML_LT_STR);
	memcpy(p, XML_LT_STR, l);
	len -= l;
	p += l;
	break;
	
      case '>':
	l = strlen(XML_GT_STR);
	memcpy(p, XML_GT_STR, l);
	len -= l;
	p += l;
	break;

      case '&':
	l = strlen(XML_AMP_STR);
	memcpy(p, XML_AMP_STR, l);
	len -= l;
	p += l;
	break;

      default:
	*p = c;
	p++;
	len--;
	break;
      }
    }

    if (len < 100)
    {
      len = CHUNK_LEN(len, 1024);
      if (!(tmp = realloc(xml, len)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not realloc xml string: %s\n", strerror(errno));
	goto error;
      }
      xml = tmp;
    }
    str++;
  }

  *p = '\0';

  BK_RETURN(B,xml);

 error:
  if (xml)
    free(xml);

  BK_RETURN(B,NULL);  
}