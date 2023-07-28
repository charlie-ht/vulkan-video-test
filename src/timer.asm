section .text
    global GetTicks

GetTicks:
    ; Function prologue
    push rbp
    mov rbp, rsp

    mfence
    rdtsc
    lfence

    

    ; Arguments are passed in registers, so we retrieve them
    mov rax, rdi  ; rdi contains the first argument
    add rax, rsi  ; rsi contains the second argument

    ; Function epilogue
    pop rbp
    ret