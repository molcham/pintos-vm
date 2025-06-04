/* anon.c: 디스크 이미지가 아닌 페이지, 즉 anonymous page를 위한 구현입니다. */

#include "vm/vm.h"
#include "devices/disk.h"

/* 아래 줄부터는 수정하지 마세요. */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* 이 구조체는 수정하지 않습니다. */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* anonymous page 관련 데이터를 초기화합니다. */
void
vm_anon_init (void) {
	/* TODO: swap_disk를 설정해야 합니다. */
	swap_disk = disk_get(1,1);		
}

/* 파일 매핑을 초기화합니다. */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* 핸들러를 설정합니다. */
	page->operations = &anon_ops;	

	/* anon_page 초기화 */
	struct anon_page *anon_page = &page->anon;			
	anon_page->swap_idx = NULL;	
	anon_page->aux = NULL;
}

/* swap 영역에서 내용을 읽어 페이지를 불러옵니다. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
}

/* 페이지 내용을 swap 영역에 기록하여 내보냅니다. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}

/* anonymous page를 파괴합니다. PAGE는 호출자가 해제합니다. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}
