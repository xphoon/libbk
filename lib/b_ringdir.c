#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2004-2011";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2004-2011 The Authors. All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Send e-mail to <projectbaka@baka.org> for further information.
 *
 * - -Copyright BAKA- -
 */

/**
 * @file
 *
 *
 * Implements a "Ring Directory" which is a directory where files are
 * created and written to in a sequential manner until the configured
 * number of files have been exhausted whereupon it truncates and write to
 * the first file again.
 *
 */

#include <libbk.h>
#include "libbk_internal.h"

#define NEXT_FILE_NUM(brd,num)		(((num)+1)%(brd)->brd_max_num_files)
#define PREVIOUS_FILE_NUM(brd,num)	(((num)==0)?((brd)->brd_max_num_files-1):((num)-1))

#define INCREMENT_FILE_NUM(brd)	 do { (brd)->brd_cur_file_num = NEXT_FILE_NUM(brd, (brd)->brd_cur_file_num); } while(0)
#define CHECK_ESTIMATE_EVERY		512	// How often to double-check state estimate
#define MAXIMUM_LEVELS			10	// Maximum number of levels we support

/**
 * Internal state for managing ring directories.
 */
struct bk_ring_directory
{
  bk_flags			brd_flags;	///< Everyone needs flags.
  off_t				brd_rotate_size; ///< The maximum size of a file.
  u_int32_t			brd_max_num_files; ///< The maximum number of files in directory.
  u_int32_t			brd_num_files;	   ///< Cur num files (<= max_num_files)
  const char *			brd_directory;	///< Directory name;
  const char *			brd_pattern;	///< File name pattern.
  const char *			brd_path;	////< Full path with pattern.
  const char *			brd_pathext;	////< Full path with pattern.
  u_int32_t			brd_cur_file_num; ///< Index of current file.
  u_int32_t			brd_offset_level;
  off_t				brd_offset_estimate; ///< The current estimate of where we are
  const char *			brd_cur_filename; ///< The current file we are updating.
  void *			brd_opaque;	///< User data.
  int				brd_split_levels; ///< How many times pattern is subdivided (typically into subdirectories)
  int				brd_perlevel;	  ///< How many items per level
  struct bk_ringdir_callbacks	brd_brc;	///< Callback structure.
};



/**
 * Private state for the "standard" ring directory implementation.
 */
struct bk_ringdir_standard
{
  bk_flags	brs_flags;			///< Everyone needs flags.
  int		brs_fd;				///< Currently active fd
  void		*brs_filehandle;			///< Currently active file handle
  off_t		(*brs_ftell)(void *);		///< File handle tell function
  const char *	brs_chkpnt_filename;		///< Name of checkpoint file.
  const char *	brs_cur_filename;		///< Current filename (for sanity check).
  void *	brs_opaque;			///< Private data for those who are only using some of the standard callbacks.
};



/**
 * Standard list of callbacks for people where is is sufficient
 */
struct bk_ringdir_callbacks bk_ringdir_standard_callbacks =
{
  bk_ringdir_standard_init,
  bk_ringdir_standard_destroy,
  bk_ringdir_standard_get_size,
  bk_ringdir_standard_open,
  bk_ringdir_standard_close,
  bk_ringdir_standard_unlink,
  bk_ringdir_standard_chkpnt,
  bk_ringdir_standard_exists,
};



static int write_settings(bk_s B, struct bk_ring_directory *brd, bk_flags flags);
static int read_settings(bk_s B, struct bk_ring_directory *brd, u_int32_t *num_filesp, u_int32_t *max_num_filesp, bk_flags flags);
static u_int32_t get_filenumber_from_path(bk_s B, struct bk_ring_directory *brd, const char *filename, bk_flags flags);


#define TEST_FILE_NUM 1

/**
 * Create a @a bk_ring_directory -- typically called for you by bk_ringdir_init
 *
 *	@param B BAKA thread/global state.
 *	@param flags BK_RINGDIR_FLAG_CREATE_NOWRITE
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
bk_ringdir_t
bk_ringdir_create(bk_s B, const char *directory, off_t rotate_size, u_int32_t max_num_files, const char *pattern, struct bk_ringdir_callbacks *callbacks, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ring_directory *brd = NULL;
  char *tmp_filename = NULL;
  const char *levelloc = NULL;
  char *tmpc;
  u_int32_t num_files = 0;
  u_int32_t tmp_max_num_files = 0;


  if (!directory || !rotate_size || !callbacks)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!BK_CALLOC(brd))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate state for bk ring directory: %s\n", strerror(errno));
    goto error;
  }

  brd->brd_rotate_size = rotate_size;
  brd->brd_max_num_files = max_num_files;
  brd->brd_brc = *callbacks;			// Structure copy.
  brd->brd_flags = flags;
  brd->brd_num_files = 0;

  if (rotate_size == (off_t)BK_RINGDIR_GET_SIZE_ERROR)
  {
    bk_error_printf(B, BK_ERR_WARN, "rotate_size is too large. Lowering limit slightly\n");
    rotate_size = BK_RINGDIR_GET_SIZE_MAX;
  }

  if (!brd->brd_brc.brc_get_size ||
      !brd->brd_brc.brc_open ||
      !brd->brd_brc.brc_close ||
      !brd->brd_brc.brc_unlink)
  {
    bk_error_printf(B, BK_ERR_ERR, "Callback list is missing at least one required callback.\n");
    goto error;
  }

  // If we don't have a ckpnt callback, turn off checkpointing.
  if (!(brd->brd_brc.brc_chkpnt))
  {
    if (BK_FLAG_ISCLEAR(brd->brd_flags, BK_RINGDIR_FLAG_NO_CHECKPOINT))
      bk_error_printf(B, BK_ERR_WARN, "No check callback specified. Turning off checkpointing\n");

    BK_FLAG_SET(brd->brd_flags, BK_RINGDIR_FLAG_NO_CHECKPOINT);
  }

  if (!(brd->brd_directory = strdup(directory)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not copy directory name: %s\n", strerror(errno));
    goto error;
  }

  if (!pattern)
    pattern = "%u";

  if (!(brd->brd_pattern = strdup(pattern)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not copy file name pattern: %s\n", strerror(errno));
    goto error;
  }

  if (!(brd->brd_path = bk_string_alloc_sprintf(B, 0, 0, "%s%s", brd->brd_directory, pattern)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create file name pattern: %s\n", strerror(errno));
    goto error;
  }

  if (!(brd->brd_pathext = bk_string_alloc_sprintf(B, 0, 0, "%s%s", brd->brd_directory, pattern)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create file name pattern: %s\n", strerror(errno));
    goto error;
  }
  for (tmpc = (char *)brd->brd_pathext + strlen(brd->brd_directory); *tmpc; tmpc++)
  {
    if (*tmpc == '/')
      *tmpc = '_';
  }

  levelloc = brd->brd_pattern;
  while (levelloc = strchr(levelloc, '%'))
  {
    if (levelloc[1] == '%')
    {
      levelloc++;
    }
    else
    {
      brd->brd_split_levels++;
    }
    levelloc++;
  }

  if (bk_ringdir_get_status(B, brd, NULL, NULL, &num_files, &tmp_max_num_files, BK_RINGDIR_FLAG_INTERNAL|flags) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not get status from existing cache file\n");
    goto error;
  }

  if (!max_num_files)
    brd->brd_max_num_files = max_num_files = tmp_max_num_files;

  brd->brd_num_files = num_files;

  if (!max_num_files)
  {
    bk_error_printf(B, (flags&BK_RINGDIR_FLAG_EWARN)?BK_ERR_WARN:BK_ERR_ERR, "Maximum number of files neither specified nor intuitable\n");
    goto error;
  }

  brd->brd_perlevel = (int)ceil(pow(max_num_files,1.0/(double)brd->brd_split_levels));

  if (brd->brd_split_levels > MAXIMUM_LEVELS)
  {
    bk_error_printf(B, BK_ERR_ERR, "We only support at most %d levels of pattern levels, you have %d\n", MAXIMUM_LEVELS, brd->brd_split_levels);
    goto error;
  }

  //Test pattern by attempting to create filename with TEST_FILE_NUM.
  if (!(tmp_filename = bk_ringdir_create_file_name(B, brd, TEST_FILE_NUM, BK_RINGDIR_FLAG_DO_NOT_CREATE)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create valid filename. Is the pattern OK: %s\n", brd->brd_pattern);
    goto error;
  }

  // Now convert *back* and make sure we still have TEST_FILE_NUM.
  if (get_filenumber_from_path(B, brd, tmp_filename, 0) != TEST_FILE_NUM)
  {
    bk_error_printf(B, BK_ERR_ERR, "Failed to properly create valid filename. Is the pattern OK? %s\n", brd->brd_pattern);
    goto error;
  }

  free(tmp_filename);
  tmp_filename = NULL;

  if (BK_FLAG_ISCLEAR(flags, BK_RINGDIR_FLAG_CREATE_NOWRITE))
  {
    if (write_settings(B, brd, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Failed to write settings file.\n");
      goto error;
    }
  }


  BK_RETURN(B,(bk_ringdir_t)brd);

 error:
  if (brd)
    bk_ringdir_idestroy(B, brd);

  if (tmp_filename)
    free(tmp_filename);

  BK_RETURN(B,NULL);
}



/**
 * Destroy a @a bk_ring_directory
 *
 *	@param B BAKA thread/global state.
 *	@param brd The @a bk_ring_directory to nuke.
 */
