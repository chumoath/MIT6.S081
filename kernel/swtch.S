# Context switch
#
#   void swtch(struct context *old, struct context *new);
# 
# Save current registers in old. Load from new.	

# callee    callee must store before use, the caller can use directly after call
#           ra is a exception, it is the return address of swtch
#                    the scheduler store the ra of the user of call scheduler, then call swtch, set ra to beghind swtch
#                    by store this ra to proc, the process can resume when it will be scheduled again

#          s* is the register that callee must store before use
#                 because the caller can use directly (the value before call) after call and no need to store before call
.globl swtch
swtch:
        sd ra, 0(a0)
        sd sp, 8(a0)
        sd s0, 16(a0)
        sd s1, 24(a0)
        sd s2, 32(a0)
        sd s3, 40(a0)
        sd s4, 48(a0)
        sd s5, 56(a0)
        sd s6, 64(a0)
        sd s7, 72(a0)
        sd s8, 80(a0)
        sd s9, 88(a0)
        sd s10, 96(a0)
        sd s11, 104(a0)

        ld ra, 0(a1)                # new process, p->context.ra = (uint64)forkret;      proc.c  allocproc
                                    # set the spec to p->trapfram->epc(exec will set or munually set), the sret will use spec when from supervison to user        
        ld sp, 8(a1)                # new process, p->context.sp = p->kstack + PGSIZE;  so ret will return to somewhere in kernel
        ld s0, 16(a1)
        ld s1, 24(a1)
        ld s2, 32(a1)
        ld s3, 40(a1)
        ld s4, 48(a1)
        ld s5, 56(a1)
        ld s6, 64(a1)
        ld s7, 72(a1)
        ld s8, 80(a1)
        ld s9, 88(a1)
        ld s10, 96(a1)
        ld s11, 104(a1)
        
        ret

	
