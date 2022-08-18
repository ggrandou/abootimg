/* abootimg -  Manipulate (read, modify, create) Android Boot Images
 * Copyright (c) 2010-2011 Gilles Grandou <gilles@grandou.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <inttypes.h>

#ifdef __linux__
#include <sys/ioctl.h>
#include <linux/fs.h> /* BLKGETSIZE64 */
#endif

#ifdef __CYGWIN__
#include <sys/ioctl.h>
#include <cygwin/fs.h> /* BLKGETSIZE64 */
#endif

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include <sys/disk.h> /* DIOCGMEDIASIZE */
#include <sys/sysctl.h>
#endif

#if defined(__APPLE__)
# include <sys/disk.h> /* DKIOCGETBLOCKCOUNT */
#endif


#ifdef HAS_BLKID
#include <blkid/blkid.h>
#endif

#include "version.h"
#include "bootimg.h"


enum command {
  none,
  help,
  info,
  extract,
  update,
  create
};


typedef struct
{
  unsigned     size;
  int          is_blkdev;

  char*        fname;
  char*        config_fname;
  char*        kernel_fname;
  char*        ramdisk_fname;
  char*        second_fname;
  char*        dtbo_fname;
  char*        dtb_fname;

  FILE*        stream;

  union {
    struct boot_img_hdr_v0 header;
    struct boot_img_hdr_v1 header_v1;
    struct boot_img_hdr_v2 header_v2;
  };

  char*        kernel;
  char*        ramdisk;
  char*        second;
  char*        dtbo;
  char*        dtb;
} t_abootimg;


#define MAX_CONF_LEN    4096
char config_args[MAX_CONF_LEN] = "";



void abort_perror(char* str)
{
  perror(str);
  exit(errno);
}

void abort_printf(char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
  exit(1);
}


int blkgetsize(int fd, unsigned long long *pbsize)
{
# if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
  return ioctl(fd, DIOCGMEDIASIZE, pbsize);
# elif defined(__APPLE__)
  return ioctl(fd, DKIOCGETBLOCKCOUNT, pbsize);
# elif defined(__NetBSD__)
  // does a suitable ioctl exist?
  // return (ioctl(fd, DIOCGDINFO, &label) == -1);
  return 1;
# elif defined(__linux__) || defined(__CYGWIN__)
  return ioctl(fd, BLKGETSIZE64, pbsize);
# elif defined(__GNU__)
  // does a suitable ioctl for HURD exist?
  return 1;
# else
  return 1;
# endif

}

void print_usage(void)
{
  printf (
 " abootimg - manipulate Android Boot Images.\n"
 " (c) 2010-2011 Gilles Grandou <gilles@grandou.net>\n"
 " " VERSION_STR "\n"
 "\n"
 " abootimg [-h]\n"
 "\n"
 "      print usage\n"
 "\n"
 " abootimg -i <bootimg>\n"
 "\n"
 "      print boot image information\n"
 "\n"
 " abootimg -x <bootimg> [<bootimg.cfg> [<kernel> [<ramdisk> [<secondstage> [<dtb> [<recovery dtbo>]]]]]]\n"
 "\n"
 "      extract objects from boot image:\n"
 "      - config file (default name bootimg.cfg)\n"
 "      - kernel image (default name zImage)\n"
 "      - ramdisk image (default name initrd.img)\n"
 "      - second stage image (default name stage2.img)\n"
 "      - dtb (default name aboot.dtb)\n"
 "      - recovery dtbo (default recovery_dtbo.img)\n"
 "\n"
 " abootimg -u <bootimg> [-c \"param=value\"] [-f <bootimg.cfg>] [-k <kernel>] [-r <ramdisk>] [-s <secondstage>] [-d <dtb>] [-o <recovery dtbo>]\n"
 "\n"
 "      update a current boot image with objects given in command line\n"
 "      - header informations given in arguments (several can be provided)\n"
 "      - header informations given in config file\n"
 "      - kernel image\n"
 "      - ramdisk image\n"
 "      - second stage image\n"
 "\n"
 "      bootimg has to be valid Android Boot Image, or the update will abort.\n"
 "\n"
 " abootimg --create <bootimg> [-c \"param=value\"] [-f <bootimg.cfg>] -k <kernel> -r <ramdisk> [-s <secondstage>] [-d <dtb>] [-o <recovery dtbo>]\n"
 "\n"
 "      create a new image from scratch.\n"
 "      if the boot image file is a block device, sanity check will be performed to avoid overwriting a existing\n"
 "      filesystem.\n"
 "\n"
 "      argurments are the same than for -u.\n"
 "      kernel and ramdisk are mandatory.\n"
 "\n"
    );
}


