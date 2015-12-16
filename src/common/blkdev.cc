/*
 * Ceph - scalable distributed file system
 *
 * Copyright (c) 2015 Hewlett-Packard Development Company, L.P.
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */
#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include "include/int_types.h"
#include "include/uuid.h"

#ifdef __linux__
#include <linux/fs.h>
#include <blkid/blkid.h>
#include <limits.h>
#include <libstoragemgmt/libstoragemgmt.h>

#define UUID_LEN 36

static const char *sandbox_dir = "";

void set_block_device_sandbox_dir(const char *dir)
{
  if (dir)
    sandbox_dir = dir;
  else
    sandbox_dir = "";
}

int get_block_device_size(int fd, int64_t *psize)
{
#ifdef BLKGETSIZE64
  int ret = ::ioctl(fd, BLKGETSIZE64, psize);
#elif defined(BLKGETSIZE)
  unsigned long sectors = 0;
  int ret = ::ioctl(fd, BLKGETSIZE, &sectors);
  *psize = sectors * 512ULL;
#else
# error "Linux configuration error (get_block_device_size)"
#endif
  if (ret < 0)
    ret = -errno;
  return ret;
}

/**
 * get the base device (strip off partition suffix and /dev/ prefix)
 *  e.g.,
 *   /dev/sda3 -> sda
 *   /dev/cciss/c0d1p2 -> cciss/c0d1
 */
int get_block_device_base(const char *dev, char *out, size_t out_len)
{
  struct stat st;
  int r = 0;
  char buf[PATH_MAX*2];
  struct dirent *de;
  DIR *dir;
  char devname[PATH_MAX], fn[PATH_MAX];
  char *p;

  if (strncmp(dev, "/dev/", 5) != 0)
    return -EINVAL;

  strncpy(devname, dev + 5, PATH_MAX-1);
  devname[PATH_MAX-1] = '\0';
  for (p = devname; *p; ++p)
    if (*p == '/')
      *p = '!';

  snprintf(fn, sizeof(fn), "%s/sys/block/%s", sandbox_dir, devname);
  if (stat(fn, &st) == 0) {
    if (strlen(devname) + 1 > out_len) {
      return -ERANGE;
    }
    strncpy(out, devname, out_len);
    return 0;
  }

  snprintf(fn, sizeof(fn), "%s/sys/block", sandbox_dir);
  dir = opendir(fn);
  if (!dir)
    return -errno;

  while (!::readdir_r(dir, reinterpret_cast<struct dirent*>(buf), &de)) {
    if (!de) {
      if (errno) {
	r = -errno;
	goto out;
      }
      break;
    }
    if (de->d_name[0] == '.')
      continue;
    snprintf(fn, sizeof(fn), "%s/sys/block/%s/%s", sandbox_dir, de->d_name,
	     devname);

    if (stat(fn, &st) == 0) {
      // match!
      if (strlen(de->d_name) + 1 > out_len) {
	r = -ERANGE;
	goto out;
      }
      strncpy(out, de->d_name, out_len);
      r = 0;
      goto out;
    }
  }
  r = -ENOENT;

 out:
  closedir(dir);
  return r;
}

/**
 * get a block device property
 *
 * return the value (we assume it is positive)
 * return negative error on error
 */
int64_t get_block_device_int_property(const char *devname, const char *property)
{
  char basename[PATH_MAX], filename[PATH_MAX];
  int64_t r;

  r = get_block_device_base(devname, basename, sizeof(basename));
  if (r < 0)
    return r;

  snprintf(filename, sizeof(filename),
	   "%s/sys/block/%s/queue/discard_granularity", sandbox_dir, basename);

  FILE *fp = fopen(filename, "r");
  if (fp == NULL) {
    return -errno;
  }

  char buff[256] = {0};
  if (fgets(buff, sizeof(buff) - 1, fp)) {
    // strip newline etc
    for (char *p = buff; *p; ++p) {
      if (!isdigit(*p)) {
	*p = 0;
	break;
      }
    }
    char *endptr = 0;
    r = strtoll(buff, &endptr, 10);
    if (endptr != buff + strlen(buff))
      r = -EINVAL;
  } else {
    r = 0;
  }
  fclose(fp);
  return r;
}

bool block_device_support_discard(const char *devname)
{
  return get_block_device_int_property(devname, "discard_granularity") > 0;
}

