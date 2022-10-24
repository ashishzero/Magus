#include <Windows.h>
#include "Kr/KrPrelude.h"
#include "Kr/KrMemory.h"

#include <windows.h>
#include <stdlib.h>
#include <stdio.h>

static wchar_t *UnicodeToWideChar(const String str, M_Arena *arena) {
	int len = MultiByteToWideChar(CP_UTF8, 0, (char *)str.data, (int)str.length, NULL, 0);

	wchar_t *result = M_PushArray(arena, wchar_t, len + 1);
	if (result) {
		MultiByteToWideChar(CP_UTF8, 0, (char *)str.data, (int)str.length, result, len);
		result[len] = 0;
	}

	return result;
}

enum PL_File_Attribute_Flags {
	PL_FILE_ATTRIBUTE_ARCHIVE    = 0x1,
	PL_FILE_ATTRIBUTE_COMPRESSED = 0x2,
	PL_FILE_ATTRIBUTE_DIRECTORY  = 0x4,
	PL_FILE_ATTRIBUTE_ENCRYPTED  = 0x8,
	PL_FILE_ATTRIBUTE_HIDDEN     = 0x10,
	PL_FILE_ATTRIBUTE_NORMAL     = 0x20,
	PL_FILE_ATTRIBUTE_OFFLINE    = 0x40,
	PL_FILE_ATTRIBUTE_READ_ONLY  = 0x80,
	PL_FILE_ATTRIBUTE_SYSTEM     = 0x100,
	PL_FILE_ATTRIBUTE_TEMPORARY  = 0x200,
};

enum PL_File_Action_Flags {
	PL_FILE_ACTION_ADDED       = 0x1,
	PL_FILE_ACTION_REMOVED     = 0x2,
	PL_FILE_ACTION_MODIFIED    = 0x4,
	PL_FILE_ACTION_RENAMED_OLD = 0x8,
	PL_FILE_ACTION_RENAMED_NEW = 0x10
};

typedef void(*PL_Watch_Directory_Notify_Proc)(void *context, String fullpath, uint32_t actions, uint32_t attrs);

enum PL_Watch_Directory_Flags {
	PL_WATCH_DIRECTORY_RECURSIVE = 0x1
};

struct PL_Watch_Directory {
	String                         path;
	uint32_t                       flags;
	PL_Watch_Directory_Notify_Proc notify;
	void *                         context;
};

void OnDirectoryNotification(void *context, String path, uint32_t actions, uint32_t attrs) {
	Trace("notified: " StrFmt "\n", StrArg(path));
}

struct PL_Watch_Directory_Context;

PL_Watch_Directory_Context *PL_WatchDirectory(Array_View<PL_Watch_Directory> directories);

int watch_main(int argc, char **argv) {
	PL_Watch_Directory directories[] = {
		{ "C:/Dev/Magus/Resources", 0, OnDirectoryNotification },
		{ "C:/Dev/Magus/Magus/Shaders", PL_WATCH_DIRECTORY_RECURSIVE, OnDirectoryNotification },
	};

	PL_Watch_Directory_Context *watch = PL_WatchDirectory(directories);
	// PL_UnWatchDirectory(watch);
	return 0;
}

constexpr int PL_MAX_WATCH_DIRECTORY   = MAXIMUM_WAIT_OBJECTS;
constexpr int PL_MAX_WATCH_PATH_LENGTH = 32 * 1024;

struct _PL_Watch_Directory {
	HANDLE                         handle;
	alignas(DWORD) uint8_t         buffer[8 * 1024];
	uint32_t                       flags;
	OVERLAPPED                     overlapped;
	void *                         context;
	PL_Watch_Directory_Notify_Proc notify;
};

struct PL_Watch_Directory_Context {
	int                   count;
	_PL_Watch_Directory * directories;
	HANDLE              * events;
	wchar_t               path_buffer[PL_MAX_WATCH_PATH_LENGTH];
	uint8_t               path_buffer_unicode[PL_MAX_WATCH_PATH_LENGTH];
	M_Allocator           allocator;
	HANDLE                thread;
};

static DWORD WINAPI _PL_WatchDirectoryThread(void *arg);

