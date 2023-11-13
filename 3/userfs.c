#include "userfs.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum
{
	BLOCK_SIZE = 512,
	MAX_FILE_SIZE = 1024 * 1024 * 100,
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block
{
	/** Block memory. */
	char *memory;
	/** How many bytes are occupied. */
	int occupied;
	/** Next block in the file. */
	struct block *next;
	/** Previous block in the file. */
	struct block *prev;

	/* PUT HERE OTHER MEMBERS */
};

struct file
{
	/** Double-linked list of file blocks. */
	struct block *block_list;
	/**
	 * Last block in the list above for fast access to the end
	 * of file.
	 */
	struct block *last_block;
	/** How many file descriptors are opened on the file. */
	int refs;
	/** File name. */
	char *name;
	/** Files are stored in a double-linked list. */
	struct file *next;
	struct file *prev;

	/** Deleted. */
	int deleted;

	/* PUT HERE OTHER MEMBERS */
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc
{
	struct file *file;

	/** Flags */
	int flags;

	/** Number of bytes written or read since openning the file*/
	int bytes_position;
	/* PUT HERE OTHER MEMBERS */
};

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc **file_descriptors = NULL;
static int file_descriptor_count = 0;
static int file_descriptor_capacity = 0;

enum ufs_error_code
ufs_errno()
{
	return ufs_error_code;
}

int ufs_open(const char *filename, int flags)
{
	struct file *found = NULL;
	for (struct file *file = file_list; file != NULL; file = file->next)
	{
		if (strcmp(file->name, filename) == 0)
		{
			if (file->deleted == 0)
			{
				found = file;
			}
			break;
		}
	}
	if (found == NULL)
	{
		if (flags & UFS_CREATE)
		{
			found = malloc(sizeof(struct file));
			found->name = malloc(strlen(filename) + 1);
			strcpy(found->name, filename);
			found->block_list = NULL;
			found->last_block = NULL;
			found->refs = 0;
			found->next = file_list;
			found->prev = NULL;
			found->deleted = 0;
			if (file_list != NULL)
			{
				file_list->prev = found;
			}
			file_list = found;
		}
		else
		{
			ufs_error_code = UFS_ERR_NO_FILE;
			return -1;
		}
	}
	found->refs++;
	int fd = -1;
	for (int i = 0; i < file_descriptor_count; i++)
	{
		if (file_descriptors[i] == NULL)
		{
			fd = i;
			break;
		}
	}

	if (fd == -1)
	{
		if (file_descriptor_count == file_descriptor_capacity)
		{
			file_descriptor_capacity = file_descriptor_capacity * 2 + 1;
			file_descriptors = realloc(file_descriptors, file_descriptor_capacity * sizeof(struct filedesc *));
		}
		fd = file_descriptor_count;
		file_descriptor_count++;
	}

	file_descriptors[fd] = malloc(sizeof(struct filedesc));
	file_descriptors[fd]->file = found;
	file_descriptors[fd]->flags = flags;
	file_descriptors[fd]->bytes_position = 0;
	return fd;
}

ssize_t
ufs_write(int fd, const char *buf, size_t size)
{
	if (fd < 0 || fd >= file_descriptor_capacity || file_descriptors[fd] == NULL || file_descriptors[fd]->file == NULL || file_descriptors[fd]->file->refs == 0)
	{
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}

	struct filedesc *filedesc = file_descriptors[fd];
	struct file *file = filedesc->file;
	struct block *block = file->block_list;

	if (filedesc->flags & UFS_READ_ONLY)
	{
		ufs_error_code = UFS_ERR_NO_PERMISSION;
		return -1;
	}

	if (filedesc->bytes_position + size > MAX_FILE_SIZE)
	{
		ufs_error_code = UFS_ERR_NO_MEM;
		return -1;
	}

	for (int i = 0; i < filedesc->bytes_position / BLOCK_SIZE; i++)
	{
		block = block->next;
	}

	long unsigned int written = 0;

	while (written < size)
	{
		if (block == NULL)
		{
			block = malloc(sizeof(struct block));
			block->memory = malloc(BLOCK_SIZE);
			block->occupied = 0;
			block->next = NULL;
			block->prev = file->last_block;
			if (file->last_block != NULL)
			{
				file->last_block->next = block;
			}
			file->last_block = block;
			if (file->block_list == NULL)
			{
				file->block_list = block;
			}
		}
		// now we use bytes_position to know where we are in the file
		long unsigned int to_write = BLOCK_SIZE - filedesc->bytes_position % BLOCK_SIZE;
		if (to_write > size - written)
		{
			to_write = size - written;
		}
		memcpy(block->memory + filedesc->bytes_position % BLOCK_SIZE, buf + written, to_write);
		int new_occupied = filedesc->bytes_position % BLOCK_SIZE + to_write;
		if (block->occupied < new_occupied)
		{
			block->occupied = new_occupied;
		}
		filedesc->bytes_position += to_write;
		written += to_write;
		block = block->next;
	}
	return written;
}

ssize_t
ufs_read(int fd, char *buf, size_t size)
{
	if (fd < 0 || fd >= file_descriptor_capacity || file_descriptors[fd] == NULL || file_descriptors[fd]->file == NULL || file_descriptors[fd]->file->refs == 0)
	{
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}

	struct filedesc *filedesc = file_descriptors[fd];
	struct file *file = filedesc->file;
	struct block *block = file->block_list;

	if (filedesc->flags & UFS_WRITE_ONLY)
	{
		ufs_error_code = UFS_ERR_NO_PERMISSION;
		return -1;
	}

	for (int i = 0; i < filedesc->bytes_position / BLOCK_SIZE; i++)
	{
		block = block->next;
	}

	long unsigned int read = 0;

	while (read < size)
	{
		if (block == NULL)
		{
			break;
		}
		// we use bytes_position to know where we are in the file
		long unsigned int to_read = block->occupied - filedesc->bytes_position % BLOCK_SIZE;
		if (to_read > size - read)
		{
			to_read = size - read;
		}
		memcpy(buf + read, block->memory + filedesc->bytes_position % BLOCK_SIZE, to_read);
		filedesc->bytes_position += to_read;
		read += to_read;
		block = block->next;
	}
	return read;
}

int ufs_close(int fd)
{
	if (fd < 0 || fd >= file_descriptor_capacity || file_descriptors[fd] == NULL)
	{
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}
	file_descriptor_count -= 1;
	file_descriptors[fd]->file->refs -= 1;
	if (file_descriptors[fd]->file->refs == 0 && file_descriptors[fd]->file->deleted == 1)
	{
		ufs_delete(file_descriptors[fd]->file->name);
	}
	free(file_descriptors[fd]);
	file_descriptors[fd] = NULL;
	return 0;
}

int ufs_delete(const char *filename)
{
	if (filename == NULL)
	{
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}
	for (struct file *file = file_list; file != NULL; file = file->next)
	{
		if (strcmp(file->name, filename) == 0)
		{
			if (file->refs != 0)
			{
				file->deleted = 1;
				return 0;
			}
			else
			{
				if (file->prev != NULL)
				{
					file->prev->next = file->next;
				}
				if (file->next != NULL)
				{
					file->next->prev = file->prev;
				}
				if (file_list == file)
				{
					file_list = file->next;
				}
				free(file->name);
				struct block *block = file->block_list;
				while (block != NULL)
				{
					struct block *next = block->next;
					free(block->memory);
					block->memory = NULL; // Set pointer to NULL after freeing memory
					free(block);
					block = next;
				}
				free(file);
				return 0;
			}
		}
	}
	ufs_error_code = UFS_ERR_NO_FILE;
	return -1;
}

int ufs_resize(int fd, size_t new_size)
{
	if (fd < 0 || fd >= file_descriptor_capacity || file_descriptors[fd] == NULL || file_descriptors[fd]->file == NULL || file_descriptors[fd]->file->refs == 0)
	{
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}

	struct filedesc *filedesc = file_descriptors[fd];
	struct file *file = filedesc->file;
	struct block *block = file->block_list;

	if (filedesc->flags & UFS_READ_ONLY)
	{
		ufs_error_code = UFS_ERR_NO_PERMISSION;
		return -1;
	}

	if (new_size > MAX_FILE_SIZE)
	{
		ufs_error_code = UFS_ERR_NO_MEM;
		return -1;
	}

	size_t old_size = filedesc->bytes_position;
	for (int i = 0; i < file_descriptor_capacity; i++)
	{
		if (file_descriptors[i] != NULL && file_descriptors[i]->file == file)
		{
			if (file_descriptors[i]->bytes_position > (int)old_size)
			{
				old_size = file_descriptors[i]->bytes_position;
			}
		}
	}

	if (new_size < old_size)
	{
		for (size_t i = 0; i < new_size / BLOCK_SIZE; i++)
		{
			block = block->next;
		}
		struct block *next = block->next;
		block->next = NULL;
		while (next != NULL)
		{
			struct block *next_next = next->next;
			free(next->memory);
			free(next);
			next = next_next;
		}

		block->occupied = new_size % BLOCK_SIZE;
		if (block->occupied == 0)
		{
			block->occupied = BLOCK_SIZE;
		}
		file->last_block = block;
	}

	if (new_size > old_size)
	{
		size_t remaining_size = new_size - old_size;
		while (remaining_size > 0)
		{
			if (block == NULL || block->occupied == BLOCK_SIZE)
			{
				struct block *new_block = malloc(sizeof(struct block));
				if (new_block == NULL)
				{
					ufs_error_code = UFS_ERR_NO_MEM;
					return -1;
				}
				new_block->memory = malloc(BLOCK_SIZE);
				if (new_block->memory == NULL)
				{
					free(new_block);
					ufs_error_code = UFS_ERR_NO_MEM;
					return -1;
				}
				new_block->occupied = 0;
				new_block->next = NULL;
				new_block->prev = file->last_block;
				if (file->last_block != NULL)
				{
					file->last_block->next = new_block;
				}
				file->last_block = new_block;
				if (file->block_list == NULL)
				{
					file->block_list = new_block;
				}
				block = new_block;
			}
			size_t space_in_block = BLOCK_SIZE - block->occupied;
			size_t to_add = (remaining_size < space_in_block) ? remaining_size : space_in_block;
			block->occupied += to_add;
			remaining_size -= to_add;
			block = block->next;
		}
	}

	for (int i = 0; i < file_descriptor_capacity; i++)
	{
		if (file_descriptors[i] != NULL && file_descriptors[i]->file == file)
		{
			if (file_descriptors[i]->bytes_position > (int)new_size)
			{
				file_descriptors[i]->bytes_position = new_size;
			}
		}
	}

	return 0;
}

void ufs_destroy(void)
{
	for (int i = 0; i < file_descriptor_capacity; i++)
	{
		if (file_descriptors[i] != NULL)
		{
			ufs_close(i);
		}
	}
	free(file_descriptors);
	struct file *file = file_list;
	while (file != NULL)
	{
		struct file *next = file->next;
		free(file->name);
		struct block *block = file->block_list;
		while (block != NULL)
		{
			struct block *next = block->next;
			free(block->memory);
			free(block);
			block = next;
		}
		free(file);
		file = next;
	}
}
