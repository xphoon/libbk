#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_pollio.c,v 1.3 2001/12/19 20:21:02 jtt Exp $";
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
 *
 * Association of file descriptor/handle to callback.  Queue data for
 * output, translate input stream into messages.
 */

#include <libbk.h>
#include "libbk_internal.h"



/**
 * The ioh data (buffers and status) held pending user read.
 */
struct polling_io_data
{
  bk_flags		pid_flags;		///< Everyone needs flags.
  bk_vptr *		pid_data;		///< The actuall data.
  bk_ioh_status_e	pid_status;		///< The status which was returned.
};


/**
 * @name Defines: Polling io data lists.
 * List of ioh buffers and status's from the ioh level.
 *
 * <WARNING> 
 * IF YOU CHANGE THIS FROM A DLL, YOU MUST SEARCH FOR THE STRING clc_add (2
 * instances) AND CHANGE THOS REFERENCES WHICH DO NOT MATCH THE MACRO
 * FORMAT.
 * </WARNING> 
 */
// @{
#define pidlist_create(o,k,f)		dll_create((o),(k),(f))
#define pidlist_destroy(h)		dll_destroy(h)
#define pidlist_insert(h,o)		dll_insert((h),(o))
#define pidlist_insert_uniq(h,n,o)	dll_insert_uniq((h),(n),(o))
#define pidlist_append(h,o)		dll_append((h),(o))
#define pidlist_append_uniq(h,n,o)	dll_append_uniq((h),(n),(o))
#define pidlist_search(h,k)		dll_search((h),(k))
#define pidlist_delete(h,o)		dll_delete((h),(o))
#define pidlist_minimum(h)		dll_minimum(h)
#define pidlist_maximum(h)		dll_maximum(h)
#define pidlist_successor(h,o)		dll_successor((h),(o))
#define pidlist_predecessor(h,o)	dll_predecessor((h),(o))
#define pidlist_iterate(h,d)		dll_iterate((h),(d))
#define pidlist_nextobj(h,i)		dll_nextobj(h,i)
#define pidlist_iterate_done(h,i)	dll_iterate_done(h,i)
#define pidlist_error_reason(h,i)	dll_error_reason((h),(i))
// @}



static struct polling_io_data *pid_create(bk_s B);
static void pid_destroy(bk_s B, struct polling_io_data *pid);
static void polling_io_ioh_handler(bk_s B, bk_vptr data[], void *args, struct bk_ioh *ioh, bk_ioh_status_e status);
static int polling_io_flush(bk_s B, struct bk_polling_io *bpi, bk_flags flags );



/**
 * Create the context for on demand operation. <em>NB</em> We take control
 * of the @a ioh, just like in bk_relay().
 *
 *	@param B BAKA thread/global state.
 *	@param ioh The BAKA ioh structure to use.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a bk_polling_io on success.
 */
struct bk_polling_io *
bk_polling_io_create(bk_s B, struct bk_ioh *ioh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_polling_io *bpi = NULL;

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(BK_MALLOC(bpi)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bpi: %s\n", strerror(errno));
    goto error;
  }

  if (!(bpi->bpi_data = pidlist_create(NULL, NULL, DICT_UNORDERED)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create data dll\n");
  }

  bpi->bpi_ioh = ioh;
  bpi->bpi_flags = 0;
  bpi->bpi_size = 0;
  bpi->bpi_throttle_cnt = 0;
  bpi->bpi_tell = 0;
  
  if (bk_ioh_update(B, ioh, NULL, NULL, NULL, polling_io_ioh_handler, bpi, 0, 0, 0, 0, BK_IOH_UPDATE_HANDLER | BK_IOH_UPDATE_OPAQUE) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not update ioh with blocking handler.\n");
    goto error;
  }
 
  BK_RETURN(B,bpi);
 error:
  if (bpi) bk_polling_io_destroy(B, bpi);
  BK_RETURN(B,NULL);
}



/**
 * Close up on demand I/O.
 *
 * <WARNING> 
 * For various and sundry reasons it's beoome clear that ioh should be
 * closed here too. Therefore if you are using polling io then you should
 * surrender contrl of the ioh (or more to the point control it via the
 * polling routines). In this respect poilling io is like b_relay.c.
 * </WARNING>
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The on demand state to close.
 *	@param flags Flags for the future.
 */
void
bk_polling_io_close(bk_s B, struct bk_polling_io *bpi, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

  BK_FLAG_SET(bpi->bpi_flags, BPI_FLAG_CLOSING);
  if (BK_FLAG_ISSET(flags, BK_POLLING_CLOSE_FLAG_LINGER))
  {
    BK_FLAG_SET(bpi->bpi_flags, BPI_FLAG_DONT_DESTROY);
  }

  bk_ioh_close(B, bpi->bpi_ioh, 0);

  BK_VRETURN(B);
}



/**
 * Destroy the blocking I/O context.
 *
 *	@param B BAKA thread/global state.
 *	@param bpi. The context info to destroy.
 */
void
bk_polling_io_destroy(bk_s B, struct bk_polling_io *bpi)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct polling_io_data *pid;

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  while((pid=pidlist_minimum(bpi->bpi_data)))
  {
    if (pidlist_delete(bpi->bpi_data, pid) != DICT_OK)
      break;
    pid_destroy(B, pid);
  }
  pidlist_destroy(bpi->bpi_data);

  free(bpi);
  BK_VRETURN(B);
}




