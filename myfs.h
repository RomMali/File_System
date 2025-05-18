#ifndef MYFS_H
#define MYFS_H

#include "block_device_simulator.h"
#include <string>
#include <vector>

const uint32_t CURR_VERSION = 1;
const uint32_t BLOCK_SIZE = 1024;
const uint32_t INODE_TABLE_SIZE = 1024;

struct myfs_header {
    char magic[4];
    uint32_t version;
};

struct myfs_superblock {
    uint32_t block_size;
    uint32_t inode_table_size;
    uint32_t inode_bitmap_size;
    uint32_t data_bitmap_size;
    uint32_t data_blocks;
    uint32_t data_start_block;
};

struct myfs_inode {
    uint16_t mode;
    uint16_t uid;
    uint16_t gid;
    uint32_t size;
    uint32_t blocks[15];
};

struct myfs_dir_entry {
    char name[11];  // 10 characters + 1 null terminator
    uint32_t inode_num;
};

class MyFs {
public:
    struct dir_list_entry {
        std::string name;
        bool is_dir;
        uint32_t file_size;
    };
    typedef std::vector<dir_list_entry> dir_list;

    MyFs(BlockDeviceSimulator* blkdevsim_);

    void format();
    void create_file(std::string path_str, bool directory);
    dir_list list_dir(std::string path_str);
    std::string get_content(std::string path_str);
    void set_content(std::string path_str, std::string content);

private:
    BlockDeviceSimulator* blkdevsim;

    void create_regular_file(std::string name, uint32_t parent_inode);
    uint32_t find_free_inode();
    uint32_t find_free_block();
    dir_list list_dir_inode(uint32_t inode_num);
    uint32_t create_directory(std::string name, uint32_t parent_inode);

};

#endif
