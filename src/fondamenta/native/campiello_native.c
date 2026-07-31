/*
 * campiello_native.c
 *
 * Minimal in-memory userlandfs filesystem written against the Haiku-native fs_interface
 * (file_system_module_info / fs_vnode_ops), loaded via libuserlandfs_haiku_kernel.so. This
 * is the front end native mode uses (decision C, docs/VERIFIED.md section 1), in contrast
 * to the FUSE bridge.
 *
 * Purpose: prove that the native front end carries BFS attribute TYPES both ways. The
 * volume exposes one file, nota.txt, with a seeded B_STRING_TYPE and B_INT32_TYPE
 * attribute, and it accepts attribute WRITES (create_attr / write_attr / remove_attr) so a
 * `addattr -t int32 ...` round-trips: the attribute is stored with its type and reads back
 * typed via listattr / catattr. File content is read-only; only attributes are writable.
 *
 * A native userlandfs filesystem exports a `modules` array (standard Haiku kernel module
 * mechanism) containing a file_system_module_info named "file_systems/<name>/v1", and links
 * libuserlandfs_haiku_kernel.so (which provides userlandfs_create_file_system and the
 * publish_vnode / get_vnode / put_vnode helpers). Verified against
 * server/haiku/HaikuKernelFileSystem.cpp:385 and the in-tree bindfs example.
 *
 * This is a test artifact, not the real Fondamenta: in memory, single file, no persistence.
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>

#include <module.h>
#include <StorageDefs.h>
#include <TypeConstants.h>
#include <fs_info.h>
#include <fs_interface.h>

#define MODULE_NAME "file_systems/campiello_native/v1"
#define ROOT_ID ((ino_t)1)
#define FILE_ID ((ino_t)2)

static const char kContent[] =
	"Ciao dal front end nativo di Campiello. Gli attributi mantengono il loro tipo.\n";

/* Mutable attribute store for the single file, at volume scope so it survives vnode
 * get/put. Guarded by a mutex because the userlandfs server is multithreaded. */
typedef struct {
	bool   used;
	char   name[B_ATTR_NAME_LENGTH + 1];
	uint32 type;
	uint8* data;
	size_t size;
} MAttr;

#define MAX_ATTRS 64
static MAttr g_attrs[MAX_ATTRS];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
	ino_t  id;
	mode_t mode;
} Node;

static fs_volume_ops gVolumeOps;
static fs_vnode_ops gVnodeOps;

// #pragma mark - attribute store helpers (call with g_lock held)

static void
attr_clear(MAttr* a)
{
	free(a->data);
	a->data = NULL;
	a->size = 0;
	a->used = false;
	a->name[0] = '\0';
}

static int
attr_find(const char* name)
{
	for (int i = 0; i < MAX_ATTRS; i++) {
		if (g_attrs[i].used && strcmp(g_attrs[i].name, name) == 0)
			return i;
	}
	return -1;
}

/* Find or create a slot for `name`, (re)set to empty with the given type. Returns the
 * slot index, or -1 if the store is full. */
static int
attr_alloc(const char* name, uint32 type)
{
	int i = attr_find(name);
	if (i < 0) {
		for (i = 0; i < MAX_ATTRS; i++) {
			if (!g_attrs[i].used)
				break;
		}
		if (i >= MAX_ATTRS)
			return -1;
	} else {
		free(g_attrs[i].data);
		g_attrs[i].data = NULL;
	}
	g_attrs[i].used = true;
	strlcpy(g_attrs[i].name, name, sizeof(g_attrs[i].name));
	g_attrs[i].type = type;
	g_attrs[i].size = 0;
	return i;
}

static status_t
attr_put_bytes(int i, off_t pos, const void* buffer, size_t length)
{
	if (pos < 0)
		return B_BAD_VALUE;
	size_t end = (size_t)pos + length;
	if (end > g_attrs[i].size) {
		uint8* grown = (uint8*)realloc(g_attrs[i].data, end);
		if (grown == NULL && end > 0)
			return B_NO_MEMORY;
		g_attrs[i].data = grown;
		if ((size_t)pos > g_attrs[i].size) {
			memset(g_attrs[i].data + g_attrs[i].size, 0,
				(size_t)pos - g_attrs[i].size);
		}
		g_attrs[i].size = end;
	}
	if (length > 0)
		memcpy(g_attrs[i].data + pos, buffer, length);
	return B_OK;
}