enum command parse_args(int argc, char** argv, t_abootimg* img)
{
  enum command cmd = none;
  int i;

  if (argc<2)
    return none;

  if (!strcmp(argv[1], "-h")) {
    return help;
  }
  else if (!strcmp(argv[1], "-i")) {
    cmd=info;
  }
  else if (!strcmp(argv[1], "-x")) {
    cmd=extract;
  }
  else if (!strcmp(argv[1], "-u")) {
    cmd=update;
  }
  else if (!strcmp(argv[1], "--create")) {
    cmd=create;
  }
  else
    return none;

  switch(cmd) {
    case none:
    case help:
	    break;

    case info:
      if (argc != 3)
        return none;
      img->fname = argv[2];
      break;
      
    case extract:
      if ((argc < 3) || (argc > 9))
        return none;
      img->fname = argv[2];
      if (argc >= 4)
        img->config_fname = argv[3];
      if (argc >= 5)
        img->kernel_fname = argv[4];
      if (argc >= 6)
        img->ramdisk_fname = argv[5];
      if (argc >= 7)
        img->second_fname = argv[6];
      if (argc >= 8)
        img->dtb_fname = argv[7];
      if (argc >= 9)
        img->dtbo_fname = argv[8];
      break;

    case update:
    case create:
      if (argc < 3)
        return none;
      img->fname = argv[2];
      img->config_fname = NULL;
      img->kernel_fname = NULL;
      img->ramdisk_fname = NULL;
      img->second_fname = NULL;
      img->dtbo_fname = NULL;
      img->dtb_fname = NULL;
      for(i=3; i<argc; i++) {
        if (!strcmp(argv[i], "-c")) {
          if (++i >= argc)
            return none;
          unsigned len = strlen(argv[i]);
          if (strlen(config_args)+len+1 >= MAX_CONF_LEN)
            abort_printf("too many config parameters.\n");
          strcat(config_args, argv[i]);
          strcat(config_args, "\n");
        }
        else if (!strcmp(argv[i], "-f")) {
          if (++i >= argc)
            return none;
          img->config_fname = argv[i];
        }
        else if (!strcmp(argv[i], "-k")) {
          if (++i >= argc)
            return none;
          img->kernel_fname = argv[i];
        }
        else if (!strcmp(argv[i], "-r")) {
          if (++i >= argc)
            return none;
          img->ramdisk_fname = argv[i];
        }
        else if (!strcmp(argv[i], "-s")) {
          if (++i >= argc)
            return none;
          img->second_fname = argv[i];
        }
        else if (!strcmp(argv[i], "-d")) {
          if (++i >= argc)
            return none;
          img->dtb_fname = argv[i];
        }
        else if (!strcmp(argv[i], "-o")) {
          if (++i >= argc)
            return none;
          img->dtbo_fname = argv[i];
        }
        else
          return none;
      }
      break;
  }
  
  return cmd;
}

/* Compute the header size based on the header version */
static uint32_t boot_img_header_size(t_abootimg* img)
{
  if (img->header.header_version == 0)
    return sizeof(struct boot_img_hdr_v0);

  if (img->header.header_version == 1)
    return sizeof(struct boot_img_hdr_v1);

  /* 2 is the most we handle */
  return sizeof(struct boot_img_hdr_v2);
}


