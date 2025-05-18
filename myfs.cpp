#include "myfs.h"
#include <string.h>
#include <iostream>
#include <math.h>
#include <sstream>
#include <stdexcept>

const char* MyFs::MYFS_MAGIC = "MYFS";

MyFs::MyFs(BlockDeviceSimulator* blkdevsim_) : blkdevsim(blkdevsim_) {
    struct myfs_header header;
    blkdevsim->read(0, sizeof(header), (char*)&header);

    if (strncmp(header.magic, MYFS_MAGIC, sizeof(header.magic)) != 0 ||
        (header.version != CURR_VERSION)) {
        std::cout << "Did not find myfs instance on blkdev" << std::endl;
        std::cout << "Creating..." << std::endl;
        format();
        std::cout << "Finished!" << std::endl;
    }
}

void MyFs::format() {
    // Put the header in place
    struct myfs_header header;
    strncpy(header.magic, MYFS_MAGIC, sizeof(header.magic));
    header.version = CURR_VERSION;
    blkdevsim->write(0, sizeof(header), (const char*)&header);

    // Initialize superblock
    struct myfs_superblock superblock;
    superblock.block_size = BLOCK_SIZE;
    superblock.inode_table_size = INODE_TABLE_SIZE;
    superblock.inode_bitmap_size = ceil((double)INODE_TABLE_SIZE / (8 * BLOCK_SIZE));
    superblock.data_bitmap_size = 0;
    superblock.data_blocks = 0;
    superblock.data_start_block = 1 + superblock.inode_bitmap_size + superblock.data_bitmap_size;

    // Write superblock to block device
    blkdevsim->write(1, sizeof(superblock), (const char*)&superblock);

    // Initialize inode table
    struct myfs_inode inode;
    for (uint32_t i = 0; i < INODE_TABLE_SIZE; i++) {
        blkdevsim->write(
            superblock.data_start_block + i,
            sizeof(inode),
            (const char*)&inode
        );
    }

    // Update data bitmap size and data blocks count
    superblock.data_bitmap_size = ceil((double)(blkdevsim->DEVICE_SIZE - superblock.data_start_block) / BLOCK_SIZE / 8);
    superblock.data_blocks = blkdevsim->DEVICE_SIZE - superblock.data_start_block - superblock.data_bitmap_size;

    // Update superblock on block device
    blkdevsim->write(1, sizeof(superblock), (const char*)&superblock);

    // Initialize data bitmap
    std::vector<char> data_bitmap(superblock.data_bitmap_size * BLOCK_SIZE, 0);
    blkdevsim->write(
        1 + superblock.inode_bitmap_size,
        superblock.data_bitmap_size * BLOCK_SIZE,
        data_bitmap.data()
    );
}

void MyFs::create_file(std::string path_str, bool directory) {
    throw std::runtime_error("not implemented");

    // Check if path is valid
    if (path_str.empty() || path_str[0] != '/') {
        throw std::invalid_argument("Invalid path");
    }

    // Tokenize the path to get individual directory and file names
    std::istringstream iss(path_str);
    std::string token;
    std::vector<std::string> tokens;
    while (std::getline(iss, token, '/')) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }

    // Traverse the directory path and create necessary directories
    uint32_t current_inode = 0;  // Start with root directory inode
    for (size_t i = 0; i < tokens.size() - 1; i++) {
        dir_list dir_entries = list_dir_inode(current_inode);
        bool dir_found = false;
        for (const auto& entry : dir_entries) {
            if (entry.name == tokens[i] && entry.is_dir) {
                current_inode = entry.inode_num;
                dir_found = true;
                break;
            }
        }

        // If directory not found, create it
        if (!dir_found) {
            current_inode = create_directory(tokens[i], current_inode);
        }
    }

    // Create the file
    if (directory) {
        create_directory(tokens.back(), current_inode);
    }
    else {
        create_regular_file(tokens.back(), current_inode);
    }
}

