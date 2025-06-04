/* uninit.c: 초기화되지 않은 페이지의 구현입니다.
 *
 * 모든 페이지는 처음에 uninit 상태로 생성됩니다. 첫 페이지 폴트가 발생하면
 * 핸들러 체인이 uninit_initialize(page->operations.swap_in)을 호출합니다.
 * 이 함수는 페이지 객체를 초기화해 anon, file, page_cache와 같은 실제
 * 페이지 객체로 변환하고 vm_alloc_page_with_initializer에서 전달된
 * 초기화 콜백을 호출합니다.
 * */

#include "vm/vm.h"
#include "vm/uninit.h"

bool uninit_initialize (struct page *page, void *kva);
static void uninit_destroy (struct page *page);

/* 이 구조체는 수정하지 않습니다. */
static const struct page_operations uninit_ops = {
	.swap_in = uninit_initialize,
	.swap_out = NULL,
	.destroy = uninit_destroy,
	.type = VM_UNINIT,
};

/* 이 함수는 수정하지 않습니다. */
void
uninit_new (struct page *page, void *va, vm_initializer *init,
		enum vm_type type, void *aux,
		bool (*initializer)(struct page *, enum vm_type, void *)) {
	ASSERT (page != NULL);

	*page = (struct page) {
		.operations = &uninit_ops,
		.va = va,
                .frame = NULL, /* 현재는 frame이 없음 */
		.uninit = (struct uninit_page) {
			.init = init,
			.type = type,
			.aux = aux,
			.page_initializer = initializer,
		}
	};
}

/* 첫 페이지 폴트 시 페이지를 초기화합니다. */
bool
uninit_initialize (struct page *page, void *kva) {
	struct uninit_page *uninit = &page->uninit;

	/* 우선 값을 가져옵니다. page_initialize가 이 값을 덮어쓸 수 있습니다. */
	vm_initializer *init = uninit->init;
	void *aux = uninit->aux;

	/* TODO: 이 함수를 수정해야 할 수도 있습니다. */
	return uninit->page_initializer (page, uninit->type, kva) &&
		(init ? init (page, aux) : true);
}

/* uninit_page가 보유한 자원을 해제합니다.
 * 대부분의 페이지는 다른 객체로 변환되지만, 실행 중 한 번도 참조되지 않은
 * uninit 페이지가 프로세스 종료 시 남아 있을 수 있습니다.
 * PAGE는 호출자가 해제합니다. */
static void
uninit_destroy (struct page *page) {
	struct uninit_page *uninit UNUSED = &page->uninit;
        /* TODO: 이 함수를 완성하세요.
         * TODO: 특별히 할 일이 없다면 그냥 반환하면 됩니다. */
}
