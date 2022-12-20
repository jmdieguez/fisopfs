#define FUSE_USE_VERSION 30

#include <unistd.h>
#include <fuse.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include "fisopfs.h"

char file_name[MAX_FILE_NAME_SIZE] = "file_system.fisopfs";

struct superblock *sb;
struct bmap_inodes *bitmap_inodes;
struct bmap_blocks *bitmap_blocks;
struct inode *inodes;
struct block *blocks;
struct file *files;
struct dirent *dirs;

int
check_read_permissions(struct inode *inode)
{
	struct fuse_context *context = fuse_get_context();
	int access;
	if (context->uid == inode->st_uid) {
		access = inode->st_mode & S_IRUSR;
	} else if (context->uid == inode->st_gid) {
		access = inode->st_mode & S_IRGRP;
	} else {
		access = inode->st_mode & S_IROTH;
	}

	if (access == 0) {
		printf("[debug] permission denied \n");
		return 0;
	}

	return 1;
}

int
check_write_permissions(struct inode *inode)
{
	struct fuse_context *context = fuse_get_context();
	int access;
	if (context->uid == inode->st_uid) {
		access = inode->st_mode & S_IWUSR;
	} else if (context->uid == inode->st_gid) {
		access = inode->st_mode & S_IWGRP;
	} else {
		access = inode->st_mode & S_IWOTH;
	}
	if (access == 0) {
		printf("[debug] permission denied \n");
		return 0;
	}

	return 1;
}

int
get_name_index(const char *path)
{
	int len = (int) strlen(path);

	int trunc = -1;
	for (int c = len - 1; c >= 0;
	     c--) {  // Iterate through path starting from end of string
		if (path[c] == '/') {  // Look for '/'
			trunc = c;     // Save index
			break;
		}
	}

	if (len == 0 || trunc == -1)  // If path is NULL or slash not found
		return 0;             // Return 0. It may be a root dir file.

	if (trunc == 0)
		trunc = 1;

	return trunc + 1;
}

int
init_inode(mode_t mode)
{
	for (int i = 0; i < N_INODES; i++) {
		if (!bitmap_inodes->free_inodes[i]) {  // If inode is free
			bitmap_inodes->free_inodes[i] =
			        1;  // Set inode as occupied in bitmap

			inodes[i].st_mode = mode;
			inodes[i].st_nlink = 0;
			inodes[i].st_uid = getuid();
			inodes[i].st_gid = getgid();
			inodes[i].st_size = 0;
			inodes[i].st_blocks = 0;

			inodes[i].st_atime = inodes[i].st_mtime =
			        inodes[i].st_ctime = time(NULL);

			inodes[i].ref = -1;

			return i;
		}
	}

	printf("[debug] ran out of inodes\n");

	return -1;
}

int
init_file(const char *path, mode_t mode)
{
	path++;
	int i = init_inode(mode);

	if (i > -1) {
		struct file new_file;  // Initialize new file
		new_file.d_ino = i;
		strcpy(new_file.path, path);
		strcpy(new_file.filename, path + get_name_index(path));
		printf("[debug] Filename: %s \n", new_file.filename);
		files[sb->n_files] = new_file;  // Save file in array

		return sb->n_files++;
	}

	return -1;
}

int
init_block()
{
	for (int i = 0; i < N_BLOCKS; i++) {
		if (!bitmap_blocks->free_blocks[i]) {
			bitmap_blocks->free_blocks[i] =
			        1;  // Set block as occupied
			blocks[i].free_space = BLOCK_SIZE;
			strcpy(blocks[i].content, "");
			blocks[i].ref = -1;
			return i;
		}
	}

	return -1;
}

// get_dir(path);
// recv: abs path to new file
// return: dir where file is being created

