/* page_cache.c: 페이지 캐시(버퍼 캐시) 구현 파일. */

#include "vm/vm.h"
static bool page_cache_readahead (struct page *page, void *kva);
static bool page_cache_writeback (struct page *page);
static void page_cache_destroy (struct page *page);

/* 이 구조체는 수정하지 말 것 */
static const struct page_operations page_cache_op = {
	.swap_in = page_cache_readahead,
	.swap_out = page_cache_writeback,
	.destroy = page_cache_destroy,
	.type = VM_PAGE_CACHE,
};

tid_t page_cache_workerd;

/* 파일 VM 초기화 함수 */
void
pagecache_init (void) {
        /* TODO: page_cache_kworkerd 로 동작할 워커 스레드를 생성한다 */
}

/* 페이지 캐시 초기화 */
bool
page_cache_initializer (struct page *page, enum vm_type type, void *kva) {
        /* 핸들러 설정 */
	page->operations = &page_cache_op;

}

/* 스왑인 메커니즘을 이용해 read-ahead 구현 */
static bool
page_cache_readahead (struct page *page, void *kva) {
}

/* 스왑아웃 메커니즘을 이용해 write-back 구현 */
static bool
page_cache_writeback (struct page *page) {
}

/* 페이지 캐시 파괴 */
static void
page_cache_destroy (struct page *page) {
}

/* 페이지 캐시용 워커 스레드 */
static void
page_cache_kworkerd (void *aux) {
}
