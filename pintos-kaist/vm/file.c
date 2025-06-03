/* file.c: 메모리 기반 파일 객체(mmaped object) 구현입니다. */

#include "vm/vm.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* 이 구조체는 수정하지 않습니다.
   
   -mmap된 페이지는 무조건 file_page로 분류된다.
   -해당 페이지의 동작 방식은 아래의 함수 테이블을 사용 

   -아래의 함수들이 mmap된 파일을 디스크에서 읽어오고
   -필요시 dirty 페이지를 write-backed 한다.
*/
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* file VM을 초기화합니다. */
void
vm_file_init (void) {
}

/* 파일 기반 페이지를 초기화합니다. */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
        /* 핸들러를 설정합니다. */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}

/* 파일에서 내용을 읽어 페이지를 불러옵니다. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* 페이지의 내용을 파일에 기록하여 내보냅니다. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* 파일 기반 페이지를 파괴합니다. PAGE는 호출자가 해제합니다. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* mmap을 수행합니다. */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
}

/* munmap을 수행합니다. */
void
do_munmap (void *addr) {
}