MyFs::dir_list MyFs::list_dir_inode(uint32_t inode_num) {
    dir_list entries;
    struct myfs_inode inode;
    blkdevsim->read(1 + inode_num, sizeof(inode), (char*)&inode);

    if ((inode.mode & 0xF000) != 0x4000) {  // Check if inode is a directory
        throw std::invalid_argument("Not a directory");
    }

    for (size_t i = 0; i < 15; i++) {
        if (inode.blocks[i] == 0) {
            continue;
        }

        char block_data[BLOCK_SIZE];
        blkdevsim->read(inode.blocks[i], BLOCK_SIZE, block_data);

        for (size_t j = 0; j < BLOCK_SIZE / sizeof(struct myfs_dir_entry); j++) {
            struct myfs_dir_entry* dir_entry = (struct myfs_dir_entry*)(block_data + j * sizeof(struct myfs_dir_entry));
            if (dir_entry->inode_num == 0) {
                continue;
            }

            struct myfs_inode entry_inode;
            blkdevsim->read(1 + dir_entry->inode_num, sizeof(entry_inode), (char*)&entry_inode);

            struct dir_list_entry entry;
            entry.name = std::string(dir_entry->name, strnlen(dir_entry->name, 11)); // Limit to 10 characters
            entry.is_dir = (entry_inode.mode & 0xF000) == 0x4000;
            entry.file_size = entry_inode.size;
            entries.push_back(entry);
        }
    }

    return entries;
}

uint32_t MyFs::create_directory(std::string name, uint32_t parent_inode) {
    struct myfs_inode parent_inode_data;
    blkdevsim->read(1 + parent_inode, sizeof(parent_inode_data), (char*)&parent_inode_data);

    if ((parent_inode_data.mode & 0xF000) != 0x4000) {  // Check if parent inode is a directory
        throw std::invalid_argument("Parent is not a directory");
    }

    // Find a free inode
    uint32_t free_inode = find_free_inode();
    if (free_inode == 0) {
        throw std::runtime_error("No free inode available");
    }

    // Update inode for new directory
    struct myfs_inode dir_inode;
    dir_inode.mode = 0x4000;  // Directory mode
    dir_inode.uid = 0;  // User ID
    dir_inode.gid = 0;  // Group ID
    dir_inode.size = 0;  // File size
    memset(dir_inode.blocks, 0, sizeof(dir_inode.blocks));  // Clear data block pointers

    blkdevsim->write(1 + free_inode, sizeof(dir_inode), (const char*)&dir_inode);

    // Update directory entry in parent directory
    struct myfs_dir_entry dir_entry;
    strncpy(dir_entry.name, name.c_str(), sizeof(dir_entry.name));
    dir_entry.inode_num = free_inode;

    for (size_t i = 0; i < 15; i++) {
        if (parent_inode_data.blocks[i] == 0) {
            // Find a free block for directory entry
            uint32_t free_block = find_free_block();
            if (free_block == 0) {
                throw std::runtime_error("No free block available");
            }

            parent_inode_data.blocks[i] = free_block;
            blkdevsim->write(1 + parent_inode, sizeof(parent_inode_data), (const char*)&parent_inode_data);
        }

        char block_data[BLOCK_SIZE];
        blkdevsim->read(parent_inode_data.blocks[i], BLOCK_SIZE, block_data);

        for (size_t j = 0; j < BLOCK_SIZE / sizeof(struct myfs_dir_entry); j++) {
            struct myfs_dir_entry* entry = (struct myfs_dir_entry*)(block_data + j * sizeof(struct myfs_dir_entry));
            if (entry->inode_num == 0) {
                *entry = dir_entry;
                blkdevsim->write(parent_inode_data.blocks[i], BLOCK_SIZE, block_data);
                return free_inode;
            }
        }
    }

    throw std::runtime_error("No space in parent directory for new directory entry");
}

uint32_t MyFs::find_free_inode() {
    struct myfs_superblock superblock;
    blkdevsim->read(1, sizeof(superblock), (char*)&superblock);

    uint32_t inode_bitmap_blocks = superblock.inode_bitmap_size * BLOCK_SIZE / BLOCK_SIZE;
    std::vector<char> inode_bitmap(inode_bitmap_blocks * BLOCK_SIZE);
    blkdevsim->read(1 + 1, inode_bitmap_blocks * BLOCK_SIZE, inode_bitmap.data());

    for (uint32_t i = 0; i < inode_bitmap.size(); i++) {
        if (inode_bitmap[i] != 0xFF) {
            for (int j = 0; j < 8; j++) {
                if ((inode_bitmap[i] & (1 << j)) == 0) {
                    inode_bitmap[i] |= (1 << j);
                    blkdevsim->write(1 + 1, inode_bitmap_blocks * BLOCK_SIZE, inode_bitmap.data());
                    return i * 8 + j;
                }
            }
        }
    }

    return 0;
}

