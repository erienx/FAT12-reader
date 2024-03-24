#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>

#define SECTOR_LEN 512
struct disk_t{
    uint32_t sector_count;
    FILE* f;
};
struct dir_entry_t{
    char name[13];
    int is_archived;
    int is_readonly;
    int is_system;
    int is_hidden;
    int is_directory;
    size_t size;
};
struct dir_entry_raw_t
{
    char name[11];
    uint8_t attributes;
    uint8_t unused;
    uint8_t file_creation_time;
    uint16_t creation_date;
    uint16_t creation_time;
    uint16_t access_date;
    uint8_t cluster_part1_1;
    uint8_t cluster_part1_2;
    uint16_t modification_date;
    uint16_t modification_time;
    uint8_t cluster_part2_1;
    uint8_t cluster_part2_2;
    uint32_t size;
}__attribute__((__packed__));
struct fat_boot_info{
    uint8_t jump_code[3];
    char name_oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reversed_sectors_area;
    uint8_t fat_count;
    uint16_t root_dir_file_cap;
    uint16_t logical_sectors16;
    uint8_t media_type;
    uint16_t fat_size;
    uint16_t sectors_per_track;
    uint16_t number_of_heads;
    uint32_t number_of_sectors_before_partition;
    uint32_t number_of_sectors_in_filesystem;
    uint8_t drive_number;
    uint8_t filler;
    uint8_t boot_sig;
    uint32_t serial_number;
    char volume_label[11];
    char type[8];
    uint8_t boot_code[448];
    uint16_t sig;
} __attribute__((packed));

struct volume_t{
    struct disk_t *disk;
    struct fat_boot_info boot_info;
    void *root_dir;
    void* backup_table;
    void *table;
};
struct clusters_chain_t {
    uint16_t *clusters;
    size_t size;
};
struct file_t{
    struct volume_t* volume;
    uint32_t current_position;
    uint32_t helper_pos;
    struct clusters_chain_t* clusters_chain;
    uint32_t size;
};
struct dir_t{
    uint32_t current_position;
    void *info;
    uint32_t size;
};

struct clusters_chain_t *get_chain_fat12( void * buffer, size_t size, uint16_t first_cluster);

struct dir_t* dir_open(struct volume_t* pvolume, const char* dir_path);
int dir_read(struct dir_t* pdir, struct dir_entry_t* pentry);
int dir_close(struct dir_t* pdir);

struct file_t* file_open(struct volume_t* vol, const char* file_name);
int file_close(struct file_t* stream);
size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream);
int32_t file_seek(struct file_t* stream, int32_t offset, int whence);

struct disk_t* disk_open_from_file(const char* volume_file_name);
int disk_close(struct disk_t* pdisk);
int disk_read(struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read);

struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector);
int fat_close(struct volume_t* volume);