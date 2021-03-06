#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2001-2011";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2001-2011 The Authors. All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Send e-mail to <projectbaka@baka.org> for further information.
 *
 * - -Copyright BAKA- -
 */

/**
 * @file This module implements the BAKA config file facility. Config
 * files are nothing more than key/value pairs. Keys are single words
 * and values are comprised of everything which follows the first
 * instance of a key/value separator character (by default, this is
 * '=', though there may be one optional space before and after the
 * equal sign.
 *
 * <TODO>Implement reconfig</TODO>
 *
 * The format of these files is intended to be not totally
 * incompatible with the java.util.Properties load/store file format
 * described below but probably is after all.
 *
 * The BAKA config file facility also supports included config files with the
 *	#include file.name
 * directive (the particular token used for include can be changed from the
 * default "#include" in the BAKA config user preferences structure passed to
 * bk_configure_init.  Include loops are detected and quashed.
 *
 * No further interpolation of the key or value is performed.
 * Backslashes, control characters, or whatever are just normal
 * characters.
 *
 * lines with no separator character generate a warning and are ignored
 */

/*IGNORE*
 *IGNORE* For no apparent reason, we will now discuss in detail what some
 *IGNORE* random java routine does which is at most of interest to people
 *IGNORE* attempting to construct bk_config files which may be compatible.
 *IGNORE*
 *IGNORE* Properties.store is specified as follows:
 *IGNORE*
 *IGNORE* Every entry in this Properties table is written out, one per line. For each
 *IGNORE* entry the key string is written, then an ASCII =, then the associated
 *IGNORE* element string. Each character of the element string is examined to see
 *IGNORE* whether it should be rendered as an escape sequence. The ASCII characters \,
 *IGNORE* tab, newline, and carriage return are written as \\, \t, \n, and \r,
 *IGNORE* respectively. Characters less than \u0020 and characters greater than \u007E
 *IGNORE* are written as \uxxxx for the appropriate hexadecimal value xxxx. Leading
 *IGNORE* space characters, but not embedded or trailing space characters, are written
 *IGNORE* with a preceding \. The key and value characters #, !, =, and : are written
 *IGNORE* with a preceding slash to ensure that they are properly loaded.
 *IGNORE*
 *IGNORE* Properties.load is specified as follows:
 *IGNORE*
 *IGNORE* Reads a property list (key and element pairs) from the input stream. The
 *IGNORE* stream is assumed to be using the ISO 8859-1 character encoding.
 *IGNORE*
 *IGNORE* Every property occupies one line of the input stream. Each line is
 *IGNORE* terminated by a line terminator (\n or \r or \r\n). Lines from the input
 *IGNORE* stream are processed until end of file is reached on the input stream.
 *IGNORE*
 *IGNORE* A line that contains only whitespace or whose first non-whitespace character
 *IGNORE* is an ASCII # or ! is ignored (thus, # or ! indicate comment lines).
 *IGNORE*
 *IGNORE* Every line other than a blank line or a comment line describes one property
 *IGNORE* to be added to the table (except that if a line ends with \, then the
 *IGNORE* following line, if it exists, is treated as a continuation line, as
 *IGNORE* described below). The key consists of all the characters in the line
 *IGNORE* starting with the first non-whitespace character and up to, but not
 *IGNORE* including, the first ASCII =, :, or whitespace character. All of the key
 *IGNORE* termination characters may be included in the key by preceding them with a
 *IGNORE* \. Any whitespace after the key is skipped; if the first non-whitespace
 *IGNORE* character after the key is = or :, then it is ignored and any whitespace
 *IGNORE* characters after it are also skipped. All remaining characters on the line
 *IGNORE* become part of the associated element string. Within the element string, the
 *IGNORE* ASCII escape sequences \t, \n, \r, \\, \", \', \ (a backslash and a space),
 *IGNORE* and \uxxxx are recognized and converted to single characters. Moreover, if
 *IGNORE* the last character on the line is \, then the next line is treated as a
 *IGNORE* continuation of the current line; the \ and line terminator are simply
 *IGNORE* discarded, and any leading whitespace characters on the continuation line
 *IGNORE* are also discarded and are not part of the element string.
 *IGNORE*
 *IGNORE* As an example, each of the following four lines specifies the key "Truth"
 *IGNORE* and the associated element value "Beauty":
 *IGNORE*
 *IGNORE* Truth = Beauty
 *IGNORE*	Truth:Beauty
 *IGNORE* Truth	:Beauty
 *IGNORE*
 *IGNORE*
 *IGNORE* As another example, the following three lines specify a single property:
 *IGNORE*
 *IGNORE* fruits			apple, banana, pear, \
 *IGNORE*				cantaloupe, watermelon, \
 *IGNORE*				kiwi, mango
 *IGNORE*
 *IGNORE* The key is "fruits" and the associated element is:
 *IGNORE*
 *IGNORE* "apple, banana, pear, cantaloupe, watermelon, kiwi, mango"
 *IGNORE*
 *IGNORE* Note that a space appears before each \ so that a space will appear after
 *IGNORE* each comma in the final result; the \, line terminator, and leading
 *IGNORE* whitespace on the continuation line are merely discarded and are not
 *IGNORE* replaced by one or more other characters.
 *IGNORE*
 *IGNORE* As a third example, the line:
 *IGNORE*
 *IGNORE* cheeses
 *IGNORE*
 *IGNORE* specifies that the key is "cheeses" and the associated element is the empty
 *IGNORE* string.
 *IGNORE*/

