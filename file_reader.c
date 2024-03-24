#include "file_reader.h"

struct clusters_chain_t *get_chain_fat12( void * buffer, size_t size, uint16_t first_cluster) {
    if (buffer == NULL || size <= 0)
        return NULL;

    struct clusters_chain_t *clusters_chain = malloc(sizeof(struct clusters_chain_t));
    if (!clusters_chain)
        return NULL;

    clusters_chain->clusters = NULL;
    clusters_chain->size = 0;

    uint16_t current_cluster = first_cluster;
    while (current_cluster >= 0x002 && current_cluster <= 0xFF6) {
        uint16_t *temp_clusters = realloc(clusters_chain->clusters, (clusters_chain->size + 1) * sizeof(uint16_t));
        if (temp_clusters == NULL) {
            free(clusters_chain->clusters);
            free(clusters_chain);
            return NULL;
        }
        clusters_chain->clusters = temp_clusters;
        clusters_chain->clusters[clusters_chain->size] = current_cluster;
        clusters_chain->size++;

        int offset = current_cluster + (current_cluster / 2);
        if (current_cluster % 2 == 1) {
            current_cluster = *((const uint16_t *) ((const char *) buffer + offset)) >> 4;
        } else {
            current_cluster = *((const uint16_t *) ((const char *) buffer + offset)) & 0xFFF;
        }
    }
    return clusters_chain;
}

struct disk_t* disk_open_from_file(const char* volume_file_name){
    if (volume_file_name==NULL) {
        errno = EFAULT;
        return NULL;
    }
    struct disk_t *unpacked_disk = malloc(sizeof(struct disk_t));
    if (unpacked_disk==NULL) {
        errno = ENOMEM;
        return NULL;
    }
    unpacked_disk->f = fopen(volume_file_name, "rb");
    if (unpacked_disk->f == NULL) {
        errno = ENOENT;
        free(unpacked_disk);
        return NULL;
    }
    char trash[SECTOR_LEN];
    unpacked_disk->sector_count = 0;
    while (fread(trash, SECTOR_LEN, 1, unpacked_disk->f)) {
        unpacked_disk->sector_count++;
    }
    return unpacked_disk;
}
int disk_close(struct disk_t* pdisk){
    if (pdisk == NULL){
        errno = EFAULT;
        return -1;
    }
    fclose(pdisk->f);
    free(pdisk);
    return 0;
}
int disk_read(struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read){
    if (buffer == NULL || pdisk == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (pdisk->sector_count<(uint32_t)(sectors_to_read+first_sector)){
        errno = ERANGE;
        return -1;
    }
    fseek(pdisk->f,SECTOR_LEN*first_sector,SEEK_SET);
    fread(buffer,SECTOR_LEN, sectors_to_read,pdisk->f);
    return sectors_to_read;
}

struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector){
    if (pdisk == NULL){
        errno = EFAULT;
        return NULL;
    }
    struct volume_t* volume = (struct volume_t*)malloc(sizeof(struct volume_t));
    if (volume == NULL){
        errno = ENOMEM;
        return NULL;
    }
    volume->disk = pdisk;
    int8_t code_read = (int8_t)disk_read(volume->disk, (int32_t)first_sector, &volume->boot_info,1);
    if (code_read == -1){
        errno = EINVAL;
        free(volume);
        return NULL;
    }
    if (volume->boot_info.sig != 0xAA55){
        errno=EINVAL;
        free(volume);
        return NULL;
    }
    volume->root_dir = malloc(volume->boot_info.root_dir_file_cap*sizeof(struct dir_entry_raw_t));
    if (volume->root_dir == NULL){
        errno = ENOMEM;
        free(volume);
        return NULL;
    }
    volume->table = malloc(volume->boot_info.fat_size * volume->boot_info.bytes_per_sector);
    if (volume->table == NULL){
        errno = ENOMEM;
        free(volume->root_dir);
        free(volume);
        return NULL;
    }
    volume->backup_table = malloc(volume->boot_info.fat_size * volume->boot_info.bytes_per_sector);
    if (volume->backup_table == NULL){
        errno = ENOMEM;
        free(volume->root_dir);
        free(volume->table);
        free(volume);
        return NULL;
    }
    if (disk_read(pdisk, volume->boot_info.reversed_sectors_area, volume->table, volume->boot_info.fat_size)==-1 ||
        disk_read(pdisk, volume->boot_info.fat_size+volume->boot_info.reversed_sectors_area, volume->backup_table,volume->boot_info.fat_size)==-1 ){
        errno = EFAULT;
        free(volume->root_dir);
        free(volume->table);
        free(volume);
        return NULL;
    }
    if (0 != memcmp(volume->table,volume->backup_table, volume->boot_info.fat_size)){
        errno = ENOMEM;
        free(volume->root_dir);
        free(volume->table);
        free(volume->backup_table);
        free(volume);
        return NULL;
    }
    if (disk_read(pdisk, volume->boot_info.fat_size * 2+ volume->boot_info.reversed_sectors_area,
                  volume->root_dir,(int32_t )sizeof(struct dir_entry_raw_t) * volume->boot_info.root_dir_file_cap / volume->boot_info.bytes_per_sector)==-1 ){
        errno = EFAULT;
        free(volume->root_dir);
        free(volume->table);
        free(volume->backup_table);
        free(volume);
        return NULL;
    }

    return volume;
}




