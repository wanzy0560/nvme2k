;// THIS IS GARBAGE, IT DOESNT COMPILE BUT YOU WANTED IT SO THERE IT IS
;//
;// atomic_alpha.s - Alpha AXP atomic operations
;// Implements atomic operations using Alpha load-locked/store-conditional
;//

        .text
        .align  4

;//
;// AtomicIncrement - Atomically increment a ULONG value
;// Arguments: a0 = pointer to ULONG
;// Returns: v0 = new value (after increment)
;//
        .globl  AtomicIncrement
        .ent    AtomicIncrement
AtomicIncrement:
        .frame  sp, 0, ra
        mb                          ; memory barrier before
retry_inc:
        ldl_l   t0, 0(a0)          ; load-locked
        addl    t0, 1, t1          ; increment
        mov     t1, v0             ; save return value
        stl_c   t1, 0(a0)          ; store-conditional
        beq     t1, retry_inc      ; retry if failed (t1=0)
        mb                          ; memory barrier after
        ret     zero, (ra), 1
        .end    AtomicIncrement

;//
;// AtomicRead - Atomically read a ULONG value with memory barriers
;// Arguments: a0 = pointer to ULONG
;// Returns: v0 = value
;//
        .globl  AtomicRead
        .ent    AtomicRead
AtomicRead:
        .frame  sp, 0, ra
        mb                          ; memory barrier before
        ldl     v0, 0(a0)          ; load value
        mb                          ; memory barrier after
        ret     zero, (ra), 1
        .end    AtomicRead

;//
;// AtomicCompareExchange - Atomically compare and exchange
;// Arguments: a0 = pointer to destination
;//            a1 = exchange value
;//            a2 = comparand value
;// Returns: v0 = 1 if succeeded, 0 if failed
;//
        .globl  AtomicCompareExchange
        .ent    AtomicCompareExchange
AtomicCompareExchange:
        .frame  sp, 0, ra
        mb                          ; memory barrier before
retry_cmpxchg:
        ldl_l   t0, 0(a0)          ; load-locked current value
        cmpeq   t0, a2, t1         ; compare with comparand
        beq     t1, failed_cmpxchg ; if not equal, fail
        mov     a1, t2             ; save exchange value
        stl_c   t2, 0(a0)          ; store-conditional
        beq     t2, retry_cmpxchg  ; retry if failed (t2=0)
        mov     1, v0              ; return TRUE (1)
        mb                          ; memory barrier after
        ret     zero, (ra), 1
failed_cmpxchg:
        mov     0, v0              ; return FALSE (0)
        mb                          ; memory barrier after
        ret     zero, (ra), 1
        .end    AtomicCompareExchange

;//
;// AtomicSet - Atomically write a ULONG value with memory barriers
;// Arguments: a0 = pointer to destination
;//            a1 = value to write
;// Returns: void
;//
        .globl  AtomicSet
        .ent    AtomicSet
AtomicSet:
        .frame  sp, 0, ra
        mb                          ; memory barrier before
        stl     a1, 0(a0)          ; store value
        mb                          ; memory barrier after
        ret     zero, (ra), 1
        .end    AtomicSet

        .end