void
bk_ringdir_idestroy(bk_s B, bk_ringdir_t brdh)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ring_directory *brd = (struct bk_ring_directory *)brdh;

  if (!brd)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (brd->brd_directory)
    free((char *)brd->brd_directory);

  if (brd->brd_pattern)
    free((char *)brd->brd_pattern);

  if (brd->brd_path)
    free((char *)brd->brd_path);

  if (brd->brd_pathext)
    free((char *)brd->brd_pathext);

  if (brd->brd_cur_filename)
    free((char *)brd->brd_cur_filename);

  free(brd);

  BK_VRETURN(B);
}



/**
 * Initialize a ring directory. Specify the directory name, the file name
 * pattern (if desired), and the maximum files in directory. If the
 * directory does not exist it will be created unless caller demurs.
 *
 *
 * BK_RINGDIR_FLAG_NUKE_DIR_ON_DESTROY: If passed in here, this flag will
 * be saved for use in in bk_ringdir_destroy().
 *
 * BK_RINGDIR_FLAG_DO_NOT_CREATE: Do not create non-existent directories;
 * return an error instead.
 *
 * BK_RINGDIR_FLAG_NO_CONTINUE: Do not pick up at checkpointed spot. Simply
 * start over.
 *
 * BK_RINGDIR_FLAG_CANT_APPEND: The underlying "file" object does not
 * support append so checkpoints must simply start with the next "file"
 * name instead of continuing right where the last job left off.
 *
 *	@param B BAKA thread/global state.
 *	@param directory The name of the ring "directory"
 *	@param rotate_size The size threshold which triggers a rotation on the next check.
 *	@param max_num_files How many "files" to create in the "directory" before reuse kicks in.
 *	@param file_name_pattern sprintf(3) pattern (must contain %u or similar) from which the
 *			"directory" "file" names will be generated--%u. May be NULL.
 *			If there are multiple %u or similar character, the assumption is you desire something like
 *			a directory heirarchy with a smaller number of files per directory.
 *	@param private Private data for the init() <b>callback</b>.
 *	@param callbacks Ponter to the structure summarizing the underlying implementation callbacks.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a bk_ringdir_t on success.
 */