int fat_close(struct volume_t* volume){
    if (volume==NULL){
        errno = EFAULT;
        return -1;
    }

    free(volume->root_dir);
    free(volume->table);
    free(volume->backup_table);
    free(volume);

    return 0;
}
void fix_dir_name(char* root_name,char* root_name_with_a_dot){
    uint8_t name_len = 0;
    for (; name_len < 8; name_len++) {
        if (root_name[name_len] == 32) {
            break;
        }
        root_name_with_a_dot[name_len] = root_name[name_len];
    }
    root_name_with_a_dot[name_len] = '.';
    uint8_t ext_ix = 8;
    for (; ext_ix < 11; ext_ix++) {
        if (root_name[ext_ix] == 32) {
            break;
        }
        root_name_with_a_dot[++name_len] = root_name[ext_ix];
    }
    root_name_with_a_dot[name_len+1] = '\0';
    if (ext_ix==8){
        root_name_with_a_dot[name_len] = '\0';
    }
}
struct file_t *file_open(struct volume_t *pvolume, const char *file_name) {

    if (pvolume == NULL || file_name == NULL) {
        errno = EFAULT;
        return NULL;
    }
    struct file_t *file_out = malloc(sizeof(struct file_t));
    if (file_out == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    file_out->current_position = 0;
    file_out->helper_pos=0;
    file_out->volume = pvolume;
    struct dir_entry_raw_t *root_dir_casted = pvolume->root_dir;
    int name_there = 0;

    char dirname_with_a_dot[13];
    int i = 0;
    for (; i < pvolume->boot_info.root_dir_file_cap; i++) {
        fix_dir_name((root_dir_casted+i)->name,dirname_with_a_dot);
        if (strcmp(file_name, dirname_with_a_dot) == 0) {
            if (((root_dir_casted+i)->attributes >> 4) & 1) {
                errno = EISDIR;
                free(file_out);
                return NULL;
            }
            else{
                name_there = 1;
                file_out->size = (*(root_dir_casted+i)).size;
                break;
            }
        }
    }
    if (name_there == 0){
        free(file_out);
        errno = ENOENT;
        return NULL;
    }
    unsigned char cluster_bytes[2] = {(root_dir_casted+i)->cluster_part2_1,(root_dir_casted+i)->cluster_part2_2};
    uint16_t cluster;
    memcpy(&cluster, cluster_bytes, sizeof(uint16_t));
    file_out->clusters_chain = get_chain_fat12(pvolume->table, pvolume->boot_info.fat_size * pvolume->boot_info.bytes_per_sector,cluster);

    if (file_out->clusters_chain == NULL) {
        errno = ENOMEM;
        free(file_out);
        return NULL;
    }
    return file_out;
}


int file_close(struct file_t* stream){
    if (stream==NULL){
        errno = EFAULT;
        return -1;
    }

    free(stream->clusters_chain->clusters);
    free(stream->clusters_chain);
    free(stream);
    return 0;
}
int32_t file_seek(struct file_t* stream, int32_t offset, int whence){
    if (stream == NULL){
        errno = EFAULT;
        return -1;
    }
    if (whence == SEEK_CUR){
        if ((uint32_t )offset+stream->current_position>stream->size){
            errno=ENXIO;
            return -1;
        }
        stream->current_position+=offset;
    }
    else if (whence == SEEK_SET){
        if ((uint32_t)offset>stream->size){
            errno=ENXIO;
            return -1;
        }
        stream->current_position=offset;
    }
    else if (whence == SEEK_END){
        if ((int32_t)stream->size+offset<0){
            errno=ENXIO;
            return -1;
        }
        stream->current_position=stream->size + offset;
    }
    else{
        errno=EINVAL;
        return -1;
    }
    stream->helper_pos = stream->current_position%2048;
    return 0;
}
int calculate_current_sector(struct file_t *stream){
    return stream->volume->boot_info.root_dir_file_cap*(int)sizeof(struct dir_entry_raw_t)/stream->volume->boot_info.bytes_per_sector+
           stream->volume->boot_info.fat_size*stream->volume->boot_info.fat_count
           +(stream->clusters_chain->clusters[stream->current_position / (stream->volume->boot_info.bytes_per_sector * stream->volume->boot_info.sectors_per_cluster)]-2)
            *stream->volume->boot_info.sectors_per_cluster+stream->volume->boot_info.reversed_sectors_area;
}

size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream) {
    if (ptr == NULL  || stream == NULL) {
        errno = EFAULT;
        return -1;
    }
    size_t sector_bytes = SECTOR_LEN*stream->volume->boot_info.sectors_per_cluster;
    if ( nmemb == 1){
        if (stream->current_position >= stream->size) {
            return 0;
        }
        int sector_index= calculate_current_sector(stream);
        if (stream->helper_pos+size>sector_bytes){
            if (size!=1){
                fseek(stream->volume->disk->f, SECTOR_LEN * sector_index + stream->helper_pos, SEEK_SET);
                int to_read = sector_bytes-stream->helper_pos;
                fread(ptr,to_read,1,stream->volume->disk->f);
                stream->helper_pos=0;
                stream->current_position+=to_read;
                if (stream->current_position >= stream->size) {
                    return 0;
                }
                sector_index= calculate_current_sector(stream);
                fseek(stream->volume->disk->f, SECTOR_LEN * sector_index + stream->helper_pos, SEEK_SET);
                fread((char*)ptr+to_read,size-to_read,1,stream->volume->disk->f);
                stream->current_position+=size-to_read;
                stream->helper_pos+=size-to_read;
                if (stream->current_position >= stream->size) {
                    return 0;
                }
                return 1;
            }
            stream->helper_pos=0;
        }
        fseek(stream->volume->disk->f, SECTOR_LEN * sector_index + stream->helper_pos, SEEK_SET);
        stream->helper_pos+=size;
        if (stream->size-stream->current_position<size){
            fread(ptr,stream->size-stream->current_position,1,stream->volume->disk->f);
            stream->current_position+=stream->size-stream->current_position;
            return 0;
        }
        else {
            fread(ptr, size, 1, stream->volume->disk->f);
            stream->current_position += size;
        }
        return 1;
    }

    size_t bytes_read = 0;
    int check = 0;

    while (1) {
        if (stream->current_position >= stream->size || bytes_read >= size * nmemb) {
            break;
        }
        int sector_index= calculate_current_sector(stream);
        if ((size*nmemb)-bytes_read>=sector_bytes) {
            if (disk_read(stream->volume->disk, sector_index, ((char *) ptr + bytes_read), stream->volume->boot_info.sectors_per_cluster) == -1) {
                errno = ERANGE;
                return 1;
            }
            bytes_read+=sector_bytes;
            stream->current_position+=sector_bytes;
        }
        else{
            if (check == 0){
                check = 1;
                fseek(stream->volume->disk->f,SECTOR_LEN*sector_index,SEEK_SET);
            }
            size_t read_out = fread((char *)ptr + bytes_read, size, 1, stream->volume->disk->f);
            bytes_read+=read_out;
            stream->current_position+=read_out;
        }
    }

    return bytes_read / size;
}





