#include "post.hpp"
#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
using namespace godot;
namespace {
constexpr int W=32, STRIDE=W*W, MAX_Y=350;
constexpr uint8_t STONE=3, DIRT=2, GRASS=1, SAND=5, SNOW=19;
constexpr uint8_t LIMESTONE=40, CHALK=41, SHALE=42, CLAYSTONE=43, GRANITE=44,
 BASALT=45, SLATE=46, COPPER_ORE=47, QUARTZITE=48, NATIVE_COPPER=49;
Ref<FastNoiseLite> noise(int32_t seed, float freq) {
    Ref<FastNoiseLite> n; n.instantiate();
    n->set_seed(seed);
    n->set_noise_type(FastNoiseLite::TYPE_SIMPLEX);
    n->set_frequency(freq);
    n->set_fractal_type(FastNoiseLite::FRACTAL_FBM);
    n->set_fractal_octaves(2);
    return n;
}
uint8_t rock_for(int y,float p,float f,float v) {
    const float warped=float(y)+p*12.f+f*11.f;
    const float metamorphic=22.f+p*12.f, sediment=51.f+p*21.f;
    if (v>0.48f && warped>sediment+21.f) return BASALT;
    if (warped<metamorphic) return v>0.15f?BASALT:GRANITE;
    if (warped<sediment) return f>0.11f?QUARTZITE:SLATE;
    if (p>0.25f) return int(std::floor(warped/17.f))%3==0?CHALK:LIMESTONE;
    if (p< -0.24f) return int(std::floor(warped/20.f))%3!=0?SHALE:CLAYSTONE;
    int m = int(std::floor(warped/25.f)) % 4;
    if (m<0) m+=4;
    return m==0?CLAYSTONE:m==1?SHALE:m==2?LIMESTONE:CHALK;
}
float hash01(int gx,int gz,int64_t seed,int salt) {
    // Mirror GDScript's & 0x7fffffff at each stage: defined unsigned arithmetic.
    const uint32_t h0=(uint32_t(gx)*73856093u)^(uint32_t(gz)*19349663u)^
        (uint32_t(seed)*83492791u)^uint32_t(salt);
    uint32_t h=h0&0x7fffffffu;
    h=(h^(h>>17))&0x7fffffffu;
    h=(h*1274126177u)&0x7fffffffu;
    h^=h>>15;
    return float(h&0x7fffffffu)/2147483648.f;
}
struct Deposit {int kind,depth,thick;float cx,cz,radius,cc,ss;};
std::vector<Deposit> candidates(Vector2i pos,int64_t seed,Ref<FastNoiseLite> ore) {
    std::vector<Deposit> ds;
    const int x_min=pos.x*W,z_min=pos.y*W;
    for(int kind=0;kind<3;++kind){
        int cell=kind==0?32:64, radius=kind==0?7:13;
        int gx0=int(std::floor(double(x_min-radius)/cell));
        int gx1=int(std::floor(double(x_min+W-1+radius)/cell));
        int gz0=int(std::floor(double(z_min-radius)/cell));
        int gz1=int(std::floor(double(z_min+W-1+radius)/cell));
        for(int gz=gz0;gz<=gz1;++gz)for(int gx=gx0;gx<=gx1;++gx){
            float roll=hash01(gx,gz,seed,19007+kind*129);
            float cx=gx*cell+hash01(gx,gz,seed,19021+kind*129)*cell;
            float cz=gz*cell+hash01(gx,gz,seed,19027+kind*129)*cell;
            float abundance=ore->get_noise_2d(cx,cz);
            float chance=kind==0?0.40f:0.48f+abundance*0.35f;
            if(kind==2)chance=0.72f+abundance*0.15f;
            if(roll>std::clamp(chance,0.05f,0.95f))continue;
            float r=kind==0?3.1f+hash01(gx,gz,seed,19031+kind*129)*3.3f:
                5.f+hash01(gx,gz,seed,19031+kind*129)*7.5f;
            float angle=hash01(gx,gz,seed,19037+kind*129)*6.283185307179586f;
            int thick=1+int(hash01(gx,gz,seed,19043+kind*129)*3.f);
            float depthroll=hash01(gx,gz,seed,19049+kind*129);
            int depth=kind==0?2+int(std::floor(depthroll*7.f)):14+int(std::floor(depthroll*105.f));
            if(kind==2)depth=3+int(std::floor(depthroll*72.f));
            ds.push_back({kind,depth,thick,cx,cz,r,std::cos(angle),std::sin(angle)});
        }
    }
    return ds;
}
uint8_t ore_on_host(int kind,uint8_t id) {
    if(kind==2)return (id==LIMESTONE||id==CHALK)?COPPER_ORE:0;
    switch(id){
        case GRANITE:case BASALT:case SLATE:case QUARTZITE:case SHALE:case CLAYSTONE:return NATIVE_COPPER;
        default:return 0;
    }
}
}
Dictionary SkyTearsNativeTerrainPost::apply_geology(PackedByteArray source,Vector2i chunk,int64_t seed,int highest) {
    Dictionary result;result["changed"]=false;
    if(source.size()<STRIDE*16||source.size()%STRIDE!=0)return result;
    const int height=source.size()/STRIDE;
    const int scan_top=std::min(height-2,std::min(MAX_Y,std::max(218,highest+8)));
    struct Cache {
        int32_t seed=0;
        Ref<FastNoiseLite> province,fold,volcanic,ore;
    };
    static thread_local Cache cache;
    const int32_t wanted_seed=int32_t(seed);
    if(cache.province.is_null() || cache.seed!=wanted_seed){
        cache.seed=wanted_seed;
        cache.province=noise(int32_t(seed+18201),0.0016f);
        cache.fold=noise(int32_t(seed+18313),0.0040f);
        cache.volcanic=noise(int32_t(seed+18407),0.0020f);
        cache.ore=noise(int32_t(seed+18617),0.0017f);
    }
    Ref<FastNoiseLite> province=cache.province;
    Ref<FastNoiseLite> fold=cache.fold;
    Ref<FastNoiseLite> volcanic=cache.volcanic;
    Ref<FastNoiseLite> ore=cache.ore;
    std::array<int,STRIDE> tops{};
    std::array<float,STRIDE> p{},f{},v{};
    const uint8_t *src=source.ptr();
    int highest_found=2;
    for(int col=0;col<STRIDE;++col){
        int sy=scan_top;
        for(;sy>2;--sy){
            uint8_t id=src[col+sy*STRIDE];
            if(id==STONE||id==DIRT||id==GRASS||id==SAND||id==SNOW)break;
        }
        tops[col]=std::max(2,sy);
        highest_found=std::max(highest_found,tops[col]);
        if(tops[col]>3){
            float wx=float(chunk.x*W+col%W),wz=float(chunk.y*W+col/W);
            p[col]=province->get_noise_2d(wx,wz);
            f[col]=fold->get_noise_2d(wx,wz);
            v[col]=volcanic->get_noise_2d(wx,wz);
        }
    }
    // One C++ tight pass replaces 11+ separate GDScript section passes.
    PackedByteArray output=source.duplicate();
    uint8_t *raw=output.ptrw();
    bool changed=false;
    for(int col=0;col<STRIDE;++col){
        int cap=std::min(scan_top,tops[col]);
        for(int y=3;y<=cap;++y){
            int idx=col+y*STRIDE;
            if(src[idx]==STONE){raw[idx]=rock_for(y,p[col],f[col],v[col]);changed=true;}
        }
    }
    const auto deposits=candidates(chunk,seed,ore);
    int startx=chunk.x*W,startz=chunk.y*W;
    for(const auto &d:deposits){
        int x0=std::max(0,int(std::floor(d.cx-d.radius*1.35f))-startx);
        int x1=std::min(W-1,int(std::ceil(d.cx+d.radius*1.35f))-startx);
        int z0=std::max(0,int(std::floor(d.cz-d.radius*1.35f))-startz);
        int z1=std::min(W-1,int(std::ceil(d.cz+d.radius*1.35f))-startz);
        for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x){
            float dx=startx+x-d.cx,dz=startz+z-d.cz;
            float rx=dx*d.cc-dz*d.ss,rz=dx*d.ss+dz*d.cc;
            float dist=(rx/d.radius)*(rx/d.radius)+(rz/(d.radius*0.66f))*(rz/(d.radius*0.66f));
            if(dist>1.f)continue;
            int col=x+W*z,surface=tops[col],cy=surface-d.depth;
            int spread=std::max(0,int(std::floor(float(d.thick-1)*(1.f-dist)+0.5f)));
            for(int y=std::max(3,cy-spread);y<=std::min(scan_top,cy+spread);++y){
                if(y>=surface)continue;
                int i=col+y*STRIDE;
                uint8_t target=ore_on_host(d.kind,raw[i]);
                if(target){raw[i]=target;changed=true;}
            }
        }
    }
    if(changed){result["changed"]=true;result["blocks"]=output;}
    return result;
}
void SkyTearsNativeTerrainPost::_bind_methods(){
    ClassDB::bind_method(D_METHOD("apply_geology","blocks","chunk","seed","highest_scan_y"),
        &SkyTearsNativeTerrainPost::apply_geology,DEFVAL(240));
    ClassDB::bind_method(D_METHOD("apply_mountain","blocks","chunk","seed","highest_scan_y","settings"),
        &SkyTearsNativeTerrainPost::apply_mountain);
}
