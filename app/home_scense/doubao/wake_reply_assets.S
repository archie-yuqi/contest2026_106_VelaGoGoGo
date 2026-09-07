    .section .rodata.wake_reply,"a",%progbits
    .global wake_reply_raw_data
    .balign 4
wake_reply_raw_data:
    .incbin "doubao/wake_reply_raw.bin"
