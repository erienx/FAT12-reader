#include "file_reader.h"

int main() {

    struct disk_t *disk= disk_open_from_file("fat12_file.img");
    struct volume_t *volume= fat_open(disk,0);

    struct dir_t* dir = dir_open(volume, "\\");

    struct dir_entry_t entry;

    for (int i = 0; i < 13; ++i) {
        dir_read(dir, &entry);
        printf("%s\n",entry.name);
    }

    dir_close(dir);
    fat_close(volume);
    disk_close(disk);

    return 0;
}