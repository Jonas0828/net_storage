/* ringbuffer.c
 * Routines for packet capture windows
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * <laurent.deniel@free.fr>
 *
 * Almost completely rewritten in order to:
 *
 * - be able to use a unlimited number of ringbuffer files
 * - close the current file and open (truncating) the next file at switch
 * - set the final file name once open (or reopen)
 * - avoid the deletion of files that could not be truncated (can't arise now)
 *   and do not erase empty files
 *
 * The idea behind that is to remove the limitation of the maximum # of
 * ringbuffer files being less than the maximum # of open fd per process
 * and to be able to reduce the amount of virtual memory usage (having only
 * one file open at most) or the amount of file system usage (by truncating
 * the files at switch and not the capture stop, and by closing them which
 * makes possible their move or deletion after a switch).
 *
 */

#include <config.h>

#ifdef HAVE_LIBPCAP

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include <wsutil/wspcap.h>

#include <glib.h>

#include "ringbuffer.h"
#include <wsutil/file_util.h>
#include "/home/chenxu/LOONGSON-2k1000/src/CfgMgr/inc/config.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"

extern char *strptime(char *buf, char *format, struct tm *tm);


ringbuf_data rb_data;

static int ringbuf_load_history_file_list(const char *capfile_name)
{
    FILE *fp;
    char line[1024];
    char fileName[200];
    int lineNum = 0;
    char temp[100];
    struct tm daytime;
    unsigned int n;
    int net_num;
    struct stat s;

    net_num = capfile_name[strlen(capfile_name) - 1] - '0';
    
    snprintf(fileName, sizeof(fileName), "%shfList%d", STORAGE_PATH, net_num);
    if (NULL == (fp = fopen(fileName, "r")))
    {
        printf ("fopen %s failed\n", fileName);
        goto ringbuf_load_history_file_list_exit;
    }

    while(NULL != fgets(line, sizeof(line), fp))
    {
        for (n = 0; n < (sizeof(temp) - 1); n++)
        {
            if ((line[n] != '\r') && (line[n] != '\n'))
                temp[n] = line[n];
            else
                break;
        }
        temp[n] = 0;
//        printf ("%s : [%d] file %s\n", __func__, lineNum, temp);
        
        strptime(&temp[11], "%Y%m%d%H%M%S", &daytime);

        if (rb_data.files[lineNum % rb_data.num_files].name)
        {
            printf ("%s delete file %s\n", __func__, 
                rb_data.files[lineNum % rb_data.num_files].name);
            ws_unlink(rb_data.files[lineNum % rb_data.num_files].name);
            g_free(rb_data.files[lineNum % rb_data.num_files].name);
            rb_data.files[lineNum % rb_data.num_files].name = NULL;
        }
        rb_data.files[lineNum % rb_data.num_files].name = 
            g_strconcat(STORAGE_PATH, temp, NULL, NULL, NULL,NULL, NULL);
        if (rb_data.files[lineNum % rb_data.num_files].name == NULL) 
        {
            printf ("%s : g_strconcat mem error\n", __func__);
            goto ringbuf_load_history_file_list_exit;
        }
        
//        if (0 != stat(rb_data.files[lineNum % rb_data.num_files].name, &s))
//        {
//            g_log(LOG_DOMAIN_CAPTURE_CHILD, G_LOG_LEVEL_INFO,
//              "%s : stat file %s error\n", __func__, rb_data.files[lineNum % rb_data.num_files].name);
//            goto ringbuf_load_history_file_list_exit;
//        }
        rb_data.files[lineNum % rb_data.num_files].ti = mktime(&daytime);
//        rb_data.files[lineNum].ti = s.st_mtime;
        lineNum++;
    }

    fclose(fp);

ringbuf_load_history_file_list_exit:
    rb_data.curr_file_num = lineNum % rb_data.num_files;
    printf ("%s : curr_file_num = %d\n", __func__, rb_data.curr_file_num);

    return rb_data.curr_file_num;
}

/*
 * create the next filename and open a new binary file with that name
 */
