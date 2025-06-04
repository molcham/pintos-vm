/* inspect.c: VM 테스트를 위한 도구입니다. */
/* 이 파일은 수정하지 마세요. */

#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "vm/inspect.h"

static void
inspect (struct intr_frame *f) {
	const void *va = (const void *) f->R.rax;
	f->R.rax = PTE_ADDR (pml4_get_page (thread_current ()->pml4, va));
}

/* vm 컴포넌트를 시험하기 위한 도구입니다. int 0x42 인터럽트를 통해 호출합니다.
 * Input:
 *   @RAX - 확인할 가상 주소
 * Output:
 *   @RAX - 입력 주소에 매핑된 물리 주소 */
void
register_inspect_intr (void) {
	intr_register_int (0x42, 3, INTR_OFF, inspect, "Inspect Virtual Memory");
}