int check_boot_img_header(t_abootimg* img)
{
  if (strncmp((char*)(img->header.magic), BOOT_MAGIC, BOOT_MAGIC_SIZE)) {
    fprintf(stderr, "%s: no Android Magic Value\n", img->fname);
    return 1;
  }

  if (img->header.header_version > 2)
    abort_printf("%s: unsupported Android Boot Image version.\n", img->fname);

  if (img->header.header_version >= 1 && img->header_v1.header_size != boot_img_header_size(img))
    abort_printf("%s: Invalid header size.\n", img->fname);

  if (!(img->header.kernel_size)) {
    fprintf(stderr, "%s: kernel size is null\n", img->fname);
    return 1;
  }

  if (!(img->header.ramdisk_size)) {
    fprintf(stderr, "%s: ramdisk size is null\n", img->fname);
    return 1;
  }

  unsigned page_size = img->header.page_size;
  if (!page_size) {
    fprintf(stderr, "%s: Image page size is null\n", img->fname);
    return 1;
  }

  unsigned n = (img->header.kernel_size + page_size - 1) / page_size;
  unsigned m = (img->header.ramdisk_size + page_size - 1) / page_size;
  unsigned o = (img->header.second_size + page_size - 1) / page_size;
  unsigned p = (img->header_v1.recovery_dtbo_size + page_size - 1) / page_size;
  unsigned q = (img->header_v2.dtb_size + page_size - 1) / page_size;

  unsigned total_size = (1+n+m+o+p+q)*page_size;

  if (total_size > img->size) {
    fprintf(stderr, "%s: sizes mismatches in boot image\n", img->fname);
    return 1;
  }

  return 0;
}



void check_if_block_device(t_abootimg* img)
{
  struct stat st;

  if (stat(img->fname, &st)) {
    if (errno == ENOENT) {
      return;
    }
    else {
      printf("errno=%d\n", errno);
      abort_perror(img->fname);
    }
  }


#ifdef HAS_BLKID
  if (S_ISBLK(st.st_mode)) {
    img->is_blkdev = 1;

    char* type = blkid_get_tag_value(NULL, "TYPE", img->fname);
    if (type)
      abort_printf("%s: refuse to write on a valid partition type (%s)\n", img->fname, type);

    int fd = open(img->fname, O_RDONLY);
    if (fd == -1)
      abort_perror(img->fname);
    
    unsigned long long bsize = 0;
    if (blkgetsize(fd, &bsize))
      abort_perror(img->fname);
    img->size = bsize;

    close(fd);
  }
#endif
}



void open_bootimg(t_abootimg* img, char* mode)
{
  img->stream = fopen(img->fname, mode);
  if (!img->stream)
    abort_perror(img->fname);
}


void read_header(t_abootimg* img)
{
  /* First read the v0 part of the header to know the version */

  uint32_t header_size_read = sizeof(struct boot_img_hdr_v0);
  size_t rb = fread(&img->header, header_size_read, 1, img->stream);
  if ((rb!=1) || ferror(img->stream))
    abort_perror(img->fname);
  else if (feof(img->stream))
    abort_printf("%s: cannot read image header\n", img->fname);

  /* The read remaining parts based on the version read above */

  uint32_t header_size = boot_img_header_size(img);
  if (header_size > header_size_read) {
    fread((uint8_t *)&img->header + header_size_read,
          header_size - header_size_read,
          1, img->stream);
    if ((rb!=1) || ferror(img->stream))
      abort_perror(img->fname);
    else if (feof(img->stream))
      abort_printf("%s: cannot read image header\n", img->fname);
  }

  /* zero remaining header up to v2 */
  memset((uint8_t *)&img->header + header_size, 0, sizeof(struct boot_img_hdr_v2) - header_size);

  struct stat s;
  int fd = fileno(img->stream);
  if (fstat(fd, &s))
    abort_perror(img->fname);

  if (S_ISBLK(s.st_mode)) {
    unsigned long long bsize = 0;

    if (blkgetsize(fd, &bsize))
      abort_perror(img->fname);
    img->size = bsize;
    img->is_blkdev = 1;
  }
  else {
    img->size = s.st_size;
    img->is_blkdev = 0;
  }

  if (check_boot_img_header(img))
    abort_printf("%s: not a valid Android Boot Image.\n", img->fname);
}



