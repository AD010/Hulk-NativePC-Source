#include "../native_port/src/eor_original_pc_menu.h"
#include <cstdlib>
using namespace hulk::eor::ui;
static void Check(bool b){if(!b)std::abort();}
int main(){
  constexpr uint32_t owner=1,row=2;
  OriginalPcStorageMenu menu;
  bool reject=false,save_ok=false;
  unsigned saves=0,restores=0;
  auto paint=[&](const PcMenuVisual& v){
    if(!v.count){++restores;return true;}
    return !reject;
  };
  auto read=[](std::string_view){return std::string("false");};
  auto commit=[&](std::string_view,std::string_view){
    ++saves;return hulk::options::PcMenuCommitResult{save_ok,save_ok};
  };
  auto send=[&](PcEvent e,bool focused=true){return menu.Handle(owner,row,e,focused,paint,read,commit);};
  Check(menu.Enter(owner,row,paint));
  Check(!send(PcEvent::Down,false)); // unrelated original input passes through
  reject=true;
  Check(send(PcEvent::Select)&&!menu.Owns(owner,row)&&restores==2&&saves==0);
  Check(!send(PcEvent::Select)); // stale session cannot consume next input
  reject=false;Check(menu.Enter(owner,row,paint));
  Check(send(PcEvent::Select));Check(send(PcEvent::Select));Check(saves==0);
  reject=true;
  Check(send(PcEvent::Increase)&&saves==1&&!menu.Owns(owner,row));
  reject=false;Check(menu.Enter(owner,row,paint));
  Check(send(PcEvent::Select));Check(send(PcEvent::Select));
  save_ok=true;reject=true;
  Check(send(PcEvent::Select)&&saves==2&&!menu.Owns(owner,row));
  reject=false;Check(menu.Enter(owner,row,paint));
  Check(send(PcEvent::Select));reject=true;
  Check(send(PcEvent::Down)&&!menu.Owns(owner,row));
}
