#include "devices/serial.h"
#include <debug.h>
#include "devices/input.h"
#include "devices/intq.h"
#include "devices/timer.h"
#include "threads/io.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* PC에서 사용하는 16550A UART의 레지스터 정의.
   여기서는 필요한 부분만 다룬다.

   자세한 내용은 [PC16650D]를 참고. */

/* 첫 번째 시리얼 포트의 I/O 기본 주소. */
#define IO_BASE 0x3f8

/* DLAB=0 상태의 레지스터. */
#define RBR_REG (IO_BASE + 0)   /* 수신 버퍼 레지스터(읽기 전용). */
#define THR_REG (IO_BASE + 0)   /* 송신 보류 레지스터(쓰기 전용). */
#define IER_REG (IO_BASE + 1)   /* 인터럽트 허용 레지스터. */

/* DLAB=1 상태의 레지스터. */
#define LS_REG (IO_BASE + 0)    /* 분주 레치(LSB). */
#define MS_REG (IO_BASE + 1)    /* 분주 레치(MSB). */

/* DLAB 설정과 관계없는 레지스터. */
#define IIR_REG (IO_BASE + 2)   /* 인터럽트 식별 레지스터(읽기 전용). */
#define FCR_REG (IO_BASE + 2)   /* FIFO 제어 레지스터(쓰기 전용). */
#define LCR_REG (IO_BASE + 3)   /* 라인 제어 레지스터. */
#define MCR_REG (IO_BASE + 4)   /* 모뎀 제어 레지스터. */
#define LSR_REG (IO_BASE + 5)   /* 라인 상태 레지스터(읽기 전용). */

/* 인터럽트 허용 비트. */
#define IER_RECV 0x01           /* 데이터 수신 시 인터럽트. */
#define IER_XMIT 0x02           /* 전송 완료 시 인터럽트. */

/* 라인 제어 레지스터 비트. */
#define LCR_N81 0x03            /* 패리티 없음, 8비트 데이터, 1비트 스톱. */
#define LCR_DLAB 0x80           /* 분주 레치 접근 비트(DLAB). */

/* 모뎀 제어 레지스터. */
#define MCR_OUT2 0x08           /* OUT2 라인. */

/* 라인 상태 레지스터. */
#define LSR_DR 0x01             /* 수신 완료: RBR에 데이터 존재. */
#define LSR_THRE 0x20           /* THR 비어 있음. */

/* 전송 모드. */
static enum { UNINIT, POLL, QUEUE } mode;

/* 전송 대기 데이터. */
static struct intq txq;

static void set_serial (int bps);
static void putc_poll (uint8_t);
static void write_ier (void);
static intr_handler_func serial_interrupt;

/* 폴링 모드로 시리얼 포트를 초기화한다.
   포트가 사용 가능해질 때까지 계속 대기하므로 느리지만,
   인터럽트가 준비되기 전까지는 이렇게 동작한다. */
static void
init_poll (void) {
	ASSERT (mode == UNINIT);
        outb (IER_REG, 0);                    /* 모든 인터럽트 비활성화. */
        outb (FCR_REG, 0);                    /* FIFO 비활성화. */
        set_serial (115200);                  /* 115.2 kbps, N-8-1 설정. */
        outb (MCR_REG, MCR_OUT2);             /* 인터럽트 사용을 위해 필요. */
	intq_init (&txq);
	mode = POLL;
}

/* 큐 기반 인터럽트 방식으로 초기화한다.
   이렇게 하면 장치가 준비될 때까지 CPU 시간을 낭비하지 않는다. */
void
serial_init_queue (void) {
	enum intr_level old_level;

	if (mode == UNINIT)
		init_poll ();
	ASSERT (mode == POLL);

	intr_register_ext (0x20 + 4, serial_interrupt, "serial");
	mode = QUEUE;
	old_level = intr_disable ();
	write_ier ();
	intr_set_level (old_level);
}