void update_header_entry(t_abootimg* img, char* cmd)
{
  char *p;
  char *token;
  char *endtoken;
  char *value;

  p = strchr(cmd, '\n');
  if (p)
    *p  = '\0';

  p = cmd;
  p += strspn(p, " \t");
  token = p;
  
  p += strcspn(p, " =\t");
  endtoken = p;
  p += strspn(p, " \t");

  if (*p++ != '=')
    goto err;

  p += strspn(p, " \t");
  value = p;

  *endtoken = '\0';

  unsigned long long valuenum = strtoull(value, NULL, 0);

  if (!strcmp(token, "cmdline")) {
    unsigned len = strlen(value);
    if (len >= BOOT_ARGS_SIZE) 
      abort_printf("cmdline length (%d) is too long (max %d)", len, BOOT_ARGS_SIZE-1);
    memset(img->header.cmdline, 0, BOOT_ARGS_SIZE);
    strcpy((char*)(img->header.cmdline), value);
  }
  else if (!strncmp(token, "name", 4)) {
    strncpy((char*)(img->header.name), value, BOOT_NAME_SIZE);
    img->header.name[BOOT_NAME_SIZE-1] = '\0';
  }
  else if (!strncmp(token, "bootsize", 8)) {
    if (img->is_blkdev && (img->size != valuenum))
      abort_printf("%s: cannot change Boot Image size for a block device\n", img->fname);
    img->size = valuenum;
  }
  else if (!strncmp(token, "pagesize", 8)) {
    img->header.page_size = valuenum;
  }
  else if (!strncmp(token, "kerneladdr", 10)) {
    img->header.kernel_addr = valuenum;
  }
  else if (!strncmp(token, "ramdiskaddr", 11)) {
    img->header.ramdisk_addr = valuenum;
  }
  else if (!strncmp(token, "secondaddr", 10)) {
    img->header.second_addr = valuenum;
  }
  else if (!strncmp(token, "tagsaddr", 8)) {
    img->header.tags_addr = valuenum;
  }
  else if (!strncmp(token, "recoverydtobooffs", 17)) {
    img->header_v1.recovery_dtbo_offset = valuenum;
  }
  else if (!strncmp(token, "dtbaddr", 7)) {
    img->header_v2.dtb_addr = valuenum;
  }
  else
    goto err;
  return;

err:
  abort_printf("%s: bad config entry\n", token);
}

void update_header_version(t_abootimg* img)
{
  uint32_t new_version = 0;

  if (img->header_v1.recovery_dtbo_size > 0)
    new_version = 1;

  if (img->header_v2.dtb_size > 0)
    new_version = 2;

  /* Never change to an older version, but bump if needed */
  if (new_version > img->header.header_version) {
    img->header.header_version = new_version;
    img->header_v1.header_size = boot_img_header_size(img);
  }
}

void update_header(t_abootimg* img)
{
  if (img->config_fname) {
    FILE* config_file = fopen(img->config_fname, "r");
    if (!config_file)
      abort_perror(img->config_fname);

    printf("reading config file %s\n", img->config_fname);

    char* line = NULL;
    size_t len = 0;
    int read;

    while ((read = getline(&line, &len, config_file)) != -1) {
      update_header_entry(img, line);
      free(line);
      line = NULL;
    }
    if (ferror(config_file))
      abort_perror(img->config_fname);
  }

  unsigned len = strlen(config_args);
  if (len) {
    FILE* config_file = fmemopen(config_args, len, "r");
    if  (!config_file)
      abort_perror("-c args");

    printf("reading config args\n");

    char* line = NULL;
    size_t len = 0;
    int read;

    while ((read = getline(&line, &len, config_file)) != -1) {
      update_header_entry(img, line);
      free(line);
      line = NULL;
    }
    if (ferror(config_file))
      abort_perror("-c args");
  }
}


char *
read_new_image_part(t_abootimg *img,
                    char *part,
                    char *fname,
                    uint32_t *size)
{
  printf("reading %s from %s\n", part, fname);
  FILE* stream = fopen(fname, "r");
  if (!stream)
    abort_perror(fname);
  struct stat st;
  if (fstat(fileno(stream), &st))
    abort_perror(img->kernel_fname);
  *size = st.st_size;
  char* k = malloc(*size);
  if (!k)
    abort_perror("");
  size_t rb = fread(k, *size, 1, stream);
  if ((rb!=1) || ferror(stream))
    abort_perror(fname);
  else if (feof(stream))
    abort_printf("%s: cannot read kernel\n", img->kernel_fname);
  fclose(stream);
  return k;
}


