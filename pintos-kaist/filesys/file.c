#include "filesys/file.h"
#include <debug.h>
#include "filesys/inode.h"
#include "threads/malloc.h"

/* 열린 파일을 나타낸다. */
struct file {
        uint64_t file_magic;              /* 해당 메모리에 있는 데이터가 file인지 확인하기 위한 식별자 */
		struct inode *inode;        /* 파일이 속한 inode. */
        off_t pos;                  /* 현재 위치. */
        bool deny_write;            /* file_deny_write() 호출 여부. */
};

/* INODE 를 인수로 받아 파일을 열고 소유권을 넘겨받는다.
 * 메모리 할당 실패나 INODE 가 NULL 이면 NULL 을 반환한다. */
struct file *
file_open (struct inode *inode) {
	struct file *file = calloc (1, sizeof *file);	
	if (inode != NULL && file != NULL) {
		file->file_magic = FILE_MAGIC;
		file->inode = inode;
		file->pos = 0;
		file->deny_write = false;
		return file;
	} else {
		inode_close (inode);
		free (file);
		return NULL;
	}
}

/* FILE 과 같은 inode 를 사용하여 새 파일 구조체를 만든다.
 * 실패하면 NULL 을 반환한다. */
struct file *
file_reopen (struct file *file) {
	return file_open (inode_reopen (file->inode));
}

/* 파일의 속성을 포함해 복제한 후 같은 inode 를 가리키는 새 파일을 만든다.
 * 실패하면 NULL 을 반환한다. */
struct file *
file_duplicate (struct file *file) {
	struct file *nfile = file_open (inode_reopen (file->inode));
	if (nfile) {
		nfile->pos = file->pos;
		if (file->deny_write)
			file_deny_write (nfile);
	}
	return nfile;
}

/* FILE 을 닫는다. */
void
file_close (struct file *file) {
	if (file != NULL) {
		file_allow_write (file);
		inode_close (file->inode);
		free (file);
	}
}

/* FILE 이 감싸고 있는 inode 를 반환한다. */
struct inode *
file_get_inode (struct file *file) {
	return file->inode;
}

/* FILE 의 현재 위치에서 BUFFER 로 SIZE 바이트를 읽어 온다.
 * 실제 읽은 바이트 수를 반환하며, EOF 에 도달하면 SIZE 보다 적을 수 있다.
 * 읽은 만큼 FILE 의 위치가 증가한다. */
off_t
file_read (struct file *file, void *buffer, off_t size) {
	off_t bytes_read = inode_read_at (file->inode, buffer, size, file->pos);
	file->pos += bytes_read;
	return bytes_read;
}

/* 파일의 FILE_OFS 위치부터 SIZE 바이트를 BUFFER 로 읽어 온다.
 * 반환 값은 실제 읽은 바이트 수이며 EOF 시 더 작을 수 있다.
 * 파일의 현재 위치는 변하지 않는다. */
off_t
file_read_at (struct file *file, void *buffer, off_t size, off_t file_ofs) {
	return inode_read_at (file->inode, buffer, size, file_ofs);
}

/* 현재 위치에서 FILE 로 BUFFER 의 내용 SIZE 바이트를 기록한다.
 * 실제 기록된 바이트 수를 반환하며, EOF 이면 SIZE 보다 적을 수 있다.
 * 보통은 파일을 확장해야 하지만 아직 구현되어 있지 않다.
 * 기록한 만큼 FILE 의 위치가 증가한다. */
off_t
file_write (struct file *file, const void *buffer, off_t size) {
	off_t bytes_written = inode_write_at (file->inode, buffer, size, file->pos);
	file->pos += bytes_written;
	return bytes_written;
}

/* FILE_OFS 지점부터 SIZE 바이트를 FILE 에 기록한다.
 * 실제 기록된 바이트 수를 반환하며 EOF 시 더 작을 수 있다.
 * 파일을 확장하는 기능은 아직 없다.
 * 파일의 현재 위치는 변하지 않는다. */
off_t
file_write_at (struct file *file, const void *buffer, off_t size,
		off_t file_ofs) {
	return inode_write_at (file->inode, buffer, size, file_ofs);
}

/* file_allow_write() 호출이나 파일이 닫힐 때까지
 * 해당 inode 에 대한 쓰기를 금지한다. */
void
file_deny_write (struct file *file) {
	ASSERT (file != NULL);
	if (!file->deny_write) {
		file->deny_write = true;
		inode_deny_write (file->inode);
	}
}

/* FILE 의 inode 에 대한 쓰기를 다시 허용한다.
 * 다른 파일이 같은 inode 를 열어 두었다면 여전히 막힐 수 있다. */
void
file_allow_write (struct file *file) {
	ASSERT (file != NULL);
	if (file->deny_write) {
		file->deny_write = false;
		inode_allow_write (file->inode);
	}
}

/* FILE 의 크기(바이트)를 반환한다. */
off_t
file_length (struct file *file) {
	ASSERT (file != NULL);
	return inode_length (file->inode);
}

/* 파일의 현재 위치를 NEW_POS 바이트 지점으로 설정한다. */
void
file_seek (struct file *file, off_t new_pos) {
	ASSERT (file != NULL);
	ASSERT (new_pos >= 0);
	file->pos = new_pos;
}

/* 파일의 현재 위치를 바이트 오프셋으로 반환한다. */
off_t
file_tell (struct file *file) {
	ASSERT (file != NULL);
	return file->pos;
}