#include <libbk.h>
#include "libbk_internal.h"


#define SET_CONFIG(b,B,c) do { if (!(c)) { (b)=BK_GENERAL_CONFIG(B); } else { (b)=(c); } } while (0) ///< Set the configuration database to use--either from BAKA or from passed-in
#define LINELEN 8192				///< Initial size of one configuration line
#define CONFIG_MANAGE_FLAG_NEW_KEY	0x1	///< Internal state info required for config_manage cleanup
#define CONFIG_MANAGE_FLAG_NEW_VALUE	0x2	///< Internal state info required for config_manage cleanup
#define BK_CONFIG_SEPARATORS	"="		///< Default configuration separator
#define BK_CONFIG_COMMENTCHARS	"#!"		///< Default configuration comments
#define BK_CONFIG_INCLUDE_TAG	"#include "	///< Default include-command key



/**
 * General configuration information
 */
struct bk_config
{
  bk_flags			bc_flags;	///< Everyone needs flags
  struct bk_config_fileinfo *	bc_bcf;		///< Files of conf data
  dict_h			bc_kv;		///< Hash of value dlls
  struct bk_config_user_pref	bc_bcup;	///< User prefrences
};



/**
 * Information about the files parsed when creating this configuration group
 */
struct bk_config_fileinfo
{
  bk_flags			bcf_flags;	///< Everyone needs flags
  char *			bcf_filename;	///< File of data
  dict_h			bcf_includes;	///< Included files
  struct bk_config_fileinfo *	bcf_insideof;	///< What file am I inside of
  struct stat			bcf_stat;	///< Stat struct
};



/**
 * Information about a specific key found when parsing the configuration file
 */
struct bk_config_key
{
  char *			bck_key;	///< Key string
  bk_flags			bck_flags;	///< Everyone needs flags
  dict_h			bck_values;	///< dll of values of this key
};



/**
 * A individual value for a key.
 *
 * <TODO>Doesn't this need a bk_config_fileinfo ptr as well, to
 * give context to the lineno?</TODO>
 */
struct bk_config_value
{
  char *			bcv_value;	///< Value string
  bk_flags			bcv_flags;	///< Everyone needs flags
  u_int				bcv_lineno;	///< Where value is in file
};



static struct bk_config_fileinfo *bcf_create(bk_s B, const char *filename, struct bk_config_fileinfo *obcf);
static void bcf_destroy(bk_s B, struct bk_config_fileinfo *bcf);
static int load_config_from_file(bk_s B, struct bk_config *bc, struct bk_config_fileinfo *bcf);
static struct bk_config_key *bck_create(bk_s B, const char *key, bk_flags flags);
static void bck_destroy(bk_s B, struct bk_config_key *bck);
static struct bk_config_value *bcv_create(bk_s B, const char *value, u_int lineno, bk_flags flags);
static void bcv_destroy(bk_s B, struct bk_config_value *bcv);
static int config_manage(bk_s B, struct bk_config *bc, const char *key, const char *value, const char *ovalue, u_int lineno);
static int check_for_double_include(bk_s B, struct bk_config *bc, struct bk_config_fileinfo *cur_bcf, struct bk_config_fileinfo *new_bcf);



/**
 * @name Defines: config_kv_clc
 * Key-value database CLC definitions
 * to hide CLC choice.
 */
// @{
#define config_kv_create(o,k,f,a)	bst_create((o),(k),(f))
#define config_kv_destroy(h)		bst_destroy(h)
#define config_kv_insert(h,o)		bst_insert((h),(o))
#define config_kv_insert_uniq(h,n,o)	bst_insert_uniq((h),(n),(o))
#define config_kv_append(h,o)		bst_append((h),(o))
#define config_kv_append_uniq(h,n,o)	bst_append_uniq((h),(n),(o))
#define config_kv_search(h,k)		bst_search((h),(k))
#define config_kv_delete(h,o)		bst_delete((h),(o))
#define config_kv_minimum(h)		bst_minimum(h)
#define config_kv_maximum(h)		bst_maximum(h)
#define config_kv_successor(h,o)	bst_successor((h),(o))
#define config_kv_predecessor(h,o)	bst_predecessor((h),(o))
#define config_kv_iterate(h,d)		bst_iterate((h),(d))
#define config_kv_nextobj(h,i)		bst_nextobj((h),(i))
#define config_kv_iterate_done(h,i)	bst_iterate_done((h),(i))
#define config_kv_error_reason(h,i)	bst_error_reason((h),(i))
static int kv_oo_cmp(void *bck1, void *bck2);
static int kv_ko_cmp(void *a, void *bck2);
#ifdef WE_ARE_ACTUALLY_GOING_TO_USE_THESE
static ht_val kv_obj_hash(void *bck);
static ht_val kv_key_hash(void *a);
static const struct ht_args kv_args = { 512, 1, kv_obj_hash, kv_key_hash };
#endif
// @}



