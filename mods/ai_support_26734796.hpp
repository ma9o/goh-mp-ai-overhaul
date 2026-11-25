#pragma once

// Shared native boundaries for Auto-Medic and Auto-Resupply. See both build notes.
#include "auto_smoke_module.hpp"
#include <array>

namespace goh::ai_support {
namespace smoke = goh::auto_smoke;
using smoke::Address;
using smoke::read;
using smoke::Vec3;
using smoke::World;
using Byte = std::uint8_t;
inline constexpr bool runtime_allowed = false;
inline constexpr auto& executable_sha256 = smoke::executable_sha256;
inline Address base{};
template<class Fn> inline Fn native(Address rva) { return reinterpret_cast<Fn>(base + rva); }
using Unary = Address (*)(Address);
using Test = Byte (*)(Address);
using BinaryTest = Byte (*)(Address, Address);

inline bool host() {
    return base && !native<smoke::Role>(0xA7A80)() && !native<smoke::Role>(0xC2B7A0)()
                && native<smoke::Role>(0xC2B740)();
}
// Construct only at the verified actor slot 23 on the serial simulation path.
inline std::atomic<DWORD> main_tid{};
inline DWORD actor_tls=TLS_OUT_OF_INDEXES;
inline bool serial_thread() {return main_tid.load(std::memory_order_relaxed)==GetCurrentThreadId();}
inline Address current_actor() {
    if(actor_tls==TLS_OUT_OF_INDEXES)return 0;
    DWORD saved=GetLastError();
    auto actor=reinterpret_cast<Address>(TlsGetValue(actor_tls));
    SetLastError(saved); return actor;
}
class ActorQuantScope {
    Address previous{};
    bool entered{};
public:
    explicit ActorQuantScope(Address actor) {
        if(!actor || !host() || actor_tls==TLS_OUT_OF_INDEXES)return;
        DWORD unset{};
        main_tid.compare_exchange_strong(unset,GetCurrentThreadId(),std::memory_order_relaxed);
        if(!serial_thread())return;
        previous=current_actor();entered=TlsSetValue(actor_tls,reinterpret_cast<void*>(actor))!=0;
    }
    ~ActorQuantScope(){if(entered)TlsSetValue(actor_tls,reinterpret_cast<void*>(previous));}
    ActorQuantScope(const ActorQuantScope&)=delete;
    ActorQuantScope& operator=(const ActorQuantScope&)=delete;
};
inline bool callback(Address actor, World& w) {
    return actor && host() && serial_thread() && current_actor()==actor && smoke::world(base,w);
}
struct Span {
    Address first, end;
    bool valid(std::size_t stride, std::size_t cap) const {
        return end >= first && (first || !end) && (end-first)%stride == 0 && (end-first)/stride <= cap;
    }
};
inline Span span(Address object, unsigned offset) { return object ? read<Span>(object, offset) : Span{}; }
inline unsigned owner(Address actor) { return read<Byte>(actor, 0xDA8); }
inline Vec3 pos(Address entity) { return read<Vec3>(entity, 0x74); }
inline bool within(Vec3 a, Vec3 b, float radius) {
    return radius >= 0 && std::isfinite(radius) && smoke::distance2(a,b) <= radius*radius;
}
struct Identity {
    Address ptr{}, definition{};
    unsigned id{};
    bool matches(Address live) const {
        return live && live == ptr && read<unsigned>(live, 0xC0) == id &&
               read<Address>(live, 0xB0) == definition;
    }
};
inline Identity identity(Address a) { return {a, read<Address>(a, 0xB0), read<unsigned>(a, 0xC0)}; }
inline bool live_entity(Address a) {
    return a && (read<Byte>(a,0x30) & 1) && !(read<unsigned>(a,0x38) & 0x10A20u) &&
           !(read<Byte>(a,0x3A) & 1) && read<unsigned>(a,0xC0) != 0xffffffffu;
}
template<class Visit> inline void entities(const World& w, Visit visit) {
    const Span v = span(w.scene,0x48);
    if (!v.valid(8,131072)) return;
    for (auto p=v.first; p!=v.end; p+=8) {
        const Address a=read<Address>(p);
        if (live_entity(a)) visit(a);
    }
}
inline Address resolve(const World& w, Identity key) {
    if (!key.ptr) return 0;
    // Resolve through a fresh scene-owned borrow before inspecting the identity.
    const Span v=span(w.scene,0x48);
    if (v.valid(8,131072))
        for (auto p=v.first; p!=v.end; p+=8) {
            const auto a=read<Address>(p);
            if (a==key.ptr && live_entity(a) && key.matches(a)) return a;
        }
    return 0;
}
inline bool policy_allows(unsigned policy, unsigned flags, bool medic) {
    const auto fire=(policy>>10)&3;
    return !(policy & 3) && ((policy>>12)&3)==0 && !(flags & 0x1010003fu) &&
           (!medic || fire==0 || fire==2);
}
inline bool autonomous(Address actor, bool medic) {
    if (!live_entity(actor) || !smoke::has_actor(actor) || owner(actor)>=18 ||
        !read<std::uint16_t>(actor,0xDAA) || !(read<unsigned>(actor,0xDB4)&0x100) ||
        (read<unsigned>(actor,0x1C0)&(1u<<28))) return false;
    const Address b=read<Address>(actor,0xDC8);
    if (!b || read<Address>(b,0x38)!=actor) return false;
    const auto vt=read<Address>(b);
    if (vt!=base+0x110FF88 && (medic || vt!=base+0x101FD68)) return false;
    const Address ch=read<Address>(actor,0xDE0), wp=read<Address>(actor,0xDD8);
    if (!policy_allows(read<unsigned>(b,0x398),read<unsigned>(b,0x39C),medic) ||
        !ch || !wp || read<int>(ch,0x18) || (read<Byte>(wp,0x70)&2) ||
        read<int>(b,0x88) || read<Address>(b,0x98)!=read<Address>(b,0xA0)) return false;
    if (vt==base+0x110FF88) {
        const Address root=read<Address>(b,0xE0);
        if (!root || read<Address>(root)!=base+0x1110CD0 || read<Address>(root,0x30)!=b ||
            read<int>(root,0x4C0) || read<Address>(root,0x568)) return false;
    }
    return true;
}
inline bool free_slot(Address actor) {
    const Address b=read<Address>(actor,0xDC8);
    // Admission is stricter than interruption. Every occupied order, including
    // an active grenade dodge buried in a complex queue, keeps the actor.
    return !(read<unsigned>(b,0x39C)&0x01000000u) && !read<Address>(b,0xF0);
}
inline Address manager(Address actor) {
    const auto agg=read<Address>(actor,0x578);
    return agg ? read<Address>(agg,0x28) : 0;
}
inline Address box(Address actor) {
    auto m=manager(actor), b=m ? read<Address>(m,0xA0) : 0;
    return b && read<Address>(b,0x50)==actor ? b : 0;
}
inline bool inventory_free(Address actor) {
    auto m=manager(actor);
    return m && !read<Address>(m,0xC8) && read<Address>(m,0xA8)==read<Address>(m,0xB0);
}
inline bool tag(Span rows,const char* name) {
    if (rows.valid(0x60,4096))
        for (auto p=rows.first;p!=rows.end;p+=0x60) if (smoke::SmokeTagEquals(p,name)) return true;
    return false;
}

// Object and terrain costs for one direct corridor. Pre-clamp endpoints before
// each short line so the native terrain sampler never enters its jitter branch.
inline bool corridor(const World& w, Address actor, Vec3 end) {
    const Vec3 start=pos(actor);
    if (!smoke::finite(start) || !smoke::finite(end)) return false;
    const float length=std::sqrt(smoke::length2_xy(start,end));
    if (!(length<=4096)) return false;
    alignas(16) std::byte context[0xF0]{};
    using Init=void*(*)(void*,Address);
    native<Init>(0x8DF010)(context,actor);
    struct Release { void* ctx; ~Release(){ native<void(*)(void*)>(0xBF640)(ctx); } } release{context};
    using Clamp=Byte(*)(void*,Vec3*);
    using Block=Byte(*)(void*,const Vec3*,const Vec3*,Address);
    const int steps=1+static_cast<int>(length/12.f);
    Vec3 last=start;
    if (native<Clamp>(0x8DF4D0)(context,&last)) return false;
    const auto terrain=read<Address>(w.scene,0x18);
    for (int i=1;i<=steps;++i) {
        const float t=static_cast<float>(i)/steps;
        Vec3 next{start.x+(end.x-start.x)*t,start.y+(end.y-start.y)*t,end.z};
        float xy[]{next.x,next.y};
        if (native<Clamp>(0x8DF4D0)(context,&next) || !smoke::dry_ground(base,terrain,xy) ||
            !(smoke::length2_xy(last,next)<16.f*16.f) ||
            native<Block>(0x8C19E0)(context,&last,&next,0)) return false;
        last=next;
    }
    return true;
}
inline bool ground(const World& w, Vec3& p) {
    float xy[]{p.x,p.y};
    auto terrain=read<Address>(w.scene,0x18);
    if (!smoke::finite(p) || !smoke::dry_ground(base,terrain,xy)) return false;
    p.z=native<float(*)(Address,const float*,int)>(0xA5D440)(terrain,xy,0);
    return smoke::finite(p);
}
inline bool approach(const World& w, Address actor, Vec3 center, float radius, Vec3& goal) {
    const Vec3 self=pos(actor);
    float dx=self.x-center.x, dy=self.y-center.y, len=std::hypot(dx,dy);
    if (!std::isfinite(len) || !(radius>0)) return false;
    if (within(self,center,radius)) {goal=self; return true;}
    if (len>0) { dx/=len; dy/=len; } else {dx=1;dy=0;}
    constexpr float turns[]={0,.7853982f,-.7853982f,1.5707963f,-1.5707963f,3.1415927f};
    for (float angle: turns) {
        float c=std::cos(angle), s=std::sin(angle);
        Vec3 p{center.x+radius*.8f*(dx*c-dy*s), center.y+radius*.8f*(dx*s+dy*c),center.z};
        if (ground(w,p) && within(p,center,radius) && corridor(w,actor,p)) {goal=p;return true;}
    }
    return false;
}

struct Danger { bool observed{}, unsafe{}; Vec3 towards{}; bool direction{}; };
// Use last observed positions. Incoming fire on an observer near the
// destination also vetoes a trip when the shooter's position is outside it.
inline void observe(const World& w, Address observer,Vec3 point,float radius,Danger& d) {
    const auto sen=read<Address>(observer,0xDD0);
    if (!sen || read<Address>(sen,0x260)!=observer) return;
    const Span subjects=span(sen,0x280);
    if (!subjects.valid(8,4096)) return;
    const bool nearby=within(pos(observer),point,radius);
    d.observed |= nearby;
    if (nearby && smoke::recent(w,read<unsigned>(sen,0x2F4),4)) d.unsafe=true;
    for (auto p=subjects.first;p!=subjects.end;p+=8) {
        const auto sub=read<Address>(p);
        if (!sub || read<Byte>(sub,0x6C)) continue;
        auto rel=read<unsigned>(sub,0x14), f=read<unsigned>(sub,0x10);
        if ((rel&0x1e)!=2 || !(f&(1u<<28))) continue;
        const Vec3 seen=read<Vec3>(sub,0x28);
        bool hit=(rel&0x180) && smoke::recent(w,read<unsigned>(sub,0x68),4);
        if ((nearby && hit) || (((f&(1u<<29)) || smoke::recent(w,read<unsigned>(sub,0x64),5)) &&
                                 within(seen,point,radius))) {
            d.unsafe=true;
            if (smoke::finite(seen) && (!d.direction || smoke::distance2(seen,point)<smoke::distance2(d.towards,point))) {
                d.direction=true; d.towards=seen;
            }
        }
    }
}
inline Danger danger(const World& w,Address actor,Vec3 point,float metres) {
    Danger d{};
    observe(w,actor,point,metres*w.length,d);
    entities(w,[&](Address ally){
        if (ally!=actor && smoke::has_actor(ally) && within(pos(ally),point,metres*w.length) &&
            smoke::allies(base,owner(actor),owner(ally))) observe(w,ally,point,metres*w.length,d);
    });
    return d;
}

// A stable native order listener. The stock deleting destructor clears ptr.
// A saved order may survive a deadline or scene reset. Keep its guard slot
// until native destruction even when the policy has abandoned the assignment.
struct OrderHandle { Address ptr{}; int result{}; };
using Permit=bool(*)(const World&,Address,Address);
struct Watched { OrderHandle handle{}; Permit permit{}; bool skipped_start{}; std::uint64_t generation{}; };
struct OrderRef {
    Watched* watch{}; std::uint64_t generation{};
    Address get() const {return watch && watch->generation==generation ? watch->handle.ptr : 0;}
    void abandon() {if (get()) watch->permit=nullptr; *this={};}
};
inline std::uint64_t next_generation{};
inline std::array<Watched,512> watches{};
using Exec=int(*)(Address,int);
inline Exec orig_move{},orig_medic{};
inline int guarded(Exec orig, Address order,int phase) {
    if (!serial_thread()) return orig(order,phase);
    const Address listener=read<Address>(order,0x10);
    auto first=reinterpret_cast<Address>(watches.data());
    if (listener<first || listener-first>=sizeof(watches) || (listener-first)%sizeof(Watched))
        return orig(order,phase);
    auto& watch=watches[(listener-first)/sizeof(Watched)];
    if (watch.handle.ptr!=order) return orig(order,phase);
    if (phase==2) return watch.skipped_start ? 1 : orig(order,phase);
    const auto ordered=read<Address>(order,8);
    const auto actor=ordered ? ordered-0xAF0 : 0;
    World w{};
    if (!actor || !callback(actor,w) || !watch.permit || !watch.permit(w,actor,order)) {
        if (phase==0) watch.skipped_start=true;
        watch.permit=nullptr;
        return 2;
    }
    if (phase==0) watch.skipped_start=false;
    return orig(order,phase);
}
inline int move_exec(Address o,int ph){return guarded(orig_move,o,ph);}
inline int medic_exec(Address o,int ph){return guarded(orig_medic,o,ph);}
inline void discard(Address o) {
    if (o) read<Address(*)(Address,unsigned)>(read<Address>(o))(o,1);
}
inline bool install(Address actor, Address fresh, Permit permit, OrderRef& owned) {
    if (!fresh) return false;
    if (free_slot(actor)) for (auto& watch:watches) if (!watch.handle.ptr) {
        if (!++next_generation) ++next_generation;
        watch={{fresh,0},permit,false,next_generation};
        using Listen=void(*)(Address,OrderHandle*);
        read<Listen>(read<Address>(fresh),0x78)(fresh,&watch.handle);
        owned={&watch,next_generation};
        native<void(*)(Address,Address)>(0x91DB30)(read<Address>(actor,0xDC8),fresh);
        return true;
    }
    discard(fresh);
    return false;
}
inline bool move(Address actor, Vec3 goal, Permit permit, OrderRef& owned) {
    if (!smoke::finite(goal) || (std::fabs(goal.x)<0.01f && std::fabs(goal.y)<0.01f)) return false;
    const float xy[]{goal.x,goal.y};
    return install(actor,native<Address(*)(const float*,unsigned)>(0x90A8C0)(xy,0),permit,owned);
}
template<class Create,class Remove> inline bool prepare_hooks(Address image,const char* digest,bool stock,
                                                              Create create,Remove remove) {
    if (base || !image || !stock || !digest || std::strcmp(digest,executable_sha256)) return false;
    void* p=nullptr;
    if (create(reinterpret_cast<void*>(image+0x90E3F0),reinterpret_cast<void*>(&move_exec),&p)) return false;
    orig_move=reinterpret_cast<Exec>(p);
    if (create(reinterpret_cast<void*>(image+0x378710),reinterpret_cast<void*>(&medic_exec),&p)) {
        remove(reinterpret_cast<void*>(image+0x90E3F0)); return false;
    }
    orig_medic=reinterpret_cast<Exec>(p);
    actor_tls=TlsAlloc();
    if(actor_tls==TLS_OUT_OF_INDEXES){
        remove(reinterpret_cast<void*>(image+0x378710));remove(reinterpret_cast<void*>(image+0x90E3F0));return false;
    }
    base=image; return true;
}
inline bool quiescent() {
    for (const auto& w:watches) if (w.handle.ptr) return false;
    return true;
}
inline void after_hooks_removed(){ if(actor_tls!=TLS_OUT_OF_INDEXES)TlsFree(actor_tls); actor_tls=TLS_OUT_OF_INDEXES; main_tid=0;base=0;orig_medic=nullptr;orig_move=nullptr; }
} // namespace goh::ai_support
