//
// atomic.h - Atomic operations and synchronization primitives
// Windows 2000 ScsiPort miniport drivers cannot use kernel InterlockedXxx functions
// so we implement them using inline assembly for x86 and Alpha AXP
//

#ifndef _ATOMIC_H_
#define _ATOMIC_H_

//
// ATOMIC type - Use this for variables that need atomic access
// This is just volatile ULONG, but makes code more self-documenting
//
typedef volatile ULONG ATOMIC;

//
// Atomic operations for SMP safety
//
#if defined(_M_IX86)

// x86 inline assembly for atomic increment
// Returns the NEW value (after increment)
static __inline ULONG AtomicIncrement(volatile ULONG *Addend)
{
    ULONG result;
    __asm {
        mov ecx, Addend
        mov eax, 1
        lock xadd dword ptr [ecx], eax
        inc eax
        mov result, eax
    }
    return result;
}

// x86 inline assembly for atomic read (with memory barrier)
static __inline ULONG AtomicRead(volatile ULONG *Value)
{
    ULONG result;
    __asm {
        mov ecx, Value
        mov eax, dword ptr [ecx]
        mov result, eax
    }
    return result;
}

// x86 inline assembly for atomic compare-exchange
// Returns TRUE if exchange succeeded, FALSE otherwise
static __inline BOOLEAN AtomicCompareExchange(volatile ULONG *Destination, ULONG Exchange, ULONG Comparand)
{
    ULONG result;
    __asm {
        mov ecx, Destination
        mov edx, Exchange
        mov eax, Comparand
        lock cmpxchg dword ptr [ecx], edx
        setz al
        movzx eax, al
        mov result, eax
    }
    return (BOOLEAN)result;
}

// x86 inline assembly for atomic write (with memory barrier)
static __inline VOID AtomicSet(volatile ULONG *Destination, ULONG Value)
{
    __asm {
        mov ecx, Destination
        mov eax, Value
        lock xchg dword ptr [ecx], eax
    }
}

#elif defined(_M_ALPHA)

// Alpha AXP atomic operations implemented in assembly (atomic_alpha.s)
// MSVC for Alpha doesn't support __asm blocks, so we use external assembly

#ifdef __cplusplus
extern "C" {
#endif

// Atomic operations implemented in atomic_alpha.s
ULONG AtomicIncrement(volatile ULONG *Addend);
ULONG AtomicRead(volatile ULONG *Value);
BOOLEAN AtomicCompareExchange(volatile ULONG *Destination, ULONG Exchange, ULONG Comparand);
VOID AtomicSet(volatile ULONG *Destination, ULONG Value);

#ifdef __cplusplus
}
#endif

#else
#error "Unsupported architecture - need atomic operations for x86 or Alpha AXP"
#endif

#endif // _ATOMIC_H_