/**
 * The on demand I/O subsytem's ioh handler. Remember this handles both
 * reads and writes.
 *
 *	@param B BAKA thread/global state.
 *	@param opaque My local data.
 *	@param ioh ioh over which the data arrived.
 *	@param state What's going on in the world.
 */
static void
polling_io_ioh_handler(bk_s B, bk_vptr data[], void *args, struct bk_ioh *ioh, bk_ioh_status_e status)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_polling_io *bpi = args;
  struct polling_io_data *pid = NULL;
  u_int data_cnt = 0;
  u_int cnt;
  bk_vptr *ndata = NULL;
  char *p;
  int size = 0;
  int (*clc_add)(dict_h dll, dict_obj obj) = dll_append;

  if (!bpi || !ioh)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (!(pid = pid_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate pid: %s\n", strerror(errno));
    goto error;
  }

  switch (status)
  {
  case BkIohStatusReadComplete:
  case BkIohStatusIncompleteRead:
    // Copy the data. Sigh....
    for(data_cnt=0; data[data_cnt].ptr; data_cnt++)
    {
      size += data[data_cnt].len;
    }
    if (!(BK_CALLOC(ndata)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate copy vptr: %s\n", strerror(errno));
      goto error;
    }

    if (!(BK_CALLOC_LEN(ndata->ptr, size)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate copy data pointer: %s\n", strerror(errno));
      goto error;
    }
    
    p = ndata->ptr;
    for(data_cnt=0; data[data_cnt].ptr; data_cnt++)
    {
      memmove(p, data[data_cnt].ptr, data[data_cnt].len);
      p += data[data_cnt].len;
    }
    pid->pid_data = ndata;
    pid->pid_data->len = size;
    break;

  case BkIohStatusIohClosing:
    if (BK_FLAG_ISCLEAR(bpi->bpi_flags, BPI_FLAG_CLOSING))
    {
      bk_error_printf(B, BK_ERR_WARN, "Polling io underlying IOH was nuked before bpi closed. Not Good (probably not fatal).\n");
      // Forge on.
    }
    if (BK_FLAG_ISCLEAR(bpi->bpi_flags, BPI_FLAG_DONT_DESTROY))
    {
      bk_polling_io_destroy(B,bpi);
      BK_VRETURN(B);
    }
    
    // The ioh is now dead.
    BK_FLAG_SET(bpi->bpi_flags, BPI_FLAG_IOH_DEAD);

  case BkIohStatusIohReadError:
    BK_FLAG_SET(bpi->bpi_flags, BPI_FLAG_READ_DEAD);
    break;

  case BkIohStatusIohWriteError:
    BK_FLAG_SET(bpi->bpi_flags, BPI_FLAG_WRITE_DEAD);
    break;

  case BkIohStatusIohReadEOF:
    BK_FLAG_SET(bpi->bpi_flags, BPI_FLAG_SAW_EOF);
    break;

  case BkIohStatusWriteComplete:
  case BkIohStatusWriteAborted:
    free(data[0].ptr);
    free(data);
    data = NULL;
    break;


  case BkIohStatusIohSeekSuccess:
    polling_io_flush(B, bpi, 0);
    // Intentional fall through.
  case BkIohStatusIohSeekFailed:
    clc_add = dll_insert;			// Put seek messages on front.
    break;

    // No default so gcc can catch missing cases.
  }

  if ((*clc_add)(bpi->bpi_data, pid) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not append data to data list: %s\n", pidlist_error_reason(bpi->bpi_data, NULL));
    goto error;
  }

  pid->pid_status = status;

  if (pid->pid_data)
  {
    bpi->bpi_size += pid->pid_data->len;

    // <TODO> if file open for only writing mark the case so we don't do this (??)</TODO>
    /* Pause reading if buffer is full */
    if (ioh->ioh_readq.biq_queuemax && bpi->bpi_size >= ioh->ioh_readq.biq_queuemax)
    {
      bk_polling_io_throttle(B, bpi, 0);
    }
  }

  BK_VRETURN(B);

 error:
  if (ndata) bk_polling_io_data_destroy(B, ndata);
  if (pid) pid_destroy(B, pid);
  
  BK_VRETURN(B);
}



/**
 * Destroy a NULL terminated vptr list. The user <em>must</em> call this on
 * data read from the subsystem or the underlying memory will never be
 * freed.
 *
 *	@param B BAKA thread/global state.
 *	@param data The list of vptr's to nuke.
 */
void 
bk_polling_io_data_destroy(bk_s B, bk_vptr *data)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!data)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

  /*
   * Free up all the data.
   */
  free(data->ptr);
  free(data);

  BK_VRETURN(B);
}



