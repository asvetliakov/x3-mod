#pragma once
#include "chase_transition_core.h"
#include "chase_transition_identity_core.h"

// Portable one-use rear-chase restore ticket (docs/reverse-engineering/
// chase-view-transition.md, run60 "identity and restoration boundary"). The
// state holds scalar identities and opaque keys only; every decision is made
// from fresh checked reads at the seam. No engine pointer is retained across
// callbacks and no borrowed context survives a callback.
namespace x3m::chase_transition::detail {
constexpr std::uint32_t restore_mode_pc=0xf0c4b, restore_player_pc=0xe5da1, restore_controller_pc=0x83a03;
constexpr std::uint32_t restore_killed_pcs[2]={0x13b48,0x13b62};
constexpr std::uint32_t restore_reset_prefix[3]={0xedc91,0x16724,0};
constexpr std::uint32_t restore_warp_prefix[5]={0xefbff,0xedba0,0xedbe3,0x1661c,0};
constexpr std::uint32_t restore_monitor_class=0x25e, restore_warp_class=0x96, restore_rear_mode=258;
constexpr std::uint32_t restore_destructor_caller=0x42d402;
constexpr unsigned restore_filter_count=5; // mode, player, controller, killed x2 operand addresses
constexpr unsigned restore_stack_cells=64;
enum RestoreCancel : unsigned {
    cancel_none=0, cancel_destructor=1, cancel_second_destruction=2, cancel_selection=3,
    cancel_identity_store=4, cancel_killed_store=5, cancel_global_slot=6, cancel_task_complete=7,
    cancel_task_abort=8, cancel_epoch=9, cancel_proof=10, cancel_mode_left=11, cancel_lifetime=12,
    cancel_seam_unreadable=13, cancel_expiry=14, cancel_count=15
};
// Consume/transfer refusal subreasons (diagnostic; any nonzero refuses).
enum RestoreRefusal : unsigned {
    refuse_none=0, refuse_thread=1, refuse_task=2, refuse_task_id=3, refuse_context=4, refuse_opcode=5,
    refuse_monitor_identity=6, refuse_monitor_class=7, refuse_mode_cell=8, refuse_handle_cell=9, refuse_ref_cell=10,
    refuse_globals=11, refuse_warp=12, refuse_source=13, refuse_stack=14, refuse_writable=15, refuse_write=16,
    refuse_epoch=17, refuse_provenance=18, refuse_prefix=19, refuse_identity=20, refuse_count=21
};
struct RestoreState {
    bool armed=false, pending=false;
    std::uint32_t arm_cockpit=0, arm_player=0, arm_controller=0, arm_native_script=0, arm_epoch=0, arm_code=0;
    std::uint64_t arm_generation=0;
    std::uint32_t pending_monitor=0, pending_task=0, pending_task_id=0, pending_thread=0, pending_epoch=0;
    std::uint64_t attempt_generation=0; std::uint32_t attempt_mode=0, pending_updates=0;
    std::uint64_t arms=0, arm_refusals=0, transfers=0, consumed=0, writes_failed=0, seam_calls=0;
    std::uint64_t cancels[cancel_count]{}, refusals[refuse_count]{};
    unsigned last_refusal=0;
    void clear_arm(unsigned reason) noexcept { if(armed)++cancels[reason]; armed=false;arm_cockpit=0;arm_generation=0;arm_player=0;arm_controller=0;arm_native_script=0;arm_code=0; }
    void clear_pending(unsigned reason) noexcept { if(pending)++cancels[reason]; take_pending(); }
    void take_pending() noexcept { pending=false;pending_monitor=0;pending_task=0;pending_task_id=0;pending_thread=0;pending_epoch=0;pending_updates=0; }
    void transfer(std::uint32_t monitor,std::uint32_t task,std::uint32_t task_id,std::uint32_t thread,std::uint32_t epoch) noexcept {
        armed=false;pending=true;pending_monitor=monitor;pending_task=task;pending_task_id=task_id;pending_thread=thread;pending_epoch=epoch;pending_updates=0;++transfers;
    }
    void clear_all(unsigned reason) noexcept { clear_arm(reason);clear_pending(reason); }
    void refuse(unsigned why) noexcept { last_refusal=why;++refusals[why]; }
    // 0 idle, 1 armed (operand-address prefilter), 2 pending (every store).
    std::uint32_t filter_mode() const noexcept { return pending?2u:armed?1u:0u; }
};
// Registers the optimized store seam presents (docs: 4a3ffd ABI).
struct SeamRegs { std::uint32_t eax=0,ebx=0,esi=0,edi=0,ebp=0,esp=0,thread=0; };
struct SeamDecode { std::uint32_t vm=0,code=0,pc=0,opcode=0,index=0,discard=0,task=0; };
// Bounded live interpreter stack walk from EBX (current top) to the task's
// stack base. Task+18/+1c are not used: they are stale at this internal seam.
template<class Reader,class CodeRange>
bool live_stack_prefix(std::uint32_t task,std::uint32_t ebx,std::uint32_t code,Reader bytes,CodeRange code_address,
                       const std::uint32_t* expected,unsigned expected_count) {
    auto field=[&](std::uintptr_t base,unsigned offset,auto& out){return bytes(base,offset,&out,sizeof out);};
    std::uint32_t base=0,capacity=0;
    if(!field(task,0x14,base)||!field(task,0x10,capacity)||capacity>1048576||!ebx||ebx>base)return false;
    const std::uint32_t span=base-ebx;
    if(span%5||span/5>capacity||span/5>restore_stack_cells)return false; // truncated proof cancels
    const unsigned cells=span/5;
    unsigned char stack[restore_stack_cells*5]{};
    if(!cells||!bytes(ebx,0,stack,cells*5))return false;
    unsigned count=0;
    for(unsigned i=0;i+1<cells;++i)if(stack[i*5]==10&&stack[(i+1)*5]==3){
        std::uint32_t context=0,word0=0,ret=0;std::uintptr_t at=0;unsigned char op=0;
        std::memcpy(&context,stack+i*5+1,4);std::memcpy(&ret,stack+(i+1)*5+1,4);
        if(!context||(context&3)||!field(context,0,word0))return false;
        if(!code_address(code,ret,1,at)||!field(at,0,op))return false;
        if(count==expected_count||ret!=expected[count])return false;
        ++count;
    }
    return count==expected_count;
}
// Executing-context contract: current context ID, nonnull live class pointer,
// registered static descriptor with the expected identity, variable bounds.
template<class Reader>
bool borrowed_context(IdentityReader<Reader>& r,std::uint32_t context,std::uint32_t class_id,std::uint32_t& id,std::uint32_t (&desc)[14]) {
    std::uint32_t head[3]{};
    return r.pointer(context)&&r.field(context,0,head)&&head[2]&&r.descriptor(head[2],desc)&&desc[0]==class_id&&(id=head[0],true);
}
template<class Reader>
bool integer_cell(IdentityReader<Reader>& r,std::uint32_t context,const std::uint32_t (&desc)[14],unsigned index,std::uint32_t& value) {
    std::uint32_t tag=0;return r.cell(context,desc,index,tag,value)&&tag==1;
}
// Global player/controller and warp/killed scalars from the class table.
template<class Reader>
bool global_scalars(IdentityReader<Reader>& r,std::uint32_t& player,std::uint32_t& controller,std::uint32_t& warp,std::uint32_t& killed) {
    std::uint32_t global[14]{},warp_desc[14]{},warp_context=0;
    if(!r.descriptor(r.classes,global)||global[0]!=0)return false;
    if(!integer_cell(r,r.classes,global,9,player)||!integer_cell(r,r.classes,global,8,controller))return false;
    if(!r.context(restore_warp_class,warp_context,warp_desc)||warp_desc[0]!=restore_warp_class)return false;
    return integer_cell(r,warp_context,warp_desc,3,warp)&&integer_cell(r,warp_context,warp_desc,6,killed);
}
// Decodes the seam: VM from the interpreter frame must be the process VM root,
// the operand pointer must lie in CODE and name a recognised optimized store.
template<class Reader,class CodeRange>
bool seam_decode(const SeamRegs& s,Reader bytes,CodeRange code_address,std::uint32_t vm_root,SeamDecode& d) {
    auto field=[&](std::uintptr_t base,unsigned offset,auto& out){return bytes(base,offset,&out,sizeof out);};
    std::uint32_t root=0;
    if(!field(s.esp,0x18,d.vm)||!field(vm_root,0,root)||root!=d.vm||!d.vm||(d.vm&3)||!field(d.vm,8,d.code))return false;
    if(!d.code||s.edi<=d.code||s.edi-d.code>0x1000000)return false;
    d.pc=s.edi-1-d.code;
    std::uintptr_t at=0;unsigned char raw[4]{};
    if(!code_address(d.code,d.pc,4,at)||!bytes(at,0,raw,4))return false;
    d.opcode=raw[0];d.index=unsigned(raw[1])|(unsigned(raw[2])<<8);d.discard=raw[3];
    if((d.opcode!=0x93&&d.opcode!=0x94)||d.discard!=0x24)return false;
    return field(s.ebp,8,d.task)&&d.task&&!(d.task&3);
}
// Full consume proof at the SelectMode(1) assignment. Returns 0 when every
// check passed and the caller may clear pending and write; otherwise the
// refusal subreason. No write happens here.
template<class Reader,class CodeRange>
unsigned seam_consume_proof(const RestoreState& st,const SeamRegs& s,const SeamDecode& d,std::uint32_t epoch,
                            Reader bytes,CodeRange code_address,std::uint32_t vm_root) {
    auto field=[&](std::uintptr_t base,unsigned offset,auto& out){return bytes(base,offset,&out,sizeof out);};
    if(st.pending_epoch!=epoch)return refuse_epoch;
    if(s.thread!=st.pending_thread)return refuse_thread;
    if(d.task!=st.pending_task)return refuse_task;
    std::uint32_t task_id=0,context=0,task_context=0;
    if(!field(d.task,8,task_id)||task_id!=st.pending_task_id)return refuse_task_id;
    if(!field(s.esp,0x20,context)||!context||context!=s.eax||!field(d.task,0x3c,task_context)||task_context!=context)return refuse_context;
    if(d.opcode!=0x94||d.index!=0||s.esi!=0||d.pc!=restore_mode_pc)return refuse_opcode;
    IdentityReader<Reader> r{bytes};r.vm_root=vm_root;
    if(!r.root()||r.vm!=d.vm)return refuse_globals;
    std::uint32_t monitor=0,desc[14]{};
    if(!borrowed_context(r,context,restore_monitor_class,monitor,desc))return refuse_monitor_class;
    if(monitor!=st.pending_monitor)return refuse_monitor_identity;
    std::uint32_t mode=0,handle=0,ref=0;
    if(!integer_cell(r,context,desc,0,mode)||mode!=restore_rear_mode)return refuse_mode_cell;
    if(!integer_cell(r,context,desc,1,handle)||handle!=0)return refuse_handle_cell;
    std::uint32_t player=0,controller=0,warp=0,killed=0;
    if(!global_scalars(r,player,controller,warp,killed)||player!=st.arm_player||controller!=st.arm_controller||!player)return refuse_globals;
    if(warp!=1||killed!=0)return refuse_warp;
    if(!integer_cell(r,context,desc,11,ref)||ref!=player)return refuse_ref_cell;
    unsigned char source[5]{};std::uint32_t requested=0;
    if(!bytes(s.ebx,0,source,5))return refuse_source;
    std::memcpy(&requested,source+1,4);
    if(source[0]!=1||requested!=1)return refuse_source;
    if(!live_stack_prefix(d.task,s.ebx,d.code,bytes,code_address,restore_reset_prefix,3))return refuse_stack;
    return refuse_none;
}
// Killed store (class96 cell6) at its known PCs: only a well-formed zero store
// keeps the ticket; a nonzero or malformed store cancels.
template<class Reader>
bool killed_store_is_zero(const SeamRegs& s,const SeamDecode& d,Reader bytes,std::uint32_t vm_root) {
    IdentityReader<Reader> r{bytes};r.vm_root=vm_root;
    std::uint32_t warp_context=0,warp_desc[14]{};unsigned char source[5]{};std::uint32_t value=0;
    if(d.opcode!=0x94||d.index!=6||s.esi!=30||!r.root()||r.vm!=d.vm)return false;
    if(!r.context(restore_warp_class,warp_context,warp_desc)||warp_desc[0]!=restore_warp_class||s.eax!=warp_context)return false;
    if(!bytes(s.ebx,0,source,5)||source[0]!=1)return false;
    std::memcpy(&value,source+1,4);return value==0;
}
// Pending transfer at the warp destructor: the exact measured return prefix,
// the armed player/controller scalars, live warp1/killed0 and a borrowed
// monitor context of the expected class.
template<class Reader>
unsigned transfer_proof(const RestoreState& st,const Origin& o,Reader bytes,std::uint32_t vm_root,
                        std::uint32_t& monitor,std::uint32_t& task_id) {
    auto field=[&](std::uintptr_t base,unsigned offset,auto& out){return bytes(base,offset,&out,sizeof out);};
    if(o.valid!=15||o.flags||o.count!=5)return refuse_provenance;
    for(unsigned i=0;i<5;++i)if(o.returns[i]!=restore_warp_prefix[i]||!o.contexts[i])return refuse_prefix;
    if(!o.task||(o.task&3)||!field(o.task,8,task_id))return refuse_task_id;
    IdentityReader<Reader> r{bytes};r.vm_root=vm_root;std::uint32_t desc[14]{};
    if(!r.root())return refuse_globals;
    if(!borrowed_context(r,o.context,restore_monitor_class,monitor,desc))return refuse_monitor_class;
    std::uint32_t player=0,controller=0,warp=0,killed=0;
    if(!global_scalars(r,player,controller,warp,killed)||player!=st.arm_player||controller!=st.arm_controller)return refuse_identity;
    if(warp!=1||killed!=0)return refuse_warp;
    return refuse_none;
}
}