int block_device_discard(int fd, int64_t offset, int64_t len)
{
  uint64_t range[2] = {(uint64_t)offset, (uint64_t)len};
  return ioctl(fd, BLKDISCARD, range);
}

int get_device_by_uuid(uuid_d dev_uuid, const char* label, char* partition,
	char* device)
{
  char uuid_str[UUID_LEN+1];
  char basename[PATH_MAX];
  const char* temp_partition_ptr = NULL;
  blkid_cache cache = NULL;
  blkid_dev dev = NULL;
  int rc = 0;

  dev_uuid.print(uuid_str);

  if (blkid_get_cache(&cache, NULL) >= 0)
    dev = blkid_find_dev_with_tag(cache, label, (const char*)uuid_str);
  else
    rc = -EINVAL;

  if (dev) {
    temp_partition_ptr = blkid_dev_devname(dev);
    strncpy(partition, temp_partition_ptr, PATH_MAX);
    rc = get_block_device_base(partition, basename,
      sizeof(basename));
    if (rc >= 0) {
      strncpy(device, basename, sizeof(basename));
      rc = 0;
    } else {
      rc = -ENODEV;
    }
  } else {
    rc = -EINVAL;
  }

  /* From what I can tell, blkid_put_cache cleans up dev, which
   * appears to be a pointer into cache, as well */
  if (cache)
    blkid_put_cache(cache);
  return rc;
}

int get_device_by_symlink(const char* symlink, char* device)
{
  char basename[PATH_MAX];
  char *return_ptr;

  return_ptr = realpath(symlink, basename);

  if (return_ptr != basename)
    goto out;

  strncpy(device, basename, sizeof(basename));

out:
  return -1;
}

int enable_locate_led(const char *uri, const char *dev_path)
{
  lsm_connect *lsm_conn = NULL;
  lsm_error *lsm_err = NULL;
  int rc = 0;
  uint32_t i = 0;
  uint32_t  j = 0;
  const char *lsm_password = NULL;
  uint32_t lsm_tmo = 3000;
  lsm_system **lsm_sys = NULL;
  uint32_t sys_count = 0;
  const char *sys_id = NULL;
  lsm_volume **lsm_vol = NULL;
  uint32_t vol_count = 0;
  lsm_disk **lsm_disk = NULL;
  uint32_t disk_count = 0;
  const char *lsm_dev_path = NULL;
  lsm_system_mode_type sys_mode = LSM_SYSTEM_MODE_NO_SUPPORT;
  lsm_storage_capabilities *lsm_cap = NULL;
  char basename[PATH_MAX];

  rc = lsm_connect_password(uri, lsm_password, &lsm_conn, lsm_tmo,
	&lsm_err, LSM_CLIENT_FLAG_RSVD);

  if (rc)
    return rc; 

  rc = lsm_system_list(lsm_conn, &lsm_sys, &sys_count, LSM_CLIENT_FLAG_RSVD);

  if (rc)
    return rc;

  for (i = 0; i<sys_count; i++) {

    rc = lsm_capabilities(lsm_conn, lsm_sys[i], &lsm_cap,
          LSM_CLIENT_FLAG_RSVD);

    if (rc)
      goto free;

    if (!lsm_capability_get(lsm_cap, LSM_CAP_SYS_MODE_GET))
      goto free;

    rc = lsm_system_mode_get(lsm_sys[i], &sys_mode);

    if (rc)
      goto free;

    sys_id = lsm_system_id_get(lsm_sys[i]);

    if (sys_mode == LSM_SYSTEM_MODE_HARDWARE_RAID) {

    if (!lsm_capability_get(lsm_cap, LSM_CAP_VOLUMES))
      goto free;

      rc = lsm_volume_list(lsm_conn, "system_id", sys_id, &lsm_vol, &vol_count,
            LSM_CLIENT_FLAG_RSVD);

      if (rc)
        goto free;

      if (lsm_capability_get(lsm_cap, LSM_CAP_VOLUME_SD_PATH) &&
          lsm_capability_get(lsm_cap, LSM_CAP_VOLUME_LED)) {

        for (j = 0; j<vol_count; j++) {

          rc = lsm_volume_sd_path_get(lsm_vol[j], &lsm_dev_path);
          rc = get_block_device_base(lsm_dev_path, basename, sizeof(basename));
          if (strcmp(basename, dev_path)) {
            continue;
          } else {
            rc = lsm_volume_ident_led_set(lsm_conn, lsm_vol[j],
                  LSM_CLIENT_FLAG_RSVD);
            break;
          }
        }
      }
      lsm_volume_record_array_free(lsm_vol, vol_count);
    } else if (sys_mode == LSM_SYSTEM_MODE_HBA) {
      rc = lsm_disk_list(lsm_conn, "system_id", sys_id, &lsm_disk, &disk_count,
            LSM_CLIENT_FLAG_RSVD);

      if (rc)
        goto free;

      if (lsm_capability_get(lsm_cap, LSM_CAP_DISK_SD_PATH) &&
          lsm_capability_get(lsm_cap, LSM_CAP_DISK_LED)) {

        for (j = 0; j<disk_count; j++) {
          rc = lsm_disk_sd_path_get(lsm_disk[j], &lsm_dev_path);
          rc = get_block_device_base(lsm_dev_path, basename, sizeof(basename));
          if (strcmp(basename, dev_path)) {
            continue;
          } else {
            rc = lsm_disk_set_ident_led(lsm_conn, lsm_disk[j],
                  LSM_CLIENT_FLAG_RSVD);
            break;
          }
        }
      }
      lsm_disk_record_array_free(lsm_disk, disk_count);
    } else {
      rc = -EOPNOTSUPP;
      goto free;
    }
  }
free:
  lsm_connect_close(lsm_conn, LSM_CLIENT_FLAG_RSVD);
  lsm_system_record_array_free(lsm_sys, sys_count);
  lsm_capability_record_free(lsm_cap);
  return rc;
}
 