static void
attr_seed(void)
{
	memset(g_attrs, 0, sizeof(g_attrs));
	int i = attr_alloc("MyApp:comment", B_STRING_TYPE);
	if (i >= 0)
		attr_put_bytes(i, 0, "ciao tipizzato", sizeof("ciao tipizzato")); /* incl. null */
	i = attr_alloc("MyApp:rating", B_INT32_TYPE);
	if (i >= 0) {
		int32 rating = 5;
		attr_put_bytes(i, 0, &rating, sizeof(rating));
	}
}

// #pragma mark - nodes and dirents

static Node*
make_node(ino_t id)
{
	Node* n = (Node*)malloc(sizeof(Node));
	if (n == NULL)
		return NULL;
	n->id = id;
	/* file is 0644 so attribute writes are permitted; content stays read-only (no write
	 * op is published). */
	n->mode = (id == ROOT_ID) ? (S_IFDIR | 0755) : (S_IFREG | 0644);
	return n;
}

static status_t
emit_dirent(struct dirent* buf, size_t bufSize, dev_t dev, ino_t ino, const char* name,
	uint32* _num)
{
	size_t nameLen = strlen(name);
	size_t recLen = offsetof(struct dirent, d_name) + nameLen + 1;
	if (recLen > bufSize) {
		*_num = 0;
		return B_BUFFER_OVERFLOW;
	}
	buf->d_dev = dev;
	buf->d_ino = ino;
	buf->d_reclen = (unsigned short)recLen;
	memcpy(buf->d_name, name, nameLen + 1);
	*_num = 1;
	return B_OK;
}

// #pragma mark - volume ops

static status_t
c_mount(fs_volume* volume, const char* device, uint32 flags, const char* args,
	ino_t* _rootID)
{
	(void)device;
	(void)flags;
	(void)args;
	Node* root = make_node(ROOT_ID);
	if (root == NULL)
		return B_NO_MEMORY;

	pthread_mutex_lock(&g_lock);
	attr_seed();
	pthread_mutex_unlock(&g_lock);

	volume->private_volume = NULL;
	volume->ops = &gVolumeOps;

	status_t error = publish_vnode(volume, ROOT_ID, root, &gVnodeOps,
		root->mode & S_IFMT, 0);
	if (error != B_OK) {
		free(root);
		return error;
	}
	*_rootID = ROOT_ID;
	return B_OK;
}

static status_t
c_unmount(fs_volume* volume)
{
	(void)volume;
	pthread_mutex_lock(&g_lock);
	for (int i = 0; i < MAX_ATTRS; i++) {
		if (g_attrs[i].used)
			attr_clear(&g_attrs[i]);
	}
	pthread_mutex_unlock(&g_lock);
	return B_OK;
}

static status_t
c_read_fs_info(fs_volume* volume, struct fs_info* info)
{
	info->dev = volume->id;
	info->root = ROOT_ID;
	info->flags = B_FS_IS_PERSISTENT | B_FS_HAS_ATTR | B_FS_HAS_MIME;
	info->block_size = 1024;
	info->io_size = 65536;
	info->total_blocks = 1;
	info->free_blocks = 0;
	info->total_nodes = 2;
	info->free_nodes = 0;
	strlcpy(info->volume_name, "Campiello Native", sizeof(info->volume_name));
	return B_OK;
}

static status_t
c_get_vnode(fs_volume* volume, ino_t id, fs_vnode* vnode, int* _type, uint32* _flags,
	bool reenter)
{
	(void)volume;
	(void)reenter;
	if (id != ROOT_ID && id != FILE_ID)
		return B_ENTRY_NOT_FOUND;
	Node* n = make_node(id);
	if (n == NULL)
		return B_NO_MEMORY;
	vnode->private_node = n;
	vnode->ops = &gVnodeOps;
	*_type = n->mode & S_IFMT;
	*_flags = 0;
	return B_OK;
}

// #pragma mark - vnode ops

static status_t
c_lookup(fs_volume* volume, fs_vnode* dir, const char* name, ino_t* _id)
{
	Node* d = (Node*)dir->private_node;
	if (d->id != ROOT_ID)
		return B_NOT_A_DIRECTORY;

	ino_t id;
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		id = ROOT_ID;
	else if (strcmp(name, "nota.txt") == 0)
		id = FILE_ID;
	else
		return B_ENTRY_NOT_FOUND;

	status_t error = get_vnode(volume, id, NULL);
	if (error != B_OK)
		return error;
	*_id = id;
	return B_OK;
}

