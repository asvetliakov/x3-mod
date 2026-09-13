/* Original synthetic workload. No game loading, assets, COM, GPU or hooks. */
#include <windows.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "bloom_return_bridge.h"
_Static_assert(sizeof(BloomCpu)==BC_SIZE,"CPU layout");
_Static_assert(offsetof(BloomCpu,x87)==BC_X87,"x87 layout");
_Static_assert(offsetof(BloomCpu,xmm)==BC_XMM,"XMM layout");
_Static_assert(offsetof(BloomCall,original)==BR_TARGET,"target layout");
_Static_assert(offsetof(BloomCall,caller_stack)==BR_STACK,"stack layout");
_Static_assert(offsetof(BloomCall,ticket)==BR_TICKET,"ticket layout");
_Static_assert(sizeof(BloomCall)==BR_SIZE,"call layout");
void bloom_return_synthetic_original(void);
void bloom_return_clobber(void);
void bloom_return_test_call(void (*target)(void), unsigned);
void bloom_return_catch(void (*target)(void), unsigned);
void (*bloom_return_original)(void)=bloom_return_synthetic_original;
BloomCpu bloom_return_observed, bloom_return_seen_input;
uintptr_t bloom_return_stack_before, bloom_return_stack_after;
static unsigned checks, failures, mode, pre_count, post_count, cleanup_count;
static unsigned abnormal_count, live, caught, filter_count, original_count;
static unsigned expected_live;
static unsigned continuation;
static BloomCpu post_observed;
static const DWORD test_exception_code=0xe3459013;
static void check(int good,const char* label) { ++checks; if(!good){++failures;printf("FAIL %s\n",label);} }
static void raise_original(void) { ULONG_PTR args[2]={0x13572468,0x24681357};RaiseException(test_exception_code,0,2,args); }
void bloom_return_pre(BloomCall* call) {
    ++pre_count;check(call->ticket==0,"invocation starts empty");
    call->ticket=1; ++live;
    bloom_return_clobber();
    if(mode==2)raise_original();
}
void bloom_return_post(BloomCall* call) {
    ++post_count;check(call->ticket==1&&live==1,"normal post owns ticket");
    post_observed=call->output;
    bloom_return_clobber();
    if(mode==3)raise_original();
}
void bloom_return_cleanup(BloomCall* call,int abnormal) {
    ++cleanup_count; abnormal_count+=(abnormal!=0);
    if(call->ticket){call->ticket=0;--live;}
    bloom_return_clobber();
}
void bloom_return_original_body(void) {
    ++original_count;
    if(mode==1||mode==5)raise_original();
    if(mode==4){ volatile unsigned* invalid=(volatile unsigned*)0;*invalid=0xdead; }
    SetLastError(0x87654321);
}
int bloom_return_filter(void* raw) {
    EXCEPTION_POINTERS* p=(EXCEPTION_POINTERS*)raw;
    ++filter_count;
    check(p->ExceptionRecord->ExceptionCode==(mode==4?EXCEPTION_ACCESS_VIOLATION:test_exception_code),"same original exception code reaches outer filter");
    if(mode==4)check(p->ExceptionRecord->NumberParameters==2&&p->ExceptionRecord->ExceptionInformation[0]==1&&p->ExceptionRecord->ExceptionInformation[1]==0,"hardware access violation information forwarded");
    else check(p->ExceptionRecord->NumberParameters==2&&p->ExceptionRecord->ExceptionInformation[0]==0x13572468&&p->ExceptionRecord->ExceptionInformation[1]==0x24681357,"original exception parameters forwarded");
    check(live==expected_live&&cleanup_count==0,"search phase does not run unwind cleanup");
    if(mode==5){++continuation;return EXCEPTION_CONTINUE_EXECUTION;}
    return EXCEPTION_EXECUTE_HANDLER;
}
void bloom_return_caught(void) {++caught;check(live==0&&cleanup_count==1&&abnormal_count==1,"ticket released before outer handler");}
static void reset(unsigned next_mode) {
    mode=next_mode;continuation=0;pre_count=post_count=cleanup_count=abnormal_count=live=caught=filter_count=original_count=0;
}
/* FNSAVE's reserved high words and instruction/data pointers are not numerical
 * state. Compare control/status/tag words and each complete 80-bit x87 slot. */
static void same_cpu(const BloomCpu* a,const BloomCpu* b,const char* label) {
    unsigned i;int same=1;
    for(i=0;i<9;++i)if(i!=3&&a->regs[i]!=b->regs[i])same=0;
    check(same,label);
    check(a->mxcsr==b->mxcsr&&a->error==b->error,"MXCSR status/control and LastError match baseline");
    check(!memcmp(a->xmm,b->xmm,128),"all eight XMM outputs match baseline");
    same=!memcmp(a->x87,b->x87,2)&&!memcmp(a->x87+4,b->x87+4,2)&&!memcmp(a->x87+8,b->x87+8,2);
    {unsigned tags=a->x87[8]|(a->x87[9]<<8),top=(a->x87[5]>>3)&7;
     for(i=0;i<8;++i)if(((tags>>(((top+i)&7)*2))&3)!=3&&memcmp(a->x87+28+i*10,b->x87+28+i*10,10))same=0;}
    check(same,"x87 control status tags and live registers match baseline");
}
int main(void) {
    unsigned skew,kind;BloomCpu direct_in,direct_out;
    setvbuf(stdout,NULL,_IONBF,0);
    for(skew=0;skew<16;skew+=4){
        reset(0);bloom_return_test_call(bloom_return_synthetic_original,skew);
        direct_in=bloom_return_seen_input;direct_out=bloom_return_observed;
        check(direct_out.error==0x87654321&&direct_in.error==0x12345678,"synthetic input/output LastError deliberately differ");
        reset(0);bloom_return_test_call(bloom_return_bridge,skew);
        same_cpu(&direct_in,&bloom_return_seen_input,"all incoming GPRs and flags survive pre callback");
        same_cpu(&direct_out,&bloom_return_observed,"all output GPRs and flags survive post/finally");
        same_cpu(&direct_out,&post_observed,"post sees original output captured before C");
        check(pre_count==1&&post_count==1&&cleanup_count==1&&abnormal_count==0&&live==0&&original_count==1,"normal call consumes ticket exactly once");
        /* PUSHAD records ESP after pushfd. Exact return stack is original's
         * direct entry stack +4; wrapped entry necessarily has an extra frame. */
        check(bloom_return_observed.regs[3]+4==bloom_return_stack_after&&bloom_return_stack_before==bloom_return_stack_after,"exact caller ESP restored");
        for(kind=1;kind<=4;++kind){
            reset(kind);expected_live=1;
            bloom_return_catch(bloom_return_bridge,skew);
            check(caught==1&&filter_count==1&&cleanup_count==1&&abnormal_count==1&&live==0,"pre/original/post/hardware abnormal unwind cleaned once");
            check(post_count==(kind==3?1u:0u),"pre/original exception never enters post callback");
            check(original_count==(kind==2?0u:1u),"pre failure does not call original");
        }
        reset(5);expected_live=1;bloom_return_catch(bloom_return_bridge,skew);
        check(continuation==1&&caught==0&&post_count==1&&cleanup_count==1&&abnormal_count==0&&live==0,"continued exception resumes original and cleans normally");
        same_cpu(&direct_out,&bloom_return_observed,"continued original preserves complete output");
    }
    printf("BLOOM RETURN BRIDGE RESULT checks=%u failures=%u normal=4 exceptional=16 continued=4 stack_alignments=4\n",checks,failures);
    return failures?1:0;
}
