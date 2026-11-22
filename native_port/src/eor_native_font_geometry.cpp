#include "eor_native_font_geometry.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace hulk::eor::ui {
namespace {
uint32_t ReadIndex(const std::byte* p,uint8_t size,bool be) noexcept {
  uint32_t value=0;
  for(uint8_t i=0;i<size;++i) {
    const uint32_t part=std::to_integer<uint8_t>(p[i]);
    if(be)value=(value<<8)|part;
    else value|=part<<(8u*i);
  }
  return value;
}
void CheckFormat(const model::VerifiedAttributeLayout& l,
                 uint32_t stride) {
  if(!l.components || l.components>4 || l.original_byte_offset>=stride)
    throw std::invalid_argument("Original EFont shader input layout invalid");
}
}
LiveFontGeometry DecodeOriginalLiveFontDraw(
    const xdk::RawDraw& call,const VerifiedFontVertexLayout& layout,
    uint64_t frame,uint32_t ordinal) {
  constexpr uint32_t kFontMethod=0x8266A0C0u;
  constexpr uint32_t kFontVtable=0x8206ED6Cu;
  if(!frame || ordinal==UINT32_MAX ||
     !layout.original_vertex_declaration_layout_verified ||
     !layout.original_font_sampler_owner_verified ||
     !layout.original_vertex_declaration_guest ||
     !layout.original_font_atlas_hash ||
     layout.original_pixel_texture_slot>=16 ||
     layout.original_stride<12 || layout.original_stride>192 ||
     (layout.original_stride%4)!=0 ||
     !call.original_engine_shader_method || !call.observed_vs_matrices ||
     !call.observed_live_eor_effect_pass || !call.source_vb0)
    throw std::invalid_argument("Missing original live EFontShader provenance");
  const auto& parent=*call.original_engine_shader_method;
  const auto& state=*call.observed_vs_matrices;
  const auto& vb=*call.source_vb0;
  const uint16_t slot=uint16_t(1u)<<layout.original_pixel_texture_slot;
  if(parent.original_shader_method_guest!=kFontMethod ||
     parent.original_shader_vtable_guest!=kFontVtable ||
     !parent.original_shader_method_vtable_verified ||
     !parent.original_shader_instance_guest || !parent.nesting_depth ||
     !parent.original_pass_vs_ps_xdk_binding_verified ||
     !parent.original_pass_sampler_slots_verified ||
     !parent.original_pass_live_texture_objects_present ||
     !(parent.original_pass_sampler_slot_mask&slot) ||
     state.original_guest_device!=call.gpr[0] ||
     state.original_vertex_declaration_guest_object!=
         layout.original_vertex_declaration_guest ||
     state.original_vs_guest_object!=parent.original_pass_vs_xdk_guest_object ||
     state.original_ps_guest_object!=parent.original_pass_ps_xdk_guest_object ||
     state.original_vertex_buffer_guest_objects[0]!=vb.guest_object ||
     !state.original_texture_guest_objects[layout.original_pixel_texture_slot] ||
     call.original_pixel_texture_keys[layout.original_pixel_texture_slot]!=
         layout.original_font_atlas_hash ||
     !vb.guest_object || !vb.guest_address ||
     vb.bytes.empty() || vb.guest_size!=vb.bytes.size())
    throw std::invalid_argument("Original EFont vertex/shader/atlas source disagrees");
  CheckFormat(layout.position,layout.original_stride);
  CheckFormat(layout.texcoord,layout.original_stride);
  if(layout.position.components<2)
    throw std::invalid_argument("EFont position must contain X and Y");
  if(layout.texcoord.components<2)
    throw std::invalid_argument("EFont source UV requires two components");
  if(layout.color)CheckFormat(*layout.color,layout.original_stride);
  const auto indexed=xdk::DecodeOriginalIndexedXdkCall(call);
  const auto plain=xdk::DecodeOriginalVerticesXdkCall(call);
  if(!indexed && !plain)
    throw std::invalid_argument("Original EFont Draw method unsupported");
  if(indexed && indexed->primitive!=4u)
    throw std::invalid_argument("Original EFont indexed primitive unsupported");
  if(plain && plain->primitive!=4u && plain->primitive!=6u)
    throw std::invalid_argument("Original EFont primitive unsupported");
  uint32_t first_vertex=0,number_of_vertices=0;
  std::vector<uint32_t> indices;
  if(plain) {
    if(layout.original_index_element_bytes ||
       layout.original_index_encoding_verified ||
       plain->vertex_count<3 || plain->vertex_count>65535 ||
       (plain->primitive==4u && plain->vertex_count%3u))
      throw std::invalid_argument("Original EFont nonindexed count/encoding mismatch");
    first_vertex=plain->start_vertex;
    number_of_vertices=plain->vertex_count;
    if(plain->primitive==4u) {
      indices.reserve(number_of_vertices);
      for(uint32_t i=0;i<number_of_vertices;++i)indices.push_back(i);
    } else {
      if(number_of_vertices>21847u)
        throw std::invalid_argument("Original EFont triangle fan GPU budget exceeded");
      indices.reserve((number_of_vertices-2u)*3u);
      for(uint32_t i=1;i+1<number_of_vertices;++i) {
        indices.push_back(0);
        indices.push_back(i);
        indices.push_back(i+1);
      }
    }
  } else {
    if(!layout.original_index_encoding_verified ||
       (layout.original_index_element_bytes!=2u &&
        layout.original_index_element_bytes!=4u) ||
       !call.source_ib || indexed->index_count<3 ||
       indexed->index_count>262143u || indexed->index_count%3u)
      throw std::invalid_argument("Unverified original EFont index-buffer format");
    const auto& ib=*call.source_ib;
    const uint32_t size=layout.original_index_element_bytes;
    if(!ib.guest_object || !ib.guest_address ||
       ib.guest_object!=state.original_index_buffer_guest_object ||
       ib.guest_size!=ib.bytes.size() || ib.bytes.empty() ||
       uint64_t(indexed->first_index)+indexed->index_count>
           ib.bytes.size()/size)
      throw std::invalid_argument("Original EFont indexed source exceeds IB");
    std::vector<uint32_t> guest_indices;
    guest_indices.reserve(indexed->index_count);
    uint32_t minimum=UINT32_MAX,maximum=0;
    for(uint32_t i=0;i<indexed->index_count;++i) {
      const uint64_t byte_at=
          (uint64_t(indexed->first_index)+i)*size;
      const uint32_t raw=ReadIndex(ib.bytes.data()+byte_at,size,
                                   layout.original_index_big_endian);
      const int64_t original=int64_t(raw)+indexed->base_vertex;
      if(original<0 || original>=int64_t(vb.bytes.size()/layout.original_stride))
        throw std::invalid_argument("Original EFont guest index out of range");
      const auto vertex=static_cast<uint32_t>(original);
      minimum=std::min(minimum,vertex);
      maximum=std::max(maximum,vertex);
      guest_indices.push_back(vertex);
    }
    if(minimum==UINT32_MAX || maximum-minimum>=65535u)
      throw std::invalid_argument("Original EFont indexed vertex span unbounded");
    first_vertex=minimum;
    number_of_vertices=maximum-minimum+1u;
    indices.reserve(guest_indices.size());
    for(uint32_t guest:guest_indices)indices.push_back(guest-minimum);
  }
  if(!number_of_vertices ||
     uint64_t(first_vertex)+number_of_vertices>
       vb.bytes.size()/layout.original_stride ||
     indices.size()<3 || indices.size()>262143u || indices.size()%3u)
    throw std::invalid_argument("Original EFont source VB window invalid");
  model::NativeIndexedMesh temporary;
  temporary.original_vertex_stride=layout.original_stride;
  temporary.positions.resize(number_of_vertices);
  const size_t from=size_t(first_vertex)*layout.original_stride;
  const size_t bytes=size_t(number_of_vertices)*layout.original_stride;
  temporary.original_vertex_stream.assign(
      vb.bytes.begin()+from,vb.bytes.begin()+from+bytes);
  LiveFontGeometry output;
  output.original_frame_sequence=frame;
  output.original_draw_ordinal=ordinal;
  output.original_shader_instance_guest=parent.original_shader_instance_guest;
  output.original_guest_vertex_declaration=layout.original_vertex_declaration_guest;
  output.original_font_atlas_hash=layout.original_font_atlas_hash;
  output.original_effect_hash=call.observed_live_eor_effect_pass->effect_hash;
  output.original_pass_index=call.observed_live_eor_effect_pass->pass_index;
  output.positions=model::DecodeVerifiedAttribute(temporary,layout.position);
  output.texcoords=model::DecodeVerifiedAttribute(temporary,layout.texcoord);
  if(layout.color)
    output.colors=model::DecodeVerifiedAttribute(temporary,*layout.color);
  output.triangle_indices=std::move(indices);
  return output;
}
UploadedLiveFontGeometry::UploadedLiveFontGeometry(
    UploadedLiveFontGeometry&& other) noexcept {
  *this=std::move(other);
}
UploadedLiveFontGeometry& UploadedLiveFontGeometry::operator=(
    UploadedLiveFontGeometry&& other) noexcept {
  if(this!=&other) {
    Reset();
    gpu_=std::exchange(other.gpu_,nullptr);
    position_handle=std::exchange(other.position_handle,0);
    texcoord_handle=std::exchange(other.texcoord_handle,0);
    color_handle=std::exchange(other.color_handle,0);
    index_handle=std::exchange(other.index_handle,0);
    vertex_count=std::exchange(other.vertex_count,0);
    index_count=std::exchange(other.index_count,0);
  }
  return *this;
}
UploadedLiveFontGeometry::~UploadedLiveFontGeometry() noexcept {Reset();}
void UploadedLiveFontGeometry::Reset() noexcept {
  if(gpu_) {
    if(index_handle)gpu_->ReleaseBuffer(index_handle);
    if(color_handle)gpu_->ReleaseBuffer(color_handle);
    if(texcoord_handle)gpu_->ReleaseBuffer(texcoord_handle);
    if(position_handle)gpu_->ReleaseBuffer(position_handle);
  }
  gpu_=nullptr;
  position_handle=texcoord_handle=color_handle=index_handle=0;
  vertex_count=index_count=0;
}
UploadedLiveFontGeometry UploadOriginalLiveFontDraw(
    const LiveFontGeometry& source,model::NativeMeshUploadBackend& gpu) {
  if(!source.original_frame_sequence ||
     source.original_draw_ordinal==UINT32_MAX ||
     !source.original_shader_instance_guest ||
     !source.original_guest_vertex_declaration ||
     !source.original_font_atlas_hash ||
     !source.original_effect_hash ||
     source.original_pass_index==UINT32_MAX ||
     source.positions.size()<3 || source.positions.size()>65535 ||
     source.texcoords.size()!=source.positions.size() ||
     (!source.colors.empty() &&
      source.colors.size()!=source.positions.size()) ||
     source.triangle_indices.size()<3 ||
     source.triangle_indices.size()>262143 ||
     source.triangle_indices.size()%3u)
    throw std::invalid_argument("Unverified original EFont GPU upload");
  for(uint32_t index:source.triangle_indices)
    if(index>=source.positions.size())
      throw std::invalid_argument("Original EFont native index out of bounds");
  UploadedLiveFontGeometry out;
  out.gpu_=&gpu;
  out.vertex_count=static_cast<uint32_t>(source.positions.size());
  out.index_count=static_cast<uint32_t>(source.triangle_indices.size());
  const auto upload=[&](uint64_t handle,uint64_t& destination) {
    if(!handle || handle==out.position_handle ||
       handle==out.texcoord_handle || handle==out.color_handle ||
       handle==out.index_handle)
      throw std::runtime_error("Original EFont host GPU buffer allocation failed");
    destination=handle;
  };
  upload(gpu.UploadDecodedAttributes(source.positions),out.position_handle);
  upload(gpu.UploadDecodedAttributes(source.texcoords),out.texcoord_handle);
  if(!source.colors.empty())
    upload(gpu.UploadDecodedAttributes(source.colors),out.color_handle);
  upload(gpu.UploadIndices(source.triangle_indices),out.index_handle);
  return out;
}
} // namespace hulk::eor::ui