/**
 * @name Defines: config_values_clc
 * list of values for a particular key CLC definitions
 * to hide CLC choice.
 */
// @{
#define config_values_create(o,k,f)		dll_create((o),(k),(f))
#define config_values_destroy(h)		dll_destroy(h)
#define config_values_insert(h,o)		dll_insert((h),(o))
#define config_values_insert_uniq(h,n,o)	dll_insert_uniq((h),(n),(o))
#define config_values_append(h,o)		dll_append((h),(o))
#define config_values_append_uniq(h,n,o)	dll_append_uniq((h),(n),(o))
#define config_values_search(h,k)		dll_search((h),(k))
#define config_values_delete(h,o)		dll_delete((h),(o))
#define config_values_minimum(h)		dll_minimum(h)
#define config_values_maximum(h)		dll_maximum(h)
#define config_values_successor(h,o)		dll_successor((h),(o))
#define config_values_predecessor(h,o)		dll_predecessor((h),(o))
#define config_values_iterate(h,d)		dll_iterate((h),(d))
#define config_values_nextobj(h,i)		dll_nextobj((h),(i))
#define config_values_iterate_done(h,i)		dll_iterate_done((h),(i))
#define config_values_error_reason(h,i)		dll_error_reason((h),(i))
static int bcv_oo_cmp(void *bck1, void *bck2);
static int bcv_ko_cmp(void *a, void *bck2);
// @}



/**
 * Initialize the config file subsystem.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state
 *	@param filename The file from which to read the data.
 *	@param bcup BAKA configure user preferences (see libbk.h for fields)
 *	@param flags reserved for future use
 *	@return <i>NULL</i> on call or allocation failure, other fatal error.
 *	@return <br><i>baka config structure</i> if successful.
 */