uint32_t MyFs::find_free_block() {
    struct myfs_superblock superblock;
    blkdevsim->read(1, sizeof(superblock), (char*)&superblock);

    uint32_t data_bitmap_blocks = superblock.data_bitmap_size * BLOCK_SIZE / BLOCK_SIZE;
    std::vector<char> data_bitmap(data_bitmap_blocks * BLOCK_SIZE);
    blkdevsim->read(1 + 1 + superblock.inode_bitmap_size, data_bitmap_blocks * BLOCK_SIZE, data_bitmap.data());

    for (uint32_t i = 0; i < data_bitmap.size(); i++) {
        if (data_bitmap[i] != 0xFF) {
            for (int j = 0; j < 8; j++) {
                if ((data_bitmap[i] & (1 << j)) == 0) {
                    data_bitmap[i] |= (1 << j);
                    blkdevsim->write(1 + 1 + superblock.inode_bitmap_size, data_bitmap_blocks * BLOCK_SIZE, data_bitmap.data());
                    return i * 8 + j + 1 + superblock.inode_bitmap_size;
                }
            }
        }
    }

    return 0;
}

void MyFs::create_regular_file(std::string name, uint32_t parent_inode) {
    struct myfs_inode parent_inode_data;
    blkdevsim->read(1 + parent_inode, sizeof(parent_inode_data), (char*)&parent_inode_data);

    if ((parent_inode_data.mode & 0xF000) != 0x4000) {  // Check if parent inode is a directory
        throw std::invalid_argument("Parent is not a directory");
    }

    // Find a free inode
    uint32_t free_inode = find_free_inode();
    if (free_inode == 0) {
        throw std::runtime_error("No free inode available");
    }

    // Update inode for new regular file
    struct myfs_inode file_inode;
    file_inode.mode = 0x8000;  // Regular file mode
    file_inode.uid = 0;  // User ID
    file_inode.gid = 0;  // Group ID
    file_inode.size = 0;  // File size
    memset(file_inode.blocks, 0, sizeof(file_inode.blocks));  // Clear data block pointers

    blkdevsim->write(1 + free_inode, sizeof(file_inode), (const char*)&file_inode);

    // Update directory entry in parent directory
    struct myfs_dir_entry dir_entry;
    strncpy(dir_entry.name, name.substr(0, 10).c_str(), sizeof(dir_entry.name)); // Limit to 10 characters
    dir_entry.inode_num = free_inode;

    for (size_t i = 0; i < 15; i++) {
        if (parent_inode_data.blocks[i] == 0) {
            // Find a free block for directory entry
            uint32_t free_block = find_free_block();
            if (free_block == 0) {
                throw std::runtime_error("No free block available");
            }

            parent_inode_data.blocks[i] = free_block;
            blkdevsim->write(1 + parent_inode, sizeof(parent_inode_data), (const char*)&parent_inode_data);
        }

        char block_data[BLOCK_SIZE];
        blkdevsim->read(parent_inode_data.blocks[i], BLOCK_SIZE, block_data);

        for (size_t j = 0; j < BLOCK_SIZE / sizeof(struct myfs_dir_entry); j++) {
            struct myfs_dir_entry* entry = (struct myfs_dir_entry*)(block_data + j * sizeof(struct myfs_dir_entry));
            if (entry->inode_num == 0) {
                *entry = dir_entry;
                blkdevsim->write(parent_inode_data.blocks[i], BLOCK_SIZE, block_data);
                return;
            }
        }
    }

    throw std::runtime_error("No space in parent directory for new directory entry");
}

std::string MyFs::get_content(std::string path_str) {
    // Check if path is valid
    if (path_str.empty() || path_str[0] != '/') {
        throw std::invalid_argument("Invalid path");
    }

    // Tokenize the path to get individual directory and file names
    std::istringstream iss(path_str);
    std::string token;
    std::vector<std::string> tokens;
    while (std::getline(iss, token, '/')) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }

    // Traverse the directory path to find the file
    uint32_t current_inode = 0;  // Start with root directory inode
    for (size_t i = 0; i < tokens.size(); i++) {
        dir_list dir_entries = list_dir_inode(current_inode);
        bool file_found = false;
        for (const auto& entry : dir_entries) {
            if (entry.name == tokens[i] && !entry.is_dir) {
                current_inode = entry.inode_num;
                file_found = true;
                break;
            }
        }

        // If file not found, return error
        if (!file_found) {
            throw std::invalid_argument("File not found");
        }
    }

    // Retrieve content of the file
    struct myfs_inode file_inode;
    blkdevsim->read(1 + current_inode, sizeof(file_inode), (char*)&file_inode);

    if ((file_inode.mode & 0xF000) != 0x8000) {  // Check if inode is a regular file
        throw std::invalid_argument("Not a regular file");
    }

    std::string content;
    for (size_t i = 0; i < 12 && file_inode.blocks[i] != 0; i++) {
        char block_data[BLOCK_SIZE];
        blkdevsim->read(file_inode.blocks[i], BLOCK_SIZE, block_data);
        content.append(block_data, BLOCK_SIZE);
    }

    return content;
}