char *
read_original_image_part(t_abootimg *img,
                         char *part,
                         uint32_t size,
                         uint64_t offset)
{
    char* r = malloc(size);
    if (!r)
      abort_perror("");
    if (fseek(img->stream, offset, SEEK_SET))
      abort_perror(img->fname);
    size_t rb = fread(r, size, 1, img->stream);
    if ((rb!=1) || ferror(img->stream))
      abort_perror(img->fname);
    else if (feof(img->stream))
      abort_printf("%s: cannot read %s\n", img->fname, part);
    return r;
}

void update_images(t_abootimg *img)
{
  unsigned page_size = img->header.page_size;
  unsigned ksize = img->header.kernel_size;
  unsigned rsize = img->header.ramdisk_size;
  unsigned ssize = img->header.second_size;
  unsigned dosize = img->header_v1.recovery_dtbo_size;
  unsigned dsize = img->header_v2.dtb_size;

  if (!page_size)
    abort_printf("%s: Image page size is null\n", img->fname);

  unsigned n = (ksize + page_size - 1) / page_size;
  unsigned m = (rsize + page_size - 1) / page_size;
  unsigned o = (ssize + page_size - 1) / page_size;
  unsigned p = (dosize + page_size - 1) / page_size;
  unsigned q = (dsize + page_size - 1) / page_size;

  unsigned roffset = (1+n)*page_size;
  unsigned soffset = (1+n+m)*page_size;
  unsigned dooffset = (1+n+m+o)*page_size;
  unsigned doffset = (1+n+m+o+p)*page_size;

  int offsets_changed = 0;
  uint32_t part_size;

  if (img->kernel_fname) {
    img->kernel =
      read_new_image_part(img, "kernel",
                          img->kernel_fname,
                          &part_size);
    img->header.kernel_size = part_size;
    offsets_changed = 1;
  }

  if (img->ramdisk_fname) {
    img->ramdisk =
      read_new_image_part(img, "ramdisk",
                          img->ramdisk_fname,
                          &part_size);
    img->header.ramdisk_size = part_size;
    offsets_changed = 1;
  }
  else if (offsets_changed) {
    img->ramdisk = read_original_image_part (img, "ramdisk", rsize, roffset);
  }

  if (img->second_fname) {
    img->second =
      read_new_image_part(img, "second stage",
                          img->second_fname,
                          &part_size);
    img->header.second_size = part_size;
    offsets_changed = 1;
  }
  else if (offsets_changed && img->header.second_size) {
    img->second = read_original_image_part (img, "second stage", ssize, soffset);
  }

  if (img->dtbo_fname) {
    img->dtbo =
      read_new_image_part(img, "recovery dtbo",
                          img->dtbo_fname,
                          &part_size);
    img->header_v1.recovery_dtbo_size = part_size;
    offsets_changed = 1;
  }
  else if (offsets_changed && img->header_v1.recovery_dtbo_size) {
    img->dtbo = read_original_image_part (img, "recovery dtbo", dosize, dooffset);
  }

  if (img->dtb_fname) {
    img->dtb =
      read_new_image_part(img, "dtb",
                          img->dtb_fname,
                          &part_size);
    img->header_v2.dtb_size = part_size;
    offsets_changed = 1;
  }
  else if (offsets_changed && img->header_v2.dtb_size) {
    img->dtb = read_original_image_part (img, "dtb", dsize, doffset);
  }

  n = (img->header.kernel_size + page_size - 1) / page_size;
  m = (img->header.ramdisk_size + page_size - 1) / page_size;
  o = (img->header.second_size + page_size - 1) / page_size;
  p = (img->header_v1.recovery_dtbo_size + page_size - 1) / page_size;
  q = (img->header_v2.dtb_size + page_size - 1) / page_size;
  unsigned total_size = (1+n+m+o+p+q)*page_size;

  if (!img->size)
    img->size = total_size;
  else if (total_size > img->size)
    abort_printf("%s: updated is too big for the Boot Image (%u vs %u bytes)\n", img->fname, total_size, img->size);
}