void PL_UnWatchDirectory(PL_Watch_Directory_Context *watch) {
	size_t allocation_size = 0;
	allocation_size += sizeof(PL_Watch_Directory_Context);
	allocation_size += sizeof(_PL_Watch_Directory) * (watch->count + 1); // +1 for alignment
	allocation_size += sizeof(HANDLE) * (watch->count + 1); // events; +1 for alignment

	if (watch->thread) {
		TerminateThread(watch->thread, 0);
		CloseHandle(watch->thread);
	}

	for (int i = 0; i < watch->count; ++i) {
		if (watch->directories[i].handle && watch->directories[i].handle != INVALID_HANDLE_VALUE)
			CloseHandle(watch->directories[i].handle);
		if (watch->events[i] && watch->events[i] != INVALID_HANDLE_VALUE)
			CloseHandle(watch->events[i]);
	}

	M_Free(watch, allocation_size, watch->allocator);
}

PL_Watch_Directory_Context *PL_WatchDirectory(Array_View<PL_Watch_Directory> directories) {
	if (!directories.count)
		return nullptr;

	if (directories.count > PL_MAX_WATCH_DIRECTORY) {
		LogErrorEx("Windows", "Failed to watch directories. Given directories count: %zd. Supported max %d.",
			directories.count, PL_MAX_WATCH_DIRECTORY);
		return nullptr;
	}

	size_t allocation_size = 0;
	allocation_size += sizeof(PL_Watch_Directory_Context);
	allocation_size += sizeof(_PL_Watch_Directory) * (directories.count + 1); // +1 for alignment
	allocation_size += sizeof(HANDLE) * (directories.count + 1); // events; +1 for alignment

	uint8_t *mem = (uint8_t *)MemAlloc(allocation_size);
	if (!mem) {
		LogErrorEx("Windows", "Failed to watch directories. Reason: Out of memory");
		return nullptr;
	}

	memset(mem, 0, allocation_size);

	PL_Watch_Directory_Context *watch = (PL_Watch_Directory_Context *)mem;
	mem += sizeof(PL_Watch_Directory_Context);
	watch->count = (int)directories.count;

	mem = M_AlignPointer(mem, alignof(_PL_Watch_Directory));
	watch->directories = (_PL_Watch_Directory *)mem;
	mem += watch->count * sizeof(_PL_Watch_Directory);

	mem = M_AlignPointer(mem, alignof(HANDLE));
	watch->events = (HANDLE *)mem;
	mem += watch->count * sizeof(HANDLE);

	watch->allocator = ThreadContext.allocator;

	M_Arena *arena = ThreadScratchpad();

	for (ptrdiff_t index = 0; index < directories.count; ++index) {
		M_Temporary temp = M_BeginTemporaryMemory(arena);
		Defer{ M_EndTemporaryMemory(&temp); };

		wchar_t *dirpath = UnicodeToWideChar(directories[index].path, arena);

		watch->directories[index].handle = CreateFileW(dirpath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);

		if (watch->directories[index].handle == INVALID_HANDLE_VALUE) {
			// log windows
			PL_UnWatchDirectory(watch);
			return nullptr;
		}

		watch->directories[index].flags   = directories[index].flags;
		watch->directories[index].context = directories[index].context;
		watch->directories[index].notify  = directories[index].notify;

		watch->events[index] = CreateEventW(nullptr, FALSE, 0, nullptr);
	}

	watch->thread = CreateThread(nullptr, 0, _PL_WatchDirectoryThread, watch, 0, NULL);

	if (!watch->thread) {
		// log windows
		PL_UnWatchDirectory(watch);
		return nullptr;
	}

	return watch;
}

static void _PL_WatchDirectoryChanges(_PL_Watch_Directory *dir, HANDLE hevent) {
	constexpr int val = sizeof(PL_Watch_Directory_Context);

	memset(&dir->overlapped, 0, sizeof(dir->overlapped));
	dir->overlapped.hEvent = hevent;

	bool sub_dir = dir->flags & PL_WATCH_DIRECTORY_RECURSIVE;
	DWORD watch_filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_ACCESS;
	ReadDirectoryChangesW(dir->handle, dir->buffer, sizeof(dir->buffer), sub_dir, watch_filter, NULL, &dir->overlapped, NULL);
}

