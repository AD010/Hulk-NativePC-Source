#include "../src/launcher/native_input_map.h"
#include <array>
#include <cstdlib>
#include <iostream>
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL: " #x << " at line " << __LINE__ << "\n"; std::exit(1); } } while (0)
#include <cstdint>
using namespace hulk::input;
static int16_t be(const std::array<uint8_t,16>& s, int at) {
 return static_cast<int16_t>((s[at]<<8)|s[at+1]);
}
int main() {
 std::array<uint8_t,16> s{};
 s.fill(0xCC);
 ApplyFrame({}, s.data());
 CHECK(s[0]==0xCC && s[3]==0xCC); // caller's packet stays untouched
 for(int i=4;i<16;i++) CHECK(s[i]==0);
 Frame f{}; f.keys=W|D|Space|MouseLeft|Up|Shift|Ctrl|Enter;
 f.mouse_dx=5; f.mouse_dy=-3; f.sensitivity=4;
 ApplyFrame(f,s.data());
 CHECK(be(s,4)==static_cast<int16_t>(0x5011)); // X+A+Start+Dpad up
 CHECK(s[6]==255 && s[7]==255);
 CHECK(be(s,8)>0 && be(s,10)>0 && be(s,12)>0 && be(s,14)>0);
 f.keys=A|S|Left|Down|MouseRight|Tab;
 f.mouse_dx=-10000; f.mouse_dy=10000;
 ApplyFrame(f,s.data());
 CHECK(be(s,4)==static_cast<int16_t>(0x2026));
 CHECK(be(s,8)<0 && be(s,10)<0 && be(s,12)==-32767 && be(s,14)==-32767);
 // Even a one-count mouse delta must clear the XInput right-stick deadzone.
 Frame tiny{}; tiny.mouse_dx=1; tiny.sensitivity=0.1f;
 ApplyFrame(tiny,s.data()); CHECK(be(s,12)>8689);
 tiny.mouse_dx=-1; ApplyFrame(tiny,s.data()); CHECK(be(s,12)<-8689);
 tiny.mouse_dx=0; ApplyFrame(tiny,s.data()); CHECK(be(s,12)==0);
 f.keys=W|S|A|D;
 ApplyFrame(f,s.data()); CHECK(be(s,8)==0 && be(s,10)==0);
 // A native guest camera consumes mouse deltas directly. The right stick
 // MUST stay neutral while ordinary keyboard buttons still enter XINPUT_STATE.
 f.keys=W|Space|MouseLeft;
 f.mouse_dx=63; f.mouse_dy=-57;
 f.mouse_as_right_stick=false;
 ApplyFrame(f,s.data());
 CHECK(be(s,12)==0 && be(s,14)==0);
 CHECK(be(s,10)>0);
 CHECK((static_cast<uint16_t>(be(s,4)) & 0x5000)==0x5000);
 // Walk speed is genuinely analog, not a binary walk/run toggle.
 Frame move{};
 move.keys = W;
 move.move_stick_strength = MovementStrength(0, 0.36f);
 ApplyFrame(move, s.data());
 const int16_t slow = be(s, 10);
 CHECK(slow > 7849 && slow < 18000); // clears XInput default deadzone
 move.move_stick_strength = MovementStrength(2, 0.36f);
 ApplyFrame(move, s.data());
 const int16_t medium = be(s, 10);
 CHECK(medium > slow && medium < 32767);
 move.move_stick_strength = MovementStrength(4, 0.36f);
 ApplyFrame(move, s.data());
 CHECK(be(s, 10) == 32767);
 move.keys = W|S;
 ApplyFrame(move, s.data());
 CHECK(be(s, 10)==0);
 move.keys = D|A;
 ApplyFrame(move, s.data());
 CHECK(be(s, 8)==0);
 CHECK(MovementStrength(-10, 0.36f)==MovementStrength(0, 0.36f));
 CHECK(MovementStrength(99, 0.36f)==1.0f);
 CHECK(MovementStrength(0, 0.0f)>=0.26f);
 CHECK(MovementStrength(0, 1.0f)<=0.95f);
 // The packet number is owned by the XInput hook, not the pure mapping layer.
 CHECK(s[0]==0xCC && s[3]==0xCC);
 return 0;
}