struct dirent *
get_dir(const char *path)
{
	int len = (int) strlen(path);
	int slashs = 0;
	int first_slash = 0;
	for (int c = len - 1; c >= 0; c--) {  // Search through path for '/'
		if (path[c] == '/') {
			if (slashs == 0) {  // Save first slash
				first_slash = c;
				printf("[debug] First slash found on: "
				       "path[%d]\n",
				       first_slash);
			}
			slashs++;  // Update number of slashes
		}
	}

	if (slashs == 0)
		return NULL;  // Slash not found: dir not found

	else if (slashs == 1)
		return &dirs[0];  // One slash found (must be on path[0]) : dir = root

	else {
		char *aux;
		aux = malloc(first_slash);
		strncpy(aux,
		        path + 1,
		        first_slash - 1);  // Copy the calculated dir path

		aux[first_slash - 1] = '\0';
		aux = strtok(aux, "\uFFFD");
		printf("[debug] Looking for directory: %s \n", aux);
		printf("[debug] Strlen: %ld \n", strlen(aux));
		for (int i = 0; i < sb->n_dirs; i++) {
			printf("[debug] Comparing against: %s \n", dirs[i].path);
			printf("[debug] Strlen: %ld \n", strlen(dirs[i].path));
			if (strcmp(aux, dirs[i].path) == 0) {
				free(aux);
				return &dirs[i];
			}
		}
	}
	printf("[debug] Directory not found\n");
	return NULL;
}

int
add_file(const char *filename, mode_t mode)
{
	struct dirent *dir = get_dir(filename);

	struct inode *inode = &inodes[dir->d_ino];

	if (!check_write_permissions(inode)) {
		return 0;
	}

	int n_file = init_file(filename, mode);

	if (dir && (n_file >= 0))
		dir->files[dir->n_files++] = n_file;
	else {
		printf("[debug] ERROR while creating file \n");
		return 0;
	}

	return 1;
}

int
get_file_index(const char *path)
{
	path++;

	for (int i = 0; i <= sb->n_files; i++)
		if (strcmp(path, files[i].path) == 0)
			return i;

	return -1;
}

int
get_dir_index(const char *path)
{
	path++;

	for (int i = 0; i <= sb->n_dirs; i++)
		if (strcmp(path, dirs[i].path) == 0)
			return i;

	return -1;
}

int
is_dir(const char *path)
{
	path++;

	for (int i = 0; i <= N_INODES; i++) {  // Iterate through dirs
		if (strcmp(path, dirs[i].path) ==
		    0)  // Check if path equals to dir path
			return 1;
	}
	return 0;
}

int
is_file(const char *path)
{
	path++;

	for (int i = 0; i < sb->n_files; i++) {
		if (strcmp(path, files[i].path) == 0)
			return 1;
	}

	return 0;
}

void
load_file_system(FILE *file)
{
	if (fread(sb, sizeof(struct superblock), 1, file) <= 0) {
		printf("error reading loading file: %s", file_name);
	}
	if (fread(bitmap_inodes, sizeof(struct bmap_inodes), 1, file) <= 0) {
		printf("error reading loading file: %s", file_name);
	}
	if (fread(bitmap_blocks, sizeof(struct bmap_blocks), 1, file) <= 0) {
		printf("error reading loading file: %s", file_name);
	};
	if (fread(inodes, sizeof(struct inode), N_INODES, file) <= 0) {
		printf("error reading loading file: %s", file_name);
	}
	if (fread(blocks, sizeof(struct block), N_BLOCKS, file) <= 0) {
		printf("error reading loading file: %s", file_name);
	}
	if (fread(files, sizeof(struct file), N_INODES, file) <= 0) {
		printf("error reading loading file: %s", file_name);
	}
	if (fread(dirs, sizeof(struct dirent), N_INODES, file) <= 0) {
		printf("error reading loading file: %s", file_name);
	}

	fclose(file);
}