static int ringbuf_open_file(rb_file *rfile, int *err)
{
  char    filenum[5+1];
  char    timestr[14+1];
  time_t  current_time;
  struct tm *tm;

  if (rfile->name != NULL) {
    if (rb_data.unlimited == FALSE) {
      /* remove old file (if any, so ignore error) */
      ws_unlink(rfile->name);
    }
    g_free(rfile->name);
  }

#ifdef _WIN32
  _tzset();
#endif
  current_time = time(NULL);

  g_snprintf(filenum, sizeof(filenum), "%05u", (rb_data.curr_file_num + 1) % RINGBUFFER_MAX_NUM_FILES);
  tm = localtime(&current_time);
  if (tm != NULL)
    strftime(timestr, sizeof(timestr), "%Y%m%d%H%M%S", tm);
  else
    g_strlcpy(timestr, "196912312359", sizeof(timestr)); /* second before the Epoch */
  rfile->name = g_strconcat(rb_data.fprefix, "_", filenum, "_", timestr,
                            rb_data.fsuffix, NULL);
  rfile->ti = current_time;

  if (rfile->name == NULL) {
    if (err != NULL)
      *err = ENOMEM;
    return -1;
  }

  rb_data.fd = ws_open(rfile->name, O_RDWR|O_BINARY|O_TRUNC|O_CREAT,
                            rb_data.group_read_access ? 0640 : 0600);

  if (rb_data.fd == -1 && err != NULL) {
    *err = errno;
  }

  return rb_data.fd;
}

/*
 * Initialize the ringbuffer data structures
 */
int
ringbuf_init(const char *capfile_name, guint num_files, gboolean group_read_access)
{
  unsigned int i;
  char        *pfx, *last_pathsep;
  gchar       *save_file;

  rb_data.files = NULL;
  rb_data.curr_file_num = 0;
  rb_data.fprefix = NULL;
  rb_data.fsuffix = NULL;
  rb_data.unlimited = FALSE;
  rb_data.fd = -1;
  rb_data.pdh = NULL;
  rb_data.group_read_access = group_read_access;

  /* just to be sure ... */
  if (num_files <= RINGBUFFER_MAX_NUM_FILES) {
    rb_data.num_files = num_files;
  } else {
    rb_data.num_files = RINGBUFFER_MAX_NUM_FILES;
  }

  /* Check file name */
  if (capfile_name == NULL) {
    /* ringbuffer does not work with temporary files! */
    return -1;
  }

  /* set file name prefix/suffix */

  save_file = g_strdup(capfile_name);
  last_pathsep = strrchr(save_file, G_DIR_SEPARATOR);
  pfx = strrchr(save_file,'.');
  if (pfx != NULL && (last_pathsep == NULL || pfx > last_pathsep)) {
    /* The pathname has a "." in it, and it's in the last component
       of the pathname (because there is either only one component,
       i.e. last_pathsep is null as there are no path separators,
       or the "." is after the path separator before the last
       component.

       Treat it as a separator between the rest of the file name and
       the file name suffix, and arrange that the names given to the
       ring buffer files have the specified suffix, i.e. put the
       changing part of the name *before* the suffix. */
    pfx[0] = '\0';
    rb_data.fprefix = g_strdup(save_file);
    pfx[0] = '.'; /* restore capfile_name */
    rb_data.fsuffix = g_strdup(pfx);
  } else {
    /* Either there's no "." in the pathname, or it's in a directory
       component, so the last component has no suffix. */
    rb_data.fprefix = g_strdup(save_file);
    rb_data.fsuffix = NULL;
  }
  g_free(save_file);
  save_file = NULL;

  /* allocate rb_file structures (only one if unlimited since there is no
     need to save all file names in that case) */

  if (num_files == RINGBUFFER_UNLIMITED_FILES) {
    rb_data.unlimited = TRUE;
    rb_data.num_files = 1;
  }

  rb_data.files = (rb_file *)g_malloc(rb_data.num_files * sizeof(rb_file));
  if (rb_data.files == NULL) {
    return -1;
  }

  for (i=0; i < rb_data.num_files; i++) {
    rb_data.files[i].name = NULL;
  }

  ringbuf_load_history_file_list(capfile_name);

  /* create the first file */
  if (ringbuf_open_file(&rb_data.files[rb_data.curr_file_num], NULL) == -1) {
    ringbuf_error_cleanup();
    return -1;
  }

  return rb_data.fd;
}


const gchar *ringbuf_current_filename(void)
{
  return rb_data.files[rb_data.curr_file_num % rb_data.num_files].name;
}