static DWORD WINAPI _PL_WatchDirectoryThread(void *arg) {
	PL_Watch_Directory_Context *watch = (PL_Watch_Directory_Context *)arg;

	for (int i = 0; i < watch->count; ++i) {
		_PL_WatchDirectoryChanges(&watch->directories[i], watch->events[i]);
	}

	while (true) {
		DWORD wait_status = WaitForMultipleObjects(watch->count, watch->events, FALSE, INFINITE);
		if (wait_status == WAIT_FAILED) {
			continue;
		}

		for (int i = 0; i < watch->count; ++i) {
			if (wait_status != WAIT_OBJECT_0 + i)
				continue;

			_PL_Watch_Directory *dir = &watch->directories[i];

			DWORD bytes_transferred = 0;
			GetOverlappedResult(dir->handle, &dir->overlapped, &bytes_transferred, TRUE);

			if (bytes_transferred) {
				uint8_t *read_ptr = dir->buffer;

				while (true) {
					FILE_NOTIFY_INFORMATION *info = (FILE_NOTIFY_INFORMATION *)read_ptr;

					DWORD length = GetFinalPathNameByHandleW(dir->handle, watch->path_buffer, PL_MAX_WATCH_PATH_LENGTH, 0);

					int req_length = length + 1 + info->FileNameLength / sizeof(wchar_t) + 1;

					if (req_length <= PL_MAX_WATCH_PATH_LENGTH) {
						watch->path_buffer[length] = '\\';
						memcpy(watch->path_buffer + length + 1, info->FileName, info->FileNameLength);
						watch->path_buffer[req_length - 1] = 0;

						int length = WideCharToMultiByte(CP_UTF8, 0, watch->path_buffer, req_length - 1, 
							(char *)watch->path_buffer_unicode, PL_MAX_WATCH_PATH_LENGTH - 1, 0, 0);
						watch->path_buffer_unicode[length] = 0;

						DWORD action = info->Action;
						uint32_t translated_actions = 0;
						if (action & FILE_ACTION_ADDED) translated_actions |= PL_FILE_ACTION_ADDED;
						if (action & FILE_ACTION_REMOVED) translated_actions |= PL_FILE_ACTION_REMOVED;
						if (action & FILE_ACTION_MODIFIED) translated_actions |= PL_FILE_ACTION_MODIFIED;
						if (action & FILE_ACTION_RENAMED_OLD_NAME) translated_actions |= PL_FILE_ACTION_RENAMED_OLD;
						if (action & FILE_ACTION_RENAMED_NEW_NAME) translated_actions |= PL_FILE_ACTION_RENAMED_NEW;

						DWORD attrs = GetFileAttributesW(watch->path_buffer);
						uint32_t translated_attrs = 0;
						if (attrs & FILE_ATTRIBUTE_ARCHIVE) translated_attrs |= PL_FILE_ATTRIBUTE_ARCHIVE;
						if (attrs & FILE_ATTRIBUTE_COMPRESSED) translated_attrs |= PL_FILE_ATTRIBUTE_COMPRESSED;
						if (attrs & FILE_ATTRIBUTE_DIRECTORY) translated_attrs |= PL_FILE_ATTRIBUTE_DIRECTORY;
						if (attrs & FILE_ATTRIBUTE_ENCRYPTED) translated_attrs |= PL_FILE_ATTRIBUTE_ENCRYPTED;
						if (attrs & FILE_ATTRIBUTE_HIDDEN) translated_attrs |= PL_FILE_ATTRIBUTE_HIDDEN;
						if (attrs & FILE_ATTRIBUTE_NORMAL) translated_attrs |= PL_FILE_ATTRIBUTE_NORMAL;
						if (attrs & FILE_ATTRIBUTE_OFFLINE) translated_attrs |= PL_FILE_ATTRIBUTE_OFFLINE;
						if (attrs & FILE_ATTRIBUTE_READONLY) translated_attrs |= PL_FILE_ATTRIBUTE_READ_ONLY;
						if (attrs & FILE_ATTRIBUTE_SYSTEM) translated_attrs |= PL_FILE_ATTRIBUTE_SYSTEM;
						if (attrs & FILE_ATTRIBUTE_TEMPORARY) translated_attrs |= PL_FILE_ATTRIBUTE_TEMPORARY;

						dir->notify(dir->context, String(watch->path_buffer_unicode, length), translated_actions, translated_attrs);
					}

					if (info->NextEntryOffset == 0)
						break;

					read_ptr += info->NextEntryOffset;
				}
			}

			_PL_WatchDirectoryChanges(dir, watch->events[i]);

			break;
		}
	}
}
