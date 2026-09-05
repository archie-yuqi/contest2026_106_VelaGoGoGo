    .section .rodata.claude,"a",%progbits
    .global claude_thinking_data
    .balign 4
claude_thinking_data:
    .incbin "claude_thinking.bin"