/**
 * Do one polling read.
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The polling state to use.
 *	@param datap Data to pass up to the user (copyout).
 *	@param statusp Status to pass up to the user (copyout).
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success (with data).
 *	@return <i>>0</i> on no progress.
 */
int
bk_polling_io_read(bk_s B, struct bk_polling_io *bpi, bk_vptr **datap, bk_ioh_status_e *status, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bpi || !datap || !status)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  *datap = NULL;

  // If ioh is dead, go away..
  if (BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_READ_DEAD))
  {
    bk_error_printf(B, BK_ERR_ERR, "Reading from dead channel\n");
    BK_RETURN(B, -1);
  }

  BK_RETURN(B,bk_polling_io_do_poll(B, bpi, datap, status, 0));
}




/**
 * Do one polling poll. If we have some data, dequeue it and
 * return. Othewise call bk_run_once() one time.
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The polling state to use.
 *	@param datap Data to pass up to the user (copyout).
 *	@param statusp Status to pass up to the user (copyout).
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success (with data).
 *	@return <i>>0</i> on no progress.
 */
int
bk_polling_io_do_poll(bk_s B, struct bk_polling_io *bpi, bk_vptr **datap, bk_ioh_status_e *status, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct polling_io_data *pid;
  
  if (!bpi || !status)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (datap)
    *datap = NULL;

  // Seth sez: do bk_run_once regardless of presence of existing data to report.
  if (BK_FLAG_ISCLEAR(bpi->bpi_flags, BPI_FLAG_IOH_DEAD) &&
      bk_run_once(B, bpi->bpi_ioh->ioh_run, BK_RUN_ONCE_FLAG_DONT_BLOCK) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "polling bk_run_once failed severly\n");
    goto error;
  }

  pid = pidlist_minimum(bpi->bpi_data);

  // Now that we either have data or might
  if (!pid)
  {
    /*
     * If the IOH is in an eof state then return 0. However if there *is*
     * data then go ahead and return it. This will occur at *least* when
     * the pid contains the actual EOF message.
     */
    if (BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_SAW_EOF))
    {
      BK_RETURN(B,0);
    }
    BK_RETURN(B,1);
  }
  
  // You alway send status
  *status = pid->pid_status;

  if (pidlist_delete(bpi->bpi_data, pid) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not delete pid from data list: %s\n", pidlist_error_reason(bpi->bpi_data, NULL));
    goto error;
  }
	

  // If this is read, then subtract data from my queue.
  if (pid->pid_status == BkIohStatusReadComplete || 
      pid->pid_status == BkIohStatusIncompleteRead)
  {
    /*
     * We're removing data which is has been read by the user, so we
     * decrease the amount of data on the queue and increase our "tell"
     * position.
     */
    bpi->bpi_size -= pid->pid_data->len;
    bpi->bpi_tell += pid->pid_data->len;
  }


  // <TODO> if file open for only writing mark the case so we don't do this </TODO>
  /* Enable reading if buffer is not full */
  if (BK_FLAG_ISCLEAR(bpi->bpi_flags, BPI_FLAG_IOH_DEAD) &&
      bpi->bpi_ioh->ioh_readq.biq_queuemax &&
      bpi->bpi_size < bpi->bpi_ioh->ioh_readq.biq_queuemax)
  {
    bk_polling_io_unthrottle(B, bpi, 0);
  }

  if (datap)
  {
    *datap = pid->pid_data;
    pid->pid_data = NULL;			// Passed off. We're not responsible anymore.
  }
  
  pid_destroy(B, pid);

  BK_RETURN(B,0);

 error:
  BK_RETURN(B,-1);
}



/**
 * Write out a buffer polling. Basically <em>all</em> bk_ioh writes are
 * "polling", but we need this function to provide the glue between some
 * layers which might not have access to the @a bk_ioh structure and the
 * actuall ioh level.
 *
 *	@param B BAKA thread/global state.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_polling_io_write(bk_s B, struct bk_polling_io *bpi, bk_vptr *data, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bpi || !data)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }


  // If ioh is dead, go away..
  if (BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_WRITE_DEAD))
  {
    bk_error_printf(B, BK_ERR_ERR, "Reading from dead channel\n");
    BK_RETURN(B, -1);
  }


  BK_RETURN(B,bk_ioh_write(B, bpi->bpi_ioh, data, 0));
}



/**
 * Create an on_demand_data structure.
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a bk_on_demand_data on success.
 */
