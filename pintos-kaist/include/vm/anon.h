#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
struct page;
enum vm_type;

/*
===================================================================
-구현하면서 익명 페이지의 상태나 정보를 저장을 위한 필드를 자유롭게 추가


===================================================================
*/


struct anon_page {

    
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
