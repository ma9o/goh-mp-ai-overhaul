#pragma once

#include "ai_support_26734796.hpp"
#include <string_view>

namespace goh::auto_resupply {
using namespace ai_support;
inline constexpr bool runtime_allowed=false;
constexpr float travel_metres=60, safety_metres=25;
inline Address extender(Address entity,Address table) {
    auto list=span(read<Address>(entity,0x578),0);
    if (list.valid(8,1024)) for(auto p=list.first;p!=list.end;p+=8){
        auto e=read<Address>(p);
        if(e && read<Address>(e)==base+table && read<Address>(e,0x18)==entity)return e;
    }
    return 0;
}
inline std::string_view native_string(Address str) {
    auto len=read<std::size_t>(str,0x10), cap=read<std::size_t>(str,0x18);
    auto bytes=cap<=15?str:read<Address>(str);
    if(!bytes || !len || len>1024 || cap<len)return {};
    return {reinterpret_cast<const char*>(bytes),len};
}
inline Address cached_template(Address actor) {
    const auto key=native_string(actor+0xF0);
    const auto tree=read<Address>(base,0x159BE88);
    if(!tree || key.empty())return 0;
    auto node=read<Address>(tree,8);
    for(unsigned depth=0;node && !read<Byte>(node,0x19) && depth<64;++depth){
        auto name=native_string(node+0x20);
        if(name.empty())return 0;
        int cmp=key.compare(name);
        if(!cmp)return read<Address>(node,0x70);
        node=read<Address>(node,cmp<0?0:0x10);
    }
    return 0;
}
struct Gun {Address def{},item{}, place{};float rank{};bool human{};};
inline bool weapon_definition(Address def){
    if(!def || !tag(span(def,0x11E0),"weapon"))return false;
    auto required=span(def+0x120,0x318);
    return required.first!=required.end && required.valid(0x60,4096);
}
inline Gun primary(Address actor){
    Gun best{};
    const auto b=box(actor); if(!b)return {};
    const bool human=(read<Byte>(actor,0x30)&4)!=0;
    if(human){
        auto items=span(b,0x10);
        if(!items.valid(8,2048))return {};
        auto holder=native<Unary>(0x7E04B0)(actor);
        auto hand=holder?read<Unary>(read<Address>(holder),0x18)(holder):0;
        auto consider=[&](Address item){
            if(!item || read<int>(item,0x48)<=0)return;
            auto def=read<Address>(item,0x40); if(!weapon_definition(def))return;
            auto tags=span(def,0x11E0);
            auto profile=native<Unary>(0x83F1E0)(def);
            if(!profile || !read<Address>(profile,0x1600) || tag(tags,"missile") || tag(tags,"grenade"))return;
            float rank=tag(tags,"pistol") || tag(tags,"revolver")?1:4;
            if(item==hand)rank+=.5f;
            if(rank>best.rank)best={def,item,0,rank,true};
        };
        consider(hand);
        for(auto p=items.first;p!=items.end;p+=8){
            auto i=read<Address>(p);if(i && read<Address>(i,0x30)==b)consider(i);
        }
    }else{
        if(!(read<Byte>(actor,0x30)&8) || !smoke::vehicle::hasWeaponAndPlaces(actor))return {};
        auto ws=span(read<Address>(actor,0xDD8),0x30);
        if(!ws.valid(8,256))return {};
        for(auto p=ws.first;p!=ws.end;p+=8){
            auto w=read<Address>(p);
            if(!w || smoke::SmokeTagEquals(w+8,"dummy") || read<Address>(w,0x90)!=actor)continue;
            auto mounted=read<Address>(w,0x218), charger=read<Address>(w,0xB0);
            if(!mounted || !charger || native<Test>(0x820CE0)(charger))continue;
            auto def=read<Address>(mounted,0x40), profile=read<Address>(w,0x98);
            if(!weapon_definition(def) || !profile || !read<Address>(w,0xA0))continue;
            float calibre=read<float>(profile,0x1628);
            if(!std::isfinite(calibre) || calibre<=0 || calibre>1000)continue;
            auto charged=read<Unary>(read<Address>(charger),8)(charger);
            if(calibre>best.rank)best={def,charged,w,calibre,false};
        }
    }
    return best;
}
enum class Ammo {unknown, empty, available};
inline Ammo ammo(Address actor,const Gun& g){
    if(!g.def)return Ammo::unknown;
    if(g.item && read<int>(g.item,0x58)>0 && read<Address>(g.item,0x50))return Ammo::available;
    const auto b=box(actor);auto items=span(b,0x10);
    if(!b || !items.valid(8,2048))return Ammo::unknown;
    for(auto p=items.first;p!=items.end;p+=8){
        auto item=read<Address>(p);
        if(!item || read<int>(item,0x48)<=0 || read<Address>(item,0x30)!=b)continue;
        auto def=read<Address>(item,0x40);
        if(def && native<BinaryTest>(0x565A50)(def+0xEC8,g.def+0x120))return Ammo::available;
        if(def==g.def && read<Address>(item,0x50) && read<int>(item,0x58)>0)return Ammo::available;
    }
    return Ammo::empty;
}
inline bool affordable(int spent,int limit,int cost){
    return spent>=0 && limit>=0 && cost>=0 && (!limit || static_cast<std::int64_t>(spent)+cost<=limit);
}
inline bool capacity(Address zone,Address row,Address actor,const Gun& g){
    if(g.human){
        auto breed=read<Address>(actor,0x1390);
        if(breed && !span(breed,0x508).valid(0x670,4096))return false;
        using Capacity=int(*)(Address,Address,Address,Address,Address);
        return native<Capacity>(0x48F820)(zone,row,actor,g.def,g.item)>0;
    }
    auto offered=read<Address>(row);
    auto fill=g.item?read<Address>(g.item,0x50):0;
    if(fill && read<int>(g.def,0x470)>0 && native<BinaryTest>(0x565A50)(fill+0x868,offered+0x868))return true;
    // Read only resident templates. Calling the vehicle branch of 48F820 on a
    // cache miss would instantiate a new entity during a policy query.
    auto raw=cached_template(actor);if(!raw || !(read<Byte>(raw,0x30)&1))return false;
    auto b=box(raw);auto items=span(b,0x10);
    if(b && items.valid(8,2048))for(auto p=items.first;p!=items.end;p+=8){
        auto i=read<Address>(p);
        if(i && read<Address>(i,0x40)==offered && read<int>(i,0x48)>0)return true;
    }
    return false;
}
inline bool supplies(Address zone,Address actor,const Gun& g){
    auto rows=span(zone,0x3D0);if(!rows.valid(0x18,8192))return false;
    for(auto r=rows.first;r!=rows.end;r+=0x18){
        auto offered=read<Address>(r);
        if(!offered || read<int>(r,8)<=0 || read<int>(r,0x10)<0 ||
           !affordable(read<int>(zone,0x94),read<int>(zone,0x98),read<int>(r,0x14)))continue;
        if(tag(span(offered,0x11E0),"ammo") &&
           native<BinaryTest>(0x565A50)(offered+0x868,g.def+0x120) && capacity(zone,r,actor,g))return true;
    }
    return false;
}
inline Address team(Address actor){
    auto game=native<Address(*)()>(0xD2BCC0)();
    auto players=span(game,0x160);
    if(players.valid(8,256))for(auto p=players.first;p!=players.end;p+=8){
        auto player=read<Address>(p);
        if(player && read<Byte>(player,4)==owner(actor) && !native_string(player+0x68).empty())return player+0x28;
    }
    return 0;
}
inline bool uncontested(float progress,float delta,int team_code,const float* weights){
    if(progress!=1 || delta!=0 || team_code<'a' || team_code>'j')return false;
    for(int i=0;i<10;++i)if(!std::isfinite(weights[i]) || weights[i]<0 || (weights[i]>0 && i!=team_code-'a'))return false;
    return true;
}
inline bool safe_flag(Address flag,Address point,Address actor,Address team_tag){
    if(!smoke::has_actor(flag) || !smoke::allies(base,owner(actor),owner(flag)))return false;
    auto engine=read<Address>(point,0xB58);
    if(!engine || read<Address>(engine)!=base+0x116C550 || read<Address>(engine,8)!=point ||
       read<float>(engine,0x98)!=1 || !native<BinaryTest>(0x565340)(engine+0xA8,team_tag))return false;
    const auto code=native<int(*)(Address)>(0xD0DFD0)(team_tag);
    const auto weights=read<std::array<float,10>>(engine,0x108);
    return uncontested(read<float>(engine,0x98),read<float>(engine,0x9C),code,weights.data());
}
inline Address parent_flag(const World& w,Address child){
    auto bone=extender(child,0x10FDD88);
    auto expected=bone?read<Address>(bone,0x88):0;
    Address flag=0;
    entities(w,[&](Address a){
        if(flag || (expected && expected!=a) || !smoke::has_actor(a) || !extender(a,0x10FBFF0))return;
        auto rows=span(read<Address>(a,0x568),0x28);
        if(rows.valid(0x298,1024))for(auto r=rows.first;r!=rows.end;r+=0x298)
            if(read<Address>(r,0x280)==child){flag=a;break;}
    });
    return flag;
}
inline bool source_owner(const World& w,Address source,Address zone,Address actor){
    auto teams=span(zone,0x3B8);
    if(!teams.valid(0x60,256))return false;
    auto mine=team(actor);
    const bool restricted=teams.first!=teams.end;
    if(restricted && (!mine || !native<BinaryTest>(0x1663C0)(zone+0xA0,mine)))return false;
    auto point=extender(source,0x10FBFF0);
    if(point)return mine && safe_flag(source,point,actor,mine);
    if(!smoke::has_actor(source)){
        auto flag=parent_flag(w,source);
        if(flag)return mine && safe_flag(flag,extender(flag,0x10FBFF0),actor,mine);
        return restricted && native_string(source+0xF0)!="flagpoint_ammo" && !extender(source,0x10FDD88);
    }
    if(!smoke::allies(base,owner(actor),owner(source)))return false;
    auto crew=span(read<Address>(source,0x568),0x10);
    if(!restricted && (read<Byte>(source,0x30)&8) && crew.first!=crew.end &&
       native<smoke::Role>(0xC2B5D0)() && !native<smoke::Role>(0xC2B620)()){
        auto theirs=team(source);
        return mine && theirs && native<BinaryTest>(0x565340)(mine,theirs);
    }
    return true;
}
inline bool serviceable(Address a){
    auto tags=span(a,0x4E8);
    if(!tags.valid(0x60,4096) || tag(tags,"crushed") || tag(tags,"destroyed") || tag(tags,"burning"))return false;
    auto fire=extender(a,0x10FD478);
    if(fire && (read<Address>(fire,0x88)!=read<Address>(fire,0x90) || read<int>(fire,0xA8) ||
                !(read<float>(fire,0xA0)<=0)))return false;
    if((read<Byte>(a,0x30)&8) && smoke::has_actor(a)){
        smoke::vehicle::Observation body{};
        auto chassis=read<Address>(a,0xDE0);
        return chassis && !(read<unsigned>(chassis,0x48)&0x10) && smoke::vehicle::usableBody(a,body);
    }
    return true;
}
struct Destination {Identity source{};Vec3 goal{};float radius{}, period{};};
inline bool check_source(const World& w,Address actor,const Gun& g,Address source,Destination& d,bool select){
    if(actor==source || !within(pos(actor),pos(source),travel_metres*w.length))return false;
    auto zone=extender(source,0x109BA88);
    if(!zone || !read<Byte>(zone,0x430) || !serviceable(source))return false;
    float r=read<float>(zone,0x88)*w.length, period=read<float>(zone,0x8C);
    if(!std::isfinite(r) || r<=0 || !std::isfinite(period) || period<=0 || period>90 ||
       !source_owner(w,source,zone,actor) || !supplies(zone,actor,g))return false;
    auto threats=danger(w,actor,pos(source),safety_metres);
    if(!threats.observed || threats.unsafe)return false;
    if(select && !approach(w,actor,pos(source),r*.9f,d.goal))return false;
    if(!within(d.goal,pos(source),r))return false;
    auto route_end=danger(w,actor,d.goal,safety_metres);
    if(!route_end.observed || route_end.unsafe)return false;
    d.source=identity(source);d.radius=r;d.period=period;return true;
}
enum class Phase {travel,wait,cooldown};
struct Trip {
    Identity actor{};Destination dest{};OrderRef order{};Phase phase{Phase::cooldown};
    unsigned since{},timeout{},progress_at{},owner_id{};
    Address brain{},weapon_def{};float remaining{};
};
inline std::array<Trip,256> trips{};
inline Address scene{};
inline unsigned last_tick{};
inline bool busy{};
inline void stop(const World& w,Trip& t,float seconds){
    t.order.abandon();t.phase=Phase::cooldown;t.since=w.now;t.timeout=w.duration(seconds);
}
inline void reset(){
    for(auto& t:trips){t.order.abandon();t={};}scene=0;last_tick=0;
}
inline void refresh(const World& w){
    if(w.scene!=scene || w.now-last_tick>0x7fffffffu)reset();
    for(auto& t:trips)if(t.actor.ptr && w.now-t.since>=t.timeout){
        if(t.phase==Phase::cooldown)t={};else stop(w,t,20);
    }
    scene=w.scene;last_tick=w.now;
}
inline bool claimed(Address source){
    for(auto& t:trips)if(t.phase!=Phase::cooldown && t.dest.source.matches(source))return true;
    return false;
}
inline bool recipient(Address actor){
    if(!autonomous(actor,false) || !inventory_free(actor) || extender(actor,0x109BA88))return false;
    auto def=read<Address>(actor,0x548);
    if(!def || tag(span(def,0x330),"hidden"))return false;
    if(read<Byte>(actor,0x30)&4){
        auto relation=read<Address>(actor,0x560);
        return relation && read<int>(relation,0x7C)!=5;
    }
    return serviceable(actor);
}
inline bool permit(const World& w,Address actor,Address order){
    for(auto& t:trips)if(t.actor.matches(actor) && t.phase==Phase::travel && t.order.get()==order){
        if(scene!=w.scene || w.now-t.since>=t.timeout || w.now-t.progress_at>=w.duration(9) ||
           !recipient(actor) || read<Address>(actor,0xDC8)!=t.brain || owner(actor)!=t.owner_id ||
           read<Address>(t.brain,0xF0)!=order)return false;
        auto g=primary(actor);auto s=resolve(w,t.dest.source);
        return s && t.weapon_def==g.def && ammo(actor,g)==Ammo::empty && check_source(w,actor,g,s,t.dest,false);
    }
    return false;
}
inline void tick(Address actor){
    World w{};if(busy || !callback(actor,w))return;
    struct Scope{Scope(){busy=true;}~Scope(){busy=false;}} scope;
    refresh(w);
    Trip* job=nullptr;
    for(auto& t:trips)if(t.actor.matches(actor)){job=&t;break;}
    if(job && job->phase==Phase::cooldown)return;
    if(!recipient(actor)){if(job)stop(w,*job,20);return;}
    if(!job && !free_slot(actor))return;
    const auto g=primary(actor);
    auto state=ammo(actor,g);
    if(!job){
        for(auto& t:trips)if(!t.actor.ptr){job=&t;break;}
        if(!job)return;
        auto& t=*job;
        t.actor=identity(actor);t.brain=read<Address>(actor,0xDC8);t.owner_id=owner(actor);
        if(state!=Ammo::empty){stop(w,t,3);return;}
        float best=travel_metres*w.length;best*=best;
        entities(w,[&](Address s){
            float d2=smoke::distance2(pos(actor),pos(s));
            if(!(d2<=best) || (t.dest.source.ptr && d2==best && read<unsigned>(s,0xC0)>=t.dest.source.id) || claimed(s))return;
            Destination candidate{};
            if(check_source(w,actor,g,s,candidate,true)){best=d2;t.dest=candidate;}
        });
        if(!t.dest.source.ptr){stop(w,t,6);return;}
        t.weapon_def=g.def;t.phase=Phase::travel;t.since=t.progress_at=w.now;t.timeout=w.duration(40);
        t.remaining=std::sqrt(smoke::distance2(pos(actor),t.dest.goal));
        if(!within(pos(actor),pos(t.dest.source.ptr),t.dest.radius) && !move(actor,t.dest.goal,permit,t.order)){stop(w,t,20);return;}
    }
    auto& t=*job;
    if(t.brain!=read<Address>(actor,0xDC8) || t.owner_id!=owner(actor) || g.def!=t.weapon_def || state!=Ammo::empty){
        stop(w,t,state==Ammo::available?30:20);return;
    }
    auto source=resolve(w,t.dest.source);
    if(!source || !check_source(w,actor,g,source,t.dest,false)){stop(w,t,20);return;}
    auto owned=t.order.get();
    if(owned ? read<Address>(t.brain,0xF0)!=owned : !free_slot(actor)){stop(w,t,20);return;}
    if(t.phase==Phase::wait){
        if(!within(pos(actor),pos(source),t.dest.radius))stop(w,t,20);
        return;
    }
    float remaining=std::sqrt(smoke::distance2(pos(actor),t.dest.goal));
    if(t.remaining-remaining>=.5f*w.length){t.progress_at=w.now;t.remaining=remaining;}
    if(within(pos(actor),pos(source),t.dest.radius)){
        // Let the owned point order end before waiting, so vanilla cleanup
        // runs and the next arriving actor cannot displace this one.
        if(owned)return;
        t.phase=Phase::wait;t.since=w.now;t.timeout=w.duration(4+2*t.dest.period);
    }else if(!owned || w.now-t.progress_at>=w.duration(9)){stop(w,t,20);}
}
} // namespace goh::auto_resupply
