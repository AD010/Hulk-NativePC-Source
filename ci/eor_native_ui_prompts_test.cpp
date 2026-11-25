#include "../native_port/src/eor_native_ui_prompts.h"
#include <cstdlib>
using namespace hulk::eor::ui;
static void Check(bool v){if(!v)std::abort();}
int main(){
  Check(PcPromptBinding(0x16FEB6A7u)=="hulk_bind_confirm");
  Check(PcPromptBinding(0x044B1949u)=="hulk_bind_back");
  Check(PcPromptBinding(0x3BEFAEB2u)=="hulk_bind_primary");
  Check(PcPromptBinding(0x8353C9D7u)=="hulk_bind_secondary");
  Check(PcPromptBinding(0xD9E8AD12u)=="hulk_bind_lt");
  Check(PcPromptBinding(0x063884F1u)=="hulk_bind_rt");
  Check(PcPromptBinding(0).empty());
  Check(FirstPromptToken("bad, Space, Enter")=="Space");
  Check(PromptLabel("MouseLeft")=="LMB");
  Check(PromptLabel("Space")=="SPACE");
  Check(KnownPcPromptAsset("Space")=="ui_key_space_da");
  Check(KnownPcPromptAsset("MouseLeft")=="ui_mouse_leftbutton_da");
  for(auto key:hulk::options::kBindingTokens){
    auto image=RasterPcKeycap(key,64,64);
    Check(image.size()==64u*64u*4u);
    Check(image[3]==0);
    bool ink=false;
    for(size_t i=0;i<image.size();i+=4)if(image[i]==245 && image[i+3]==255)ink=true;
    Check(ink);
    Check(image!=RasterPcKeycap("?",64,64));
  }
  Check(RasterPcKeycap("Q",1,1).empty());
  Check(RasterPcKeycap("Unknown",64,64).empty());
  std::vector<uint8_t> source(8u*4u*4u,255);
  auto fit=FitPcPromptImage(source,8,4,64,64);
  Check(fit.size()==64u*64u*4u && fit[3]==0);
  Check(fit[(16u*64u)*4u+3u]==255);
  Check(fit[(48u*64u)*4u+3u]==0);
  Check(FitPcPromptImage(source,4,4,64,64).empty());
}
