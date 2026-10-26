#include "gpu/frame_interpolation.hpp"
#include <cmath>
#include <cstdio>
#include <vector>
using namespace mhp2g::gpu::interpolation;
Matrix motion(float degrees, float x, float y, float z) {
    float a=degrees*3.14159265358979f/180.0f;
    Matrix m{}; m[0]=m[10]=std::cos(a);m[2]=-std::sin(a);m[8]=std::sin(a);
    m[5]=m[15]=1;m[12]=x;m[13]=y;m[14]=z;return m;
}
int main() {
    unsigned failed=0, checks=0;
    for (float angle : {0.0f,4.0f,20.0f}) for (unsigned bad=0;bad<17;bad++) {
        std::vector<DrawSummary> a,b;
        Matrix camera=motion(angle,20,3,-5);
        for(unsigned i=0;i<17;i++) {
            DrawSummary d{}; d.eligible=d.perspective=true;d.vertex_address=0x08800000u+i*128u;
            d.count=3; d.view=d.projection=motion(0,0,0,0);
            d.world=motion(0,float(i)*100,0,-float(i)*30);a.push_back(d);
            d.world=multiply(camera,d.world);
            if(i==bad) {d.world[12]+=1100;d.world[14]+=900;} // same turn, moving NPC
            b.push_back(d);
        }
        Matcher matcher;
        const auto &r=matcher.match(a,b,CutThresholds{});
        bool good=std::fabs(r.camera[12]-20)<0.1f && std::fabs(r.camera[13]-3)<0.1f &&
            std::fabs(r.camera[14]+5)<0.1f && r.cut==nullptr;
        checks++;if(!good) {failed++;std::printf("FAIL angle %.0f outlier %u: camera %.1f %.1f %.1f\n",angle,bad,r.camera[12],r.camera[13],r.camera[14]);}
    }
    std::printf("%u/%u camera-consensus scenarios passed\n",checks-failed,checks);
    return failed?1:0;
}
