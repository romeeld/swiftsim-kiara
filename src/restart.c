/*******************************************************************************
 * This file is part of SWIFT.
 * Copyright (c) 2018 Peter W. Draper (p.w.draper@durham.ac.uk)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/

/**
 *  @file restart.c
 *  @brief support for SWIFT restarts
 */

/* Config parameters. */
#include "../config.h"

/* Standard headers. */
#include <errno.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "engine.h"
#include "error.h"
#include "restart.h"
#include "version.h"

/* The signature for restart files. */
#define SWIFT_RESTART_SIGNATURE "SWIFT-restart-file"

#define FNAMELEN 200
#define LABLEN 20

/* Structure for a dumped header. */
struct header {
  size_t len;             /* Total length of data in bytes. */
  char label[LABLEN + 1]; /* A label for data */
};

/**
 * @brief generate a name for a restart file.
 *
 * @param dir the directory of restart files.
 * @param basename the basename of the restart files.
 * @param nodeID a unique integer, usually the nodeID of the engine.
 * @param name pointer to a string to hold the result.
 * @param size length of name.
 *
 * @result 0 if the string was large enough.
 */
int restart_genname(const char *dir, const char *basename, int nodeID,
                    char *name, int size) {
  int n = snprintf(name, size, "%s/%s_%06d.rst", dir, basename, nodeID);
  return (n >= size);
}

/**
 * @brief locate all the restart files in the given directory with the given
 *        basename.
 * @param dir the directory of restart files.
 * @param basename the basename of the restart files.
 * @param nfiles the number of restart files located.
 *
 * @result pointer to an array of strings with all the filenames found,
 *         these should be collated using the current locale, i.e. sorted
 *         alphabetically (so make sure the filenames are zero padded to get
 *         numeric ordering). Release by calling restart_locate_free().
 */
char **restart_locate(const char *dir, const char *basename, int *nfiles) {
  *nfiles = 0;

  /* Construct the glob pattern for locating files. */
  char pattern[FNAMELEN];
  if (snprintf(pattern, FNAMELEN, "%s/%s_[0-9]*.rst", dir, basename) <
      FNAMELEN) {

    glob_t globbuf;
    char **files = NULL;
    if (glob(pattern, 0, NULL, &globbuf) == 0) {
      *nfiles = globbuf.gl_pathc;
      files = malloc(sizeof(char *) * *nfiles);
      for (int i = 0; i < *nfiles; i++) {
        files[i] = strdup(globbuf.gl_pathv[i]);
      }
    }

    globfree(&globbuf);
    return files;
  }
  error("Failed to construct pattern to locate restart files");

  return NULL;
}

/**
 * @brief Release the memory allocated to hold the restart file names.
 *
 * @param nfiles the number of restart files located.
 * @param files the list of filenames found in call to restart_locate().
 */
void restart_locate_free(int nfiles, char **files) {
  for (int i = 0; i < nfiles; i++) {
    free(files[i]);
  }
  free(files);
}

/**
 * @brief Write a restart file for the state of the given engine struct.
 *
 * @param e the engine with our state information.
 * @param filename name of the file to write the restart data to.
 */
void restart_write(struct engine *e, const char *filename) {

  FILE *stream = fopen(filename, "w");
  if (stream == NULL)
    error("Failed to open restart file: %s (%s)", filename, strerror(errno));

  /* Dump our signature and version. */
  restart_write_blocks(SWIFT_RESTART_SIGNATURE, strlen(SWIFT_RESTART_SIGNATURE),
                       1, stream, "signature", "SWIFT signature");
  restart_write_blocks((void *)package_version(), strlen(package_version()), 1,
                       stream, "version", "SWIFT version");

  engine_struct_dump(e, stream);
  fclose(stream);
}

/**
 * @brief Read a restart file to construct a saved engine struct state.
 *
 * @param e the engine to recover from the saved state.
 * @param filename name of the file containing the staved state.
 */
