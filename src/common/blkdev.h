#ifndef __CEPH_COMMON_BLKDEV_H
#define __CEPH_COMMON_BLKDEV_H

/* for testing purposes */
extern void set_block_device_sandbox_dir(const char *dir);

extern int get_block_device_base(const char *dev, char *out, size_t out_len);
extern int get_block_device_size(int fd, int64_t *psize);
extern int64_t get_block_device_int_property(const char *devname, const char *property);
extern bool block_device_support_discard(const char *devname);
extern int block_device_discard(int fd, int64_t offset, int64_t len);
extern int get_device_by_uuid(uuid_d dev_uuid, const char* label,
		char* partition, char* device);
extern int get_device_by_symlink(const char* symlink, char* device);
extern int enable_locate_led(const char *uri, const char *dev_path);
extern int disable_locate_led(const char *uri, const char *dev_path);
#endif