struct dir_t* dir_open(struct volume_t* pvolume, const char* dir_path){
    if (pvolume==NULL) {
        errno = EFAULT;
        return NULL;
    }
    struct dir_t *dir_handle = (struct dir_t *)malloc(sizeof(struct dir_t));
    if(dir_handle == NULL){
        errno=ENOMEM;
        return NULL;
    }
    if (dir_path == NULL || 0 != strcmp(dir_path,"\\")){
        errno = ENOENT;
        free(dir_handle);
        return NULL;
    }

    dir_handle->size = pvolume->boot_info.root_dir_file_cap;
    dir_handle->info = pvolume->root_dir;
    dir_handle->current_position = 0;
    return dir_handle;

}
int dir_close(struct dir_t* pdir){
    if (pdir==NULL){
        errno = EFAULT;
        return -1;
    }
    free(pdir);
    return 0;
}
void decode_attributes(uint8_t attributes, struct dir_entry_t *entry) {
    entry->is_archived = (attributes >> 5) & 1;
    entry->is_readonly = (attributes >> 0) & 1;
    entry->is_system = (attributes >> 2) & 1;
    entry->is_hidden = (attributes >> 1) & 1;
    entry->is_directory = (attributes >> 4) & 1;
}


int dir_read(struct dir_t* pdir, struct dir_entry_t* pentry){
    if(pdir == NULL || pentry == NULL){
        errno = EFAULT;
        return -1;
    }
    struct dir_entry_raw_t *directory_raw=pdir->info;
    int index_works=0;
    for(;pdir->current_position<pdir->size;){
        if(directory_raw[pdir->current_position].name[0]!=(char)0xE5 && directory_raw[pdir->current_position].name[0]!=0x00){
            index_works = 1;
        }
        pdir->current_position++;
        if(index_works){
            break;
        }
    }
    if(!index_works){
        return 1;
    }
    char root_name_with_a_dot[13];
    fix_dir_name((directory_raw+pdir->current_position-1)->name,root_name_with_a_dot);
    strcpy(pentry->name,root_name_with_a_dot);
    decode_attributes((directory_raw+pdir->current_position-1)->attributes, pentry);
    pentry->size = (directory_raw+pdir->current_position-1)->size;

    return 0;
}