static struct polling_io_data *
pid_create(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct polling_io_data *pid;

  if (!(BK_CALLOC(pid)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate pid: %s\n", strerror(errno));
    BK_RETURN(B,NULL);
  }
  BK_RETURN(B,pid);
}



/**
 * Destroy an pid. 
 *	@param B BAKA thread/global state.
 *	@param pid The @a bk_on_demand_data to destroy.
 */
static void
pid_destroy(bk_s B, struct polling_io_data *pid)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  if (!pid)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }
  
  if (pid->pid_data) bk_polling_io_data_destroy(B, pid->pid_data);
  free(pid);
  BK_VRETURN(B);
}



/**
 * Throttle an polling I/O stream.
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The @a bk_polling_io to throttle.
 *	@param flags Flag for the future.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_polling_io_throttle(bk_s B, struct bk_polling_io *bpi, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_READ_DEAD))
  {
    bk_error_printf(B, BK_ERR_ERR, "Attempt to throttle a dead ioh\n");
    BK_RETURN(B,-1);
  }

  if (bpi->bpi_throttle_cnt == 0)
  {
    bk_ioh_readallowed(B, bpi->bpi_ioh, 0, 0);
  }

  bpi->bpi_throttle_cnt++;

  BK_RETURN(B,0);
}



/**
 * Unthrottle an polling I/O stream.
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The @a bk_polling_io to unthrottle.
 *	@param flags Flag for the future.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_polling_io_unthrottle(bk_s B, struct bk_polling_io *bpi, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_READ_DEAD))
  {
    bk_error_printf(B, BK_ERR_ERR, "Attempt to unthrottle a dead ioh\n");
    BK_RETURN(B,-1);
  }

  bpi->bpi_throttle_cnt--;

  if (bpi->bpi_throttle_cnt == 0)
  {
    bk_ioh_readallowed(B, bpi->bpi_ioh, 1, 0);
  }

  BK_RETURN(B,0);
}




/**
 * Flush all data associated with polling stuff. 
 *
 *	@param B BAKA thread/global state.
 *	@param bpi @a bk_polling_io to flush.
 *	@param flags Flags for the future.
 */
void
bk_polling_io_flush(bk_s B, struct bk_polling_io *bpi, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct polling_io_data *pid, *npid;

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }
  
  pid = pidlist_maximum(bpi->bpi_data);
  while(pid)
  {
    npid = pidlist_successor(bpi->bpi_data, pid);
    // Only nuke data vbufs.
    if (pid->pid_data->ptr || pid->pid_status == BkIohStatusIohReadEOF)
    {
      // If we're flusing off an EOF, then clear the fact that we have seen EOF.
      if (pid->pid_status == BkIohStatusIohReadEOF)
      {
	BK_FLAG_CLEAR(bpi->bpi_flags, BPI_FLAG_SAW_EOF);
      }
      pidlist_delete(bpi->bpi_data, pid);
      // Failure here will not kill the loop since we've already grabbed successesor
      pid_destroy(B, pid);
    }
    
    pid = npid;
  }
    
  BK_VRETURN(B);
}




/**
 * Flush out the poling cache. Very similiar to ioh flush. Flush all data buf and EOF messages.
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The @a bk_polling_io to use.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
polling_io_flush(bk_s B, struct bk_polling_io *bpi, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct polling_io_data *pid, *npid;

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  pid = pidlist_minimum(bpi->bpi_data);
  while(pid)
  {
    npid = pidlist_successor(bpi->bpi_data, pid);
    if (pid->pid_data->ptr || pid->pid_status == BkIohStatusIohReadEOF)
    {
      if (pidlist_delete(bpi->bpi_data, pid) != DICT_OK)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not delete pid from bpi data list: %s\n", pidlist_error_reason(bpi->bpi_data, NULL));
	break;
      }

      if (pid->pid_data)
      {
	/* 
	 * We're removing data which *hasnt'* been read by the user so we
	 * reduce *both* the amount of data on the queue and our tell
	 * position (in the io_poll routine we *increased* the later.
	 */
	bpi->bpi_size -= pid->pid_data->len;
	bpi->bpi_tell -= pid->pid_data->len;

	if (bpi->bpi_ioh->ioh_readq.biq_queuemax &&
	    bpi->bpi_size < bpi->bpi_ioh->ioh_readq.biq_queuemax)
	{
	  bk_polling_io_unthrottle(B, bpi, 0);
	}
      }
      pid_destroy(B, pid);
    }
    pid = npid;
  }
  BK_RETURN(B,0);
}