void *
fisopfs_init(struct fuse_conn_info *conn)
{
	printf("[debug] fisopfs_init() \n");

	char a[MAX_FILE_NAME_SIZE];
	printf("Enter a name of load system file , must finish .fisops or "
	       "press enter for default file\n");

	if (fgets(a, MAX_FILE_NAME_SIZE, stdin) <= 0) {
		printf("error when read stdin");
	}
	if (strstr(a, ".fisops") != 0) {
		printf("contine fisops\n");
		strcpy(file_name, a);
		printf("file name = %s\n", file_name);
	} else if (!strcmp(a, "\n") == 0) {
		printf("el nombre debe contener .fisops\n");
		exit(-1);
	}

	sb = calloc(1, sizeof(struct superblock));
	bitmap_inodes = calloc(1, sizeof(struct bmap_inodes));
	bitmap_blocks = calloc(1, sizeof(struct bmap_blocks));
	inodes = calloc(N_INODES, sizeof(struct inode));
	blocks = calloc(N_BLOCKS, sizeof(struct block));
	files = calloc(N_INODES, sizeof(struct file));
	dirs = calloc(N_INODES, sizeof(struct dirent));

	FILE *file = fopen(file_name, "r+");
	if (file != NULL) {
		load_file_system(file);
		printf("loaded SuperBlock - magic: %d\n", sb->magic);
		printf("loaded SuperBlock - ndirs: %d\n", sb->n_dirs);
		printf("loaded SuperBlock - nfils:%d\n", sb->n_files);
	} else {
		sb->magic = SUPERBLOCK_MAGIC;
		sb->n_dirs = 1;  // One dir: root
		sb->n_files = 0;

		struct dirent root;

		int i = init_inode(__S_IFDIR | 0775);

		if (i < 0) {
			printf("[debug] error while initializing root dir \n");
			exit(1);
		}

		root.d_ino = i;
		root.parent = -1;
		strcpy(root.path, "/");
		root.n_files = 0;
		root.n_dir = 0;
		root.level = 1;

		dirs[0] = root;

		if (sb->magic != SUPERBLOCK_MAGIC)
			exit(1);
	}

	return NULL;
}

void
save_file_system()
{
	FILE *file = fopen(file_name, "w+");

	// save super block
	fwrite(sb, sizeof(struct superblock), 1, file);
	// save bitmap nodes
	fwrite(bitmap_inodes, sizeof(struct bmap_inodes), 1, file);
	fwrite(bitmap_blocks, sizeof(struct bmap_blocks), 1, file);
	fwrite(inodes, sizeof(struct inode), N_INODES, file);
	fwrite(blocks, sizeof(struct block), N_BLOCKS, file);
	fwrite(files, sizeof(struct file), N_INODES, file);
	fwrite(dirs, sizeof(struct dirent), N_INODES, file);

	fclose(file);
}


void
fisopfs_destroy(void *a)
{
	printf("\n[debug] fisopfs_destroy() \n");

	save_file_system();
	free(sb);
	free(bitmap_inodes);
	free(bitmap_blocks);
	free(inodes);
	free(blocks);
	free(files);
	free(dirs);
}

static int
fisopfs_getattr(const char *path, struct stat *st)
{
	printf("\n[debug] fisopfs_getattr(%s) \n", path);

	ino_t i;
	struct inode *inode;

	if ((strcmp(path, "/") == 0) || is_dir(path)) {
		i = get_dir_index(path);
		inode = &inodes[dirs[i].d_ino];
		st->st_nlink = 3;

	} else if (is_file(path)) {
		i = get_file_index(path);
		inode = &inodes[files[i].d_ino];
		st->st_nlink = 1;
		st->st_size = inode->st_size;
	} else {
		return -ENOENT;
	}

	st->st_mode = inode->st_mode;
	st->st_ino = i;
	st->st_gid = inode->st_gid;
	st->st_uid = inode->st_uid;
	inode->st_atime = time(NULL);
	st->st_atime = inode->st_atime;
	st->st_mtime = inode->st_mtime;
	st->st_ctime = inode->st_ctime;
	st->st_blocks = inode->st_blocks;

	return 0;
}

static int
fisopfs_readdir(const char *path,
                void *buffer,
                fuse_fill_dir_t filler,
                off_t offset,
                struct fuse_file_info *fi)
{
	printf("\n[debug] fisopfs_readdir(%s) \n", path);

	filler(buffer, ".", NULL, 0);
	filler(buffer, "..", NULL, 0);

	struct dirent *dir = NULL;
	if (strcmp(path, "/") == 0)
		dir = &dirs[0];

	else {
		dir = &dirs[get_dir_index(path)];
	}

