#include "native_x_hulk_profile.h"
#include <cstdlib>
#include <iostream>
#define REQUIRE(x) do { if (!(x)) { std::cerr << "FAIL: " #x << " at line " << __LINE__ << "\n"; std::exit(1); } } while (0)
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <string>
int main() {
  namespace fs=std::filesystem;
  const auto path=fs::temp_directory_path()/"hulk_native_gpu_profile_test.ini";
  hulk::nx::GameProfile profile;
  REQUIRE(!hulk::nx::LoadGameProfile(path,profile));
  {
    std::ofstream f(path);
    f << "[profile]\nxdk_version=2.0.6534\n[hooks]\n0x8288D458=DrawVertices\n";
  }
  REQUIRE(!hulk::nx::LoadGameProfile(path,profile));
  REQUIRE(profile.error.find("incomplete")!=std::string::npos);
  {
    std::ofstream f(path);
    f << "[profile]\nxdk_version=2.0.6534\noutput_width=1920\noutput_height=1080\n"
      << "[device]\nvs_constants_dirty=0\nps_constants_dirty=8\nfetch_constants_dirty=16\n"
      << "fetch_dirty_slot0_bit=55\ncommand_buffer_write=48\nfetch_constants=1024\n"
      << "vs_float_constants=1792\nps_float_constants=5888\nbool_loop_constants=9984\nvertex_declaration=11820\nindex_buffer=12436\nrender_targets=12440\ndepth_stencil=12456\nvertex_buffers=12460\ntextures=12544\nviewport=12648\npixel_shader=12692\nvertex_shader=12696\npredication_mask=12708\ntile_count=12748\ntile_rects=12752\nflags=10940\ninline_draw_end=13444\n"
      << "[objects]\ntexture_fetch=16\nsurface_info=24\nsurface_color_depth_info=28\nsurface_size=32\n"
      << "surface_width_minus_one=0,14\nsurface_height_minus_one=16,14\n"
      << "[registers]\ngroup_0=10240,0x2000,19\n"
      << "[hooks]\n0x8288D458=DrawVertices\n0x8288D950=Present\n"
      << "0x8288DF30=Resolve\n0x8288E770=SetRenderTarget\n0x8289BFA0=SetTexture\n";
  }
  REQUIRE(hulk::nx::LoadGameProfile(path,profile));
  // Regresses the user's real 050 black screen: enabling an experimental
  // 7978/5426 borrowed DeviceLayout used to silence essential-hook checks.
  // The renderer then disabled Xenos output before native draws were valid.
  {
    std::ifstream original_file(path);
    const std::string original((std::istreambuf_iterator<char>(original_file)),
                                std::istreambuf_iterator<char>());
    original_file.close();
    const auto marker = original.find("[profile]\n");
    REQUIRE(marker != std::string::npos);
    std::string unsafe = original;
    unsafe.insert(marker + sizeof("[profile]\n") - 1, "experimental=1\n");
    std::ofstream out(path, std::ios::trunc);
    out << unsafe;
    out.close();
    REQUIRE(!hulk::nx::LoadGameProfile(path,profile));
    REQUIRE(profile.error.find("unverified XDK") != std::string::npos);
    std::ofstream restore(path, std::ios::trunc);
    restore << original;
  }
  REQUIRE(hulk::nx::LoadGameProfile(path,profile));
  // The exact file parser also validates profiles embedded in the Windows
  // executable. A real validated XDK map will not need a separate INI.
  {
    std::ifstream embedded_file(path);
    const std::string embedded((std::istreambuf_iterator<char>(embedded_file)),
                               std::istreambuf_iterator<char>());
    REQUIRE(hulk::nx::LoadGameProfileFromText(embedded, profile));
    REQUIRE(profile.calls.size() == 5);
  }
  // A borrowed 7978 object block had RT0+8 and depth+8. Refuse this
  // exact historical cause of black/garbled native surfaces.
  {
    std::ifstream src(path);
    std::string bad((std::istreambuf_iterator<char>(src)), std::istreambuf_iterator<char>());
    src.close();
    const auto from = bad.find("render_targets=12440");
    REQUIRE(from != std::string::npos);
    bad.replace(from, std::string("render_targets=12440").size(), "render_targets=12448");
    REQUIRE(!hulk::nx::LoadGameProfileFromText(bad, profile));
    REQUIRE(profile.error.find("observed device layout mismatch") != std::string::npos);
    REQUIRE(hulk::nx::LoadGameProfile(path, profile));
    // Reset the first mutation before testing texture offsets in isolation.
    bad.replace(from, std::string("render_targets=12448").size(), "render_targets=12440");
    const auto old_texture = bad.find("textures=12544");
    REQUIRE(old_texture != std::string::npos);
    bad.replace(old_texture, std::string("textures=12544").size(), "textures=12552");
    REQUIRE(!hulk::nx::LoadGameProfileFromText(bad, profile));
    REQUIRE(profile.error.find("observed device layout mismatch") != std::string::npos);
    REQUIRE(hulk::nx::LoadGameProfile(path, profile));
  }
  // Hulk may inline SetTexture, while native renderer reads fetch constants
  // from device shadow on draw: a synthetic profile without SetTexture passes.
  {
    std::ifstream source(path);
    std::string no_set_texture((std::istreambuf_iterator<char>(source)),
                                std::istreambuf_iterator<char>());
    const std::string setter = "0x8289BFA0=SetTexture\n";
    const auto position = no_set_texture.find(setter);
    REQUIRE(position != std::string::npos);
    no_set_texture.erase(position, setter.size());
    REQUIRE(hulk::nx::LoadGameProfileFromText(no_set_texture, profile));
  }
  REQUIRE(profile.profile.output_width==1920);
  REQUIRE(profile.profile.device.register_groups[0].first_register==0x2000);
  hulk::nx::Install(&profile);
  REQUIRE(hulk::nx::Lookup(0x8288D458)==native_x_renderer::Call::kDrawVertices);
  REQUIRE(hulk::nx::Lookup(0x828A2A18)==native_x_renderer::Call::kCount);
  hulk::nx::Install(nullptr);
  fs::remove(path);
  std::cout << "Native Hulk profile validation: OK (synthetic test data only)\n";
}