void MyFs::set_content(std::string path_str, std::string content) {
    // Check if path is valid
    if (path_str.empty() || path_str[0] != '/') {
        throw std::invalid_argument("Invalid path");
    }

    // Tokenize the path to get individual directory and file names
    std::istringstream iss(path_str);
    std::string token;
    std::vector<std::string> tokens;
    while (std::getline(iss, token, '/')) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }

    // Traverse the directory path to find the file
    uint32_t current_inode = 0;  // Start with root directory inode
    for (size_t i = 0; i < tokens.size() - 1; i++) {
        dir_list dir_entries = list_dir_inode(current_inode);
        bool dir_found = false;
        for (const auto& entry : dir_entries) {
            if (entry.name == tokens[i] && entry.is_dir) {
                current_inode = entry.inode_num;
                dir_found = true;
                break;
            }
        }

        // If directory not found, return error
        if (!dir_found) {
            throw std::invalid_argument("Directory not found");
        }
    }

    // Find the file
    dir_list dir_entries = list_dir_inode(current_inode);
    uint32_t file_inode_num = 0;
    for (const auto& entry : dir_entries) {
        if (entry.name == tokens.back() && !entry.is_dir) {
            file_inode_num = entry.inode_num;
            break;
        }
    }

    if (file_inode_num == 0) {
        throw std::invalid_argument("File not found");
    }

    // Retrieve inode of the file
    struct myfs_inode file_inode;
    blkdevsim->read(1 + file_inode_num, sizeof(file_inode), (char*)&file_inode);

    if ((file_inode.mode & 0xF000) != 0x8000) {  // Check if inode is a regular file
        throw std::invalid_argument("Not a regular file");
    }

    // Calculate number of blocks needed
    size_t num_blocks = (content.length() + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // Allocate new blocks if necessary
    for (size_t i = 0; i < num_blocks; i++) {
        if (i < 12) {
            if (file_inode.blocks[i] == 0) {
                uint32_t free_block = find_free_block();
                if (free_block == 0) {
                    throw std::runtime_error("No free block available");
                }
                file_inode.blocks[i] = free_block;
            }
        }
        else {
            throw std::runtime_error("File too large");
        }
    }

    // Write content to blocks
    for (size_t i = 0; i < num_blocks; i++) {
        size_t bytes_to_write = std::min((size_t)BLOCK_SIZE, content.length() - i * BLOCK_SIZE);
        blkdevsim->write(file_inode.blocks[i], bytes_to_write, content.c_str() + i * BLOCK_SIZE);
    }

    // Update inode size
    file_inode.size = content.length();

    // Write updated inode back to disk
    blkdevsim->write(1 + file_inode_num, sizeof(file_inode), (const char*)&file_inode);
}

MyFs::dir_list MyFs::list_dir(std::string path_str) {
    myfs_path path = parse_path(path_str);
    struct table_entry dir_te;
    int dir_te_index;

    // from path (i.e /desktop/folder) to real address in memory
    deref_path(path, &dir_te, &dir_te_index);

    // check if the path ends in a folder (i.e /desktop/folder and not /desktop/file.txt)
    if ((dir_te.flags & (1 << TABLE_ENTRY_DIR)) == 0) {
        throw std::runtime_error("Cannot list_dir a file");
    }

    dir_list entries;

    // Read directory entries
    for (int i = 0; i < dir_te.size; i++) {
        struct table_entry te;
        blkdevsim->read((dir_te_index * BLOCK_SIZE) + (i * sizeof(struct table_entry)), sizeof(te), (char*)&te);

        if (te.id != 0) {  // If this table entry is in use
            dir_list_entry entry;
            entry.name = std::string(te.name);
            entry.is_dir = (te.flags & (1 << TABLE_ENTRY_DIR)) != 0;
            entry.file_size = te.size;
            entries.push_back(entry);
        }
    }

    return entries;
}

#endif