bk_ringdir_t
bk_ringdir_init(bk_s B, const char *directory, off_t rotate_size, u_int32_t max_num_files, const char *file_name_pattern, void *private, struct bk_ringdir_callbacks *callbacks, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ring_directory *brd = NULL;
  int new_file = 0;

  if (!directory || !rotate_size || !max_num_files || !callbacks)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(brd = bk_ringdir_create(B, directory, rotate_size, max_num_files, file_name_pattern, callbacks, flags)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create bk ring directory state\n");
    goto error;
  }

  if (brd->brd_brc.brc_init && !(brd->brd_opaque = (*brd->brd_brc.brc_init)(B, directory, rotate_size, max_num_files, file_name_pattern, private, ((flags & BK_RINGDIR_FLAG_DO_NOT_CREATE)||(brd->brd_flags & BK_RINGDIR_FLAG_DO_NOT_CREATE)))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create user state\n");
    goto error;
  }

  // Recover checkpoint unless caller sez no.
  if (BK_FLAG_ISCLEAR(flags, BK_RINGDIR_FLAG_NO_CONTINUE) && brd->brd_brc.brc_chkpnt)
  {
    int ret;
    bk_flags ringdir_open_flags = 0;

    switch(ret = (*brd->brd_brc.brc_chkpnt)(B, brd->brd_opaque, BkRingDirChkpntActionRecover, brd->brd_directory, brd->brd_pattern, &brd->brd_cur_file_num, BkRingDirCallbackSourceInit, 0))
    {
    case -1:  // Error
      bk_error_printf(B, BK_ERR_ERR, "Failed to recover checkpoint value\n");
      goto error;

    case 0:  // Checkpoint found. Open for append
      if (BK_FLAG_ISCLEAR(flags, BK_RINGDIR_FLAG_CANT_APPEND))
      {
	// The underlying "file" supports append, so set the append flag.
	ringdir_open_flags = BK_RINGDIR_FLAG_OPEN_APPEND;
      }
      else
      {
	// The underlying "file" does not support append. Just skip to the next "file" name.
	INCREMENT_FILE_NUM(brd);
      }
      break;

    case 1: // Checkpoint not found. Do normal truncate open.
      break;

    default:
      bk_error_printf(B, BK_ERR_ERR, "Unknown return value from open callback: %d\n", ret);
      break;
    }

    if (!(brd->brd_cur_filename = bk_ringdir_create_file_name(B, brd, brd->brd_cur_file_num, 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create filename from pattern\n");
      goto error;
    }

    switch(ret = (*brd->brd_brc.brc_exists)(B, brd->brd_opaque, brd->brd_cur_filename, brd->brd_cur_file_num, 0))
    {
    case -1:
      bk_error_printf(B, BK_ERR_ERR, "Could not check for the existence of %s\n", brd->brd_cur_filename);
      goto error;
      break;

    case 0:
      new_file = 1;
      break;

    case 1:
      break;

    default:
      bk_error_printf(B, BK_ERR_ERR, "Unregognized return value from item existence callback: %d\n", ret);
      goto error;
      break;
    }

    if ((*brd->brd_brc.brc_open)(B, brd->brd_opaque, brd->brd_cur_filename, brd->brd_cur_file_num, BkRingDirCallbackSourceInit, ringdir_open_flags))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not open %s\n", brd->brd_cur_filename);
      goto error;
    }

    /*
     * Account for the new file immediately because even if there is a
     * later error, the file has been created in the ring.
     */
    if (new_file)
    {
      brd->brd_num_files++;
      if (write_settings(B, brd, 0) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not save ring settings after a new item was created\n");
	goto error;
      }
    }

    if (BK_FLAG_ISCLEAR(brd->brd_flags,BK_RINGDIR_FLAG_NO_CHECKPOINT) &&
	((*brd->brd_brc.brc_chkpnt)(B, brd->brd_opaque, BkRingDirChkpntActionChkpnt, brd->brd_directory, brd->brd_pattern, &brd->brd_cur_file_num, BkRingDirCallbackSourceRotate, 0) < 0))
    {
      bk_error_printf(B, BK_ERR_ERR, "Failed to checkpoint\n");
      goto error;
    }

    if (BK_FLAG_ISSET(ringdir_open_flags, BK_RINGDIR_FLAG_OPEN_APPEND))
    {
      // Check that we did just *happen* to check point a file which is full.
      if (bk_ringdir_rotate(B, brd, 0, 0) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Failed to perform rotate check\n");
	goto error;
      }
    }
  }

  BK_RETURN(B,(bk_ringdir_t)brd);

 error:
  if (brd)
    bk_ringdir_idestroy(B, brd);

  BK_RETURN(B,NULL);
}



/**
 * Destroy a ring directory.
 *
 * BK_RINGDIR_FLAG_NUKE_DIR_ON_DESTROY: Empty directory and ask callback to
 * nuke directory.
 *
 * BK_RINGDIR_FLAG_NO_NUKE: Override BK_RINGDIR_FLAG_NUKE_DIR_ON_DESTROY
 * flag which might be cached in @a brdh
 *
 *	@param B BAKA thread/global state.
 *	@param brdh Ring directory handle.
 *	@param flags Flags for future use.
 */
void
bk_ringdir_destroy(bk_s B, bk_ringdir_t brdh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ring_directory *brd = (struct bk_ring_directory *)brdh;
  const char *filename = NULL;
  u_int32_t cnt;
  bk_flags destroy_callback_flags = 0;

  if (!brd)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (brd->brd_cur_filename)
  {
    if ((*brd->brd_brc.brc_close)(B, brd->brd_opaque, brd->brd_cur_filename, brd->brd_cur_file_num, BkRingDirCallbackSourceDestroy, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not close %s\n", brd->brd_cur_filename);
      goto error;
    }

    if (BK_FLAG_ISCLEAR(brd->brd_flags,BK_RINGDIR_FLAG_NO_CHECKPOINT) &&
	((*brd->brd_brc.brc_chkpnt)(B, brd->brd_opaque, BkRingDirChkpntActionChkpnt, brd->brd_directory, brd->brd_pattern, &brd->brd_cur_file_num, BkRingDirCallbackSourceDestroy, 0) < 0))
    {
      bk_error_printf(B, BK_ERR_ERR, "Failed to checkpoint\n");
    }

    free((char *)brd->brd_cur_filename);
    brd->brd_cur_filename = NULL;
  }

  if ((BK_FLAG_ISCLEAR(flags, BK_RINGDIR_FLAG_NO_NUKE) &&
       BK_FLAG_ISSET(brd->brd_flags, BK_RINGDIR_FLAG_NUKE_DIR_ON_DESTROY)) ||
      BK_FLAG_ISSET(flags, BK_RINGDIR_FLAG_NUKE_DIR_ON_DESTROY))
  {
    BK_FLAG_SET(destroy_callback_flags, BK_RINGDIR_FLAG_NUKE_DIR_ON_DESTROY);
    for(cnt = 0; cnt < brd->brd_max_num_files; cnt++)
    {
      if (!(filename = bk_ringdir_create_file_name(B, brd, cnt, 0)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not create filename\n");
	goto error;
      }

      if ((*brd->brd_brc.brc_unlink)(B, brd->brd_opaque, filename, cnt, BkRingDirCallbackSourceDestroy, 0) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not unlink %s\n", filename);
      }

      free((char *)filename);
      filename = NULL;
    }

    if (brd->brd_brc.brc_chkpnt)
      (*brd->brd_brc.brc_chkpnt)(B, brd->brd_opaque, BkRingDirChkpntActionDelete, brd->brd_directory, brd->brd_pattern, NULL, BkRingDirCallbackSourceDestroy, 0);
  }

  if (brd->brd_brc.brc_destroy)
    (*brd->brd_brc.brc_destroy)(B, brd->brd_opaque, brd->brd_directory, destroy_callback_flags);

  bk_ringdir_idestroy(B, brd);

  BK_VRETURN(B);

 error:
  if (filename)
    free((char *)filename);

  BK_VRETURN(B);
}



/**
 * Check if a rotate is needed and do so if it is.
 *
 *
 * BK_RINGDIR_FLAG_FORCE_ROTATE: Rotate regardless of file "size"
 *
 *	@param B BAKA thread/global state.
 *	@param brdh Ring directory handle.
 *	@param estimate_size_increment An estimate as to how much bigger the file may have gotten since the last call (0 means don't know)
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_ringdir_rotate(bk_s B, bk_ringdir_t brdh, u_int estimate_size_increment, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ring_directory *brd = (struct bk_ring_directory *)brdh;
  int need_rotate = 0;
  int new_file = 0;

  if (!brd)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  BK_FLAG_CLEAR(brd->brd_flags, BK_RINGDIR_FLAG_ROTATION_OCCURED);

  if (BK_FLAG_ISSET(flags, BK_RINGDIR_FLAG_FORCE_ROTATE))
  {
    need_rotate = 1;
  }
  else
  {
    brd->brd_offset_estimate += estimate_size_increment;

    // If we have an estimate, bypass state unless
    if (!estimate_size_increment ||
	brd->brd_offset_estimate >= brd->brd_rotate_size ||
	(brd->brd_offset_level++ % CHECK_ESTIMATE_EVERY) == 1)
    {
      if ((brd->brd_offset_estimate = (*brd->brd_brc.brc_get_size)(B, brd->brd_opaque, brd->brd_cur_filename, brd->brd_cur_file_num, 0)) == (off_t)BK_RINGDIR_GET_SIZE_ERROR)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not obtain size of %s\n", brd->brd_cur_filename);
	goto error;
      }

      if (brd->brd_offset_estimate >= brd->brd_rotate_size)
	need_rotate = 1;
    }
  }

  if (need_rotate)
  {
    int ret;
    brd->brd_offset_level = brd->brd_offset_estimate = 0;

    if ((*brd->brd_brc.brc_close)(B, brd->brd_opaque, brd->brd_cur_filename, brd->brd_cur_file_num, BkRingDirCallbackSourceRotate, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not close %s\n", brd->brd_cur_filename);
      goto error;
    }

    // Checkpoint here just in case we get an error before the next open
    if (BK_FLAG_ISCLEAR(brd->brd_flags,BK_RINGDIR_FLAG_NO_CHECKPOINT) &&
	((*brd->brd_brc.brc_chkpnt)(B, brd->brd_opaque, BkRingDirChkpntActionChkpnt, brd->brd_directory, brd->brd_pattern, &brd->brd_cur_file_num, BkRingDirCallbackSourceRotate, 0) < 0))
    {
      bk_error_printf(B, BK_ERR_ERR, "Failed to checkpoint\n");
      goto error;
    }

    free((char *)brd->brd_cur_filename);
    brd->brd_cur_filename = NULL;

    INCREMENT_FILE_NUM(brd);

    if (!(brd->brd_cur_filename = bk_ringdir_create_file_name(B, brd, brd->brd_cur_file_num, 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create filename from pattern\n");
      goto error;
    }

    switch(ret = (*brd->brd_brc.brc_exists)(B, brd->brd_opaque, brd->brd_cur_filename, brd->brd_cur_file_num, 0))
    {
    case -1:
      bk_error_printf(B, BK_ERR_ERR, "Could not check for the existence of %s\n", brd->brd_cur_filename);
      goto error;
      break;

    case 0:
      new_file = 1;
      break;

    case 1:
      break;

    default:
      bk_error_printf(B, BK_ERR_ERR, "Unregognized return value from item existence callback: %d\n", ret);
      goto error;
      break;
    }

    if ((*brd->brd_brc.brc_unlink)(B, brd->brd_opaque, brd->brd_cur_filename, brd->brd_cur_file_num, BkRingDirCallbackSourceRotate, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not unlink %s\n", brd->brd_cur_filename);
      goto error;
    }

    if ((*brd->brd_brc.brc_open)(B, brd->brd_opaque, brd->brd_cur_filename, brd->brd_cur_file_num, BkRingDirCallbackSourceRotate, 0))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not open %s\n", brd->brd_cur_filename);
      goto error;
    }

    /*
     * Account for the new file immediately because even if there is a
     * later error, the file has been created in the ring.
     */
    if (new_file)
    {
      brd->brd_num_files++;
      if (write_settings(B, brd, 0) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not save ring settings after a new item was created\n");
	goto error;
      }
    }


    if (BK_FLAG_ISCLEAR(brd->brd_flags,BK_RINGDIR_FLAG_NO_CHECKPOINT) &&
	((*brd->brd_brc.brc_chkpnt)(B, brd->brd_opaque, BkRingDirChkpntActionChkpnt, brd->brd_directory, brd->brd_pattern, &brd->brd_cur_file_num, BkRingDirCallbackSourceRotate, 0) < 0))
    {
      bk_error_printf(B, BK_ERR_ERR, "Failed to checkpoint\n");
      goto error;
    }

    BK_FLAG_SET(brd->brd_flags, BK_RINGDIR_FLAG_ROTATION_OCCURED);
  }

  BK_RETURN(B,0);

 error:
  BK_RETURN(B,-1);
}



/**
 * Obtain one's private data from the ring directory handle.
 *
 *	@param B BAKA thread/global state.
 *	@param brdh Ring directory handle.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return @a private_data on success.
 */
void *
bk_ringdir_get_private_data(bk_s B, bk_ringdir_t brdh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ring_directory *brd = (struct bk_ring_directory *)brdh;

  if (!brd)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B,brd->brd_opaque);
}



/**
 * Create a filename based on the directoy, pattern, and cnt.
 *
 *	@param B BAKA thread/global state.
 *	@param brdh Ring directory handle
 *	@param cnt What file number we are expanding to
 *	@param flags BK_RINGDIR_FLAG_DO_NOT_CREATE
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>filename</i> on success.
 */
char *
bk_ringdir_create_file_name(bk_s B, bk_ringdir_t brdh, u_int32_t cnt, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ring_directory *brd = (struct bk_ring_directory *)brdh;
  char *filename = NULL;
  int levelnums[MAXIMUM_LEVELS];
  int levelcounter;
  char *dupname = NULL;
  char *curname;
  u_int32_t origcnt = cnt;
  int needfullmkdir = 1;

  if (!brd)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  for(levelcounter=0; levelcounter < brd->brd_split_levels; levelcounter++)
  {
    levelnums[levelcounter] = cnt % brd->brd_perlevel;
    cnt /= brd->brd_perlevel;
  }

  // I don't think you are allowed to portably construct a stdargs :-(
  switch(brd->brd_split_levels)
  {
  case 1: filename = bk_string_alloc_sprintf(B, 0, 0, brd->brd_path, origcnt); break;
  case 2: filename = bk_string_alloc_sprintf(B, 0, 0, brd->brd_path, levelnums[1], origcnt); break;
  case 3: filename = bk_string_alloc_sprintf(B, 0, 0, brd->brd_path, levelnums[2], levelnums[1], origcnt); break;
  case 4: filename = bk_string_alloc_sprintf(B, 0, 0, brd->brd_path, levelnums[3], levelnums[2], levelnums[1], origcnt); break;
  case 5: filename = bk_string_alloc_sprintf(B, 0, 0, brd->brd_path, levelnums[4], levelnums[3], levelnums[2], levelnums[1], origcnt); break;
  case 6: filename = bk_string_alloc_sprintf(B, 0, 0, brd->brd_path, levelnums[5], levelnums[4], levelnums[3], levelnums[2], levelnums[1], origcnt); break;
  case 7: filename = bk_string_alloc_sprintf(B, 0, 0, brd->brd_path, levelnums[6], levelnums[5], levelnums[4], levelnums[3], levelnums[2], levelnums[1], origcnt); break;
  case 8: filename = bk_string_alloc_sprintf(B, 0, 0, brd->brd_path, levelnums[7], levelnums[6], levelnums[5], levelnums[4], levelnums[3], levelnums[2], levelnums[1], origcnt); break;
  case 9: filename = bk_string_alloc_sprintf(B, 0, 0, brd->brd_path, levelnums[8], levelnums[7], levelnums[6], levelnums[5], levelnums[4], levelnums[3], levelnums[2], levelnums[1], origcnt); break;
  case 10: filename = bk_string_alloc_sprintf(B, 0, 0, brd->brd_path, levelnums[9], levelnums[8], levelnums[7], levelnums[6], levelnums[5], levelnums[4], levelnums[3], levelnums[2], levelnums[1], origcnt); break;
  default:
    bk_error_printf(B, BK_ERR_ERR, "Invalid number of split levels: %d\n", brd->brd_split_levels);
    goto error;
  }

  if (!filename)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create file name: %s\n", strerror(errno));
    goto error;
  }

  if (BK_FLAG_ISCLEAR(flags, BK_RINGDIR_FLAG_DO_NOT_CREATE) && BK_FLAG_ISCLEAR(brd->brd_flags, BK_RINGDIR_FLAG_DO_NOT_CREATE))
  {
    if (!(curname = dupname = strdup(filename)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not duplicate file name: %s\n", strerror(errno));
      goto error;
    }

    if (!(curname = strrchr(curname+1,'/')))
    {
      bk_error_printf(B, BK_ERR_ERR, "Must have a directory in the file pattern\n");
      goto error;
    }
    *curname = 0;

    if (mkdir(dupname, 0777) >= 0 || errno == EEXIST)
      needfullmkdir = 0;

    if (needfullmkdir)
    {
      *curname = '/';
      curname = dupname;
      while (curname = strchr(curname + 1, '/'))
      {
	*curname = 0;
	if (mkdir(dupname, 0777) < 0)
	{
	  if (errno != EEXIST)
	  {
	    bk_error_printf(B, BK_ERR_ERR, "Could not make path component %s: %s\n", dupname, strerror(errno));
	    goto error;
	  }
	}
	*curname = '/';
      }
    }

    free(dupname);
    dupname = NULL;
  }

  BK_RETURN(B,filename);

 error:
  if (filename)
    free(filename);

  if (dupname)
    free(dupname);

  BK_RETURN(B,NULL);
}



/**
 *
 * Allocate and initialize private data for this particular
 * implementation of a ring directory. The valued returnd from this
 * function will the @a opaque argument in all the other functions in the
 * API. This function is OPTIONAL but if it declared then it must return
 * a non NULL value for success even if there is no private state
 * created. Using (void *)1 works fine, but irriates the heck out of
 * memory checkers. We suggest returning the function name (ie function
 * pointer) instead. For your convenience, you are passed all the values
 * which the caller passed to @a bk_ringdir_init(), you may use them how
 * you will.
 *
 *	@param B BAKA thread/global state.
 *	@param directory The "path" to the ring directory;
 *	@param rotate_size How big a file may grow before rotation.
 *	@param max_num_files How many files in the ring dir before reuse.
 *	@param file_name_pattern The sprintf-like pattern for creating names in the directory.
 *	@param private Private data.
 *	@param flags Flags for future use.
 *
 * BK_RINGDIR_FLAG_DO_NOT_CREATE: If you passed this flag, do not create
 * the directory if it doesn't exist; return an error instead.
 *
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>private_data</i> on success.
 */
void *
bk_ringdir_standard_init(bk_s B, const char *directory, off_t rotate_size, u_int32_t max_num_files, const char *file_name_pattern, void *private, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs = NULL;
  struct stat st;
  int does_not_exist = 0;
  int created_directory = 1;
  bk_flags destroy_flags = 0;
  char *tmpc;

  if (!directory || !file_name_pattern)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (directory[strlen(directory)-1] != '/')
  {
    bk_error_printf(B, BK_ERR_ERR, "Unix directory name must end in '/'. Sorry...\n");
    goto error;
  }

  if (!BK_CALLOC(brs))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate standard ringdir state: %s\n", strerror(errno));
    goto error;
  }
  brs->brs_fd = -1;
  brs->brs_opaque = private;

  if (!(brs->brs_chkpnt_filename = bk_string_alloc_sprintf(B, 0, 0, "%s%s", directory, file_name_pattern)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create checkpoint file name\n");
    goto error;
  }
  for (tmpc = (char *)brs->brs_chkpnt_filename + strlen(directory); *tmpc; tmpc++)
  {
    if (*tmpc == '/')
      *tmpc = '_';
  }

  if (stat(directory, &st) < 0)
  {
    if (errno == ENOENT)
      does_not_exist = 1;
    else
      bk_error_printf(B, BK_ERR_ERR, "Could not stat %s: %s\n", directory, strerror(errno));
  }

   if (does_not_exist)
  {
    if (BK_FLAG_ISCLEAR(flags, BK_RINGDIR_FLAG_DO_NOT_CREATE))
    {
      if (mkdir(directory, 0777) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not create %s: %s\n", directory, strerror(errno));
	goto error;
      }
      created_directory = 1;
    }
    else
    {
      bk_error_printf(B, BK_ERR_ERR, "%s does not exist and caller requested not to create it\n", directory);
      goto error;
    }
  }
  else
  {
    if (!S_ISDIR(st.st_mode))
    {
      bk_error_printf(B, BK_ERR_ERR, "%s exists but is not a directory\n", directory);
      goto error;
    }
  }

  BK_RETURN(B,brs);

 error:
  if (brs)
  {
    if (created_directory)
    {
      destroy_flags = BK_RINGDIR_FLAG_NUKE_DIR_ON_DESTROY;
    }
    else
    {
      directory = NULL;
    }
    bk_ringdir_standard_destroy(B, brs, directory, destroy_flags);
  }

  BK_RETURN(B,NULL);
}



/**
 * Create a @a bk_ring_directory
 *
 *	@param B BAKA thread/global state.
 *	@param opaque Your private data.
 *	@param directory The directory you may be asked to nuke.
 *	@param flags Flags for future use.
 *
 * BK_RINGDIR_FLAG_NUKE_DIR_ON_DESTROY: If you passed this flag, the
 * caller is requesting that you destroy the directory. You are not
 * obliged to honor this, but it's a good idea.
 */
void
bk_ringdir_standard_destroy(bk_s B, void *opaque, const char *directory, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs = (struct bk_ringdir_standard *)opaque;

  if (!brs || (BK_FLAG_ISSET(flags, BK_RINGDIR_FLAG_NUKE_DIR_ON_DESTROY) && !directory))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (BK_FLAG_ISSET(flags, BK_RINGDIR_FLAG_NUKE_DIR_ON_DESTROY))
  {
    // If we get this flag the directory should be empty, it's just up to us to remove it.
    if (rmdir(directory) < 0)
      bk_error_printf(B, BK_ERR_ERR, "Could not remove directory %s: %s\n", directory, strerror(errno));
  }

  if (brs->brs_chkpnt_filename)
    free((char *)brs->brs_chkpnt_filename);

  free(brs);

  BK_VRETURN(B);
}



/**
 * Return the "size" of the current file. You may define size in any
 * manner you chose. The only requirement is that the value returned from
 * this function be greater than or equal to that of @a rotate_size (from
 * the init function) when you desire rotation to occur. The macro
 * BK_RINGDIR_GET_SIZE_MAX is provided as a convenience value for the
 * largest legal return value from this function.
 *
 *	@param B BAKA thread/global state.
 *	@param opaque Your private data.
 *	@param filename The filename you are operating on
 *	@param filenum The filenumber/index you are operating on
 *	@param flags Flags for future use.
 *	@return <i>BK_RINGDIR_GET_SIZE_ERROR</i> on failure.<br>
 *	@return <i>non-negative</i> on success.
 */
off_t
bk_ringdir_standard_get_size(bk_s B, void *opaque, const char *filename, u_int filenum, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs = (struct bk_ringdir_standard *)opaque;
  struct stat st;

  if (!brs || !filename)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    goto error;
  }

  if (brs->brs_fd >= 0)
  {
    if (fstat(brs->brs_fd, &st) < 0)
    {
      if (errno == EBADF)
      {
	goto filename_stat;
      }
      else
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not stat %s: %s\n", filename, strerror(errno));
	goto error;
      }
    }

    BK_RETURN(B,st.st_size);
  }
  else if (brs->brs_filehandle && brs->brs_ftell)
  {
    BK_RETURN(B, (*brs->brs_ftell)(brs->brs_filehandle));
  }
  else
  {
  filename_stat:
    if (stat(filename, &st) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not stat %s: %s\n", filename, strerror(errno));
      goto error;
    }

    BK_RETURN(B,st.st_size);
  }


 error:
  BK_RETURN(B,BK_RINGDIR_GET_SIZE_ERROR);
}