	if (dir != NULL) {
		if (!check_read_permissions(&inodes[dir->d_ino])) {
			return PERMISSION_DENIED;
		}

		for (int j = 0; j < dir->n_files; j++) {  // Fill files

			int n_file = dir->files[j];
			if (n_file == -1)
				continue;
			struct file *file = &files[n_file];

			if (file != NULL)
				filler(buffer, file->filename, NULL, 0);
		}

		for (int d = 0; d < sb->n_dirs; d++) {  // Fill dirs
			struct dirent *child = &dirs[d];
			if (child != NULL && (&dirs[child->parent] == dir))
				filler(buffer, child->dirname, NULL, 0);
		}
	}

	return 0;
}

/** Similar to create */
static int
fisopfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
	printf("\n[debug] fisopfs_mknod(%s) \n", path);

	if (!is_file(path) && strlen(path) < FS_FILENAME_LEN) {
		if (add_file(path, mode))
			return 0;

		else
			return PERMISSION_DENIED;
	}

	printf("\n[debug] file %s already exists, or name is too large!\n", path);

	return 1;
}

/** Create a file */
static int
fisopfs_create(const char *path, mode_t mode, struct fuse_file_info *info)
{
	printf("\n[debug] fisopfs_create(%s) \n", path);

	if (!is_file(path) && strlen(path) < FS_FILENAME_LEN) {
		if (add_file(path, mode))
			return 0;

		else
			return PERMISSION_DENIED;
	}

	printf("\n[debug] file %s already exists, or name is too large! \n", path);

	return 0 - EEXIST;
}

/** Read file */
static int
fisopfs_read(const char *path,
             char *buffer,
             size_t size,
             off_t offset,
             struct fuse_file_info *fi)
{
	printf("\n[debug] fisopfs_read(%s, %ld, %ld) \n", path, size, offset);

	int i = get_file_index(path);

	if (i < 0) {
		printf("[debug] read failed. does your file exist? \n");
		return 0;
	}

	struct file *file = &files[i];
	struct inode *inode = &inodes[file->d_ino];

	if (!check_read_permissions(inode)) {
		return PERMISSION_DENIED;
	}

	inode->st_atime = time(NULL);

	char *content;
	content = calloc(1, BLOCK_SIZE * inode->st_blocks);
	printf("[debug] reading content from %ld blocks \n", inode->st_blocks);
	strcpy(content, "");
	memset(buffer, 0, size);
	size_t len = 0;
	int id_block = inode->ref;

	if (id_block >= 0) {
		for (int j = 0; j < inode->st_blocks;
		     j++) {  // Iterate through blocks from an inode
			struct block *block = &blocks[id_block];
			printf("[debug] reading absolute block "
			       "%d\n",
			       id_block);
			len += BLOCK_SIZE - block->free_space;
			strncat(content,
			        block->content,
			        BLOCK_SIZE - block->free_space);  // Concatenate their content

			id_block = block->ref;
		}

		printf("[debug] len : %ld \n", len);

		memcpy(buffer, content,
		       len);  // Copy the content into buffer

		free(content);
	}

	return (int) size;
}

void
flush_blocks(struct inode *inode)
{
	printf("[debug] flushing blocks from inode %p \n", inode);

	int id_block = inode->ref;

	for (int j = 0; j < inode->st_blocks; j++) {
		printf("[debug] cleaning block %d of %ld\n",
		       j + 1,
		       inode->st_blocks);

		struct block *clean_block = &blocks[id_block];
		bitmap_blocks->free_blocks[id_block] =
		        0;  // Free block bitmap index*/
		memset(clean_block->content, 0, BLOCK_SIZE);
		strcpy(clean_block->content, "");  // Clean datablocks
		clean_block->free_space = BLOCK_SIZE;
		id_block = clean_block->ref;
		clean_block->ref = -1;
	}
	inode->st_blocks = 0;
	inode->ref = -1;
	inode->st_size = 0;
}

