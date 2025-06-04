/* vm.c: 가상 메모리 객체를 위한 일반적인 인터페이스입니다. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"


#include "vm/file.h"
#include "list.h"
#include "include/threads/vaddr.h"
#include "lib/kernel/hash.h"
#include "include/threads/mmu.h"
#include "userprog/process.h"
#include "threads/vaddr.h"




/* 각 서브시스템의 초기화 코드를 호출하여
 * 가상 메모리 하위 시스템을 초기화합니다. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();

#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
        /* 위의 줄은 수정하지 마세요. */
        /* TODO: 여기에 코드를 작성하세요. */

	/* frame table 초기화 */
	list_init(&frame_table);
}

/* 페이지의 유형을 얻습니다. 초기화된 이후에 어떤 타입이 될지
 * 알고 싶을 때 유용하며, 이 함수는 이미 완전히 구현되어 있습니다. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

// 윤석이형 존잘 개 섹시한 남자 여자친구 100명  심심한데 여친 구함 

/* 헬퍼 함수들 */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* 초기화 함수를 가지고 미리 생성해 두는 페이지 객체를 만듭니다.
 * 페이지를 직접 만들지 말고 이 함수나 `vm_alloc_page`를 통해 생성하세요. */
// bool
// vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
// 		vm_initializer *init, void *aux) {

// 	/* 정의된 VM 타입이 아니면 에러 처리 */		
// 	ASSERT (VM_TYPE(type) != VM_UNINIT)

// 	struct supplemental_page_table *spt = &thread_current ()->spt;

// 	/* TODO: 페이지를 만들고 VM 타입에 맞는 initializer를 얻은 뒤
// 	 * TODO: uninit_new를 호출하여 "uninit" 페이지 구조체를 생성합니다.
// 	 * TODO: 호출 후 필요한 필드를 수정해야 합니다. */
// 	/* TODO: 생성한 페이지를 spt에 넣어주세요. */

// 	/* upage가 이미 사용 중인지 확인합니다. */
// 	if (spt_find_page (spt, upage) == NULL) {		
		
// 		struct page new_page;
// 		new_page.writable = false;

// 		uninit_new(&new_page, upage, init, type, aux, new_page.uninit.page_initializer);
// 		bool result = spt_insert_page(spt, &new_page);
		
// 		return result;              
// 	}
// // err:
// 	return false;
// }


bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
                                    vm_initializer *init, void *aux)
{
    ASSERT(VM_TYPE(type) != VM_UNINIT)

    struct supplemental_page_table *spt = &thread_current()->spt;

    /* Check wheter the upage is already occupied or not. */
    // upage가 이미 사용 중인지 확인합니다.
    if (spt_find_page(spt, upage) == NULL)
    {
        /* TODO: Create the page, fetch the initialier according to the VM type,
         * TODO: and then create "uninit" page struct by calling uninit_new. You
         * TODO: should modify the field after calling the uninit_new. */
         
        // 1) 페이지를 생성하고,
        struct page *p = (struct page *)malloc(sizeof(struct page));
        
        // 2) type에 따라 초기화 함수를 가져와서
        typedef bool (*page_initializer)(struct page *, enum vm_type, void *);
        page_initializer init_func = NULL;
        switch (VM_TYPE(type))
        {
        case VM_ANON:
            init_func = anon_initializer;
            break;
        case VM_FILE:
            init_func = file_backed_initializer;
            break;
        }

        // 3) "uninit" 타입의 페이지로 초기화한다.
        uninit_new(p, upage, init, type, aux, init_func);

        // 필드 수정은 uninit_new를 호출한 이후에 해야 한다.
        p->writable = writable;

        // 4) 생성한 페이지를 SPT에 추가한다.
        if (spt_insert_page(spt,p)){
			return true;
		}
		free(p);
    }
err:
    return false;
}






/* spt에서 VA에 해당하는 페이지를 찾아 반환합니다. 실패 시 NULL을 돌려줍니다. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {	    
	
	/* 전달 받은 va를 포함한 dummie_page를 만들어 동일한 page가 있는지 확인 */
	
	struct page *dummie_page;
	dummie_page =(struct page*)malloc(sizeof(struct page));
	//dummie_page.va = pg_round_down(va);	/* 추후 파악 필요 */	

	dummie_page->va = pg_round_down(va);
	struct hash_elem *hl;

	/* 동일한 page의 hash_elem을 통해 page 확보 */
	if(!hash_empty(&spt ->hash_table)){

		hl= hash_find(&spt->hash_table, &dummie_page->hash_elem);
	}
	free(dummie_page);
	
	// if(page != hash_find(&spt->hash_table, &dummie_page.hash_elem))		
	// 	page = hash_entry(hl, struct page, hash_elem);

	// return page;

	if(hl == NULL){
		return NULL;
	}

	dummie_page =hash_entry(hl,struct page,hash_elem);

	return dummie_page;
}

/* PAGE를 spt에 검증 후 삽입합니다. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED, struct page *page UNUSED) {
	int succ = false;
	
	if(spt_find_page(spt, page->va) == NULL)
	{
		hash_insert(&spt->hash_table, &page->hash_elem);
		succ = true;
	}

	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	
	hash_delete(&spt->hash_table, &page -> hash_elem);
	vm_dealloc_page (page);
	return true;
}

/* 앞으로 쫓아낼 프레임을 얻습니다. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
         /* TODO: 어떤 프레임을 제거할지는 구현하기 나름입니다. */

	return victim;
}