struct bk_config *
bk_config_init(bk_s B, const char *filename, struct bk_config_user_pref *bcup, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config *bc=NULL;
  struct bk_config_fileinfo *bcf=NULL;
  char *separators;
  char *commentchars;
  char *include_tag;

  if (!filename)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!BK_CALLOC(bc))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bc: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }
  bk_debug_printf_and(B, 128, "bc allocate: %p\n", bc);

  /* Create kv clc */
  if (!(bc->bc_kv=config_kv_create(kv_oo_cmp, kv_ko_cmp, DICT_UNORDERED|bk_thread_safe_if_thread_ready, &kv_args)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create config values clc\n");
    goto error;
  }
  bk_debug_printf_and(B, 128, "KV allocate: %p\n", bc->bc_kv);


  if (!(bcf=bcf_create(B, filename, NULL)))
  {
    bk_error_printf(B, BK_ERR_WARN, "Could not create fileinfo entry for %s\n", filename);
    goto error;
  }


  bc->bc_bcf=bcf;

  if (!bcup || !bcup->bcup_separators)
    separators=BK_CONFIG_SEPARATORS;
  else
    separators=bcup->bcup_separators;

  if (!(bc->bc_bcup.bcup_separators=strdup(separators)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate separators: %s\n", strerror(errno));
    goto error;
  }

  if (!bcup || !bcup->bcup_commentchars)
    commentchars=BK_CONFIG_COMMENTCHARS;
  else
    commentchars=bcup->bcup_commentchars;

  if (!(bc->bc_bcup.bcup_commentchars=strdup(commentchars)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate commentchars: %s\n", strerror(errno));
    goto error;
  }

  if (!bcup || !bcup->bcup_include_tag)
    include_tag=BK_CONFIG_INCLUDE_TAG;
  else
    include_tag=bcup->bcup_include_tag;

  if (!(bc->bc_bcup.bcup_include_tag=strdup(include_tag)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate include_tag: %s\n", strerror(errno));
    goto error;
  }

  if (bcup)
    bc->bc_bcup.bcup_flags=bcup->bcup_flags;
  else
    bc->bc_bcup.bcup_flags=0;

  if (load_config_from_file(B, bc, bcf) < 0)
  {
    bk_error_printf(B, BK_ERR_WARN, "Could not load config from file: %s\n", filename);
    /* Non-fatal */
  }

  BK_RETURN(B,bc);

 error:
  if (bc) bk_config_destroy(B, bc);
  BK_RETURN(B,NULL);
}



/**
 * Destroy a config structure
 *
 * THREADS: MT-SAFE (different obc)
 *
 *	@param B BAKA thread/global state
 *	@param obc The baka config structure to destroy. If NULL we destroy the version cached in the B structure.
 */
void
bk_config_destroy(bk_s B, struct bk_config *obc)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config *bc;

  SET_CONFIG(bc,B,obc);

  if (!bc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Trying to destroy a NULL config struct\n");
    BK_VRETURN(B);
  }

  if (bc->bc_kv)
  {
    struct bk_config_key *bck;

    DICT_NUKE_CONTENTS(bc->bc_kv, config_kv, bck, bk_error_printf(B, BK_ERR_ERR, "Could not delete minimum from list: %s",config_kv_error_reason(bc->bc_kv, NULL)), bk_debug_printf_and(B,128,"KV free: %p (%s)\n", bc->bc_kv, bck->bck_key); bck_destroy(B, bck) );
    config_kv_destroy(bc->bc_kv);
  }

  if (bc->bc_bcup.bcup_separators) free (bc->bc_bcup.bcup_separators);
  if (bc->bc_bcup.bcup_commentchars) free (bc->bc_bcup.bcup_commentchars);
  if (bc->bc_bcup.bcup_include_tag) free (bc->bc_bcup.bcup_include_tag);

  if (bc->bc_bcf) bcf_destroy(B, bc->bc_bcf);

  /*
   * Do this before free(3) so that Insight (et al) will not complain about
   * "reference after free"
   */
  if (BK_GENERAL_CONFIG(B)==bc)
  {
    BK_GENERAL_CONFIG(B)=NULL;
  }

  bk_debug_printf_and(B,128,"bc free: %p\n", bc);
  free(bc);

  BK_VRETURN(B);
}



/**
 * Load up the indicated config file from the indicated filename.  It opens
 * the file, reads it in line by line, locates the separator, passes the
 * key/value on for insertion, locates included files, and calls itself
 * recursively when it finds one.
 *
 * THREADS: REENTRANT (same bc)
 * THREADS: MT-SAFE (different bc)
 *
 *	@param B BAKA thread/global state
 *	@param B bc The baka config strucuture to which the key/values pairs will be added.
 *	@param B bcf The file information.
 *	@return <i>0</i> on failure
 *	@return <br><i>1</i> on sucess
 */
static int
load_config_from_file(bk_s B, struct bk_config *bc, struct bk_config_fileinfo *bcf)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  FILE *fp = NULL;
  int ret = 0;
  char *line = NULL;
  int linelen = LINELEN;
  int lineno = 0;

  if (!bc || !bcf)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(line = malloc(LINELEN)))
  {
    bk_error_printf(B, BK_ERR_ERR,"Could not allocate initial bkconf line length: %s\n", strerror(errno));
    ret=-1;
    goto done;
  }

  if (!(fp = fopen(bcf->bcf_filename, "r")))
  {
    bk_error_printf(B, BK_ERR_WARN, "Could not open %s: %s\n", bcf->bcf_filename, strerror(errno));
    ret=-1;
    goto done;
  }

  /* Get the stat and search for loops */
  if (fstat(fileno(fp), &bcf->bcf_stat) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not fstat %s: %s\n", bcf->bcf_filename, strerror(errno));
    ret = -1;
    goto done;
  }

  // <WARNING>This isn't quite the correct use of the API, but what the hell</WARNING>
  if (check_for_double_include(B, bc, bc->bc_bcf, bcf) != 1)
  {
    bk_error_printf(B, BK_ERR_NOTICE, "Double include detected (quashed)\n");
    /*
     * But this is not even a sort-of error. If we have already included
     * the file then we have all the keys and values so anything the user
     * might want will exist (we still return failure).
     */
    ret = 0;
    goto done;
  }

  bk_debug_printf_and(B, 1, "Have opened %s while loading config info", bcf->bcf_filename);
  while(!feof(fp) && fgets(line, linelen, fp))
  {
    char *start = line;
    char *rest, *endofkey;

    // Check to see if we didn't read the entire line (presumably due to line length limitations)
    while (!memchr(line,'\n', linelen))
    {
      if (!(start = realloc(line, linelen + LINELEN)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not realloc for long line of size over %d: %s\n", linelen, strerror(errno));
	ret = -1;
	goto done;
      }

      // Readjust and read the next part of the line
      line = start;
      start = line + strlen(line);
      linelen += LINELEN;
      if (fgets(start, LINELEN, fp) == NULL)
	break;
    }
    start = line;

    lineno++;

    bk_string_rip(B, start, NULL, 0);		/* Nuke trailing CRLF */

    // Check for, and ignore, all-blank lines
    while (isspace(*start))
      start++;
    if (BK_STREQ(start,""))
      continue;

    // check for include tag
    if (BK_STREQN(start, bc->bc_bcup.bcup_include_tag, strlen(bc->bc_bcup.bcup_include_tag)))
    {
      struct bk_config_fileinfo *nbcf;

      rest = start + strlen(bc->bc_bcup.bcup_include_tag);
      while (isspace(*rest))
	rest++;

      // <TODO>should we be interpreting escape characters?</TODO>
      if (!(nbcf = bcf_create(B, rest, bcf)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not create bcf for included file: %s\n", rest);
	ret=-1;
	goto done;
      }
      if (load_config_from_file(B, bc, nbcf) < 0)
      {
	bk_error_printf(B, BK_ERR_WARN, "Could not fully load: %s\n", rest);
	bcf_destroy(B, nbcf);
	// try to read rest of file anyway
      }
      continue;
    }

    //  Skip comments
    if (strchr(bc->bc_bcup.bcup_commentchars, start[0]))
      continue;

    // Reset from leading space bypass
    start = line;

    // Look for key/value separator
    if (!(rest = strpbrk(start,bc->bc_bcup.bcup_separators)))
    {
      //  Do not complain about comments, even with leading spaces
      while (isspace(*start))
	start++;
      if (strchr(bc->bc_bcup.bcup_commentchars, start[0]))
	continue;				// don't complain about comment
      start = line;
      bk_error_printf(B, BK_ERR_ERR, "%s:%d: no separator in line '%s'\n",
		      bcf->bcf_filename, lineno, start);
      continue;
    }

    endofkey = rest;

    // Look for optional space before separator
    if (rest > start && isspace(*(rest-1)))
    {
      endofkey--;
    }

    // Look for missing key
    if (endofkey == start)
    {
      bk_error_printf(B, BK_ERR_ERR, "%s:%d: no key in line '%s'\n",
		      bcf->bcf_filename, lineno, start);
      continue;
    }

    // Null terminate key
    *endofkey = 0;

    // Focus to start of value
    rest++;

    // Look for optional space after separator
    if (isspace(*rest))
      rest++;					// Adjust start of value right

    if (config_manage(B, bc, start, rest, NULL, lineno) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not add %s ==> %s to DB\n", start, rest);
      ret=-1;
      goto done;
    }
  }

  if (ferror(fp))
  {
    bk_error_printf(B, BK_ERR_ERR, "Error reading %s: %s\n", bcf->bcf_filename, strerror(errno));
    ret=-1;
    goto done;
  }

 done:
  if (line) free(line);
  if (fp) fclose(fp);

  BK_RETURN(B, ret);

}



/**
 * Internal management function. This the main workhorse of the
 * system. It operates entirely by checking the state of the arguments
 * and determining what to do based on this. If @a key is not found,
 * it is created and @a value appended to it (NULL values are not
 * permitted, though obviously there is no reason that @a value cannot
 * be ""). If @a ovalue is NULL, then @a value is added to @a key. If
 * both @a ovalue and @a value are non-NULL, then @a value replaces @a
 * ovalue. If @a ovalue is non-NULL, but @a value is NULL, then @a
 * olvalue is deleted from @a key. If deleting @a ovalue from @a key
 * would result in @a key being devoid of values, @a key is deleted.
 *
 * THREADS: MT-SAFE (assuming different bc)
 * THREADS: MT-SAFE (assuming no ovalue)
 * THREADS: EVIL (same bc + w/ovalue + worst case)
 *
 *	@param B BAKA thread/global state.
 *	@param bc Configure structure.
 *	@param key Key to add or modify.
 *	@param value Replacement or added value.
 *	@param ovalue Value to overwrite or delete.
 *	@param lineno Line in file where this value should be.
 *	@return <i>0</i> on success.
 *	@return <br><i>-1</i> on failure.
 *
 */
static int
config_manage(bk_s B, struct bk_config *bc, const char *key, const char *value, const char *ovalue, u_int lineno)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config_key *bck=NULL;
  struct bk_config_value *bcv=NULL;
  int flags=0;
  int ret=0;

  if (!bc || !key)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  // Null values should be OK and result simply in the key being ignored.
  // <TODO> There should be a way to enter the empty string, but that's a parsing issue, not a management one </TODO>
  if (!value && !ovalue)
    BK_RETURN(B, 0);

  if (!(bck=config_kv_search(bc->bc_kv,(char *)key)))
  {
    if (!(bck=bck_create(B, key,0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create key struct for %s\n", key);
      goto error;
    }

    bk_debug_printf_and(B,1,"Adding new key: %s", key);

    BK_FLAG_SET(flags, CONFIG_MANAGE_FLAG_NEW_KEY);

    if (config_kv_insert(bc->bc_kv, bck) != DICT_OK)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not insert key %s into config clc (continuing)\n", key);
      goto error;
    }
  }

  if (!ovalue)
  {
    /* Add a new value to key */
    if (!(bcv=bcv_create(B, value, lineno, 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create value struct for %s (continuing)\n", value);
      goto error;
    }

    BK_FLAG_SET(flags, CONFIG_MANAGE_FLAG_NEW_VALUE);

    if (config_values_append(bck->bck_values,bcv) != DICT_OK)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not insert %s in values clc\n", value);
      goto error;
    }

    bk_debug_printf_and(B,1,"Added new value: %s ==> %s", bck->bck_key, value);

  }
  else
  {
    if (!(bcv=config_values_search(bck->bck_values, (char *)ovalue)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not locate value: %s in key: %s\n", value, key);
      goto error;
    }

    if (value)
    {
      /* Replace ovalue with value. If strdup fails restore ovalue */
      char *hold;
      hold=bcv->bcv_value;
      if (!(bcv->bcv_value=strdup(value)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not strdup new value: %s (restoring %s)\n", value, ovalue);
	bcv->bcv_value=hold;
	ret=-1;
      }
      else
      {
	/* OK to free now */
	if (hold) free (hold); /* hold should *always* be nonnull, but check*/
	bk_debug_printf_and(B, 1, "Replaced %s ==> %s with %s ", bck->bck_key, ovalue, value);
      }
    }
    else
    {
      /* Delete value */
      config_values_delete(bck->bck_values, bcv);
      bk_debug_printf_and(B, 1, "Deleted %s ==> %s", bck->bck_key, value);
      bcv_destroy(B, bcv);
      if (!(config_values_minimum(bck->bck_values)))
      {
	/* That was the last value in the key. Get rid the key */
	config_kv_delete(bc->bc_kv, bck);
	bk_debug_printf_and(B,1,"Deleting empty key: %s", bck->bck_key);
	bck_destroy(B, bck);
      }
    }
  }

  BK_RETURN(B, ret);

 error:
  /* Destroy the value first to make sure you don't double free */
  if (BK_FLAG_ISSET(flags, CONFIG_MANAGE_FLAG_NEW_VALUE) && bcv)
  {
    config_values_delete(bck->bck_values, bcv);
    bcv_destroy(B, bcv);
  }

  if (BK_FLAG_ISSET(flags, CONFIG_MANAGE_FLAG_NEW_KEY) && bck)
  {
    config_kv_delete(bc->bc_kv, bck);
    bck_destroy(B, bck);
  }
  BK_RETURN(B, -1);
}



/**
 * Retrieve the configuration parameters of the config system.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param ibc The baka config structure to use. If NULL, the structure
 *	is extracted from @a B.
 *	@param filename Copyout the filename configured as the initial config file
 *	@param userpref Copyout the user prefernces configured as the initial value
 *	@param getflags Copyout the config flags
 *	@return <i>-1</i> on call failure, config location failure
 *	@return <br><i>0</i> on success.
 */
int bk_config_get(bk_s B, struct bk_config *ibc, const char **filename, struct bk_config_user_pref **bcup, bk_flags *getflags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config *bc;

  SET_CONFIG(bc, B, ibc);

  if (!bc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not find config\n");
    BK_RETURN(B, -1);
  }

  if (filename)
  {
    if (bc->bc_bcf)
      *filename = bc->bc_bcf->bcf_filename;
    else
      *filename = NULL;
  }

  if (bcup)
    *bcup = &bc->bc_bcup;

  if (getflags)
    *getflags = bc->bc_flags;

  BK_RETURN(B, 0);
}



/**
 * Retrieve a value based on the key.  If @a ovalue is NULL, then get first
 * value, else get successor of @a ovalue.
 *
 * THREADS: MT-SAFE (as long as there is no reconfig or manual maintenance)
 *
 *	@param B BAKA thread/global state.
 *	@param ibc The baka config structure to use. If NULL, the structure
 *	is extracted from @a B.
 *	@param key The key in question.
 *	@param ovalue The previous value or NULL (see above).
 *	@return <i>value string</i> (not copied) on success.
 *	@return <br><i>NULL</i> on failure.
 */
char *
bk_config_getnext(bk_s B, struct bk_config *ibc, const char *key, const char *ovalue)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config *bc;
  struct bk_config_key *bck;
  struct bk_config_value *bcv=NULL;

  if (!key)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  SET_CONFIG(bc, B, ibc);

  if (!bc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not locate config structure\n");
    BK_RETURN(B,NULL);
  }

  if (!(bck=config_kv_search(bc->bc_kv, (char *)key)))
  {
    // just too noisy and stupid for WARN level logging
    bk_error_printf(B, BK_ERR_NOTICE, "Could not locate key: %s\n", key);
    goto done;
  }

  if (!ovalue)
  {
    if (!(bcv=config_values_minimum(bck->bck_values)))
    {
      /*
       * An empty key (which can (only) happen if all its values have been
       * deleted since we do not delete the key structure) should be
       * portrayed as an non-existent key.
       */
      bk_error_printf(B, BK_ERR_WARN, "Could not locate key: %s\n", key);
      goto done;
    }
  }
  else
  {
    if (!(bcv=config_values_search(bck->bck_values,(char *)ovalue)))
    {
      bk_error_printf(B, BK_ERR_WARN, "Could not locate '%s' as a value of %s in order to get its successor\n", ovalue, key);
      goto done;
    }
    else
    {
      bcv=config_values_successor(bck->bck_values,bcv);
    }
  }
 done:
  if (bcv) BK_RETURN(B, bcv->bcv_value);
  BK_RETURN(B, NULL);
}



/**
 * Delete a key and all its values.
 *
 * THREADS: EVIL (will cause other config routines to be evil as well,
 *	since while the DS are safe, searched value used past config control)
 *
 *	@param B BAKA thread/global state.
 *	@param ibc The baka config structure to use. If NULL, the structure
 *	is extracted from @a B.
 *	@return <i>-1</i> on call failure, failure to find key.
 *	@return <br><i>0</i> on success.
 */
int
bk_config_delete_key(bk_s B, struct bk_config *ibc, const char *key)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config *bc;
  struct bk_config_key *bck;

  if (!key)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  SET_CONFIG(bc, B, ibc);

  if (!bc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not locate config structure\n");
    BK_RETURN(B,-1);
  }



  if (!(bck=config_kv_search(bc->bc_kv, (char *)key)))
  {
    bk_error_printf(B, BK_ERR_WARN, "Attempt do delete nonexistent key: %s\n", key);
    goto error;
  }

  if (config_kv_delete(bc->bc_kv, bck) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not delete %s from key list: %s\n", key, config_kv_error_reason(bc->bc_kv,NULL));
  }
  bck_destroy(B, bck);

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}



/**
 * Create a fileinfo structure for filename.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param filename The filename to use.
 *	@param obcf The fileinfo for the file that @a filename is included in.
 *	@return <i>new fileinfo structure</i> on success.
 *	@return <br><i>NULL</i> on failure.
 */
static struct bk_config_fileinfo *
bcf_create(bk_s B, const char *filename, struct bk_config_fileinfo *obcf)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  static struct bk_config_fileinfo *bcf=NULL;

  if (!filename)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!BK_CALLOC(bcf))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bcf: %s\n", strerror(errno));
    goto error;
  }

  if (!(bcf->bcf_filename=strdup(filename)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup filename: %s\n", strerror(errno));
    goto error;
  }

  if ((bcf->bcf_insideof=obcf))
  {
    if (dll_insert(obcf->bcf_includes, bcf) != DICT_OK)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not insert \"%s\" as include file of \"%s\"\n", filename, obcf->bcf_filename);
      goto error;
    }
  }

  if (!(bcf->bcf_includes=dll_create(NULL,NULL,DICT_UNORDERED|bk_thread_safe_if_thread_ready)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create include file dll for bcf \"%s\"\n", filename);
    goto error;
  }

  BK_RETURN(B, bcf);

 error:
  if (bcf) bcf_destroy(B, bcf);
  BK_RETURN(B, NULL);
}