static status_t
c_put_vnode(fs_volume* volume, fs_vnode* vnode, bool reenter)
{
	(void)volume;
	(void)reenter;
	free(vnode->private_node);
	return B_OK;
}

static status_t
c_read_stat(fs_volume* volume, fs_vnode* vnode, struct stat* st)
{
	Node* n = (Node*)vnode->private_node;
	memset(st, 0, sizeof(*st));
	st->st_dev = volume->id;
	st->st_ino = n->id;
	st->st_mode = n->mode;
	st->st_nlink = 1;
	st->st_size = (n->id == FILE_ID) ? (off_t)(sizeof(kContent) - 1) : 0;
	return B_OK;
}

static status_t
c_open(fs_volume* volume, fs_vnode* vnode, int openMode, void** _cookie)
{
	(void)volume;
	(void)vnode;
	(void)openMode; /* content is read-only; no write op is published */
	*_cookie = NULL;
	return B_OK;
}

static status_t
c_close(fs_volume* volume, fs_vnode* vnode, void* cookie)
{
	(void)volume; (void)vnode; (void)cookie;
	return B_OK;
}

static status_t
c_free_cookie(fs_volume* volume, fs_vnode* vnode, void* cookie)
{
	(void)volume; (void)vnode; (void)cookie;
	return B_OK;
}

static status_t
c_read(fs_volume* volume, fs_vnode* vnode, void* cookie, off_t pos, void* buffer,
	size_t* length)
{
	(void)volume; (void)cookie;
	Node* n = (Node*)vnode->private_node;
	if (n->id != FILE_ID) {
		*length = 0;
		return B_IS_A_DIRECTORY;
	}
	size_t content = sizeof(kContent) - 1;
	if (pos < 0 || (size_t)pos >= content) {
		*length = 0;
		return B_OK;
	}
	size_t avail = content - (size_t)pos;
	size_t want = (*length < avail) ? *length : avail;
	memcpy(buffer, kContent + pos, want);
	*length = want;
	return B_OK;
}

// #pragma mark - directory ops

static status_t
c_open_dir(fs_volume* volume, fs_vnode* vnode, void** _cookie)
{
	(void)volume;
	Node* n = (Node*)vnode->private_node;
	if (n->id != ROOT_ID)
		return B_NOT_A_DIRECTORY;
	int32* cursor = (int32*)malloc(sizeof(int32));
	if (cursor == NULL)
		return B_NO_MEMORY;
	*cursor = 0;
	*_cookie = cursor;
	return B_OK;
}

static status_t
c_close_dir(fs_volume* volume, fs_vnode* vnode, void* cookie)
{
	(void)volume; (void)vnode; (void)cookie;
	return B_OK;
}

static status_t
c_free_dir_cookie(fs_volume* volume, fs_vnode* vnode, void* cookie)
{
	(void)volume; (void)vnode;
	free(cookie);
	return B_OK;
}

static status_t
c_read_dir(fs_volume* volume, fs_vnode* vnode, void* cookie, struct dirent* buffer,
	size_t bufferSize, uint32* _num)
{
	Node* n = (Node*)vnode->private_node;
	int32* cursor = (int32*)cookie;
	if (n->id != ROOT_ID) {
		*_num = 0;
		return B_OK;
	}
	static const char* const kNames[] = { ".", "..", "nota.txt" };
	static const ino_t kIds[] = { ROOT_ID, ROOT_ID, FILE_ID };
	if (*cursor >= 3) {
		*_num = 0;
		return B_OK;
	}
	status_t error = emit_dirent(buffer, bufferSize, volume->id, kIds[*cursor],
		kNames[*cursor], _num);
	if (error != B_OK)
		return error;
	(*cursor)++;
	return B_OK;
}

static status_t
c_rewind_dir(fs_volume* volume, fs_vnode* vnode, void* cookie)
{
	(void)volume; (void)vnode;
	*(int32*)cookie = 0;
	return B_OK;
}

// #pragma mark - attribute ops (typed, enumerable, and writable)