int disable_locate_led(const char *uri, const char *dev_path)
{
  lsm_connect *lsm_conn = NULL;
  lsm_error *lsm_err = NULL;
  int rc = 0;
  uint32_t i = 0;
  uint32_t  j = 0;
  const char *lsm_password = NULL;
  uint32_t lsm_tmo = 3000;
  lsm_system **lsm_sys = NULL;
  uint32_t sys_count = 0;
  const char *sys_id = NULL;
  lsm_volume **lsm_vol = NULL;
  uint32_t vol_count = 0;
  lsm_disk **lsm_disk = NULL;
  uint32_t disk_count = 0;
  const char *lsm_dev_path = NULL;
  lsm_system_mode_type sys_mode = LSM_SYSTEM_MODE_NO_SUPPORT;
  lsm_storage_capabilities *lsm_cap = NULL;
  char basename[PATH_MAX];

  rc = lsm_connect_password(uri, lsm_password, &lsm_conn, lsm_tmo,
	&lsm_err, LSM_CLIENT_FLAG_RSVD);

  if (rc)
    return rc; 

  rc = lsm_system_list(lsm_conn, &lsm_sys, &sys_count, LSM_CLIENT_FLAG_RSVD);

  if (rc)
    return rc;

  for (i = 0; i<sys_count; i++) {

    rc = lsm_capabilities(lsm_conn, lsm_sys[i], &lsm_cap,
          LSM_CLIENT_FLAG_RSVD);

    if (rc)
      goto free;

    if (!lsm_capability_get(lsm_cap, LSM_CAP_SYS_MODE_GET))
      goto free;

    rc = lsm_system_mode_get(lsm_sys[i], &sys_mode);

    if (rc)
      goto free;

    sys_id = lsm_system_id_get(lsm_sys[i]);

    if (sys_mode == LSM_SYSTEM_MODE_HARDWARE_RAID) {

    if (!lsm_capability_get(lsm_cap, LSM_CAP_VOLUMES))
      goto free;

      rc = lsm_volume_list(lsm_conn, "system_id", sys_id, &lsm_vol, &vol_count,
            LSM_CLIENT_FLAG_RSVD);

      if (rc)
        goto free;

      if (lsm_capability_get(lsm_cap, LSM_CAP_VOLUME_SD_PATH) &&
          lsm_capability_get(lsm_cap, LSM_CAP_VOLUME_LED)) {

        for (j = 0; j<vol_count; j++) {

          rc = lsm_volume_sd_path_get(lsm_vol[j], &lsm_dev_path);
          rc = get_block_device_base(lsm_dev_path, basename, sizeof(basename));
          if (strcmp(basename, dev_path)) {
            continue;
          } else {
            rc = lsm_volume_ident_led_clear(lsm_conn, lsm_vol[j],
                  LSM_CLIENT_FLAG_RSVD);
            break;
          }
        }
      }
      lsm_volume_record_array_free(lsm_vol, vol_count);
    } else if (sys_mode == LSM_SYSTEM_MODE_HBA) {
      rc = lsm_disk_list(lsm_conn, "system_id", sys_id, &lsm_disk, &disk_count,
            LSM_CLIENT_FLAG_RSVD);

      if (rc)
        goto free;

      if (lsm_capability_get(lsm_cap, LSM_CAP_DISK_SD_PATH) &&
          lsm_capability_get(lsm_cap, LSM_CAP_DISK_LED)) {

        for (j = 0; j<disk_count; j++) {
          rc = lsm_disk_sd_path_get(lsm_disk[j], &lsm_dev_path);
          rc = get_block_device_base(lsm_dev_path, basename, sizeof(basename));
          if (strcmp(basename, dev_path)) {
            continue;
          } else {
            rc = lsm_disk_clear_ident_led(lsm_conn, lsm_disk[j],
                  LSM_CLIENT_FLAG_RSVD);
            break;
          }
        }
      }
      lsm_disk_record_array_free(lsm_disk, disk_count);
    } else {
      rc = -EOPNOTSUPP;
      goto free;
    }
  }
free:
  lsm_connect_close(lsm_conn, LSM_CLIENT_FLAG_RSVD);
  lsm_system_record_array_free(lsm_sys, sys_count);
  lsm_capability_record_free(lsm_cap);
  return rc;
} 
#elif defined(__APPLE__)
#include <sys/disk.h>