/**
 * Destroy a fileinfo
 *
 * THREADS: MT-SAFE (assuming different bcf)
 *
 *	@param B BAKA thread/global state.
 *	@param bck The fileinfo to destroy.
 */
static void
bcf_destroy(bk_s B, struct bk_config_fileinfo *bcf)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config_fileinfo *ibcf;

  if (!bcf)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (bcf->bcf_includes)
  {
    while(ibcf=dll_minimum(bcf->bcf_includes))
    {
      bcf_destroy(B,ibcf);
    }
    dll_destroy(bcf->bcf_includes);
  }

  if (bcf->bcf_insideof) dll_delete(bcf->bcf_insideof->bcf_includes,bcf);
  if (bcf->bcf_filename) free(bcf->bcf_filename);
  free(bcf);
  BK_VRETURN(B);
}



/**
 * Create a key info structure.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param key Key name in string form.
 *	@param flags Flags to add to key (see code for valid flags).
 *	@return <i>new key structure</i> on success.
 *	@return <br><i>NULL</i> on failure.
 */
static struct bk_config_key *
bck_create(bk_s B, const char *key, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config_key *bck=NULL;

  if (!key)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!BK_CALLOC(bck))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bck: %s\n", strerror(errno));
    goto error;
  }

  if (!(bck->bck_key=strdup(key)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup key (%s): %s\n", key, strerror(errno));
    goto error;
  }

  bck->bck_flags=flags;

  if (!(bck->bck_values=config_values_create(bcv_oo_cmp,bcv_ko_cmp, DICT_UNORDERED|bk_thread_safe_if_thread_ready)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create values clc\n");
    goto error;
  }

  BK_RETURN(B,bck);

 error:
  if (bck) bck_destroy(B, bck);
  BK_RETURN(B,NULL);
}



/**
 * Destroy a key structure.
 *
 * THREADS: MT-SAFE (assuming different bck)
 *
 *	@param B BAKA thread/global state.
 *	@param bck The key to destroy.
 */
static void
bck_destroy(bk_s B, struct bk_config_key *bck)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config_value *bcv;

  if (!bck)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (bck->bck_key)
    free (bck->bck_key);

  /* This CLC header can be NULL if there was an error during bck creation */
  if (bck->bck_values)
  {
    DICT_NUKE_CONTENTS(bck->bck_values, config_values, bcv, bk_error_printf(B, BK_ERR_ERR, "Could not nuke a value from a key: %s\n", config_values_error_reason(bck->bck_values, NULL)); break, bcv_destroy(B, bcv));
    config_values_destroy(bck->bck_values);
  }

  free(bck);

  BK_VRETURN(B);
}