/**
 * Open a new file by whatever method is appropriate for your implementation.
 *
 *	@param B BAKA thread/global state.
 *	@param opaque Your private data.
 *	@param filename The filename you are operating on
 *	@param filenum The filenumber/index you are operating on
 *	@param flags Flags for future use.
 *
 * BK_RINGDIR_FLAG_OPEN_APPEND: If you are passed this flag you should
 * open the file in append mode (ie we are starting up from a
 * checkpointed location).
 *
 *	@param source The ring direction action which triggerd this callback
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_ringdir_standard_open(bk_s B, void *opaque, const char *filename, u_int filenum, enum bk_ringdir_callback_source source, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs = (struct bk_ringdir_standard *)opaque;
  int open_flags = O_WRONLY | O_LARGEFILE;

  if (!brs || !filename)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISSET(flags, BK_RINGDIR_FLAG_OPEN_APPEND))
    open_flags |= O_CREAT | O_APPEND;
  else
    open_flags |= O_CREAT | O_TRUNC;


  if ((brs->brs_fd = open(filename, open_flags, 0666)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open %s for writing: %s\n", filename, strerror(errno));
    goto error;
  }

  if (!(brs->brs_cur_filename = strdup(filename)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not copy filename: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B,0);

 error:
  bk_ringdir_standard_close(B, brs, filename, filenum, source, 0);
  BK_RETURN(B,-1);
}



/**
 * Close a file in the manner consistent with your ringdir implementation.
 *
 *	@param B BAKA thread/global state.
 *	@param opaque Your private data.
 *	@param filename The filename you are operating on
 *	@param filenum The filenumber/index you are operating on
 *	@param source The ring direction action which triggerd this callback
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_ringdir_standard_close(bk_s B, void *opaque, const char *filename, u_int filenum, enum bk_ringdir_callback_source source, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs = (struct bk_ringdir_standard *)opaque;

  if (!brs || !filename)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (brs->brs_cur_filename && brs->brs_fd != -1)
  {
    if (!BK_STREQ(brs->brs_cur_filename, filename))
      bk_error_printf(B, BK_ERR_ERR, "Requsted file != cached file name. Closing cached descriptor\n");
  }

  if (brs->brs_fd != -1)
    close(brs->brs_fd);
  brs->brs_fd = -1;

  if (brs->brs_cur_filename)
    free((char *)brs->brs_cur_filename);
  brs->brs_cur_filename = NULL;

  BK_RETURN(B,0);
}



/**
 * Remove a file in the manner consistent with your ringdir implementation.
 *
 *	@param B BAKA thread/global state.
 *	@param opaque Your private data.
 *	@param filename The filename you are operating on
 *	@param filenum The filenumber/index you are operating on
 *	@param source The ring direction action which triggerd this callback
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_ringdir_standard_unlink(bk_s B, void *opaque, const char *filename, u_int filenum, enum bk_ringdir_callback_source source, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs = (struct bk_ringdir_standard *)opaque;

  if (!brs || !filename)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if ((unlink(filename) < 0) && errno != ENOENT )
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not unlink %s: %s\n", filename, strerror(errno));
    goto error;
  }

  BK_RETURN(B,0);

 error:
  BK_RETURN(B,-1);
}



/**
 * Do checkpointing managment. You will be called with one of the actions:
 *	BkRingDirChkpntActionChkpnt:	Save value to checkpoint state "file"
 *	BkRingDirChkpntActionRecover:	Recover checkpoint value from state "file"
 *	BkRingDirChkpntActionDelete:	Delete the checkpoint state "file"
 *
 * The ring directory name and the file name pattern are supplied to you
 * for your convience as your checkpoint key will almost certainly need
 * to be some function of these to values.
 *
 * The value of @a valuep depends on the action. If saving then *valuep
 * is the copy in value to save. If recoving then *valuep is the copy out
 * value you need to update. If deleting, then @a valuep is NULL.
 *
 *
 *	@param B BAKA thread/global state.
 *	@param opaque Your private data.
 *	@param action The action to take (described above).
 *	@param directory The directory you may be asked to nuke.
 *	@param pattern The file pattern.
 *	@param valuep The file value.
 *	@param source The ring direction action which triggered this callback
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_ringdir_standard_chkpnt(bk_s B, void *opaque, enum bk_ringdir_chkpnt_actions action, const char *directory, const char *pattern, u_int32_t *valuep, enum bk_ringdir_callback_source source, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs = (struct bk_ringdir_standard *)opaque;
  int fd = -1;
  int len;
  u_int32_t value;
  int ret;

  if (!brs || !directory || !pattern || (action != BkRingDirChkpntActionDelete && !valuep))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  switch(action)
  {
  case BkRingDirChkpntActionChkpnt:
    value = htonl(*valuep);

    if ((fd = open(brs->brs_chkpnt_filename, O_WRONLY|O_CREAT, 0666)) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not open %s for writing: %s\n", brs->brs_chkpnt_filename, strerror(errno));
      goto error;
    }

    len = sizeof(value);
    do
    {
      if ((ret = write(fd, ((char *)&value)+sizeof(value)-len, len)) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Failed to write out check value: %s\n", strerror(errno));
	goto error;
      }
      len -= ret;
    } while(len);

    close(fd);
    fd = -1;

    break;

  case  BkRingDirChkpntActionRecover:
    // Check for file existence.
    if (access(brs->brs_chkpnt_filename, F_OK) < 0)
    {
      if (errno == ENOENT) // Not found is OK, but returns 1.
	BK_RETURN(B,1);

      bk_error_printf(B, BK_ERR_ERR, "Could not access %s: %s\n", brs->brs_chkpnt_filename, strerror(errno));
      goto error;
    }

    if ((fd = open(brs->brs_chkpnt_filename, O_RDONLY)) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not open %s for reading: %s\n", brs->brs_chkpnt_filename, strerror(errno));
      goto error;
    }

    len = sizeof(*valuep);
    do
    {
      if ((ret = read(fd, valuep+sizeof(*valuep)-len, len)) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Failed to read out check value: %s\n", strerror(errno));
	goto error;
      }

      len -= ret;
    } while (len);

    close(fd);
    fd = -1;

    *valuep = ntohl(*valuep);
    break;

  case BkRingDirChkpntActionDelete:
    if (unlink(brs->brs_chkpnt_filename) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not unlink: %s\n", strerror(errno));
      goto error;
    }
    break;
  }

  BK_RETURN(B,0);

 error:
  if (fd != -1)
    close(fd);

  BK_RETURN(B, -1);
}



/**
 *  Check for the existence of a "file" by whatever means are appropriate for your implementation.
 *
 *	@param B BAKA thread/global state.
 *	@param filename The filename to open
 *	@param filenum Number/index of the file to open
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> if filename does not exist.
 *	@return <i>1</i> if filename exists.
 */