void
write_content(struct inode *inode, const char *buffer, size_t size)
{
	int written = 0;
	int id_block = inode->ref;
	struct block *block = NULL;
	struct block *aux = NULL;

	if (id_block >= 0) {
		aux = &blocks[id_block];
	}

	while (written != size) {
		if (id_block >= 0)
			block = &blocks[id_block];

		if (!block ||  // If memory needed
		    block->free_space == 0) {
			if (inode->st_blocks < N_BLOCKS_INODE) {
				id_block = init_block();  // Initialize block

				block = &blocks[id_block];

				if (aux)
					aux->ref = id_block;

				else
					inode->ref = id_block;

				aux = block;
				printf("[debug] Initialize "
				       "block n: %d \n",
				       id_block);

				printf("[debug] Inode %p now "
				       "has %ld blocks "
				       "assigned\n",
				       inode,
				       ++inode->st_blocks);

			} else {
				printf("[debug] Inode %p can't "
				       "initialize more "
				       "blocks\n",
				       inode);
				break;
			}
		}

		int block_offset = BLOCK_SIZE - block->free_space;

		printf("[debug] writing absolute block %d\n", id_block);

		if (size > block->free_space) {
			strncpy(block->content + block_offset,
			        buffer + written,
			        block->free_space);
			written += block->free_space;
			block->free_space = 0;
		}

		else {
			strncpy(block->content + block_offset,
			        buffer + written,
			        size);
			written += (int) size;
			block->free_space -= (int) size;
		}

		id_block = blocks[id_block].ref;
	}

	inode->st_size += written;
}

/** Write to file */
static int
fisopfs_write(const char *path,
              const char *buffer,
              size_t size,
              off_t offset,
              struct fuse_file_info *info)
{
	printf("\n[debug] fisopfs_write(%s) \n", path);
	printf("[debug] writing %ld bytes in %s \n", size, path);
	printf("[debug] offset: %ld \n", offset);

	int i = get_file_index(path);

	if (i < 0) {
		printf("[debug] write failed. does your file exist? \n");
		return -1;
	}

	struct file *file = &files[i];

	printf("[debug] found %s \n", file->path);
	struct inode *inode = &inodes[file->d_ino];

	if (!check_write_permissions(inode)) {
		return PERMISSION_DENIED;
	}

	inode->st_atime = time(NULL);
	inode->st_mtime = time(NULL);

	write_content(inode, buffer, size);

	return (int) size;
}

void
remove_file(struct file *remove)
{
	struct inode *remove_inode = &inodes[remove->d_ino];
	flush_blocks(remove_inode);

	bitmap_inodes->free_inodes[remove->d_ino] = 0;  // Free inode bitmap index

	memset(remove_inode, 0, sizeof(struct inode));
	memset(remove, 0, sizeof(struct file));

	remove_inode = NULL;
	remove = NULL;
}

/** Remove a file */
static int
fisopfs_unlink(const char *path)
{
	printf("\n[debug] fisopfs_unlink(%s) \n", path);

	int i = get_file_index(path);

	struct file *file = &files[i];
	struct dirent *dir = get_dir(path);
	struct inode *inode = &inodes[dir->d_ino];

	if (!check_write_permissions(inode)) {
		return PERMISSION_DENIED;
	}

	for (int j = 0; j < dir->n_files; j++) {
		if (&files[dir->files[j]] == file) {
			dir->files[j] = -1;
		}
	}

	remove_file(file);

	return 0;
}

/** Create directory */
static int
fisopfs_mkdir(const char *path, mode_t mode)
{
	printf("\n[debug] fisopfs_mkdir(%s, %d) \n", path, mode);
	struct dirent *parent = get_dir(path);
	printf("\n[debug] parent is: %s \n", parent->path);
	printf("[debug] parent level is %d \n", parent->level);
	printf("[debug] path strlen is %ld \n", strlen(path));

	struct inode *inode = &inodes[parent->d_ino];

	if (!check_write_permissions(inode)) {
		return PERMISSION_DENIED;
	}

	inode->st_atime = time(NULL);
	inode->st_mtime = time(NULL);

	if (parent->level < MAX_DEPTH_DIR && (strlen(path) < FS_FILENAME_LEN)) {
		path++;
		int i = init_inode(__S_IFDIR | 0775);
		if (i > -1) {
			struct dirent new_dir;  // Initialize new dir
			new_dir.n_files = 0;
			strcpy(new_dir.path, path);
			strcpy(new_dir.dirname, path + get_name_index(path));
			new_dir.d_ino = i;
			new_dir.parent = parent->n_dir;
			new_dir.level = parent->level + 1;
			new_dir.n_dir = sb->n_dirs;
			dirs[sb->n_dirs++] = new_dir;

			return 0;
		}
	}

	return 1;
}