/**
 * Create a values structure.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param value The value to add
 *	@param lineno The line number within the file where this value
 *	appears.
 *	@param flags Flags to add to @a value.
 *	@return <i>new value structure</i> on success.
 *	@return <br><i>NULL</i> on failure.
 */
static struct bk_config_value *
bcv_create(bk_s B, const char *value, u_int lineno, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config_value *bcv=NULL;

  if (!value)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,NULL);
  }

  if (!BK_CALLOC(bcv))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bcv: %s\n", strerror(errno));
    goto error;
  }

  if (!(bcv->bcv_value=strdup(value)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup %s: %s\n", value, strerror(errno));
    goto error;
  }

  bcv->bcv_lineno=lineno;
  bcv->bcv_flags=flags;

  BK_RETURN(B, bcv);

 error:
  if (bcv) bcv_destroy(B, bcv);
  BK_RETURN(B,NULL);
}



/**
 * Destroy a values structure.
 *
 * THREADS: MT-SAFE (different bcv)
 *
 *	@param B BAKA thread/global state.
 *	@param bcv The values structure to destroy.
 */
static void
bcv_destroy(bk_s B, struct bk_config_value *bcv)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bcv)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (bcv->bcv_value) free (bcv->bcv_value);
  free(bcv);

  BK_VRETURN(B);
}