int
bk_ringdir_standard_exists(bk_s B, void *opaque, const char *filename, int filenum, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs = (struct bk_ringdir_standard *)opaque;
  int file_found = 1;

  if (!brs || !filename)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (access(filename, F_OK) < 0)
  {
    if (errno == ENOENT)
    {
      file_found = 0;
    }
    else
    {
      bk_error_printf(B, BK_ERR_ERR, "Check for existence of %s failed: %s\n", filename, strerror(errno));
      goto error;
    }
  }

  BK_RETURN(B, file_found);

 error:
  BK_RETURN(B, -1);
}


/**
 * Update the private data of the standard struct
 *
 *	@param B BAKA thread/global state.
 *	@param brdh Ring directory handle.
 *	@param opaque The pointer to cache in the standard structure.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_ringdir_standard_update_private_data(bk_s B, bk_ringdir_t brdh, void *opaque, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs;

  if (!brdh || !opaque || !(brs = bk_ringdir_get_private_data(B, brdh, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  brs->brs_opaque = opaque;

  BK_RETURN(B, 0);
}



/**
 * Retrieve the private of the standadrd struct
 *
 *	@param B BAKA thread/global state.
 *	@param brdh Ring directory handle.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>private data</i> on success.
 */
void *
bk_ringdir_standard_get_private_data(bk_s B, bk_ringdir_t brdh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs;

  if (!brdh || !(brs = bk_ringdir_get_private_data(B, brdh, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B,brs->brs_opaque);
}



/**
 * Retrieve the fd of the standard sttruct
 *
 *	@param B BAKA thread/global state.
 *	@param brdh Ring directory handle.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>private data</i> on success.
 */
int
bk_ringdir_standard_get_fd(bk_s B, bk_ringdir_t brdh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs;

  if (!brdh || !(brs = bk_ringdir_get_private_data(B, brdh, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  BK_RETURN(B,brs->brs_fd);
}



/**
 * Insert a file descriptor
 *
 *	@param B BAKA thread/global state.
 *	@param brdh Ring directory handle.
 *	@param fd File descriptor to add.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_ringdir_standard_set_fd(bk_s B, bk_ringdir_t brdh, int fd, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs;

  if (!brdh || !(brs = bk_ringdir_get_private_data(B, brdh, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  brs->brs_fd = fd;

  BK_RETURN(B, 0);
}



/**
 * Retrieve the private file handle of the standard struct
 *
 *	@param B BAKA thread/global state.
 *	@param brdh Ring directory handle.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>private data</i> on success.
 */
void *
bk_ringdir_standard_get_fh(bk_s B, bk_ringdir_t brdh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs;

  if (!brdh || !(brs = bk_ringdir_get_private_data(B, brdh, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B,brs->brs_filehandle);
}



/**
 * Insert a file handle and tell function
 *
 *	@param B BAKA thread/global state.
 *	@param brdh Ring directory handle.
 *	@param fh File handle to add.
 *	@param ftell Tell function to add.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_ringdir_standard_set_fh(bk_s B, bk_ringdir_t brdh, void *fh, off_t (*ssftell)(void *), bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs;

  if (!brdh || !(brs = bk_ringdir_get_private_data(B, brdh, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  brs->brs_filehandle = fh;
  brs->brs_ftell = ssftell;

  BK_RETURN(B, 0);
}



/**
 * Update the private data of the standard struct by the standard struct
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_ringdir_standard_update_private_data_by_standard(bk_s B, void *brsh, void *opaque, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs = (struct bk_ringdir_standard *)brsh;

  if (!brsh)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  brs->brs_opaque = opaque;

  BK_RETURN(B,0);
}



/**
 * Retrieve the private of the standadrd struct by the standard struct
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
void *
bk_ringdir_standard_get_private_data_by_standard(bk_s B, void *brsh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs = (struct bk_ringdir_standard *)brsh;

  if (!brsh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B,brs->brs_opaque);

}



/**
 * Check if a "file" rotation occured on last call to bk_ringdir_rotate().
 *
 *	@param B BAKA thread/global state.
 *	@param brdh Ring directory handle.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success with no rotate.
 *	@return <i>1</i> on success with rotate.
 */
int
bk_ringdir_did_rotate_occur(bk_s B, bk_ringdir_t brdh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ring_directory *brd = (struct bk_ring_directory *)brdh;

  if (!brd)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  BK_RETURN(B, BK_FLAG_ISSET(brd->brd_flags, BK_RINGDIR_FLAG_ROTATION_OCCURED));
}




/**
 * Get the filename of the oldest file. This function allocates memory for
 * the string which must be destroyed with free(3).
 *
 *	@param B BAKA thread/global state.
 *	@param brdh Ring directory handle.
 *	@param filenum Optional pointer to filenumber of oldest
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return allocated <i>filename</i> on success.
 */
char *
bk_ringdir_filename_oldest(bk_s B, bk_ringdir_t brdh, u_int *filenum, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ring_directory *brd = (struct bk_ring_directory *)brdh;
  u_int32_t oldest_num;
  char *filename = NULL;

  if (!brd)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  oldest_num = NEXT_FILE_NUM(brd, brd->brd_cur_file_num);

  if (filenum)
    *filenum = oldest_num;

  if (!(filename = bk_ringdir_create_file_name(B, brd, oldest_num, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create name of oldest file\n");
    goto error;
  }

  BK_RETURN(B, filename);

 error:
  if (filename)
    free(filename);

  BK_RETURN(B,NULL);
}



/**
 * Get the next file name in the rotation series. This function allocates
 * memory which must be destroyed with free(3).
 *
 *	@param B BAKA thread/global state.
 *	@param brdh Ring directory handle.
 *	@param filename The filename for which you want the successor
 *	@param filenum Optional pointer to filenumber of successor
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return allocated <i>filename</i> on success.
 */
char *
bk_ringdir_filename_successor(bk_s B, bk_ringdir_t brdh, const char *filename, u_int *filenum, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ring_directory *brd = (struct bk_ring_directory *)brdh;
  char *next_filename = NULL;
  u_int32_t file_num, next_num;

  if (!brd)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if ((file_num = get_filenumber_from_path(B, brd, filename, 0)) == (u_int32_t)-1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not extract file number from %s\n", filename);
    goto error;
  }

  next_num = NEXT_FILE_NUM(brd, file_num);

  if (filenum)
    *filenum = next_num;

  if (!(next_filename = bk_ringdir_create_file_name(B, brd, next_num, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create name of oldest file\n");
    goto error;
  }

  // Do this *last*.
  if (BK_FLAG_ISSET(flags, BK_RINGDIR_FILENAME_ITERATE_FLAG_FREE))
    free((char *)filename);

  BK_RETURN(B, next_filename);

 error:
  if (next_filename)
    free(next_filename);

  BK_RETURN(B,NULL);
}




/**
 * Obtain the current file name (most recent). This function allocates
 * memory which must be freed with free(3).
 *
 *	@param B BAKA thread/global state.
 *	@param brdh Ring directory handle.
 *	@param filenum Optional pointer to filenumber of current
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
char *
bk_ringdir_filename_current(bk_s B, bk_ringdir_t brdh, u_int *filenum, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ring_directory *brd = (struct bk_ring_directory *)brdh;

  if (!brd)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (filenum)
    *filenum = brd->brd_cur_file_num;

  BK_RETURN(B,strdup(brd->brd_cur_filename));
}




/**
 * Obtain the current file pattern, as constant memory
 * which will not be valid after the ringdir is destroyed
 *
 *	@param B BAKA thread/global state.
 *	@param brdh Ring directory handle.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>pattern</i> on success.
 */
const char *
bk_ringdir_getpattern(bk_s B, bk_ringdir_t brdh)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ring_directory *brd = (struct bk_ring_directory *)brdh;

  if (!brd)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B,brd->brd_path);
}



/**
 * Get the previous file name in the rotation series. This function allocates
 * memory which must be destroyed with free(3).
 *
 *	@param B BAKA thread/global state.
 *	@param brdh Ring directory handle.
 *	@param filename The filename for which you want the successor
 *	@param filenum Optional pointer to filenumber of predecessor
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return allocated <i>filename</i> on success.
 */
char *
bk_ringdir_filename_predecessor(bk_s B, bk_ringdir_t brdh, const char *filename, u_int *filenum, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ring_directory *brd = (struct bk_ring_directory *)brdh;
  char *previous_filename = NULL;
  u_int32_t file_num, previous_num;

  if (!brd)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if ((file_num = get_filenumber_from_path(B, brd, filename, 0)) == (u_int32_t)-1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not extract file number from %s with pattern %s\n", filename, brd->brd_path);
    goto error;
  }

  previous_num = PREVIOUS_FILE_NUM(brd, file_num);

  if (filenum)
    *filenum = previous_num;

  if (!(previous_filename = bk_ringdir_create_file_name(B, brd, previous_num, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create name of oldest file\n");
    goto error;
  }

  // Do this *last*.
  if (BK_FLAG_ISSET(flags, BK_RINGDIR_FILENAME_ITERATE_FLAG_FREE))
    free((char *)filename);

  BK_RETURN(B, previous_filename);

 error:
  if (previous_filename)
    free(previous_filename);

  BK_RETURN(B,NULL);
}



/**
 * Split up a ring directory pattern path into its constant base directory and pattern components
 *
 *	@param B BAKA thread/global state.
 *	@param path path to split
 *	@param dir_namep copy-out directory name (caller must free(3))
 *	@param patternp copy-out pattern (caller must free(3))
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_ringdir_split_pattern(bk_s B, const char *path, char **dir_namep, char **patternp, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *dir_name = NULL;
  char *pattern = NULL;
  char *tmp_path = NULL;
  char save;

  if (!path || !dir_namep || !patternp)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  *dir_namep = NULL;
  *patternp = NULL;

  if (BK_STREQ(path, "/"))
  {
    bk_error_printf(B, BK_ERR_ERR, "The root directory may not be a ring directory\n");
    goto error;
  }

  if (*path != '/')
  {
    bk_error_printf(B, BK_ERR_ERR, "The path must be absolute (begin with /): %s\n", path);
    goto error;
  }

  if (!(tmp_path = strdup(path)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not make private copy of path for directory name extraction: %s\n", strerror(errno));
    goto error;
  }

  // Look for first ocurrence of % not followed by %
  while ((pattern = strchr(tmp_path, '%')) && pattern[1] == '%') ;

  if (!pattern)
  {
    bk_error_printf(B, BK_ERR_ERR, "Not a valid bk_ringdir path %s: missing dymamic %% components\n",path);
    goto error;
  }

  // Look for the directory seperator between the path component with the % in it and everything before it
  while (--pattern && pattern > tmp_path && pattern[0] != '/') ;

  // There must be something before it, and no, the root directory does not count
  if (pattern <= tmp_path)
  {
    bk_error_printf(B, BK_ERR_ERR, "No directory names in the path name before the component with the first `%%' in it: %s\n",path);
    goto error;
  }

  // Null terminate the directory name
  save = *++pattern;
  *pattern = 0;

  if (!(dir_name = strdup(tmp_path)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not copy directory name: %s\n", strerror(errno));
    goto error;
  }

  *pattern = save;
  if (!(pattern = strdup(pattern)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not copy pattern: %s\n", strerror(errno));
    goto error;
  }

  free(tmp_path);
  tmp_path = NULL;

  *dir_namep = dir_name;
  *patternp = pattern;

  BK_RETURN(B,0);

 error:
  if (tmp_path)
    free(tmp_path);

  if (dir_name)
    free(dir_name);

  BK_RETURN(B,-1);
}



/**
 * Get oldest and max file number for this ring buffer.
 *
 * The caller should note that the status may change
 * (the writer may advance) between calling this
 * function and actually acting on the results.
 *
 * @param B BAKA Thread/global state
 * @param brd ring directory info
 * @param current copy-out current file
 * @param oldest copy-out oldest file
 * @param max copy-out max number of files
 * @param flags BK_RINGDIR_FLAG_EWARN - errors as warnings
 * @return <i>0</i> on success
 * @return <i>-1</i> on failure
 */
int
bk_ringdir_get_status(bk_s B, bk_ringdir_t brdh, u_int32_t *currentp, u_int32_t *oldestp, u_int32_t *num_filesp, u_int32_t *maxp, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ring_directory *brd = (struct bk_ring_directory *)brdh;
  char *chkpnt_filename = NULL;
  char *oldest_filename = NULL;
  int len;
  int ret;
  int fd = -1;
  int errlevel;
  u_int32_t current = 0;
  u_int32_t oldest = 0;
  u_int32_t max = 0;
  u_int32_t num_files = 0;

  if (!brd) // num_files is optional
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  errlevel= (BK_FLAG_ISSET(flags, BK_RINGDIR_FLAG_EWARN)?BK_ERR_WARN:BK_ERR_ERR);

  if (read_settings(B, brd, &num_files, &max, flags) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not read ring settings\n");
    goto error;
  }

  if (BK_FLAG_ISSET(flags, BK_RINGDIR_FLAG_INTERNAL))
    goto done;

  if (!(chkpnt_filename = bk_string_alloc_sprintf(B, 0, 0, "%s", brd->brd_pathext)))
  {
    bk_error_printf(B, errlevel, "Could not create cache file name\n");
    goto error;
  }

  // Read the ringdir cache file
  if ((fd = open(chkpnt_filename, O_RDONLY)) < 0)
  {
    if (errno == ENOENT)
    {
      current = 0;
      oldest = 0;
    }
    else
    {
      bk_error_printf(B, errlevel, "Could not open %s for reading: %s\n", chkpnt_filename, strerror(errno));
      goto error;
    }
  }
  else
  {
    len = sizeof(current);
    do
    {
      if ((ret = read(fd, &current+sizeof(current)-len, len)) < 0)
      {
	bk_error_printf(B, errlevel, "Failed to read out check value: %s\n", strerror(errno));
	goto error;
      }

      len -= ret;
    } while (len);

    close(fd);
    fd = -1;

    current = ntohl(current);

    /*
     * If we haven't set the max yet, then let max==1 to prevent
     * exceptions. The hope is that the value of max will actually be
     * pretty irrelevent at the point and remain so until it actually set.
     */
    if (max == 0)
    {
      max = 1;
    }

    oldest = (current + 1) % max;

    // Special check in case this ring buffer is new
    if (!(oldest_filename = bk_ringdir_create_file_name(B, brd, oldest, 0)))
    {
      bk_error_printf(B, errlevel, "Could not build filename for oldest file.\n");
      goto error;
    }

    while (access(oldest_filename, F_OK) < 0 && oldest != current)
    {
      if (errno == ENOENT)
      {
	// Next file in the ring doesn't yet exist, start search from file 0
	if (oldest > current)
	  oldest = 0;
	else
	  oldest = (oldest + 1) % max;

	free(oldest_filename);
	if (!(oldest_filename = bk_ringdir_create_file_name(B, brd, oldest, 0)))
	{
	  bk_error_printf(B, errlevel, "Could not build filename for oldest file.\n");
	  goto error;
	}
      }
      else
      {
	bk_error_printf(B, errlevel, "Could not check existence of oldest file\n");
	goto error;
      }
    }

    free(oldest_filename);
    oldest_filename = NULL;
  }

  free(chkpnt_filename);
  chkpnt_filename = NULL;

 done:
  if (currentp)
    *currentp = current;

  if (oldestp)
    *oldestp = oldest;

  if (maxp)
    *maxp = max;

  if (num_filesp)
    *num_filesp = num_files;

  BK_RETURN(B, 0);

 error:
  if (chkpnt_filename)
    free(chkpnt_filename);

  if (oldest_filename)
    free(oldest_filename);

  if (fd != -1)
    close(fd);

  BK_RETURN(B, -1);
}


#define WRITE_DATA(B, fd, value)								\
{												\
  int ret;											\
  int len = sizeof(value);									\
												\
  do												\
  {												\
    if ((ret = write((fd), ((char *)&(value))+sizeof(value)-len, len)) < 0)			\
    {												\
      bk_error_printf((B), BK_ERR_ERR, "Cache data write failed: %s\n", strerror(errno));	\
      goto error;										\
    }												\
    len -= ret;											\
  } while(len);											\
}



/**
 * Write information about the setup of the ring buffer to disk.
 *
 * @param B BAKA Thread/global state
 * @param brd ring directory info
 * @param flags Reserved.
 * @return <i>0</i> on success
 * @return <i>-1</i> on failure
 */
static int
write_settings(bk_s B, struct bk_ring_directory *brd, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int fd = -1;
  char *max_filename = NULL;
  u_int32_t value;

  if (!brd)
  {
    bk_error_printf(B, BK_ERR_ERR, "Internal error: illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(max_filename = bk_string_alloc_sprintf(B, 0, 0, "%s.max", brd->brd_pathext)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create max file name\n");
    goto error;
  }

  if ((fd = open(max_filename, O_WRONLY|O_CREAT, 0666)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open %s for writing: %s\n", max_filename, strerror(errno));
    goto error;
  }

  free(max_filename);
  max_filename = NULL;

  // Record current size of ring buffer for readers
  value = htonl(brd->brd_num_files);
  WRITE_DATA(B, fd, value);

  // Record max size of ring buffer for readers
  value = htonl(brd->brd_max_num_files);
  WRITE_DATA(B, fd, value);

  close(fd);
  fd = -1;

  BK_RETURN(B, 0);

 error:
  if (fd != -1)
    close(fd);

  if (max_filename)
    free(max_filename);

  BK_RETURN(B, -1);
}



#define READ_DATA(B, fd, value)									\
{												\
  int ret;											\
  int len = sizeof(value);									\
												\
  do												\
  {												\
    if ((ret = read((fd), ((char *)&(value))+sizeof(value)-len, len)) < 0)			\
    {												\
      bk_error_printf((B), BK_ERR_ERR, "Cache data read failed: %s\n", strerror(errno));	\
      goto error;										\
    }												\
												\
    if (ret == 0)										\
    {												\
      bk_error_printf(B, BK_ERR_ERR, "Unexpected EOF during cache file read\n");		\
      goto error;										\
    }												\
												\
    len -= ret;											\
  } while(len);											\
}



/**
 * Write information about the setup of the ring buffer to disk.
 *
 * @param B BAKA Thread/global state
 * @param brd ring directory info
 * @param flags Reserved.
 * @return <i>0</i> on success
 * @return <i>-1</i> on failure
 */
static int
read_settings(bk_s B, struct bk_ring_directory *brd, u_int32_t *num_filesp, u_int32_t *max_num_filesp, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int fd = -1;
  char *max_filename = NULL;
  u_int32_t value;

  if (!brd)
  {
    bk_error_printf(B, BK_ERR_ERR, "Internal error: illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (num_filesp)
    *num_filesp = 0;

  if (max_num_filesp)
    *max_num_filesp = 0;

  if (!(max_filename = bk_string_alloc_sprintf(B, 0, 0, "%s.max", brd->brd_pathext)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create max file name\n");
    goto error;
  }

  if ((fd = open(max_filename, O_RDONLY, 0)) < 0)
  {
    if ((errno == ENOENT) && BK_FLAG_ISSET(flags, BK_RINGDIR_FLAG_INTERNAL))
      goto done;
    bk_error_printf(B, BK_ERR_ERR, "Could not open %s for reading: %s\n", max_filename, strerror(errno));
    goto error;
  }

  free(max_filename);
  max_filename = NULL;

  // Read current size of ring buffer for readers
  READ_DATA(B, fd, value);
  value = htonl(value);

  if (num_filesp)
    *num_filesp = value;

  // Read max size of ring buffer for readers
  READ_DATA(B, fd, value);
  value  = htonl(value);

  if (max_num_filesp)
    *max_num_filesp = value;

  close(fd);
  fd = -1;

 done:
  BK_RETURN(B, 0);

 error:
  if (fd != -1)
    close(fd);

  if (max_filename)
    free(max_filename);

  BK_RETURN(B, -1);
}



/**
 * Determine test file number from path
 *
 *	@param B BAKA thread/global state.
 *	@param brd Ring Directory
 *	@param filename Name of file to convert
 *	@param flags Fun for the future
 *	@return <i>-1</i> on failure.<br />
 *	@return <i>number</i> on success
 */
static u_int32_t
get_filenumber_from_path(bk_s B, struct bk_ring_directory *brd, const char *filename, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int state = 0;
  u_int32_t levelnums[MAXIMUM_LEVELS];

  if (!filename || !brd)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  // I don't think you are allowed to portably construct a stdargs :-(
  switch(brd->brd_split_levels)
  {
  case 1: state = sscanf(filename, brd->brd_path, &levelnums[0]); break;
  case 2: state = sscanf(filename, brd->brd_path, &levelnums[1], &levelnums[0]); break;
  case 3: state = sscanf(filename, brd->brd_path, &levelnums[2], &levelnums[1], &levelnums[0]); break;
  case 4: state = sscanf(filename, brd->brd_path, &levelnums[3], &levelnums[2], &levelnums[1], &levelnums[0]); break;
  case 5: state = sscanf(filename, brd->brd_path, &levelnums[4], &levelnums[3], &levelnums[2], &levelnums[1], &levelnums[0]); break;
  case 6: state = sscanf(filename, brd->brd_path, &levelnums[5], &levelnums[4], &levelnums[3], &levelnums[2], &levelnums[1], &levelnums[0]); break;
  case 7: state = sscanf(filename, brd->brd_path, &levelnums[6], &levelnums[5], &levelnums[4], &levelnums[3], &levelnums[2], &levelnums[1], &levelnums[0]); break;
  case 8: state = sscanf(filename, brd->brd_path, &levelnums[7], &levelnums[6], &levelnums[5], &levelnums[4], &levelnums[3], &levelnums[2], &levelnums[1], &levelnums[0]); break;
  case 9: state = sscanf(filename, brd->brd_path, &levelnums[8], &levelnums[7], &levelnums[6], &levelnums[5], &levelnums[4], &levelnums[3], &levelnums[2], &levelnums[1], &levelnums[0]); break;
  case 10: state = sscanf(filename, brd->brd_path, &levelnums[9], &levelnums[8], &levelnums[7], &levelnums[6], &levelnums[5], &levelnums[4], &levelnums[3], &levelnums[2], &levelnums[1], &levelnums[0]); break;
  default:
    bk_error_printf(B, BK_ERR_ERR, "Invalid number of split levels: %d\n", brd->brd_split_levels);
    BK_RETURN(B, -1);
  }

  if (state != brd->brd_split_levels)
  {
    bk_error_printf(B, BK_ERR_ERR, "Failed to parse path %s with pattern %s, got %d/%d items\n", filename, brd->brd_path, state, brd->brd_split_levels);
    BK_RETURN(B, -1);
  }

  BK_RETURN(B, levelnums[0]);
}