/** Remove a directory */
static int
fisopfs_rmdir(const char *path)
{
	printf("\n[debug] fisopfs_rmdir(%s) \n", path);
	path++;
	for (int i = 0; i < sb->n_dirs; i++) {
		struct dirent *dir = &dirs[i];
		if (strcmp(dir->path, path) == 0) {
			struct dirent *parent = &dirs[dir->parent];
			struct inode *inode = &inodes[parent->d_ino];

			if (!check_write_permissions(inode))
				return PERMISSION_DENIED;

			inode->st_atime = time(NULL);
			inode->st_mtime = time(NULL);

			for (int j = 0; j < dir->n_files; j++)
				remove_file(&files[dir->files[j]]);  // Remove contained files
			bitmap_inodes->free_inodes[dir->d_ino] =
			        0;  // Free inode in bitmap
			memset(&inodes[dir->d_ino], 0, sizeof(struct inode));
			memset(dir, 0, sizeof(struct dirent));
			dir = NULL;
		}
	}

	return 0;
}

/** Update file's times (modification, access) */
static int
fisopfs_utimens(const char *path, const struct timespec tv[2])
{
	printf("\n[debug] fisopfs_utimens(%s) \n", path);

	return 0;
}

static int
fisopfs_getxattr(const char *a, const char *b, char *c, size_t s)
{
	printf("\n[debug] fisopfs_getxattr(%s, %s, %s, %ld) \n", a, b, c, s);
	return 0;
}

static int
fisopfs_chmod(const char *path, mode_t mode)
{
	printf("\n[debug] fisopfs_chmod(%s, %d) \n", path, mode);
	struct inode *inode;

	int i = get_file_index(path);
	if (i < 0) {
		i = get_dir_index(path);
		if (i < 0)
			return -1;
		inode = &inodes[dirs[i].d_ino];
	}

	else {
		inode = &inodes[files[i].d_ino];
	}

	inode->st_atime = time(NULL);
	inode->st_ctime = time(NULL);

	struct fuse_context *context = fuse_get_context();

	printf("[debug] context_uid(%d) - uid(%d) \n", context->uid, inode->st_uid);
	if (inode->st_uid == context->uid)
		inode->st_mode = mode;

	printf("[debug] inode in mode %d \n", inode->st_mode);

	return 0;
}

static int
fisopfs_chown(const char *path, uid_t uid, gid_t gid)
{
	printf("\n[debug] fisopfs_chown(%s, %d, %d) \n", path, uid, gid);

	int i = get_file_index(path);
	if (i < 0)
		return -1;
	struct file *file = &files[i];

	struct inode *inode = &inodes[file->d_ino];

	inode->st_atime = time(NULL);
	inode->st_ctime = time(NULL);

	if (uid != -1) {
		inode->st_uid = uid;
	}

	if (gid != -1) {
		inode->st_gid = gid;
	}

	return 0;
}

static int
fisopfs_truncate(const char *path, off_t offset)
{
	printf("\n[debug] fisopfs_truncate(%s, %ld) \n", path, offset);

	int i = get_file_index(path);
	if (i < 0)
		return -1;
	struct file *file = &files[i];

	printf("[debug] found %s \n", path);
	struct inode *inode = &inodes[file->d_ino];
	flush_blocks(inode);

	return 0;
}

static struct fuse_operations operations = {
	.getattr = fisopfs_getattr,
	.readdir = fisopfs_readdir,
	.read = fisopfs_read,
	.mkdir = fisopfs_mkdir,
	.unlink = fisopfs_unlink,
	.rmdir = fisopfs_rmdir,
	.write = fisopfs_write,
	.mknod = fisopfs_mknod,
	.create = fisopfs_create,
	.utimens = fisopfs_utimens,
	.init = fisopfs_init,
	.getxattr = fisopfs_getxattr,
	.chown = fisopfs_chown,
	.chmod = fisopfs_chmod,
	.truncate = fisopfs_truncate,
	.destroy = fisopfs_destroy,
};

int
main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &operations, NULL);
}