/* 한 페이지를 교체하고 그에 해당하는 프레임을 반환합니다.
 * 실패하면 NULL을 반환합니다.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
        /* TODO: victim을 swap 영역으로 내보내고, 그 프레임을 반환하세요. */

	return NULL;
}

/* palloc()을 이용해 프레임을 얻습니다. 남는 페이지가 없다면 하나를
 * 제거하여 돌려줍니다. 즉 사용자 풀 메모리가 가득 차도 이 함수는
 * 프레임을 얻기 위해 기존 프레임을 내보낸 뒤 유효한 주소를 반환합니다.*/
static struct frame *
vm_get_frame (void) {


	struct frame *new_frame = malloc(sizeof(struct frame));
	new_frame->kva = palloc_get_page(PAL_USER | PAL_ZERO);
	
	/* 할당할 frame이 없으면 교체 로직 호출(추가 구현) */

	if(new_frame == NULL){
		PANIC("TODO");
	}


	ASSERT (new_frame != NULL);
	ASSERT (new_frame->page == NULL);

	/* 할당받은 frame을 frame table에 삽입 */
	list_push_front(&frame_table, &new_frame->frame_elem);
	return new_frame;
    
	// struct frame *frame = NULL;
    // void* kva = palloc_get_page(PAL_USER);

	// if(kva == NULL){
	// 	frame = vm_evict_frame();
		
	// 	frame->page = NULL;
	// 	return frame;
	// }

	// frame =(struct frame*)malloc(sizeof(struct frame));
	// frame->kva =kva;
	// frame->page = NULL;


    // list_push_back(&frame_table,&frame->frame_elem);

	// ASSERT (frame != NULL);
	// ASSERT (frame->page == NULL);
	// return frame;

}

/* 스택을 확장합니다. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* write_protected 페이지에서의 fault 처리 */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* 성공하면 true를 반환합니다 */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) 
{

	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;

	
	/* 페이지 폴트 유형
	 * 0. 잘못된 주소 접근 (미할당 주소)
	 * 1. Lazy_Loading
	 * 2. 스왑 아웃된 상태
	 * 3. 쓰기 권한 에러
	 * 4. 커널 주소 접근 */



    if(addr == NULL){
		return false;
	}

	if(is_kernel_vaddr(addr)){
		return false;
	}

	if(not_present){

		page = spt_find_page(spt, addr);

		/* 프로세스에 할당된 가상 주소가 아닐 경우 함수 종료 */
	    if(page == NULL){
			return false;
		}
		/* 쓰기 권한 에러, 또는 커널 주소 접근이면 함수 종료 */
	    //if(write == true || user == false)	
		  //return false;
        
		if(write ==1 && page->writable ==0 ){
			return false;
		}


		return vm_do_claim_page (page);


	}
	return false;
}

/* 페이지를 해제합니다.
 * 이 함수는 수정하지 마세요. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* VA에 할당된 페이지를 SPT를 탐색하여 확보합니다. */
bool
vm_claim_page (void *va UNUSED) {			
	//struct supplemental_page_table *spt = &thread_current()->spt;

	/* 전달받은 va를 통해 page 확보 */
	struct page *page = NULL;
	page = spt_find_page(&thread_current()->spt, va);	
	
	/* page가 없을 경우 함수 종료 (추후 페이지 폴트 처리?)*/
	if(page == NULL)
		return false;

	return vm_do_claim_page (page);
}

/* 확보한 PAGE를 FRAME에 매핑하여 MMU 설정을 완료합니다. */
static bool
vm_do_claim_page (struct page *page) {
	/* 매핑할 frame 획득 */
	struct frame *frame = vm_get_frame ();

	/* 매핑할 frame이 없으면 함수 종료 (추후 교체 로직 도입 필요)*/
	if(frame == NULL)
		return false;

	/* page와 frame의 상호 참조 */
	frame->page = page;
	page->frame = frame;
	
	struct thread *t = thread_current ();

	/* 해당 가상 주소에 이미 페이지가 없는지 확인한 뒤 매핑한다. */
	bool result = (pml4_get_page (t->pml4, page->va) == NULL && pml4_set_page (t->pml4, page->va, frame->kva, page->writable));
	if(result == false)	
		return false;

	return swap_in (page, frame->kva);
}

/* 새로운 supplemental page table을 초기화합니다 */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED)
{	
	/* SPT 내부의 해시 테이블 초기화 */
	hash_init(&spt->hash_table, get_hash, cmp_page, NULL);
}

/* src에서 dst로 supplemental page table을 복사합니다 */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* supplemental page table이 가진 자원을 해제합니다 */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
        /* TODO: 스레드가 보유한 모든 supplemental_page_table을 파괴하고
         * TODO: 수정된 내용을 저장소에 모두 반영하세요. */
}

/* hash_elem으로 bucket_idx 획득 */
uint64_t get_hash (const struct hash_elem *e, void *aux)
{
	struct page *upage = hash_entry(e, struct page, hash_elem);
	void *va = upage->va;

	return hash_bytes(&va, sizeof(va));	
}

/* 두 페이지간의 대소관계 비교(정렬에 큰 의미 없음) */
bool cmp_page (const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
	void *a_va = hash_entry(a, struct page, hash_elem)->va;
	void *b_va = hash_entry(b, struct page, hash_elem)->va;

	return (uint64_t *)a_va < (uint64_t *)b_va ? true : false;
}

