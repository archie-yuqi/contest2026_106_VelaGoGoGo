    .section .rodata.claude,"a",%progbits
    .global claude_idle_data
    .balign 4
claude_idle_data:
    .incbin "claude_idle.bin"
