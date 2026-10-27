#include "post.hpp"
#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include <memory>
using namespace godot;
namespace {
constexpr int W=32,S=W*W,MIN_BASE=85,MAX_BASE=270,MAX_NEW=320,DECOR_SCAN=42,WATER_Y=72;
constexpr uint8_t AIR=0,GRASS=1,DIRT=2,STONE=3,SAND=5,WOOD=6,LEAVES=8,
    SHORT_GRASS=12,TALL_GRASS=13,WOOD_X=17,WOOD_Z=18,SNOW=19,SNOW_LAYER=20,STICK_LEAVES=28;
Ref<FastNoiseLite> make_noise(int seed,float frequency,int octaves){
    Ref<FastNoiseLite> n;n.instantiate();n->set_seed(seed);
    n->set_noise_type(FastNoiseLite::TYPE_SIMPLEX);
    n->set_frequency(frequency);
    n->set_fractal_type(FastNoiseLite::FRACTAL_FBM);
    n->set_fractal_octaves(octaves);
    n->set_fractal_gain(0.50f);
    n->set_fractal_lacunarity(2.f);
    return n;
}
float smooth(float a,float b,float x){
    float t=std::clamp((x-a)/std::max(b-a,0.0001f),0.f,1.f);
    return t*t*(3.f-2.f*t);
}
struct MountainNoiseState {
    Ref<FastNoiseLite> region,warp,hills,ridges,density,arches,zones;
    explicit MountainNoiseState(int s){
        region=make_noise(s^0x25B71,0.00085f,3);
        warp=make_noise(s^0x29FC3,0.00160f,2);
        hills=make_noise(s^0xA1E73,0.00240f,3);
        ridges=make_noise(s^0x41C9B,0.00460f,3);
        density=make_noise(s^0x5C531,0.00700f,3);
        arches=make_noise(s^0xD1A47,0.02300f,2);
        zones=make_noise(s^0x2B9F1,0.00160f,2);
    }
    float macro(float x,float z) const {
        float drift=warp->get_noise_2d(x,z)*75.f;
        return 0.5f+region->get_noise_2d(x+drift,z-drift*0.65f)*0.5f;
    }
};
bool geology(uint8_t id){return id==STONE||id==DIRT||id==GRASS||id==SAND||id==SNOW;}
bool plant(uint8_t id){return id==LEAVES||id==WOOD||id==WOOD_X||id==WOOD_Z||id==SHORT_GRASS||id==TALL_GRASS||id==SNOW_LAYER;}
bool tree_part(uint8_t id){return id==WOOD||id==WOOD_X||id==WOOD_Z||id==LEAVES||id==STICK_LEAVES;}
struct Root{int x,y,z;};
struct Part{int idx,root;uint8_t id;};
struct Relief{int rise;float peaks;};
Relief height_add(const MountainNoiseState &n,float x,float z,int base,float strength,float flatland) {
    float macro=n.macro(x,z);
    float foothills=smooth(flatland,flatland+0.18f,macro);
    if(foothills<0.025f)return {0,0.f};
    float peaks=smooth(flatland+0.08f,flatland+0.30f,macro);
    float alpine=smooth(flatland+0.18f,flatland+0.34f,macro);
    float hills=0.5f+n.hills->get_noise_2d(x,z)*0.5f;
    float ridges=0.5f+n.ridges->get_noise_2d(x,z)*0.5f;
    float rise=foothills*(6.f+hills*16.f);
    rise+=std::pow(peaks,1.18f)*(24.f+ridges*44.f);
    rise+=n.density->get_noise_3d(x,base*0.72f,z)*14.f*peaks;
    rise+=std::pow(std::max(ridges,0.f),1.65f)*alpine*74.f;
    rise*=smooth(float(WATER_Y+8),float(WATER_Y+27),float(base));
    rise*=1.f-smooth(188.f,305.f,float(base))*0.68f;
    // Positive values: Godot roundi() rounds to nearest integer.
    int delta=std::clamp(int(std::floor(std::max(0.f,rise)*strength+0.5f)),0,128);
    return {delta,std::max(peaks,alpine)};
}
}
Dictionary SkyTearsNativeTerrainPost::apply_mountain(PackedByteArray source,Vector2i chunk,int64_t seed,
                                                     int highest_scan_y,Dictionary opts){
    Dictionary result;result["changed"]=false;
    if(source.size()%S!=0||source.size()/S<290)return result;
    const int height=source.size()/S;
    float strength=std::clamp(float(double(opts.get("noise3d_relief_strength",1.0))),0.f,1.65f);
    if(strength<=0.f)return result;
    float flatland=std::clamp(float(double(opts.get("noise3d_flatland_protection",0.49))),0.38f,0.61f);
    bool arches_enabled=bool(opts.get("noise3d_arches_enabled",true));
    // Cache Godot FastNoiseLite resources between chunks for the same seed.
    static thread_local int32_t cache_seed = 0;
    static thread_local std::unique_ptr<MountainNoiseState> cached;
    const int32_t wanted_seed = int32_t(seed);
    if (!cached || wanted_seed != cache_seed) {
        cached = std::make_unique<MountainNoiseState>(wanted_seed);
        cache_seed = wanted_seed;
    }
    const MountainNoiseState &noise = *cached;
    float max_macro=0.f;
    for(int sz:{0,16,31})for(int sx:{0,16,31}){
        max_macro=std::max(max_macro,noise.macro(float(chunk.x*W+sx),float(chunk.y*W+sz)));
    }
    if(max_macro<flatland-0.10f)return result;
    const uint8_t *src=source.ptr();
    // Reconstruct recognizable native trees as WHOLE objects, never separate columns.
    std::vector<Root> roots;
    for(int idx=0;idx<source.size();++idx){
        if(src[idx]!=WOOD)continue;
        int x=idx%W,z=(idx/W)%W,y=idx/S;
        if(y<=MIN_BASE||y+16>=height||!geology(src[idx-S]))continue;
        bool straight=true;
        for(int k=1;k<=3;++k)if(src[idx+k*S]!=WOOD){straight=false;break;}
        if(!straight)continue;
        int leaves=0;
        for(int dy=3;dy<17&&leaves<7;++dy)
          for(int dz=-4;dz<=4&&leaves<7;++dz){
            int zz=z+dz;if(zz<0||zz>=W)continue;
            for(int dx=-4;dx<=4;++dx){int xx=x+dx;if(xx<0||xx>=W)continue;
                uint8_t id=src[xx+W*zz+(y+dy)*S];
                if(id==LEAVES||id==STICK_LEAVES)++leaves;
            }
          }
        if(leaves>=7)roots.push_back({x,y,z});
    }
    std::vector<Part> parts;
    // Source without tree parts, just as original GDScript _extract_native_trees.
    PackedByteArray terrain=source;
    if(!roots.empty()){
        std::vector<int> owner(size_t(source.size()),-1);
        for(int r=0;r<int(roots.size());++r){
            auto root=roots[r];
            for(int z=std::max(0,root.z-6);z<std::min(W,root.z+7);++z)
              for(int x=std::max(0,root.x-6);x<std::min(W,root.x+7);++x){
                int d2=(x-root.x)*(x-root.x)+(z-root.z)*(z-root.z);
                if(d2>36)continue;
                for(int y=std::max(1,root.y-1);y<std::min(height,root.y+27);++y){
                    int idx=x+W*z+y*S;
                    if(!tree_part(src[idx]))continue;
                    int old=owner[idx];
                    if(old>=0){
                        auto oldroot=roots[old];
                        int oldd2=(x-oldroot.x)*(x-oldroot.x)+(z-oldroot.z)*(z-oldroot.z);
                        if(oldd2<=d2)continue;
                    }
                    owner[idx]=r;
                }
              }
        }
        terrain=source.duplicate();
        uint8_t *t=terrain.ptrw();
        for(int i=0;i<source.size();++i)if(owner[i]>=0){
            parts.push_back({i,owner[i],src[i]});t[i]=AIR;
        }
    }
    const uint8_t *tsrc=terrain.ptr();
    PackedByteArray output;
    bool changed=false;
    int raised_columns=0, highest=std::max(0,highest_scan_y);
    std::array<int,S> raised{};
    int top_scan=std::min(height-2, highest_scan_y<MIN_BASE?290:std::max(200,std::min(highest_scan_y+1,290)));
    for(int z=0;z<W;++z){
        float wz=float(chunk.y*W+z);
        for(int x=0;x<W;++x){
            float wx=float(chunk.x*W+x);
            if(noise.macro(wx,wz)<=flatland+0.006f)continue;
            int col=x+W*z,top=-1;
            for(int y=top_scan;y>=MIN_BASE;--y)if(geology(tsrc[col+y*S])){top=y;break;}
            if(top<MIN_BASE||top>=MAX_BASE)continue;
            if(!geology(tsrc[col+(top-1)*S])||!geology(tsrc[col+(top-2)*S]))continue;
            Relief relief=height_add(noise,wx,wz,top,strength,flatland);
            if(relief.rise<2)continue;
            int newtop=std::min(top+relief.rise,std::min(MAX_NEW,height-48));
            int rise=newtop-top;
            if(rise<2)continue;
            std::vector<std::pair<int,uint8_t>> plants;
            bool blocked=false;
            for(int y=top+1;y<std::min(top+DECOR_SCAN,height-rise-1);++y){
                uint8_t id=tsrc[col+y*S];
                if(id==AIR)continue;
                if(tree_part(id)){blocked=true;break;}
                if(plant(id))plants.push_back({y,id});
                else{blocked=true;break;}
            }
            if(blocked)continue;
            if(!changed){output=terrain.duplicate();changed=true;}
            uint8_t *raw=output.ptrw();
            for(auto p:plants)raw[col+p.first*S]=AIR;
            float peaks=relief.peaks;
            float zone=noise.zones->get_noise_2d(wx,wz);
            int actual=top;
            for(int y=top;y<=newtop+3;++y){
                int index=col+y*S;
                float density=float(newtop-y);
                if(peaks>0.16f&&y>=newtop-11)
                    density+=noise.density->get_noise_3d(wx,y*0.72f,wz)*3.2f*peaks;
                bool solid=density>=-0.001f;
                if(solid&&arches_enabled&&peaks>0.75f&&zone>0.38f&&
                   y>top+11&&y<newtop-9){
                    float hollow=noise.arches->get_noise_3d(wx,y*0.67f,wz);
                    if(std::abs(hollow)<0.10f)solid=false;
                }
                if(solid){raw[index]=STONE;actual=y;}
                else raw[index]=AIR;
            }
            uint8_t orig=tsrc[col+top*S];
            if(orig==GRASS){
                raw[col+actual*S]=GRASS;
                for(int d=1;d<4;++d){int y=actual-d;
                    if(y>top&&raw[col+y*S]==STONE)raw[col+y*S]=DIRT;
                }
            }else if(orig==DIRT)raw[col+actual*S]=DIRT;
            else if(orig==SNOW)raw[col+actual*S]=SNOW;
            int snowline=int(160.f-peaks*18.f);
            if(actual>=snowline){
                raw[col+actual*S]=SNOW;
                for(int d=1;d<3;++d){int y=actual-d;
                    if(y>top&&raw[col+y*S]==DIRT)raw[col+y*S]=STONE;
                }
            }
            for(auto p:plants){
                int moved=p.first+actual-top;
                if(moved>actual&&moved<height){raw[col+moved*S]=p.second;highest=std::max(highest,moved);}
            }
            raised[col]=actual-top;
            ++raised_columns;
            highest=std::max(highest,actual);
        }
    }
    if(!changed)return result;
    uint8_t *raw=output.ptrw();
    for(const Part &part:parts){
        const Root &root=roots[part.root];
        int shift=raised[root.x+W*root.z];
        int y=part.idx/S+shift;
        if(y<=0||y>=height)continue;
        int dest=part.idx%S+y*S;
        uint8_t id=raw[dest];
        if(id==AIR||id==SHORT_GRASS||id==TALL_GRASS||id==LEAVES||id==STICK_LEAVES){
            raw[dest]=part.id;
            highest=std::max(highest,y);
        }
    }
    result["changed"]=true;
    result["blocks"]=output;
    result["highest_y"]=highest;
    result["raised_columns"]=raised_columns;
    return result;
}
