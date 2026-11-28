#include "../native_port/src/eor_exit_confirmation.h"
#include <cassert>
using namespace hulk::eor::ui;
int main(){
  PcExitConfirmation c;
  assert(!c.Yes());
  assert(c.Handle(PcExitInput::Select)==PcExitResult::Cancel);
  assert(c.Handle(PcExitInput::Toggle)==PcExitResult::Stay && c.Yes());
  assert(c.Handle(PcExitInput::None)==PcExitResult::Stay && c.Yes());
  assert(c.Handle(PcExitInput::Select)==PcExitResult::Confirm);
  assert(c.Handle(PcExitInput::Back)==PcExitResult::Cancel);
  c.Reset();assert(!c.Yes());
  for(int n=0;n<10000;++n){c.Reset();for(int k=0;k<n%17;++k)c.Handle(PcExitInput::Toggle);assert(c.Yes()==bool((n%17)%2));assert(c.Handle(PcExitInput::Select)==(c.Yes()?PcExitResult::Confirm:PcExitResult::Cancel));}
}