/*
 * Calls ws_fdopen() for the current ringbuffer file
 */
FILE *
ringbuf_init_libpcap_fdopen(int *err)
{
  rb_data.pdh = ws_fdopen(rb_data.fd, "wb");
  if (rb_data.pdh == NULL) {
    if (err != NULL) {
      *err = errno;
    }
  }
  return rb_data.pdh;
}

/*
 * Switches to the next ringbuffer file
 */
gboolean
ringbuf_switch_file(FILE **pdh, gchar **save_file, int *save_file_fd, int *err)
{
  int     next_file_index;
  rb_file *next_rfile = NULL;

  /* close current file */

  if (fclose(rb_data.pdh) == EOF) {
    if (err != NULL) {
      *err = errno;
    }
    ws_close(rb_data.fd);  /* XXX - the above should have closed this already */
    rb_data.pdh = NULL;    /* it's still closed, we just got an error while closing */
    rb_data.fd = -1;
    return FALSE;
  }

  rb_data.pdh = NULL;
  rb_data.fd  = -1;

  /* get the next file number and open it */

  rb_data.curr_file_num++ /* = next_file_num*/;
  next_file_index = (rb_data.curr_file_num) % rb_data.num_files;
  next_rfile = &rb_data.files[next_file_index];

  if (ringbuf_open_file(next_rfile, err) == -1) {
    return FALSE;
  }

  if (ringbuf_init_libpcap_fdopen(err) == NULL) {
    return FALSE;
  }

  /* switch to the new file */
  *save_file = next_rfile->name;
  *save_file_fd = rb_data.fd;
  (*pdh) = rb_data.pdh;

  return TRUE;
}

/*
 * Calls fclose() for the current ringbuffer file
 */
gboolean
ringbuf_libpcap_dump_close(gchar **save_file, int *err)
{
  gboolean  ret_val = TRUE;

  /* close current file, if it's open */
  if (rb_data.pdh != NULL) {
    if (fclose(rb_data.pdh) == EOF) {
      if (err != NULL) {
        *err = errno;
      }
      ws_close(rb_data.fd);
      ret_val = FALSE;
    }
    rb_data.pdh = NULL;
    rb_data.fd  = -1;
  }

  /* set the save file name to the current file */
  *save_file = rb_data.files[rb_data.curr_file_num % rb_data.num_files].name;
  return ret_val;
}

/*
 * Frees all memory allocated by the ringbuffer
 */
void
ringbuf_free(void)
{
  unsigned int i;

  if (rb_data.files != NULL) {
    for (i=0; i < rb_data.num_files; i++) {
      if (rb_data.files[i].name != NULL) {
        g_free(rb_data.files[i].name);
        rb_data.files[i].name = NULL;
      }
    }
    g_free(rb_data.files);
    rb_data.files = NULL;
  }
  if (rb_data.fprefix != NULL) {
    g_free(rb_data.fprefix);
    rb_data.fprefix = NULL;
  }
  if (rb_data.fsuffix != NULL) {
    g_free(rb_data.fsuffix);
    rb_data.fsuffix = NULL;
  }
}

/*
 * Frees all memory allocated by the ringbuffer
 */
void
ringbuf_error_cleanup(void)
{
  unsigned int i;

  /* try to close via wtap */
  if (rb_data.pdh != NULL) {
    if (fclose(rb_data.pdh) == 0) {
      rb_data.fd = -1;
    }
    rb_data.pdh = NULL;
  }

  /* close directly if still open */
  if (rb_data.fd != -1) {
    ws_close(rb_data.fd);
    rb_data.fd = -1;
  }

  if (rb_data.files != NULL) {
    for (i=0; i < rb_data.num_files; i++) {
      if (rb_data.files[i].name != NULL) {
        ws_unlink(rb_data.files[i].name);
      }
    }
  }
  /* free the memory */
  ringbuf_free();
}

#endif /* HAVE_LIBPCAP */

/*
 * Editor modelines  -  http://www.wireshark.org/tools/modelines.html
 *
 * Local Variables:
 * c-basic-offset: 2
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * ex: set shiftwidth=2 tabstop=8 expandtab:
 * :indentSize=2:tabSize=8:noTabs=true:
 */