/**
 * Print out all the keys and their values.
 *
 * Note that the resulting file may not be a valid input for bk_config_init, if
 * escaped characters were used.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param ibc The baka config structure to use. If NULL, the structure
 *	is extracted from the @a B
 *	@param fp File stream to which to print.
 */
void
bk_config_print(bk_s B, struct bk_config *ibc, FILE *fp)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config *bc;
  struct bk_config_key *bck;
  struct bk_config_value *bcv;

  if (!fp)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  SET_CONFIG(bc,B,ibc);

  if (!bc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not locate config structure\n");
    BK_VRETURN(B);
  }


  for (bck=config_kv_minimum(bc->bc_kv);
       bck;
       bck=config_kv_successor(bc->bc_kv,bck))
  {
    for (bcv=config_values_minimum(bck->bck_values);
	 bcv;
	 bcv=config_values_successor(bck->bck_values,bcv))
      {
	/*
	 * <TODO>Should use bk_string_quote for key and value, to generate
	 * output that can be read back in correctly, but bk_string_quote uses
	 * octal escapes for everything, which only make matters worse.</TODO>
	 */
	fprintf (fp, "%s=%s\n", bck->bck_key, bcv->bcv_value);
      }
  }
  BK_VRETURN(B);
}



/**
 * Check the current fileinfo for possible "double inclusion". We do it by
 * fstat(2) so we should be fairly certain that the double inclusion is
 * real as well as being reasonably safe from race conditions. Note this a
 * recursive function.
 *
 * THREADS: MT-SAFE (different bc)
 *
 *	@param B BAKA thread/global state
 *	@param bc @a bk_config structure.
 *	@param cur_bcf File info of file against which we are checking.
 *	@param new_cvs File info for file we propose to include.
 *	@return <i>1</i> on no match.
 *	@return <i>0</i> on match.
 *	@return <br><i>-1</i> on severe failure.
 */
