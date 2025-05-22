#include "userprog/syscall.h"
#include "userprog/process.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	// TODO: Your implementation goes here.
	switch (f->R.rax)
	{
		case SYS_EXIT:
		
		break;
		
		case SYS_OPEN: 		
		f->R.rax = open(f->R.rdi);		
		break;

		case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	
	default:
		break;
	}	
}

/* 파라미터로 전달받은 주소의 유효성 검증 */
bool validate_addr(void *addr)
{
	if(addr == NULL)
		return false;

	void *v = pml4_get_page(thread_current()->pml4, addr);

	if( v == NULL)
		return false;

	return true;
}

int open(const char *path)
{
	/* 파라미터 유효성 검증 */
	bool v = validate_addr(path);
	if(!v) return -1;
	
	struct thread *curr = thread_current();		

	/* 파일명과 경로 전달한 뒤 file 획득 */
	struct file *file = filesys_open(path);
	if(file == NULL) return -1;	

	/* fdt에 등록 */
	int fd = curr->next_fd; // 추후 가리키는 file이 없는 번호를 찾는 로직 필요!	
	curr->fdt[fd] = file;
	curr->next_fd++;		

	return fd;	
}

int write(int fd, const void *buffer, unsigned size)
{
	if(fd == 1)
	{
		/* size에 long unsigned int만큼만 담을 수 있음. buffer가 너무 크면 나눠줘야 함 */	
		putbuf(buffer, size);
		return size;
	}
	else if(fd < 2)
	{
		/* 실행 중인 스레드의 fd_table을 확인하여 fd에 매핑되는 file 정의 */		
		// file_write(file, buffer, size);
	}
	else
	{
		/* 에러 처리 */
	}	
}

