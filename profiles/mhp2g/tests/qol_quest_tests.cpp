#include "game/key_quests.hpp"
#include "game/capture_marker.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
using namespace mhp2g::game;
void check(bool value, const char *why) { if (!value) { std::cerr << why << '\n'; std::exit(1); } }
std::vector<std::uint8_t> list(bool key, unsigned padding=3) {
    std::vector<std::uint8_t> b(8+0x34);
    b[0]=8; b[8]=4; if(key)b[11]=4;
    const char name[]="Test quest";b.insert(b.end(),name,name+10);
    b.insert(b.end(),padding,0);
    const char objective[]="Objective";b.insert(b.end(),objective,objective+sizeof(objective));return b;
}
int main() {
    auto original=list(true);auto b=original;
    check(mark_key_quests(b)==1,"key marked");check(b.size()==original.size(),"size preserved");
    check(std::equal(original.begin(),original.begin()+60,b.begin()),"header preserved");
    check(b[60]=='*' && std::equal(original.begin()+60,original.begin()+70,b.begin()+61),"whole title preserved");
    check(std::equal(original.begin()+72,original.end(),b.begin()+72),"objective preserved");
    check(mark_key_quests(b)==0,"no double star");
    b=list(false);original=b;check(mark_key_quests(b)==0&&b==original,"nonkey unchanged");
    b=list(true,1);original=b;check(mark_key_quests(b)==0&&b==original,"no room rejected atomically");
    b=list(true);b[4]=1;original=b;check(mark_key_quests(b)==0&&b==original,"bad directory unchanged");
    b={};check(mark_key_quests(b)==0,"empty list");
    CaptureState s{0,300,1000,0,0.2f,true};
    check(capture_ready(s),"inclusive capture boundary");s.health=301;check(!capture_ready(s),"above boundary");
    s.health=300;s.painted=false;check(!capture_ready(s),"paint expired with map QOL off");
    s.map_revealed=true;check(capture_ready(s),"map QOL reveals unpainted capturable monster");
    s.health=301;check(!capture_ready(s),"map QOL respects capture threshold");
    s.health=300;s.painted=true;check(capture_ready(s),"paint and map QOL together");
    s.map_revealed=false;check(capture_ready(s),"paint alone still works");
    s.painted=false;check(!capture_ready(s),"map QOL off without paint restores normal marker");
    s.painted=true;
    for(auto kind:{54,59,60,64,65,76,79,80,88}){s.kind=kind;s.health=1;check(!capture_ready(s),"uncapturable kind");}
    s.kind=0;s.strength=16;s.health=180;check(capture_ready(s),"strength band16 boundary");
    s.health=181;check(!capture_ready(s),"strength band16 above");
    s.strength=24;s.health=150;check(capture_ready(s),"strength band24 boundary");
    s.health=151;check(!capture_ready(s),"strength band24 above");
    s.health=0;check(!capture_ready(s),"dead monster");s.health=1;s.maximum=0;check(!capture_ready(s),"invalid max");
    s.maximum=1000;s.base_ratio=0;check(!capture_ready(s),"no capture ratio");
    s.base_ratio=std::numeric_limits<float>::quiet_NaN();check(!capture_ready(s),"NaN rejected");
    s.base_ratio=0.2f;s.health=1001;check(!capture_ready(s),"invalid health");
    std::cout << "QOL quest stars and capture predicate tests passed\n";
}