static int
check_for_double_include(bk_s B, struct bk_config *bc, struct bk_config_fileinfo *cur_bcf, struct bk_config_fileinfo *new_bcf)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config_fileinfo *bcf;
  int ret;

  if (!bc || !cur_bcf || !new_bcf)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (new_bcf == cur_bcf)
  {
    /* Base case. Looking comparing the same bcf structures. Ignore. */
    BK_RETURN(B,1);
  }

  if (new_bcf->bcf_stat.st_dev == cur_bcf->bcf_stat.st_dev &&
      new_bcf->bcf_stat.st_ino == cur_bcf->bcf_stat.st_ino)
  {
    bk_error_printf(B, BK_ERR_ERR, "%s is the same file as %s\n", new_bcf->bcf_filename, cur_bcf->bcf_filename);
    BK_RETURN(B,0);
  }

  for(bcf=dll_minimum(cur_bcf->bcf_includes);
      bcf;
      bcf=dll_successor(cur_bcf->bcf_includes,bcf))
  {
    if ((ret=check_for_double_include(B,bc,bcf,new_bcf))!=1)
      BK_RETURN(B,ret);
  }

  BK_RETURN(B,1);
}



/*
 * CLC key-side comparison routines
 */
static int kv_oo_cmp(void *bck1, void *bck2)
{
  return(strcmp(((struct bk_config_key *)bck1)->bck_key, ((struct bk_config_key *)bck2)->bck_key));
}
static int kv_ko_cmp(void *a, void *bck)
{
  return(strcmp((char *)a, ((struct bk_config_key *)bck)->bck_key));
}
#ifdef WE_ARE_ACTUALLY_GOING_TO_USE_THESE
static ht_val kv_obj_hash(void *bck)
{
  return(bk_strhash(((struct bk_config_key *)bck)->bck_key, 0));
}
static ht_val kv_key_hash(void *a)
{
  return(bk_strhash((char *)a, 0));
}
#endif



/*
 * CLC value-side comparison routines
 */
static int bcv_oo_cmp(void *bcv1, void *bcv2)
{
  return(((struct bk_config_value *)bcv1)->bcv_value - ((struct bk_config_value *)bcv2)->bcv_value);
}
static int bcv_ko_cmp(void *a, void *bcv)
{
  return((char *)a - ((struct bk_config_value *)bcv)->bcv_value);
}
