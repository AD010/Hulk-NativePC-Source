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
  Check(PcPromptBinding(0xAC9AA551u)=="hulk_bind_lb");
  Check(PcPromptBinding(0x734A8CB2u)=="hulk_bind_rb");
  Check(PcPromptBinding(0xB3951152u)=="hulk_bind_start");
  Check(PcPromptBinding(0xEC6B934Cu)=="hulk_bind_select");
  Check(PcPromptBinding(0xC76C7AB4u)=="hulk_bind_menu_up");
  Check(PcPromptBinding(0x9E32DFE1u)=="hulk_bind_menu_down");
  Check(PcPromptBinding(0x6E62BB29u)=="hulk_bind_menu_left");
  Check(PcPromptBinding(0xEBDDBBC2u)=="hulk_bind_menu_right");
  Check(PcPromptBinding(0).empty());
  Check(PcPromptBinding(0x6592347Eu).empty()); // composite D-pad is not one key
  Check(!PcPromptInputActive());
  for(unsigned i=0;i<1000;++i) {
    PublishPcPromptInput(true);Check(PcPromptInputActive());
    PublishPcPromptInput(false);Check(!PcPromptInputActive());
  }
  Check(FirstPromptToken("bad, Space, Enter")=="Space");
  Check(PromptLabel("MouseLeft")=="LMB");
  Check(PromptLabel("Space")=="SPACE");
  Check(KnownPcPromptAsset("Space")=="ui_key_space_da");
  Check(KnownPcPromptAsset("MouseLeft")=="ui_mouse_leftbutton_da");
  Check(KnownPcPromptAsset("MouseRight")=="ui_mouse_rightbutton_da");
  Check(KnownPcPromptAsset("MouseMiddle")=="ui_mouse_middlebutton_da");
  Check(KnownPcPromptAsset("Enter")=="ui_key_enter_da");
  Check(KnownPcPromptAsset("Tab")=="ui_key_tab_da");
  Check(KnownPcPromptAsset("Esc")=="ui_key_escape_da");
  Check(KnownPcPromptAsset("F")=="ui_key_f_da");
  Check(KnownPcPromptAsset("M")=="ui_key_m_da");
  Check(KnownPcPromptAsset("1")=="ui_key_1_da");
  Check(KnownPcPromptAsset("2")=="ui_key_2_da");
  Check(KnownPcPromptAsset("3")=="ui_key_3_da");
  Check(KnownPcPromptAsset("4")=="ui_key_4_da");
  Check(KnownPcPromptAsset("Q").empty());
  for(auto key:hulk::options::kBindingTokens){
    auto image=RasterPcKeycap(key,64,64);
    Check(image.size()==64u*64u*4u);
    Check(image[3]==0);
    bool ink=false;
    for(size_t i=0;i<image.size();i+=4)if(image[i]==245 && image[i+3]==255)ink=true;
    Check(ink);
    Check(image!=RasterPcKeycap("?",64,64));
  }
  for(uint32_t w:{16u,20u,24u,28u,32u,64u})
    for(auto key:hulk::options::kBindingTokens) {
      const auto pixels=RasterPcKeycap(key,w,16u);
      Check(pixels.size()==size_t(w)*16u*4u);
      Check(pixels[3]==0);
      bool ink=false;
      for(size_t i=0;i<pixels.size();i+=4)
        if(pixels[i]==245u && pixels[i+3]==255u)ink=true;
      Check(ink);
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
