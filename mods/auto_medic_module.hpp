#pragma once

#include "ai_support_26734796.hpp"

namespace goh::auto_medic {
using namespace ai_support;
inline constexpr bool runtime_allowed=false;
enum class Phase { scan, fetch, exchange, approach, screen, revive, cooldown };
struct Job {
    Identity medic{}, patient{}, donor{};
    OrderRef order{};
    Phase phase{Phase::scan};
    unsigned start{}, since{}, wait{}, player{};
    Address scene{}, brain{}, smoke_def{}, operation{};
    int action{-1};
    std::uint64_t ticket{};
    Vec3 endpoint{}, screened_at{};
    bool released{};
};
inline std::array<Job,128> jobs{};
inline Address scene{};
inline unsigned last_tick{};
inline bool busy{}, release_installed{};
inline bool expired(const World& w,const Job& j) {
    return w.now-j.start>=w.duration(70) || w.now-j.since>=j.wait;
}
inline void phase(Job& j,const World& w,Phase p,float seconds) {
    j.phase=p;j.since=w.now;j.wait=w.duration(seconds);
}
inline void revoke(Job& j) {
    j.order.abandon();
    for (auto& r:smoke::reservations)
        if (j.ticket && r.ticket==j.ticket && !r.released) r={};
}
inline void cancel_exchange(const World& w,Job& j) {
    if (j.action<0 || !j.operation) return;
    const auto donor=resolve(w,j.donor), medic=resolve(w,j.medic);
    const auto m=donor ? manager(donor) : 0;
    const Span ops=span(m,0xA8);
    if (medic && ops.valid(8,512)) for (auto p=ops.first;p!=ops.end;p+=8) {
        auto op=read<Address>(p);
        if (op!=j.operation || read<int>(op,0xF0)!=j.action || read<Address>(op,0x10)!=donor ||
            read<Address>(op,0x50)!=medic) continue;
        native<void(*)(Address,const int*)>(0x7DDDC0)(m,&j.action);
        break;
    }
    j.operation=0;j.action=-1;
}
inline void finish(const World& w,Job& j,float seconds=12) {
    revoke(j);
    phase(j,w,Phase::cooldown,seconds);
    cancel_exchange(w,j);
}
inline void refresh(const World& w) {
    bool reset=scene!=w.scene || w.now-last_tick>0x7fffffffu;
    for (auto& j:jobs) {
        if (!j.medic.ptr) continue;
        if (reset) {revoke(j); j={};continue;}
        if (j.phase==Phase::cooldown) {if (w.now-j.since>=j.wait) j={};}
        else if (expired(w,j)) finish(w,j);
    }
    scene=w.scene;last_tick=w.now;
}
inline Job* find(Address actor) {
    for (auto& j:jobs) if (j.medic.matches(actor)) return &j;
    return nullptr;
}
inline bool claimed(Address a,bool donor) {
    for (const auto& j:jobs) {
        if (donor && j.phase==Phase::cooldown) continue;
        if ((donor ? j.donor : j.patient).matches(a)) return true;
    }
    return false;
}
inline bool valid_patient(const World& w,Address actor,Address p) {
    return p && p!=actor && smoke::patient(w,p) && read<std::uint16_t>(p,0xDAA) &&
           smoke::allies(base,owner(actor),owner(p)) && within(pos(actor),pos(p),42*w.length);
}
inline bool valid(const World& w,const Job& j,Address actor,Address& patient) {
    if (j.scene!=w.scene || !j.medic.matches(actor) || j.phase==Phase::cooldown || expired(w,j) ||
        !autonomous(actor,true) || owner(actor)!=j.player || read<Address>(actor,0xDC8)!=j.brain)
        return false;
    patient=resolve(w,j.patient);
    if (!valid_patient(w,actor,patient)) return false;
    const auto order=j.order.get();
    return !read<Byte>(patient,0x12BA) || (j.phase==Phase::revive && order &&
        read<Address>(j.brain,0xF0)==order && read<Address>(order,0x48)==patient &&
        read<int>(order,0x44)==6);
}
inline Address spare(Address actor) {
    const auto b=box(actor);
    const Span items=span(b,0x10);
    if (!b || !items.valid(8,2048)) return 0;
    Address first{};
    for (auto p=items.first;p!=items.end;p+=8) {
        const auto i=read<Address>(p);
        if (!i || read<Address>(i,0x30)!=b || read<Address>(i,0x38) ||
            read<Address>(i,0x10)!=read<Address>(i,0x18) || read<Address>(i,0x50) ||
            read<int>(i,0x48)<=0 || !smoke::IsPureSmokeDefinition(read<Address>(i,0x40))) continue;
        if (read<int>(i,0x48)>=2) return i;
        if (first && first!=i) return first;
        first=i;
    }
    return 0;
}
inline bool donor_ok(const World& w,Address medic,Address d) {
    return d && medic!=d && autonomous(d,true) && free_slot(d) && inventory_free(d) &&
           smoke::allies(base,owner(medic),owner(d)) && within(pos(medic),pos(d),14*w.length);
}
inline bool permit(const World& w,Address actor,Address order) {
    auto* j=find(actor);
    Address patient{};
    if (!j || j->order.get()!=order || !valid(w,*j,actor,patient) ||
        read<Address>(j->brain,0xF0)!=order) return false;
    if (j->phase==Phase::fetch) {
        const auto d=resolve(w,j->donor);
        return donor_ok(w,actor,d) && spare(d) && within(j->endpoint,pos(d),2*w.length);
    }
    if (j->phase==Phase::revive && !j->released) {
        auto d=danger(w,actor,pos(patient),25);
        return d.observed && !d.unsafe;
    }
    return j->phase==Phase::revive || j->phase==Phase::approach;
}

// Two stock locations and a private operation builder. The source cell
// constructor takes an explicit amount. The destination leaves cell allocation
// to Xchange. Destruction unlinks both borrowed entity receivers.
inline int exchange(Address donor,Address medic,Address item) {
    alignas(16) std::byte source[0x40]{}, dest[0x40]{}, builder[0x100]{};
    const auto cell=read<std::uint16_t>(item,0x29);
    native<void*(*)(void*,Address,const std::uint16_t*,int)>(0x7E1D50)(source,box(donor),&cell,1);
    native<void*(*)(void*,Address)>(0x7E1CE0)(dest,box(medic));
    int action=-1;
    if (native<Address(*)(void*)>(0x7E1F40)(source)==item) {
        native<void*(*)(void*,const char*)>(0x7DF910)(builder,"Xchange");
        native<void*(*)(void*,void*)>(0x7DFEE0)(builder,source);
        native<void*(*)(void*,void*)>(0x7E35E0)(builder+0x40,dest);
        native<int*(*)(void*,int*)>(0x7DFBF0)(builder,&action);
        native<void(*)(void*)>(0x7DFE00)(builder);
    }
    native<void(*)(void*)>(0x7E1EC0)(dest);
    native<void(*)(void*)>(0x7E1EC0)(source);
    return action;
}
inline Address nearest(const World& w,Job& j,Address medic,bool donor) {
    Address best=0;
    float cap=(donor ? 14.f : 36.f)*w.length, best2=cap*cap;
    entities(w,[&](Address a) {
        if (!smoke::has_actor(a) || a==medic) return;
        float d2=smoke::distance2(pos(medic),pos(a));
        if (!(d2<=best2) || (best && d2==best2 && read<unsigned>(a,0xC0)>=read<unsigned>(best,0xC0))) return;
        if (donor) {
            if (!donor_ok(w,medic,a) || claimed(a,true) || !spare(a)) return;
        } else if (!valid_patient(w,medic,a) || read<Byte>(a,0x12BA) || claimed(a,false)) return;
        Vec3 goal{};
        if (!approach(w,medic,pos(a),(donor?1.8f:2.5f)*w.length,goal)) return;
        best=a;best2=d2;j.endpoint=goal;
    });
    return best;
}
inline void revive(const World& w,Job& j,Address medic,Address patient) {
    if (!free_slot(medic)) {finish(w,j);return;}
    phase(j,w,Phase::revive,35);
    if (!install(medic,native<Unary>(0x378670)(patient),permit,j.order)) finish(w,j);
}
inline bool aim(const World& w,Address actor,Vec3 anchor,const Danger& d,float range,Vec3& point) {
    if (d.direction) return smoke::screen_point(w,pos(actor),anchor,d.towards,range,point);
    point=anchor;
    return ground(w,point) && within(pos(actor),point,range);
}
inline void screen(const World& w,Job& j,Address actor,Address patient,Address item) {
    const auto def=read<Address>(item,0x40), squad=read<Address>(j.brain,0x100);
    const auto d=danger(w,actor,pos(patient),25);
    Vec3 point{};
    const auto range=smoke::nominal_throw_range(base,def);
    if (!(range>0)) {finish(w,j);return;}
    if (!aim(w,actor,pos(patient),d,range,point)) {
        if (j.phase==Phase::approach || !approach(w,actor,pos(patient),5*w.length,j.endpoint) ||
            within(pos(actor),j.endpoint,w.length)) {finish(w,j);return;}
        phase(j,w,Phase::approach,16);
        if (!move(actor,j.endpoint,permit,j.order)) finish(w,j);
        return;
    }
    smoke::reset_scene_cache(w);
    auto* r=smoke::available(w,actor,squad,pos(actor),false);
    if (!r || !release_installed || smoke::image_base!=base) {finish(w,j);return;}
    const auto path=smoke::orders::inspect(base,j.brain);
    if (!path.allowed || !free_slot(actor)) {finish(w,j);return;}
    smoke::reserve(*r,w,actor,squad,pos(actor),false);
    r->medical=true;r->protected_actor=patient;r->protected_id=j.patient.id;r->known_threat=d.towards;
    j.ticket=r->ticket;j.screened_at=pos(patient);j.endpoint=point;
    phase(j,w,Phase::screen,9);
    auto shot=smoke::make_smoke_order(base,def,point);
    if (shot && smoke::track_smoke_order(w,actor,shot,def,point,j.ticket) &&
        smoke::orders::install(base,j.brain,path,shot)) return;
    if (shot) smoke::forget_smoke_order(shot);
    discard(shot);finish(w,j);
}
// Decorate the single existing throw detour at creation. Stock smoke retains
// its original trampoline and handles every other firing context.
inline void release(Address shooter) noexcept {
    if (!host() || !smoke::serial_thread()) {smoke::smoke_release(shooter);return;}
    const auto e=smoke::lookup_smoke_intent(&smoke::SmokeIntent::shooter,shooter);
    Job* job=nullptr;
    if (e.generation && e.lease_ticket)
        for (auto& j:jobs) if (j.ticket==e.lease_ticket) {job=&j;break;}
    if (!job) {smoke::smoke_release(shooter);return;}
    const auto frame=smoke::quant_frame();
    auto f=frame ? frame->firing : nullptr;
    const bool synchronous=f && f->shooter==shooter;
    if (!synchronous && !e.owns_latch) {smoke::smoke_release(shooter);return;}
    if ((synchronous && (!f->permit || f->generation!=e.generation)) || e.spent) return;
    World w{};
    Address p{};
    const auto weapon=read<Address>(shooter,8);
    auto actor=weapon ? read<Address>(weapon,0x90) : 0;
    if (job->phase!=Phase::screen || !callback(actor,w) || !valid(w,*job,actor,p) ||
        !smoke::matching_smoke_target(weapon,e.screen) || !smoke::smoke_may_continue(e,actor,w) || !e.child || read<Address>(e.root,0x30)!=e.child ||
        !within(pos(p),job->screened_at,2*w.length)) return;
    auto d=danger(w,actor,pos(p),25);
    if (d.direction) {
        const Vec3 a=pos(p);
        const float x=d.towards.x-a.x,y=d.towards.y-a.y,len=std::hypot(x,y);
        const float sx=e.screen.x-a.x, sy=e.screen.y-a.y;
        if (!within(a,e.screen,2*w.length) && (!(len>0) || sx*x+sy*y<=0 ||
            sx*x+sy*y>=len*len || std::fabs(sx*y-sy*x)>3*w.length*len)) return;
    }
    if (!smoke::change_smoke_intent(e.generation,[](auto& entry){entry.spent=true;})) return;
    if (!smoke::scoped_smoke_release(base,smoke::original_smoke_release,shooter,e.definition,e.screen,true)) return;
    smoke::change_smoke_intent(e.generation,[](auto& entry){entry.emitted=true;});
    const auto live=smoke::lookup_smoke_intent(&smoke::SmokeIntent::shooter,shooter);
    if (live.generation!=e.generation || read<Address>(shooter,8)!=weapon || read<Address>(weapon,0x90)!=actor) return;
    smoke::receipt(actor,w.now,e.lease_ticket);
    if (job->ticket==e.lease_ticket && job->phase==Phase::screen) job->released=true;
}
template<class Create,class Remove>
inline bool prepare_smoke_hooks(Address image,const char* digest,bool stock,Create create,Remove remove) {
    auto dispatch=[&](void* entry,void* hook,void** original) {
        if (entry==reinterpret_cast<void*>(image+0x8313B0)) hook=reinterpret_cast<void*>(&release);
        return create(entry,hook,original);
    };
    return release_installed=smoke::prepare_hooks(image,digest,stock,dispatch,remove);
}
inline void tick(Address actor) {
    World w{};
    if (busy || !callback(actor,w)) return;
    struct Scope { Scope(){busy=true;} ~Scope(){busy=false;} } scope;
    refresh(w);
    auto* j=find(actor);
    if (j && j->phase==Phase::cooldown) return;
    if (!autonomous(actor,true)) {if (j) finish(w,*j);return;}
    const Address b=read<Address>(actor,0xDC8);
    if (!j) {
        if (!free_slot(actor) || !inventory_free(actor) || !box(actor) ||
            !native<Test>(0x96AAF0)(read<Address>(b,0xE0))) return;
        for (auto& slot:jobs) if (!slot.medic.ptr) {j=&slot;break;}
        if (!j) return;
        j->medic=identity(actor);j->scene=w.scene;j->brain=b;j->start=w.now;j->player=owner(actor);
        auto p=nearest(w,*j,actor,false);
        if (!p) {finish(w,*j,2);return;}
        j->patient=identity(p);
        phase(*j,w,Phase::scan,3);
    }
    Address p{};
    if (!valid(w,*j,actor,p)) {finish(w,*j);return;}
    if (j->phase==Phase::revive) {
        if (!j->order.get() || read<Address>(b,0xF0)!=j->order.get()) finish(w,*j);
        return;
    }
    if (j->phase==Phase::screen) {
        if (j->released && free_slot(actor) && within(pos(p),j->screened_at,2*w.length)) revive(w,*j,actor,p);
        return;
    }
    if (j->order.get()) {
        if (!permit(w,actor,j->order.get())) finish(w,*j);
        return;
    }
    if (!free_slot(actor)) {finish(w,*j);return;}
    auto item=smoke::FindCarriedPureSmokeItem(base,actor);
    if (j->phase==Phase::exchange && (!item || !inventory_free(actor))) {
        const auto d=resolve(w,j->donor);
        if(!d || !autonomous(d,true) || !free_slot(d) || !smoke::allies(base,owner(actor),owner(d)) ||
            !within(pos(actor),pos(d),2*w.length)) finish(w,*j);
        return;
    }
    if (item) {
        if (j->phase==Phase::exchange && read<Address>(item,0x40)!=j->smoke_def) {finish(w,*j);return;}
        j->action=-1;j->operation=0;
        screen(w,*j,actor,p,item);
        return;
    }
    if (j->phase==Phase::approach || !inventory_free(actor)) {finish(w,*j);return;}
    auto d=resolve(w,j->donor);
    if (!j->donor.ptr) {
        d=nearest(w,*j,actor,true);
        if (d) j->donor=identity(d);
    }
    if (!donor_ok(w,actor,d)) {
        auto threat=danger(w,actor,pos(p),25);
        if (threat.observed && !threat.unsafe) revive(w,*j,actor,p);
        else finish(w,*j);
        return;
    }
    if (!within(pos(actor),pos(d),2*w.length)) {
        if (j->phase==Phase::fetch) {finish(w,*j);return;}
        phase(*j,w,Phase::fetch,14);
        if (!move(actor,j->endpoint,permit,j->order)) finish(w,*j);
        return;
    }
    const auto offered=spare(d);
    if (!offered || !box(actor)) {finish(w,*j);return;}
    j->smoke_def=read<Address>(offered,0x40);
    phase(*j,w,Phase::exchange,8);
    j->action=exchange(d,actor,offered);
    if (j->action<0) {finish(w,*j);return;}
    const Span ops=span(manager(d),0xA8);
    if (ops.valid(8,512)) for (auto s=ops.first;s!=ops.end;s+=8) {
        const auto op=read<Address>(s);
        if (op && read<int>(op,0xF0)==j->action && read<Address>(op,0x10)==d &&
            read<Address>(op,0x50)==actor) {j->operation=op;break;}
    }
}
inline void reset() {
    for (auto& j:jobs) {revoke(j);j={};}
    scene=0;last_tick=0;
}
} // namespace goh::auto_medic