void write_bootimg_part(t_abootimg* img,
                        char *part,
                        uint32_t offset,
                        uint32_t size,
                        char *padding)
{
  unsigned psize = img->header.page_size;

  if (fseek(img->stream, offset, SEEK_SET))
    abort_perror(img->fname);
  fwrite(part, size, 1, img->stream);
  if (ferror(img->stream))
    abort_perror(img->fname);

  fwrite(padding, psize - (size % psize), 1, img->stream);
  if (ferror(img->stream))
    abort_perror(img->fname);
}


void write_bootimg(t_abootimg* img)
{
  unsigned psize;
  char* padding;

  printf ("Writing Boot Image %s\n", img->fname);

  psize = img->header.page_size;
  padding = calloc(psize, 1);
  if (!padding)
    abort_perror("");

  unsigned n = (img->header.kernel_size + psize - 1) / psize;
  unsigned m = (img->header.ramdisk_size + psize - 1) / psize;
  unsigned o = (img->header.second_size + psize - 1) / psize;
  unsigned p = (img->header_v1.recovery_dtbo_size + psize - 1) / psize;
  //unsigned q = (img->header_v2.dtb_size + psize - 1) / psize;

  if (fseek(img->stream, 0, SEEK_SET))
    abort_perror(img->fname);

  uint32_t header_size = boot_img_header_size(img);
  fwrite(&img->header, header_size, 1, img->stream);
  if (ferror(img->stream))
    abort_perror(img->fname);

  fwrite(padding, psize - header_size, 1, img->stream);
  if (ferror(img->stream))
    abort_perror(img->fname);

  if (img->kernel)
    write_bootimg_part(img, img->kernel, 1 * psize,
                       img->header.kernel_size, padding);

  if (img->ramdisk)
    write_bootimg_part(img, img->ramdisk, (1+n) * psize,
                       img->header.ramdisk_size, padding);

  if (img->second && img->header.second_size)
    write_bootimg_part(img, img->second, (1+n+m) * psize,
                       img->header.second_size, padding);

  if (img->dtbo && img->header_v1.recovery_dtbo_size)
    write_bootimg_part(img, img->dtbo, (1+n+m+o) * psize,
                       img->header_v1.recovery_dtbo_size, padding);

  if (img->dtb && img->header_v2.dtb_size)
    write_bootimg_part(img, img->dtb, (1+n+m+o+p) * psize,
                       img->header_v2.dtb_size, padding);

  ftruncate (fileno(img->stream), img->size);

  free(padding);
}



void print_bootimg_info(t_abootimg* img)
{
  printf ("\nAndroid Boot Image Info:\n\n");
  printf ("* file name = %s %s\n\n", img->fname, img->is_blkdev ? "[block device]":"");

  printf ("* image size = %u bytes (%.2f MB)\n", img->size, (double)img->size/0x100000);
  printf ("  page size  = %u bytes\n", img->header.page_size);
  printf ("  version    = %u\n\n", img->header.header_version);

  printf ("* Boot Name = \"%s\"\n", img->header.name);
  uint32_t v = img->header.os_version;
  if (v != 0)
    printf ("  OS Version = %d.%d.%d (patch level %d-%d)\n\n",
            v >> 25 & 0x7f, v >> 18 & 0x7f, v >> 11 & 0x7f,
            (v >> 4) & 0x7f, v & 0xf);

  unsigned kernel_size = img->header.kernel_size;
  unsigned ramdisk_size = img->header.ramdisk_size;
  unsigned second_size = img->header.second_size;
  unsigned recovery_dtbo_size = img->header_v1.recovery_dtbo_size;
  unsigned dtb_size = img->header_v2.dtb_size;

  printf ("* kernel size       = %u bytes (%.2f MB)\n", kernel_size, (double)kernel_size/0x100000);
  printf ("  ramdisk size      = %u bytes (%.2f MB)\n", ramdisk_size, (double)ramdisk_size/0x100000);
  if (second_size)
    printf ("  second stage size = %u bytes (%.2f MB)\n", second_size, (double)second_size/0x100000);

  if (recovery_dtbo_size)
    printf ("  recovery dtbo size = %u bytes (%.2f MB)\n", recovery_dtbo_size, (double)recovery_dtbo_size/0x100000);
  if (dtb_size)
    printf ("  dtb size          = %u bytes (%.2f MB)\n", dtb_size, (double)dtb_size/0x100000);

  printf ("\n* load addresses:\n");
  printf ("  kernel:       0x%08x\n", img->header.kernel_addr);
  printf ("  ramdisk:      0x%08x\n", img->header.ramdisk_addr);
  if (second_size)
    printf ("  second stage: 0x%08x\n", img->header.second_addr);
  printf ("  tags:         0x%08x\n", img->header.tags_addr);
  if (recovery_dtbo_size)
    printf ("  recovery dtbo: 0x%08"PRIx64"\n", img->header_v1.recovery_dtbo_offset);
  if (dtb_size)
    printf ("  dtb:          0x%08"PRIx64"\n", img->header_v2.dtb_addr);
  printf ("\n");

  if (img->header.cmdline[0])
    printf ("* cmdline = %s\n\n", img->header.cmdline);
  else
    printf ("* empty cmdline\n");

  printf ("* id = ");
  int i;
  for (i=0; i<8; i++)
    printf ("0x%08x ", img->header.id[i]);
  printf ("\n\n");
}