void restart_read(struct engine *e, const char *filename) {

  FILE *stream = fopen(filename, "r");
  if (stream == NULL)
    error("Failed to open restart file: %s (%s)", filename, strerror(errno));

  /* Get our version and signature back. These should match. */
  char signature[strlen(SWIFT_RESTART_SIGNATURE) + 1];
  int len = strlen(SWIFT_RESTART_SIGNATURE);
  restart_read_blocks(signature, len, 1, stream, NULL, "SWIFT signature");
  signature[len] = '\0';
  if (strncmp(signature, SWIFT_RESTART_SIGNATURE, len) != 0)
    error(
        "Do not recognise this as a SWIFT restart file, found '%s' "
        "expected '%s'",
        signature, SWIFT_RESTART_SIGNATURE);

  char version[FNAMELEN];
  len = strlen(package_version());
  restart_read_blocks(version, len, 1, stream, NULL, "SWIFT version");
  version[len] = '\0';

  /* It might work! */
  if (strncmp(version, package_version(), len) != 0)
    message(
        "WARNING: restoring from a different version of SWIFT.\n You have:"
        " '%s' and the restarts files are from: '%s'. This may fail"
        " badly.",
        package_version(), version);

  engine_struct_restore(e, stream);
  fclose(stream);
}

/**
 * @brief Read blocks of memory from a file stream into a memory location.
 *        Exits the application if the read fails and does nothing if the
 *        size is zero.
 *
 * @param ptr pointer to the memory
 * @param size size of a block
 * @param nblocks number of blocks to read
 * @param stream the file stream
 * @param label the label recovered for the block, needs to be at least 20
 *              characters, set to NULL if not required
 * @param errstr a context string to qualify any errors.
 */
void restart_read_blocks(void *ptr, size_t size, size_t nblocks, FILE *stream,
                         char *label, const char *errstr) {
  if (size > 0) {
    struct header head;
    size_t nread = fread(&head, sizeof(struct header), 1, stream);
    if (nread != 1)
      error("Failed to read the %s header from restart file (%s)", errstr,
            strerror(errno));

    /* Check that the stored length is the same as the expected one. */
    if (head.len != nblocks * size)
      error("Mismatched data length in restart file for %s (%zu != %zu)",
            errstr, head.len, nblocks * size);

    /* Return label, if required. */
    if (label != NULL) strncpy(label, head.label, LABLEN);

    nread = fread(ptr, size, nblocks, stream);
    if (nread != nblocks)
      error("Failed to restore %s from restart file (%s)", errstr,
            ferror(stream) ? strerror(errno) : "unexpected end of file");
  }
}

/**
 * @brief Write blocks of memory to a file stream from a memory location.
 *        Exits the application if the write fails and does nothing
 *        if the size is zero.
 *
 * @param ptr pointer to the memory
 * @param size the blocks
 * @param nblocks number of blocks to write
 * @param stream the file stream
 * @param label a label for the content, can only be 20 characters.
 * @param errstr a context string to qualify any errors.
 */
void restart_write_blocks(void *ptr, size_t size, size_t nblocks, FILE *stream,
                          const char *label, const char *errstr) {
  if (size > 0) {

    /* Add a preamble header. */
    struct header head;
    head.len = nblocks * size;
    strncpy(head.label, label, LABLEN);
    head.label[LABLEN] = '\0';

    /* Now dump it and the data. */
    size_t nwrite = fwrite(&head, sizeof(struct header), 1, stream);
    if (nwrite != 1)
      error("Failed to save %s header to restart file (%s)", errstr,
            strerror(errno));

    nwrite = fwrite(ptr, size, nblocks, stream);
    if (nwrite != nblocks)
      error("Failed to save %s to restart file (%s)", errstr, strerror(errno));
  }
}

/**
 * @brief check if the stop file exists in the given directory and optionally
 *        remove it if found.
 *
 * @param dir the directory of restart files.
 * @param cleanup remove the file if found. Should only do this from one rank
 *                once all ranks have tested this file.
 *
 * @result 1 if the file was found.
 */
int restart_stop_now(const char *dir, int cleanup) {
  static struct stat buf;
  char filename[FNAMELEN];
  strcpy(filename, dir);
  strcat(filename, "/stop");
  if (stat(filename, &buf) == 0) {
    if (cleanup && unlink(filename) != 0) {
      /* May not be fatal, so press on. */
      message("Failed to delete restart stop file (%s)", strerror(errno));
    }
    return 1;
  }
  return 0;
}