static status_t
c_open_attr_dir(fs_volume* volume, fs_vnode* vnode, void** _cookie)
{
	(void)volume; (void)vnode;
	int32* cursor = (int32*)malloc(sizeof(int32));
	if (cursor == NULL)
		return B_NO_MEMORY;
	*cursor = 0;
	*_cookie = cursor;
	return B_OK;
}

static status_t
c_close_attr_dir(fs_volume* volume, fs_vnode* vnode, void* cookie)
{
	(void)volume; (void)vnode; (void)cookie;
	return B_OK;
}

static status_t
c_free_attr_dir_cookie(fs_volume* volume, fs_vnode* vnode, void* cookie)
{
	(void)volume; (void)vnode;
	free(cookie);
	return B_OK;
}

static status_t
c_read_attr_dir(fs_volume* volume, fs_vnode* vnode, void* cookie, struct dirent* buffer,
	size_t bufferSize, uint32* _num)
{
	Node* n = (Node*)vnode->private_node;
	int32* cursor = (int32*)cookie;
	if (n->id != FILE_ID) {
		*_num = 0;
		return B_OK;
	}
	pthread_mutex_lock(&g_lock);
	int32 i = *cursor;
	while (i < MAX_ATTRS && !g_attrs[i].used)
		i++;
	if (i >= MAX_ATTRS) {
		pthread_mutex_unlock(&g_lock);
		*_num = 0;
		return B_OK;
	}
	status_t error = emit_dirent(buffer, bufferSize, volume->id, n->id, g_attrs[i].name,
		_num);
	if (error == B_OK)
		*cursor = i + 1;
	pthread_mutex_unlock(&g_lock);
	return error;
}

static status_t
c_rewind_attr_dir(fs_volume* volume, fs_vnode* vnode, void* cookie)
{
	(void)volume; (void)vnode;
	*(int32*)cookie = 0;
	return B_OK;
}

/* Cookie for an open attribute is a heap int32 holding the store slot index. */
static status_t
make_attr_cookie(int index, void** _cookie)
{
	int32* c = (int32*)malloc(sizeof(int32));
	if (c == NULL)
		return B_NO_MEMORY;
	*c = index;
	*_cookie = c;
	return B_OK;
}

static status_t
c_create_attr(fs_volume* volume, fs_vnode* vnode, const char* name, uint32 type,
	int openMode, void** _cookie)
{
	(void)volume; (void)openMode;
	Node* n = (Node*)vnode->private_node;
	if (n->id != FILE_ID)
		return B_NOT_ALLOWED;
	pthread_mutex_lock(&g_lock);
	int index = attr_alloc(name, type);
	pthread_mutex_unlock(&g_lock);
	if (index < 0)
		return B_NO_MEMORY;
	return make_attr_cookie(index, _cookie);
}

static status_t
c_open_attr(fs_volume* volume, fs_vnode* vnode, const char* name, int openMode,
	void** _cookie)
{
	(void)volume; (void)openMode;
	Node* n = (Node*)vnode->private_node;
	if (n->id != FILE_ID)
		return B_ENTRY_NOT_FOUND;
	pthread_mutex_lock(&g_lock);
	int index = attr_find(name);
	pthread_mutex_unlock(&g_lock);
	if (index < 0)
		return B_ENTRY_NOT_FOUND;
	return make_attr_cookie(index, _cookie);
}

static status_t
c_close_attr(fs_volume* volume, fs_vnode* vnode, void* cookie)
{
	(void)volume; (void)vnode; (void)cookie;
	return B_OK;
}

static status_t
c_free_attr_cookie(fs_volume* volume, fs_vnode* vnode, void* cookie)
{
	(void)volume; (void)vnode;
	free(cookie);
	return B_OK;
}

static status_t
c_read_attr(fs_volume* volume, fs_vnode* vnode, void* cookie, off_t pos, void* buffer,
	size_t* length)
{
	(void)volume; (void)vnode;
	int32 index = *(int32*)cookie;
	pthread_mutex_lock(&g_lock);
	status_t error = B_OK;
	if (index < 0 || index >= MAX_ATTRS || !g_attrs[index].used) {
		error = B_ENTRY_NOT_FOUND;
	} else if (pos < 0 || (size_t)pos >= g_attrs[index].size) {
		*length = 0;
	} else {
		size_t avail = g_attrs[index].size - (size_t)pos;
		size_t want = (*length < avail) ? *length : avail;
		memcpy(buffer, g_attrs[index].data + pos, want);
		*length = want;
	}
	pthread_mutex_unlock(&g_lock);
	return error;
}