void write_bootimg_config(t_abootimg* img)
{
  printf ("writing boot image config in %s\n", img->config_fname);

  FILE* config_file = fopen(img->config_fname, "w");
  if (!config_file)
    abort_perror(img->config_fname);

  fprintf(config_file, "bootsize = 0x%x\n", img->size);
  fprintf(config_file, "pagesize = 0x%x\n", img->header.page_size);

  fprintf(config_file, "kerneladdr = 0x%x\n", img->header.kernel_addr);
  fprintf(config_file, "ramdiskaddr = 0x%x\n", img->header.ramdisk_addr);
  fprintf(config_file, "secondaddr = 0x%x\n", img->header.second_addr);
  fprintf(config_file, "tagsaddr = 0x%x\n", img->header.tags_addr);
  fprintf(config_file, "recoverydtobooffs = 0x%"PRIx64"\n", img->header_v1.recovery_dtbo_offset);
  fprintf(config_file, "dtbaddr = 0x%"PRIx64"\n", img->header_v2.dtb_addr);

  fprintf(config_file, "name = %s\n", img->header.name);
  fprintf(config_file, "cmdline = %s\n", img->header.cmdline);

  fclose(config_file);
}

void extract_part(t_abootimg* img,
                  char *part_fname,
                  uint32_t size, uint32_t offset) {
  void* k = malloc(size);
  if (!k)
    abort_perror(NULL);

  if (fseek(img->stream, offset, SEEK_SET))
    abort_perror(img->fname);

  size_t rb = fread(k, size, 1, img->stream);
  if ((rb!=1) || ferror(img->stream))
    abort_perror(img->fname);

  FILE* part_file = fopen(part_fname, "w");
  if (!part_file)
    abort_perror(part_fname);

  fwrite(k, size, 1, part_file);
  if (ferror(part_file))
    abort_perror(part_fname);

  fclose(part_file);
  free(k);
}

void extract_kernel(t_abootimg* img)
{
  unsigned psize = img->header.page_size;
  unsigned ksize = img->header.kernel_size;

  printf ("extracting kernel in %s\n", img->kernel_fname);
  extract_part(img, img->kernel_fname, ksize, psize);
}

void extract_ramdisk(t_abootimg* img)
{
  unsigned psize = img->header.page_size;
  unsigned ksize = img->header.kernel_size;
  unsigned rsize = img->header.ramdisk_size;

  unsigned n = (ksize + psize - 1) / psize;
  unsigned roffset = (1+n)*psize;

  printf ("extracting ramdisk in %s\n", img->ramdisk_fname);

  extract_part(img, img->ramdisk_fname, rsize, roffset);
}

