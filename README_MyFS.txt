MyFS - Custom File System
Overview:
MyFS is a simplified, custom-built file system implemented in C++. It provides basic file system functionalities such as formatting, file creation (regular files and directories), directory listing, and internal storage management using a block device simulator.

Project Structure:
myfs.h: Header file containing class definitions and structure declarations for the file system.

myfs.cpp: Implementation of MyFS file system methods, including initialization, formatting, inode and block allocation, and file creation.

BlockDeviceSimulator: Simulates block device read/write operations.

Features:
- Custom file system with a defined header and superblock layout.

- Inode table and bitmap management.

- Support for directories and regular files.

- Directory traversal and creation using absolute paths.

- Basic error handling and structural validation.

How It Works:
Initialization:
On construction of MyFs, the block device is checked for an existing file system header (MYFS_MAGIC). If not found or the version is incorrect, the file system is automatically formatted.

Format:
The format() function initializes the file system:

- Writes a file system header and superblock.

- Sets up inode and data bitmaps.

- Initializes an empty inode table.

- Prepares space for directory entries and data blocks.

File Creation:
Files and directories are created using the create_file method:

- The path is split into components.

- Each directory in the path is validated or created as needed.

- The final component is created as a directory or regular file.

Directory Listing:
list_dir_inode() reads directory entries of a given inode and lists their names, types, and sizes.

Usage:
BlockDeviceSimulator blkdev;
MyFs myfs(&blkdev);

// Create a directory and a file
myfs.create_file("/dir1", true);            // Directory
myfs.create_file("/dir1/file.txt", false);  // File

Building & running:
g++ -o myfs main.cpp myfs.cpp
./myfs

Notes:
- All blocks and inodes are managed manually using bitmaps.

- The file system supports only basic hierarchical structure.

- Designed primarily for educational purposes.