static status_t
c_write_attr(fs_volume* volume, fs_vnode* vnode, void* cookie, off_t pos,
	const void* buffer, size_t* length)
{
	(void)volume; (void)vnode;
	int32 index = *(int32*)cookie;
	pthread_mutex_lock(&g_lock);
	status_t error;
	if (index < 0 || index >= MAX_ATTRS || !g_attrs[index].used)
		error = B_ENTRY_NOT_FOUND;
	else
		error = attr_put_bytes(index, pos, buffer, *length);
	pthread_mutex_unlock(&g_lock);
	return error;
}

static status_t
c_read_attr_stat(fs_volume* volume, fs_vnode* vnode, void* cookie, struct stat* st)
{
	(void)volume; (void)vnode;
	int32 index = *(int32*)cookie;
	memset(st, 0, sizeof(*st));
	pthread_mutex_lock(&g_lock);
	status_t error = B_OK;
	if (index < 0 || index >= MAX_ATTRS || !g_attrs[index].used) {
		error = B_ENTRY_NOT_FOUND;
	} else {
		st->st_type = g_attrs[index].type; /* the type code, preserved */
		st->st_size = (off_t)g_attrs[index].size;
	}
	pthread_mutex_unlock(&g_lock);
	return error;
}

static status_t
c_write_attr_stat(fs_volume* volume, fs_vnode* vnode, void* cookie,
	const struct stat* st, int statMask)
{
	(void)volume; (void)vnode; (void)cookie; (void)st; (void)statMask;
	/* Size is managed by create_attr/write_attr; nothing to do. */
	return B_OK;
}

static status_t
c_remove_attr(fs_volume* volume, fs_vnode* vnode, const char* name)
{
	(void)volume;
	Node* n = (Node*)vnode->private_node;
	if (n->id != FILE_ID)
		return B_ENTRY_NOT_FOUND;
	pthread_mutex_lock(&g_lock);
	int index = attr_find(name);
	if (index >= 0)
		attr_clear(&g_attrs[index]);
	pthread_mutex_unlock(&g_lock);
	return index < 0 ? B_ENTRY_NOT_FOUND : B_OK;
}

// #pragma mark - module glue

static fs_volume_ops gVolumeOps = {
	.unmount      = c_unmount,
	.read_fs_info = c_read_fs_info,
	.get_vnode    = c_get_vnode,
};

static fs_vnode_ops gVnodeOps = {
	.lookup     = c_lookup,
	.put_vnode  = c_put_vnode,
	.read_stat  = c_read_stat,
	.open       = c_open,
	.close      = c_close,
	.free_cookie = c_free_cookie,
	.read       = c_read,
	.open_dir   = c_open_dir,
	.close_dir  = c_close_dir,
	.free_dir_cookie = c_free_dir_cookie,
	.read_dir   = c_read_dir,
	.rewind_dir = c_rewind_dir,
	.open_attr_dir = c_open_attr_dir,
	.close_attr_dir = c_close_attr_dir,
	.free_attr_dir_cookie = c_free_attr_dir_cookie,
	.read_attr_dir = c_read_attr_dir,
	.rewind_attr_dir = c_rewind_attr_dir,
	.create_attr = c_create_attr,
	.open_attr  = c_open_attr,
	.close_attr = c_close_attr,
	.free_attr_cookie = c_free_attr_cookie,
	.read_attr  = c_read_attr,
	.write_attr = c_write_attr,
	.read_attr_stat = c_read_attr_stat,
	.write_attr_stat = c_write_attr_stat,
	.remove_attr = c_remove_attr,
};

static status_t
c_std_ops(int32 op, ...)
{
	if (op == B_MODULE_INIT || op == B_MODULE_UNINIT)
		return B_OK;
	return B_ERROR;
}

static file_system_module_info sModuleInfo = {
	.info = {
		.name = MODULE_NAME,
		.flags = 0,
		.std_ops = c_std_ops,
	},
	.short_name  = "campiello_native",
	.pretty_name = "Campiello Native",
	.flags = 0,
	.mount = c_mount,
};

module_info* modules[] = {
	(module_info*)&sModuleInfo,
	NULL,
};