/* BYTE를 시리얼 포트로 전송한다. */
void
serial_putc (uint8_t byte) {
	enum intr_level old_level = intr_disable ();

	if (mode != QUEUE) {
                /* 아직 인터럽트 방식이 준비되지 않았으면
                   단순 폴링으로 전송한다. */
		if (mode == UNINIT)
			init_poll ();
		putc_poll (byte);
	} else {
                /* 그렇지 않으면 바이트를 큐에 넣고
                   인터럽트 레지스터를 갱신한다. */
		if (old_level == INTR_OFF && intq_full (&txq)) {
                        /* 인터럽트가 꺼진 상태에서 전송 큐가 가득 찼다.
                           큐가 빌 때까지 기다리려면 인터럽트를 다시 켜야 하므로
                           예의상 폴링으로 문자를 하나 보낸다. */
			putc_poll (intq_getc (&txq));
		}

		intq_putc (&txq, byte);
		write_ier ();
	}

	intr_set_level (old_level);
}

/* 폴링 모드로 시리얼 버퍼의 데이터를 모두 비운다. */
void
serial_flush (void) {
	enum intr_level old_level = intr_disable ();
	while (!intq_empty (&txq))
		putc_poll (intq_getc (&txq));
	intr_set_level (old_level);
}

/* 입력 버퍼의 가득 찼는지 여부가 바뀌었을 수 있으니
   수신 인터럽트를 막을지 다시 판단한다.
   버퍼에 문자가 들어오거나 빠질 때 호출된다. */
void
serial_notify (void) {
	ASSERT (intr_get_level () == INTR_OFF);
	if (mode == QUEUE)
		write_ier ();
}

/* 시리얼 포트를 BPS 속도로 설정한다. */
static void
set_serial (int bps) {
        int base_rate = 1843200 / 16;         /* 16550A의 기본 주파수(Hz). */
        uint16_t divisor = base_rate / bps;   /* 클록 분주 값. */

	ASSERT (bps >= 300 && bps <= 115200);

        /* DLAB 활성화. */
	outb (LCR_REG, LCR_N81 | LCR_DLAB);

        /* 데이터 전송 속도 설정. */
	outb (LS_REG, divisor & 0xff);
	outb (MS_REG, divisor >> 8);

        /* DLAB 비활성화. */
	outb (LCR_REG, LCR_N81);
}

/* 인터럽트 허용 레지스터 갱신. */
static void
write_ier (void) {
	uint8_t ier = 0;

	ASSERT (intr_get_level () == INTR_OFF);

        /* 전송할 문자가 있으면 전송 인터럽트 활성화. */
	if (!intq_empty (&txq))
		ier |= IER_XMIT;

        /* 수신 버퍼에 공간이 있으면 수신 인터럽트 활성화. */
	if (!input_full ())
		ier |= IER_RECV;

	outb (IER_REG, ier);
}

/* 포트가 준비될 때까지 대기한 뒤 BYTE를 전송한다. */
static void
putc_poll (uint8_t byte) {
	ASSERT (intr_get_level () == INTR_OFF);

	while ((inb (LSR_REG) & LSR_THRE) == 0)
		continue;
	outb (THR_REG, byte);
}

/* 시리얼 인터럽트 핸들러. */
static void
serial_interrupt (struct intr_frame *f UNUSED) {
        /* UART에서 인터럽트 원인을 읽는다.
           그렇지 않으면 QEMU에서 간혹 인터럽트를 놓칠 수 있다. */
	inb (IIR_REG);

        /* 버퍼 공간이 있고 하드웨어에 데이터가 있으면 계속 읽는다. */
	while (!input_full () && (inb (LSR_REG) & LSR_DR) != 0)
		input_putc (inb (RBR_REG));

        /* 전송할 데이터가 남아 있고 하드웨어가 준비되면 계속 보낸다. */
	while (!intq_empty (&txq) && (inb (LSR_REG) & LSR_THRE) != 0)
		outb (THR_REG, intq_getc (&txq));

        /* 큐 상태에 맞춰 인터럽트 레지스터를 갱신한다. */
	write_ier ();
}
