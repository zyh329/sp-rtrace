version=2.0, arch=i686, timestamp=2011.6.16 15:49:48, process=../bin/shmseg_test, pid=17913, filter=leaks|resolve, backtrace depth=10, origin=sp-rtrace 1.6, 
<1> : segment (shared memory segment) [refcount]
<2> : address (shared memory attachments)
<4> : control (shared memory segment control operation)
: /lib/libc-2.12.1.so => 0x136000-0x28d000
: /usr/lib/libsp-rtrace-main.so.1.0.6 => 0x619000-0x61f000
: /lib/libgcc_s.so.1 => 0x6fc000-0x716000
: /lib/libm-2.12.1.so => 0x808000-0x82c000
: /usr/lib/libsp-rtrace1.so.1.0.6 => 0x9e0000-0x9f4000
: /lib/librt-2.12.1.so => 0xa6b000-0xa72000
: /usr/lib/sp-rtrace/libsp-rtrace-shmsysv.so => 0xc8e000-0xc91000
: /lib/libpthread-2.12.1.so => 0xcde000-0xcf3000
: /lib/libdl-2.12.1.so => 0xeb6000-0xeb8000
: /lib/ld-2.12.1.so => 0xed4000-0xef0000
: /scratchbox/users/wiper/home/wiper/bugs/rtrace/sp-rtrace/tests/bin/shmseg_test => 0x8048000-0x8049000
## tracing module: [0] main (1.0)
## tracing module: [1] shmsysv (1.0)