void extract_second(t_abootimg* img)
{
  unsigned psize = img->header.page_size;
  unsigned ksize = img->header.kernel_size;
  unsigned rsize = img->header.ramdisk_size;
  unsigned ssize = img->header.second_size;

  if (!ssize) // Second Stage not present
    return;

  unsigned n = (ksize + psize - 1) / psize;
  unsigned m = (rsize + psize - 1) / psize;
  unsigned soffset = (1+n+m)*psize;

  printf ("extracting second stage image in %s\n", img->second_fname);

  extract_part(img, img->second_fname, ssize, soffset);
}

void extract_dtbo(t_abootimg* img)
{
  unsigned psize = img->header.page_size;
  unsigned ksize = img->header.kernel_size;
  unsigned rsize = img->header.ramdisk_size;
  unsigned ssize = img->header.second_size;
  unsigned dosize = img->header_v1.recovery_dtbo_size;

  if (!dosize) // recovery dtbo not present
    return;

  unsigned n = (ksize + psize - 1) / psize;
  unsigned m = (rsize + psize - 1) / psize;
  unsigned o = (ssize + psize - 1) / psize;
  unsigned dooffset = (1+n+m+o)*psize;

  printf ("extracting recovery dtbo image in %s\n", img->dtbo_fname);

  extract_part(img, img->dtbo_fname, dosize, dooffset);
}

void extract_dtb(t_abootimg* img)
{
  unsigned psize = img->header.page_size;
  unsigned ksize = img->header.kernel_size;
  unsigned rsize = img->header.ramdisk_size;
  unsigned ssize = img->header.second_size;
  unsigned dosize = img->header_v1.recovery_dtbo_size;
  unsigned dsize = img->header_v2.dtb_size;

  if (!dsize) // dtb not present
    return;

  unsigned n = (ksize + psize - 1) / psize;
  unsigned m = (rsize + psize - 1) / psize;
  unsigned o = (ssize + psize - 1) / psize;
  unsigned p = (dosize + psize - 1) / psize;
  unsigned doffset = (1+n+m+o+p)*psize;

  printf ("extracting dtb image in %s\n", img->dtb_fname);

  extract_part(img, img->dtb_fname, dsize, doffset);
}

t_abootimg* new_bootimg()
{
  t_abootimg* img;

  img = calloc(sizeof(t_abootimg), 1);
  if (!img)
    abort_perror(NULL);

  img->config_fname = "bootimg.cfg";
  img->kernel_fname = "zImage";
  img->ramdisk_fname = "initrd.img";
  img->second_fname = "stage2.img";
  img->dtbo_fname = "recovery_dtbo.img";
  img->dtb_fname = "aboot.dtb";

  memcpy(img->header.magic, BOOT_MAGIC, BOOT_MAGIC_SIZE);
  img->header.page_size = 2048;  // a sensible default page size

  return img;
}


int main(int argc, char** argv)
{
  t_abootimg* bootimg = new_bootimg();

  switch(parse_args(argc, argv, bootimg))
  {
    case none:
      printf("error - bad arguments\n\n");
      print_usage();
      break;

    case help:
      print_usage();
      break;

    case info:
      open_bootimg(bootimg, "r");
      read_header(bootimg);
      print_bootimg_info(bootimg);
      break;

    case extract:
      open_bootimg(bootimg, "r");
      read_header(bootimg);
      write_bootimg_config(bootimg);
      extract_kernel(bootimg);
      extract_ramdisk(bootimg);
      extract_second(bootimg);
      extract_dtbo(bootimg);
      extract_dtb(bootimg);
      break;

    case update:
      open_bootimg(bootimg, "r+");
      read_header(bootimg);
      update_header(bootimg);
      update_images(bootimg);
      update_header_version(bootimg);
      write_bootimg(bootimg);
      break;

    case create:
      if (!bootimg->kernel_fname || !bootimg->ramdisk_fname) {
        print_usage();
        break;
      }
      check_if_block_device(bootimg);
      open_bootimg(bootimg, "w");
      update_header(bootimg);
      update_images(bootimg);
      update_header_version(bootimg);
      if (check_boot_img_header(bootimg))
        abort_printf("%s: Sanity cheks failed", bootimg->fname);
      write_bootimg(bootimg);
      break;
  }

  return 0;
}