int get_block_device_size(int fd, int64_t *psize)
{
  unsigned long blocksize = 0;
  int ret = ::ioctl(fd, DKIOCGETBLOCKSIZE, &blocksize);
  if (!ret) {
    unsigned long nblocks;
    ret = ::ioctl(fd, DKIOCGETBLOCKCOUNT, &nblocks);
    if (!ret)
      *psize = (int64_t)nblocks * blocksize;
  }
  if (ret < 0)
    ret = -errno;
  return ret;
}

bool block_device_support_discard(const char *devname)
{
  return false;
}

int block_device_discard(int fd, int64_t offset, int64_t len)
{
  return -EOPNOTSUPP;
}

int get_device_by_uuid(uuid_d dev_uuid, const char* label, char* partition,
	char* device)
{
  return -EOPNOTSUPP;
}

int get_device_by_symlink(const char* symlink, char* device)
{
  return -EOPNOTSUPP;
}

int enable_locate_led(const char *dev_path)
{
  return -EOPNOTSUPP;
}

int disable_locate_led(const char *dev_path)
{
  return -EOPNOTSUPP;
}
#elif defined(__FreeBSD__)
#include <sys/disk.h>

int get_block_device_size(int fd, int64_t *psize)
{
  int ret = ::ioctl(fd, DIOCGMEDIASIZE, psize);
  if (ret < 0)
    ret = -errno;
  return ret;
}

bool block_device_support_discard(const char *devname)
{
  return false;
}

int block_device_discard(int fd, int64_t offset, int64_t len)
{
  return -EOPNOTSUPP;
}

int get_device_by_uuid(uuid_d dev_uuid, const char* label, char* partition,
	char* device)
{
  return -EOPNOTSUPP;
}

int get_device_by_symlink(const char* symlink, char* device)
{
  return -EOPNOTSUPP;
}

int enable_locate_led(const char *dev_path)
{
  return -EOPNOTSUPP;
}

int disable_locate_led(const char *dev_path)
{
  return -EOPNOTSUPP;
}
#else
int get_block_device_size(int fd, int64_t *psize)
{
  return -EOPNOTSUPP;
}

bool block_device_support_discard(const char *devname)
{
  return false;
}

int block_device_discard(int fd, int64_t offset, int64_t len)
{
  return -EOPNOTSUPP;
}

int get_device_by_uuid(uuid_d dev_uuid, const char* label, char* partition,
	char* device)
{
  return -EOPNOTSUPP;
}
#endif
