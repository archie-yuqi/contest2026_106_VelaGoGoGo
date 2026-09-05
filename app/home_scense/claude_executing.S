    .section .rodata.claude,"a",%progbits
    .global claude_executing_data
    .balign 4
claude_executing_data:
    .incbin "claude_executing.bin"
