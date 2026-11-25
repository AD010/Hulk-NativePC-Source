#!/usr/bin/env python3
"""Prevent native EoR runtime/CI from silently dropping linked source files.

The last Windows Release attempt failed with LNK2019 after a test command
omitted the model materials and native mesh decoder implementations. The
actual native target can similarly compile a Vulkan backend yet omit its
packed VS/PS constants implementation, leaving dead, uncallable EoR code.
This runs before private game data is downloaded, without a release build.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / "native_port" / "CMakeLists.txt"
WORKFLOW = ROOT / ".github" / "workflows" / "hulk-free-windows.yml"


class NativeEorLinkCoverage(unittest.TestCase):
    def test_every_eor_native_implementation_linked(self):
        cmake = TARGET.read_text(encoding="utf-8")
        sources = sorted((ROOT / "native_port" / "src").glob("eor_*.cpp"))
        self.assertGreaterEqual(len(sources), 40)
        target = cmake.partition("set(INCREDIBLEHULK_SOURCES")[2]
        target += cmake.partition("target_sources(incredible_hulk PRIVATE src/eor_diligent_mesh_buffers.cpp")[2]
        missing = [source.name for source in sources
                   if not re.search(r"\bsrc/" + re.escape(source.name) + r"\b", target)]
        self.assertEqual(missing, [], f"Native EoR source not linked into EXE: {missing}")

    def test_native_host_services_do_not_require_the_sdk(self):
        cmake = TARGET.read_text(encoding="utf-8")
        core = cmake.split("add_library(hulk_native_core STATIC", 1)[1].split("\n)", 1)[0]
        for source in ("eor_archive.cpp", "eor_frame_scheduler.cpp",
                       "native_pc_save_format.cpp", "native_pc_options_store.cpp",
                       "eor_fmod_catalog.cpp", "eor_native_lod.cpp"):
            self.assertIn("src/" + source, core)
            self.assertEqual(cmake.count("src/" + source), 1)
        self.assertLess(cmake.index("if(HULK_NATIVE_CORE_ONLY)"),
                        cmake.index("include(generated/rexglue.cmake)"))
        self.assertIn("target_link_libraries(incredible_hulk PRIVATE hulk_native_core)", cmake)
        self.assertNotRegex(cmake, r"target_link_libraries\(hulk_native_core[^\n]*(?:rex::|native_x_renderer)")

    def test_private_build_model_adapter_links_its_dependents(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        match = re.search(r"\$modelAssetsArgs\s*=\s*@\((.*?)\)\s*\n\s*&\s*cl\.exe\s+@modelAssetsArgs", workflow, re.S)
        self.assertIsNotNone(match, "Original model Windows command changed")
        args = match.group(1)
        for source in (
            "eor_original_model_assets.cpp",
            "eor_model_surface_materials.cpp",
            "eor_native_mesh_geometry.cpp",
            "eor_model_surfaces.cpp",
            "eor_model_resource.cpp",
            "eor_archive.cpp",
        ):
            self.assertIn(source, args,
                          f"Model adapter test cannot link original dependency: {source}")
        aot = re.search(r"\$aotDrawArgs\s*=\s*@\((.*?)\)\s*\n\s*&\s*cl\.exe\s+@aotDrawArgs", workflow, re.S)
        self.assertIsNotNone(aot)
        for source in ("eor_xdk_draw_capture.cpp",
                       "eor_verified_sampler_bindings.cpp",
                       "eor_spirv_texture_bindings.cpp"):
            self.assertIn(source, aot.group(1),
                          f"AOT draw test cannot link dependency: {source}")

    def test_host_vulkan_present_avoids_redundant_fullscreen_clear(self):
        host = (ROOT / "native_port" / "src" /
                "eor_diligent_host_output.cpp").read_text(encoding="utf-8")
        self.assertIn("HasFullNativeCompositePixels(", host)
        self.assertIn("desc.Width!=output.width || desc.Height!=output.height", host)
        self.assertIn("Blitter::Source::kColor,", host)
        self.assertIn("device_->Present();", host)
        self.assertRegex(host, r"SetRenderTargets\(0,nullptr,nullptr,\s*Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE\);")
        # Detachment is now a shared exception-safe lambda. Check its CALL
        # order, not the position of the lambda definition before the copy.
        self.assertLess(host.index("blitter_->Copy("), host.index("if(!detach())return false;"))
        self.assertLess(host.index("if(!detach())return false;"), host.index("device_->Present();"))
        self.assertIn("catch (...) {\n    detach();", host)
        self.assertNotIn("ClearRenderTarget(", host,
                         "Exact-size host copy must not pre-clear the full swapchain")

    def test_existing_vulkan_worker_owns_the_only_eor_present(self):
        host = (ROOT / "native_port/src/eor_diligent_host_output.cpp").read_text()
        api = (ROOT / "native_port/src/eor_diligent_host_output.h").read_text()
        blit = host.split("bool DiligentEorHostOutput::CompositeToBackbuffer(", 1)[1]
        blit = blit.split("bool DiligentEorHostOutput::CompositeAndPresent(", 1)[0]
        standalone = host.split("bool DiligentEorHostOutput::CompositeAndPresent(", 1)[1]
        standalone = standalone.split("NativeEorCompositeOutput BindNativeEorVulkanBackbuffer(", 1)[0]
        worker = host.split("NativeEorCompositeOutput BindNativeEorVulkanBackbuffer(", 1)[1]
        worker = worker.split("NativeEorCompositeOutput BindNativeEorVulkanPresentation(", 1)[0]
        self.assertIn("HasFullNativeCompositePixels(", blit)
        self.assertIn("blitter_->Copy(", blit)
        self.assertNotIn("device_->Present()", blit)
        self.assertNotIn("device_->Present()", worker)
        self.assertIn("host.CompositeToBackbuffer(", worker)
        self.assertIn("if(!CompositeToBackbuffer(source,dest,output))return false;", standalone)
        self.assertEqual(standalone.count("device_->Present();"), 1)
        self.assertIn("bool CompositeToBackbuffer(", api)
        self.assertIn("BindNativeEorVulkanBackbuffer(", api)
        # The pinned NativeX patch uses a separate worker decision:
        # kBackbufferReady -> existing worker FinishPresent, not another
        # EoR CompositeAndPresent.
        nx = (ROOT / "ci/patch_native_x_hulk_present_semantics.py").read_text()
        self.assertIn("HulkEorPresentResult::kBackbufferReady", nx)
        self.assertIn("FinishPresent();", nx)
        self.assertNotIn("CompositeAndPresent(", nx)

    def test_live_eor_draw_requires_original_frame_timeline(self):
        bridge = (ROOT / "native_port" / "src" /
                  "eor_aot_draw_bridge.cpp").read_text(encoding="utf-8")
        self.assertIn("if(captured_source && !exact_original_timeline)", bridge)
        self.assertIn("Live original EoR Draw requires exact XDK frame timeline", bridge)
        self.assertLess(
            bridge.index("if(captured_source && !exact_original_timeline)"),
            bridge.index("backend.PreflightOriginalScene(draws)"),
            "Missing live chronology must fail before GPU resource preparation")

    def test_original_sampler_bijection_rejected_before_gpu_begin(self):
        backend = (ROOT / "native_port" / "src" /
                   "eor_diligent_aot_backend.cpp").read_text(encoding="utf-8")
        self.assertIn("bool HasExactSourceSamplerBijection(", backend)
        function = backend.split("bool HasExactSourceSamplerBijection(", 1)[1]
        function = function.split("\n}", 1)[0]
        self.assertIn("matched.insert(unique).second", function)
        self.assertIn("unique!=resident.size()", function)
        preflight = backend.split("bool DiligentEorDirectBackend::PreflightOriginalScene(", 1)[1]
        preflight = preflight.split("bool DiligentEorDirectBackend::BeginScene(", 1)[0]
        self.assertIn("HasExactSourceSamplerBijection(", preflight)
        self.assertLess(preflight.index("HasExactSourceSamplerBijection("),
                        preflight.index("CreateOriginalVulkanPipeline("))
        bind = backend.split("bool DiligentEorDirectBackend::BindOriginalSamplers(", 1)[1]
        bind = bind.split("bool DiligentEorDirectBackend::DrawIndexed(", 1)[0]
        self.assertIn("HasExactSourceSamplerBijection(expected,textures)", bind)

    def test_original_material_owners_are_indexed_for_live_vulkan_draws(self):
        backend = (ROOT / "native_port" / "src" /
                   "eor_diligent_aot_backend.cpp").read_text(encoding="utf-8")
        constructor = backend.split("struct DiligentEorDirectBackend::State {", 1)[1]
        constructor = constructor.split("DiligentEorDirectBackend::DiligentEorDirectBackend(", 1)[0]
        self.assertIn("material_lookup.emplace(key,&original)", constructor)
        self.assertIn("std::map<MaterialKey,const NativeEorPassResources*> material_lookup", constructor)
        preflight = backend.split("bool DiligentEorDirectBackend::PreflightOriginalScene(", 1)[1]
        preflight = preflight.split("bool DiligentEorDirectBackend::BeginScene(", 1)[0]
        self.assertIn("s.material_lookup.find(", preflight)
        self.assertIn("s.material_lookup.find(key)", preflight)
        self.assertNotIn("for(const auto& material:s.passes)", preflight)
        bind_surface = backend.split("bool DiligentEorDirectBackend::BindOriginalSurface(", 1)[1]
        bind_surface = bind_surface.split("bool DiligentEorDirectBackend::BindOriginalEffect(", 1)[0]
        self.assertIn("s.material_lookup.lower_bound(", bind_surface)
        self.assertIn("std::get<0>(item->first)!=model", bind_surface)
        self.assertIn("std::get<1>(item->first)!=ordinal", bind_surface)
        bind_effect = backend.split("bool DiligentEorDirectBackend::BindOriginalEffect(", 1)[1]
        bind_effect = bind_effect.split("bool DiligentEorDirectBackend::BindOriginalConstants(", 1)[0]
        self.assertIn("s.material_lookup.find(", bind_effect)
        self.assertIn("s.selected=item->second;", bind_effect)

    def test_live_draw_accepts_multiple_attributes_per_original_vertex_stream(self):
        backend = (ROOT / "native_port" / "src" /
                   "eor_diligent_aot_backend.cpp").read_text(encoding="utf-8")
        draw = backend.split("bool DiligentEorDirectBackend::DrawIndexed(", 1)[1]
        draw = draw.split("bool DiligentEorDirectBackend::ResolveScene(", 1)[0]
        helper = backend.split("uint32_t SourceVertexSlotMask(", 1)[1]
        helper = helper.split("\n}", 1)[0]
        self.assertNotIn("slots.size()!=layout.size()", draw,
                         "Layout elements are attributes, not distinct GPU streams")
        self.assertIn("SourceVertexSlotMask(layout,slots)", draw)
        self.assertIn("HasVerifiedHostAttributeLayout(layout)", helper)
        self.assertIn("element.BufferSlot>=slots.size() || !slots[element.BufferSlot]", helper)
        self.assertIn("mask |= 1u << element.BufferSlot", helper)
        self.assertIn("if(!(used_slots & (1u << i)))continue", draw)
        self.assertIn("SetVertexBuffers(0,binding_count", draw)
        # The constructor and preflight must agree with live DrawIndexed:
        # one source-proven GPU buffer can supply multiple vertex attributes.
        constructor = backend.split("DiligentEorDirectBackend::DiligentEorDirectBackend(", 1)[1]
        constructor = constructor.split("DiligentEorDirectBackend::~DiligentEorDirectBackend(", 1)[0]
        self.assertNotIn("(used_slots & (1u << v.BufferSlot))", constructor)
        validator = backend.split("static void ValidateSourceOriginalMaterialPass(", 1)[1]
        validator = validator.split("DiligentEorDirectBackend::DiligentEorDirectBackend(", 1)[0]
        self.assertIn("ValidateSourceOriginalMaterialPass(owner,bank,effects,frame,formats)", constructor)
        self.assertIn("else if(!HasVerifiedHostAttributeLayout(pass.exact_host_input_layout))", validator)
        self.assertIn("!SourceRuntimeSlotMask(pass.exact_host_input_layout,symbolic)", validator)
        preflight = backend.split("bool DiligentEorDirectBackend::PreflightOriginalScene(", 1)[1]
        preflight = preflight.split("bool DiligentEorDirectBackend::BeginScene(", 1)[0]
        self.assertRegex(preflight, r"if\(!\(is_font \?\s*SourceFontSlotMask\([^)]+\)\s*:\s*runtime \?\s*SourceRuntimeSlotMask\([^)]+\)\s*:\s*SourceVertexSlotMask\([^)]+\)\) \|\|\s*!s\.meshes\.IndexWindowInRange\(")
        self.assertNotIn("mesh->second.host_vertex_slots.size()!=", preflight)


    def test_native_original_host_layout_is_checked_before_pipeline_preparation(self):
        backend = (ROOT / "native_port/src/eor_diligent_aot_backend.cpp").read_text()
        validator = (ROOT / "native_port/src/eor_host_vertex_layout.h").read_text()
        self.assertIn('#include "eor_host_vertex_layout.h"', backend)
        self.assertIn("bool HasVerifiedHostAttributeLayout(", backend)
        self.assertIn("ValidateHostVertexLayout(", backend)
        self.assertIn("occupied[slot] & byte_mask", validator)
        self.assertIn("offset > width || offset % 4u || size > width - offset", validator)
        self.assertIn("highest_end[slot] != (slot == 0 ? 12u : 16u)", validator)
        self.assertIn("slot == 0 && (attribute.components != 3 || offset != 0)", validator)
        preflight = backend.split("bool DiligentEorDirectBackend::PreflightOriginalScene(", 1)[1]
        preflight = preflight.split("bool DiligentEorDirectBackend::BeginScene(", 1)[0]
        self.assertRegex(preflight, r"if\(!\(is_font \?\s*SourceFontSlotMask\([^)]+\)\s*:\s*runtime \?\s*SourceRuntimeSlotMask\([^)]+\)\s*:\s*SourceVertexSlotMask\([^)]+\)\) \|\|\s*!s\.meshes\.IndexWindowInRange\(")
        draw = backend.split("bool DiligentEorDirectBackend::DrawIndexed(", 1)[1]
        self.assertIn("SourceVertexSlotMask(layout,slots)", draw)
        uploaded = (ROOT / "ci/eor_native_mesh_upload_test.cpp").read_text()
        self.assertIn("ValidateHostVertexLayout(", uploaded)

    def test_runtime_uint_vertex_storage_matches_upload_preflight_and_draw(self):
        backend = (ROOT / "native_port/src/eor_diligent_aot_backend.cpp").read_text()
        mask = backend.split("uint32_t SourceRuntimeSlotMask(", 1)[1].split("\n}", 1)[0]
        self.assertIn("e.ValueType==Diligent::VT_UINT32", mask)
        self.assertIn("e.InputIndex==1u || e.InputIndex==2u || e.InputIndex==9u", mask)
        storage = (ROOT / "native_port/src/eor_host_vertex_layout.h").read_text()
        self.assertIn("if(location==9u)return 4u;", storage)
        self.assertIn("if(location==1u || location==2u)return 3u;", storage)
        self.assertIn("return location==9u?UINT32_MAX:3u;", storage)
        self.assertRegex(backend, r"const auto storage=OriginalVertexStorageKind\(e.BufferSlot,e.InputIndex,\s*e.ValueType==Diligent::VT_UINT32,true,false\);")
        self.assertIn("if(storage==4u) {", backend)
        self.assertIn("UploadDecodedIntegerAttributes(cpu.integer_attributes[i])", backend)
        self.assertIn("UploadDecodedAttributes(cpu.attributes[i])", backend)
        preflight = backend.split("bool DiligentEorDirectBackend::PreflightOriginalScene(", 1)[1].split("bool DiligentEorDirectBackend::BeginScene(", 1)[0]
        self.assertRegex(preflight, r"OriginalVertexStorageKind\(slot,element.InputIndex,\s*element.ValueType==Diligent::VT_UINT32,runtime,is_font\)")
        self.assertIn("s.meshes.KindOf(mesh->second.host_vertex_slots[slot])!=kind", preflight)
        self.assertLess(preflight.index("OriginalVertexStorageKind("), preflight.index("CreateOriginalVulkanPipeline("))
        draw = backend.split("bool DiligentEorDirectBackend::DrawIndexed(", 1)[1].split("bool DiligentEorDirectBackend::ResolveScene(", 1)[0]
        self.assertRegex(draw, r"OriginalVertexStorageKind\(i,e.InputIndex,\s*e.ValueType==Diligent::VT_UINT32,runtime,is_font\)")
        self.assertIn("if(expected==UINT32_MAX || expected!=kind)return false;", draw)
        self.assertLess(draw.index("OriginalVertexStorageKind("), draw.index("SetVertexBuffers("))
        regression = (ROOT / "ci/eor_runtime_vertex_compaction_test.cpp").read_text()
        self.assertIn("OriginalVertexStorageKind(1,1,true,runtime,false)==3", regression)
        self.assertIn("OriginalVertexStorageKind(2,2,true,runtime,false)==3", regression)
        self.assertIn("OriginalVertexStorageKind(3,9,true,runtime,false)==4", regression)

    def test_failed_shader_pair_skips_unusable_heavy_capture(self):
        hook=(ROOT / "native_port/src/eor_xdk_hook_bridge.cpp").read_text()
        capture=hook.split("void CapturePpcDraw(",1)[1].split("void CapturePpcCommand(",1)[0]
        self.assertLess(capture.index("g_frame_shader_complete=false"),
                        capture.index("CaptureUsaAllShaderRegisters("))
        self.assertIn("diagnostic_mesh || (replay && g_frame_shader_complete)",capture)
        self.assertIn("g_collector.Record(std::move(draw))",capture)
        self.assertLess(capture.index("CaptureUsaShaderObjects("),
                        capture.index("CaptureUsaVsMatrices("))
        self.assertIn("ObserveBoundShaderIdentity(draw,bindings->vertex,bindings->pixel",capture)
        self.assertIn("g_frame_shader_complete.load(std::memory_order_relaxed)",capture)
        self.assertIn("const bool diagnostic_mesh=!replay",capture)
        self.assertIn("if(replay && !g_frame_shader_complete.load",capture)
        self.assertIn("!learn_material_owner",capture)
        self.assertIn("original_pass_sampler_slots_verified",capture.split("const bool learn_material_owner=",1)[1].split("if(replay",1)[0])
        self.assertLess(capture.index("g_original_texture_owners.Remember"),capture.index("const bool need_shader_identity"))
        self.assertIn("if(need_shader_identity && draw.observed_vs_matrices",capture)
        self.assertNotIn("capture_heavy=!replay",capture)
        self.assertIn("g_frame_shader_complete=true;",hook.split("void EndPpcHook(",1)[1])

    def test_built_in_eor_claims_before_archive_and_target_work(self):
        hook=(ROOT / "native_port/src/eor_xdk_hook_bridge.cpp").read_text()
        worker=hook.split("native_x_renderer::HulkEorPresentResult TryWorkerEorFrame(",1)[1]
        self.assertLess(worker.index("ClaimLatestIndependentIfSequence(source_sequence)"),
                        worker.index("RecordObservedOriginalAotPairDemand(*source_snapshot)"))
        self.assertIn("BuildAutomaticOriginalOwnerCatalogForFrame(host,source_snapshot)",worker)
        self.assertIn("source_sequence,independent_claimed",worker)
        self.assertIn("owner_catalog->accepts_live_source(*source_snapshot",worker)
        mixed=hook.split("HulkEorPresentResult ReplayVerifiedMixedOriginalFrame(",1)[1]
        self.assertIn("if(!independent_claimed)",mixed)
        self.assertLess(mixed.index("owner->gpu->CanReplayOriginalXdkTimeline(*snapshot)"),
                        mixed.index("aot::ReplayAotScene("))

    def test_original_archive_graphics_indices_include_real_texstreams(self):
        index = (ROOT / "native_port/src/eor_usa_index.cpp").read_text()
        texture = (ROOT / "native_port/src/eor_original_texture_usa.cpp").read_text()
        worker = (ROOT / "native_port/src/eor_xdk_hook_bridge.cpp").read_text()
        self.assertIn("Index::LoadPinnedUsaGraphicsArchive", index)
        self.assertIn('find("TexStreams")', index)
        self.assertIn("size()!=5836u", index)
        self.assertIn("Index::LoadPinnedUsaGraphicsArchive(index)", texture)
        self.assertIn("Index::LoadPinnedUsaGraphicsArchive(index)", worker)
        self.assertIn('root/"index(2).ind"', worker)

    def test_portable_eor_pack_root_shared_between_app_and_worker(self):
        header = (ROOT / "native_port/src/eor_source_pack_paths.h").read_text()
        app = (ROOT / "native_port/src/incredible_hulk_app.h").read_text()
        worker = (ROOT / "native_port/src/native_x_renderer_hulk.cpp").read_text()
        bridge = (ROOT / "native_port/src/eor_xdk_hook_bridge.cpp").read_text()
        self.assertIn('HULK_EOR_SOURCE_BANK_DIR', header)
        self.assertIn('config_dir/"eor_offline"', header)
        self.assertLess(header.index("if(HasCompletePack(root))"), header.index('if(!root.empty() && HasEither'))
        self.assertIn("eor_source_pack_root_=offline;", app)
        self.assertIn("eor_source_pack_root_);", app)
        self.assertIn("source_root.empty() ?", worker)
        self.assertIn("PairCount()!=2597u", app)
        self.assertIn("PairCount()!=2597u", bridge)
        self.assertIn("loader rejected source bank", bridge)

    def test_live_draw_vertex_slot_count_cannot_overrun_host_arrays(self):
        backend = (ROOT / "native_port" / "src" /
                   "eor_diligent_aot_backend.cpp").read_text(encoding="utf-8")
        # The native Draw path uses fixed 16-entry buffer/offset arrays.
        # DILIGENT_MAX_LAYOUT_ELEMENTS is not necessarily 16.
        self.assertIn("std::array<Diligent::IBuffer*,16> buffers{}", backend)
        self.assertIn("std::array<Diligent::Uint64,16> offsets{}", backend)
        self.assertRegex(
            backend,
            r"slots\.size\(\)>16",
        )


    def test_active_vulkan_scene_preflight_is_nondestructive(self):
        backend = (ROOT / "native_port" / "src" /
                   "eor_diligent_aot_backend.cpp").read_text(encoding="utf-8")
        prepare = backend.split("bool DiligentEorDirectBackend::PreflightOriginalScene(", 1)[1]
        prepare = prepare.split("bool DiligentEorDirectBackend::BeginScene(", 1)[0]
        self.assertRegex(prepare, r"if\s*\(s\.scene_open\)\s*return false;")
        self.assertLess(prepare.index("if(s.scene_open)return false;"),
                        prepare.index("s.frame_preflight_ready=false;"))
        self.assertLess(prepare.index("if(s.scene_open)return false;"),
                        prepare.index("s.packed_constants.clear();"))

    def test_indexed_source_windows_do_not_assume_triangle_aligned_start(self):
        # D3D triangle-list index COUNT must be divisible by three, not the
        # offset within a verified shared GPU index buffer.
        geometry = (ROOT / "native_port/src/eor_native_draw_geometry.cpp").read_text()
        upload = (ROOT / "native_port/src/eor_diligent_mesh_buffers.cpp").read_text()
        self.assertIn("src.index_count % 3u", geometry)
        self.assertNotIn("src.first_index % 3u", geometry)
        self.assertNotIn("host_first%3", geometry)
        self.assertIn("count%3", upload)
        self.assertNotIn("first%3", upload)
        self.assertIn("!expected.index_buffer || !observed.index_buffer", geometry)
        cpp = (ROOT / "ci/eor_native_draw_geometry_test.cpp").read_text()
        self.assertIn("offset.first_index=1;", cpp)
        self.assertIn("later.calls[0].gpr[3]=121u;", cpp)
        self.assertIn("invalidIbWitness[0].original_resources.index_buffer=0;", cpp)

    def test_native_draw_binding_is_single_use_even_on_failure(self):
        backend = (ROOT / "native_port" / "src" /
                   "eor_diligent_aot_backend.cpp").read_text(encoding="utf-8")
        draw = backend.split("bool DiligentEorDirectBackend::DrawIndexed(", 1)[1]
        draw = draw.split("bool DiligentEorDirectBackend::ResolveScene(", 1)[0]
        self.assertRegex(draw, r"const bool source_bindings_ready\s*=\s*"
                         r"s\.constants_ready\s*&&\s*s\.program_ready\s*&&"
                         r"\s*s\.textures_ready;")
        self.assertLess(draw.index("s.constants_ready=s.program_ready=s.textures_ready=false;"),
                        draw.index("if(!s.scene_open"),
                        "A failing Draw must consume previous material/matrix bindings")
        self.assertIn("!source_bindings_ready", draw)

    def test_live_vulkan_begin_scene_detaches_after_partial_bind_failure(self):
        source = (ROOT / "native_port" / "src" /
                  "eor_diligent_scene_targets.cpp").read_text(encoding="utf-8")
        begin = source.split("bool DiligentSceneTargets::BeginScene() {", 1)[1]
        begin = begin.split("bool DiligentSceneTargets::ClearVerifiedOriginalXdk(", 1)[0]
        abort = source.split("void DiligentSceneTargets::AbortScene() noexcept {", 1)[1]
        abort = abort.split("void DiligentSceneTargets::Reset() noexcept {", 1)[0]
        self.assertLess(begin.index("state_->bound=true;"),
                        begin.index("state_->context->SetRenderTargets(1,"),
                        "Even a partially successful, throwing Vulkan bind must be detached")
        self.assertIn("catch (...) {AbortScene();return false;}", begin)
        self.assertIn("if (state_->bound)", abort)
        self.assertIn("state_->context->SetRenderTargets(0,nullptr,nullptr,", abort)

    def test_native_eor_actual_worker_backbuffer_feedback(self):
        hook = (ROOT / "native_port/src/eor_xdk_hook_bridge.cpp").read_text()
        header = (ROOT / "native_port/src/eor_xdk_hook_bridge.h").read_text()
        menu = (ROOT / "native_port/src/pc_menu.cpp").read_text()
        options = (ROOT / "native_port/src/native_pc_options_store.cpp").read_text()
        self.assertIn("EorWorkerStatus GetEorWorkerStatus() noexcept", header)
        self.assertIn("g_eor_ready_backbuffers.fetch_add(1", hook)
        self.assertIn("g_eor_gpu_failures.fetch_add(1", hook)
        self.assertIn('key == "hulk_eor_vulkan_replay"', options)
        self.assertIn("GetEorWorkerStatus()", menu)
        for start in ("RetireCompletedOriginalFontDraws(source_sequence);", "if(submitted) {"):
            success=hook.index("g_eor_ready_backbuffers.fetch_add(1", hook.index(start))
            self.assertLess(success, hook.index("return Result::kBackbufferReady", hook.index(start)))

    def test_real_usa_menu_quad_fan_reaches_verified_vulkan_topology(self):
        bridge=(ROOT / "native_port/src/eor_aot_draw_bridge.cpp").read_text()
        geometry=(ROOT / "native_port/src/eor_native_draw_geometry.cpp").read_text()
        source=(ROOT / "native_port/src/eor_xdk_draw_capture.h").read_text()
        cpp=(ROOT / "ci/eor_native_draw_geometry_test.cpp").read_text()
        self.assertIn("xdk::VerifiedTriangleListCount(",bridge)
        self.assertIn("OriginalTriangleFanVertex(j/3u,j%3u)",geometry)
        self.assertIn("primitive==6u",source)
        self.assertIn("0x8268BFE4u",cpp)
        self.assertIn("fanGpuDraw[0].draw.index_count==6",cpp)

    def test_live_original_xdk_buffer_bytes_match_original_gpu_resident_mesh(self):
        hook=(ROOT / "native_port/src/eor_xdk_hook_bridge.cpp").read_text()
        geom=(ROOT / "native_port/src/eor_native_draw_geometry.cpp").read_text()
        source=(ROOT / "native_port/src/eor_native_mesh_upload.cpp").read_text()
        test=(ROOT / "ci/eor_native_draw_geometry_test.cpp").read_text()
        self.assertIn("draw.source_ib=SourceIndexBuffer(",hook)
        self.assertIn("capture_stream(0);",hook)
        self.assertIn("state.original_vertex_buffer_guest_objects[slot]",hook)
        self.assertIn("binding=ReadUsaVertexStreamBinding(",hook)
        self.assertIn("binding->allocation_address,binding->allocation_bytes,ReadGuest,guest_base",hook)
        self.assertIn("state.original_index_buffer_guest_object",hook)
        self.assertIn("g_original_buffers.Reset();",hook)
        self.assertIn("g_original_buffers.Capture(object,binding->address,binding->bytes,ReadGuest,guest_base)",hook)
        self.assertIn("rec.original_source_vertex_bytes=m.original_vertex_stream;",source)
        self.assertIn("IdentifyExactOriginalGuestMesh(",geom)
        # Actual XDK buffer may be BE16/LE16 or BE32/LE32. Accept only
        # a unique byte-exact original model source index window.
        self.assertIn("for(uint32_t index_bytes:{2u,4u})",geom)
        self.assertIn("if(unique)return std::nullopt;",geom)
        self.assertIn("indexed->first_index)+indexed->index_count>",geom)
        self.assertIn("guest_vertex>=origin+mesh.vertex_count",geom)
        self.assertIn("guest_vertex>=limit",geom)
        self.assertIn("Actual original XDK buffer bytes disprove engine mesh owner",geom)
        self.assertIn("wrongIb->bytes[0]=std::byte{0xFA};",test)

    def test_live_draw_shader_guest_objects_match_real_usa_pass_catalog(self):
        source=(ROOT / "native_port/src/eor_native_draw_geometry.cpp").read_text()
        header=(ROOT / "native_port/src/eor_effect_catalog.h").read_text()
        cpp=(ROOT / "ci/eor_native_draw_geometry_test.cpp").read_text()
        self.assertIn("ValidateLiveOriginalShaderPasses(captured,source,effects);",source)
        self.assertIn("effects.FindOriginalPass(",source)
        identities=(ROOT / "native_port/src/eor_original_virtual_shader_identity.cpp").read_text()
        resolver=identities.split("std::optional<OriginalEffectPass> ResolveOriginalLiveEorEffectPass(",1)[1].split("bool MatchesOriginalLiveXdkShaderSource(",1)[0]
        self.assertIn("effects.UniquePixelForVertex(*vs)",resolver)
        self.assertIn("if(pixel.alias_paired_pixel_source)",resolver)
        self.assertIn("if(!paired || *paired!=*ps || *paired!=*pixel.alias_paired_pixel_source)",resolver)
        self.assertLess(resolver.index("effects.UniquePixelForVertex(*vs)"),resolver.rindex("return effects.FindOriginalPass(*vs,*ps)"))
        for stage in ("Pixel", "Vertex"):
            self.assertIn(f"count_unambiguous(metadata,Stage::{stage})",identities)
            self.assertIn(f"count_unambiguous(next,Stage::{stage})",identities)
        self.assertIn("effects.UniqueVertexForPixel(*ps)",resolver)
        self.assertIn("effects.UniqueLivePair(v,p)",resolver)
        self.assertIn("joint->first!=*vs || joint->second!=*ps",resolver)
        self.assertIn("vertex_admitted!=2595u || vertex_metadata!=1598u",identities)
        self.assertIn("admitted!=2595u || metadata_admitted!=1601u",identities)
        self.assertIn("draw.observed_live_eor_effect_pass->effect_hash",source)
        self.assertIn("draw.observed_live_eor_effect_pass->pass_index",source)
        hook=(ROOT / "native_port/src/eor_xdk_hook_bridge.cpp").read_text()
        observe=hook.split("static void ObserveBoundShaderIdentity(",1)[1].split("void CapturePpcDraw(",1)[0]
        # Exactly one live observation of each stage. Subsequent alias selection
        # and effect verification must use those same authenticated snapshots.
        self.assertEqual(observe.count("aot::ObserveOriginalBoundXdkShader("),2)
        self.assertIn("const auto& effects=*g_original_effect_capture_catalog;",observe)
        self.assertIn("effects.UniquePixelForVertex(*vs)",observe)
        self.assertIn("effects.UniqueVertexForPixel(*ps)",observe)
        self.assertIn("aot::SelectJointOriginalLiveAliasPair(",observe)
        self.assertIn("aot::ResolveOriginalLiveEorEffectPass(",observe)
        self.assertIn("if(!draw.observed_live_eor_effect_pass)",observe)
        self.assertLess(observe.index("aot::ResolveOriginalLiveEorEffectPass("),
                        observe.index("draw.observed_live_vertex_shader_header=std::move(v)"))
        self.assertIn("state.original_vs_guest_object",source)
        self.assertIn("state.original_ps_guest_object",source)
        self.assertIn("Same live XDK VS guest object claimed two original source shaders",source)
        self.assertIn("Same live XDK PS guest object claimed two original source shaders",source)
        self.assertIn("distinctShaderObjects.original_vs_guest_object^=4u;",cpp)
        self.assertIn("std::map<std::pair<Digest,Digest>,OriginalEffectPass> reverse_",header)

    def test_verified_material_texture_owner_matches_original_surface(self):
        geometry=(ROOT / "native_port/src/eor_native_draw_geometry.cpp").read_text()
        cpp=(ROOT / "ci/eor_native_draw_geometry_test.cpp").read_text()
        self.assertIn("IdentifyOriginalMaterialSurface(",geometry)
        self.assertIn("surface.indexed_texture_references",geometry)
        self.assertIn("*archive_material_surface!=src.original_surface_ordinal",geometry)
        self.assertIn("wrongMaterial.original_surface_name=",cpp)
        self.assertIn("ambiguous,original.original_sampler_slots",cpp)

    def test_original_shared_xdk_buffer_windows_and_draw_rebase(self):
        geometry=(ROOT / "native_port/src/eor_native_draw_geometry.cpp").read_text()
        header=(ROOT / "native_port/src/eor_native_draw_geometry.h").read_text()
        cpp=(ROOT / "ci/eor_native_draw_geometry_test.cpp").read_text()
        self.assertIn("boyer_moore_horspool_searcher",geometry)
        self.assertIn("int64_t guest_first_vertex=0",header)
        self.assertIn("uint32_t guest_first_index=0",header)
        self.assertIn("original->first_index-exact->guest_first_index",geometry)
        self.assertIn("resolved[i].first_index!=",geometry)
        self.assertIn("original->base_vertex)-exact->guest_first_vertex",geometry)
        self.assertIn("sharedVb->bytes.insert(",cpp)
        self.assertIn("sharedIb->bytes.insert(",cpp)
        self.assertIn("for(uint32_t index_bytes:{2u,4u})",geometry)
        self.assertIn("original_index_element_bytes==4",cpp)
        self.assertIn("wrongSharedBase.gpr[2]=0",cpp)

    def test_live_exact_gpu_bytes_filter_ambiguous_engine_mesh_witnesses(self):
        geo=(ROOT / "native_port/src/eor_native_draw_geometry.cpp").read_text()
        cxx=(ROOT / "ci/eor_native_draw_geometry_test.cpp").read_text()
        self.assertIn("exact_mesh_at_draw.reserve(frame.calls.size())",geo)
        self.assertIn("candidate_source.original_model_hash!=exact->model_hash",geo)
        self.assertIn("exact_mesh_at_draw[i]",geo)
        self.assertIn("twoResidences",cxx)
        self.assertIn("recoveredOwner[0].original_model_hash==0xCAFE",cxx)
        self.assertIn("sourceFrame.calls[0].source_vb0.reset();",cxx)

    def test_direct_eor_request_enters_shared_vulkan_host_without_native_x_manual_toggle(self):
        source=(ROOT / "native_port/src/native_x_renderer_hulk.cpp").read_text()
        assert_line='rex::cvar::Query<bool>("hulk_eor_vulkan_replay")'
        self.assertIn(assert_line,source)
        self.assertIn('rex::cvar::SetFlagByName(\n        "native_x_renderer","true")',source)
        self.assertLess(source.index('rex::cvar::SetFlagByName(\n        "native_x_renderer","true")'),
                        source.index('if (!native_requested && !verify_only && !eor_observer_only)'))
        self.assertIn('native_requested=selected &&',source)
        self.assertIn('preserving Xenos',source)

    def test_native_backend_resets_viewport_and_scissor_per_actual_eor_draw(self):
        source=(ROOT / "native_port/src/eor_diligent_aot_backend.cpp").read_text()
        start=source.index("bool DiligentEorDirectBackend::DrawIndexed(")
        end=source.index("bool DiligentEorDirectBackend::ResolveScene(",start)
        render=source[start:end]
        preflight=source[source.index("bool DiligentEorDirectBackend::PreflightOriginalScene("):start]
        self.assertIn("VerifiedOriginalViewSpace::OriginalAspectHud",preflight)
        self.assertIn("native::MapOriginalRasterWindow(",preflight)
        self.assertIn("raster=packed->second.raster",render)
        self.assertIn("raster.min_depth,raster.max_depth",render)
        self.assertIn("s.context->SetViewports(1,&original_viewport",render)
        self.assertIn("s.context->SetScissorRects(1,&original_scissor",render)
        self.assertLess(render.index("SetViewports("),
                        render.index("SetPipelineState("))
        self.assertNotIn("if(s.frame.widescreen_camera) {",render)

    def test_opt_in_original_xdk_capture_preserves_every_vertex_stream(self):
        src=(ROOT / "native_port/src/eor_xdk_hook_bridge.cpp").read_text()
        header=(ROOT / "native_port/src/eor_xdk_draw_capture.h").read_text()
        geo=(ROOT / "native_port/src/eor_native_draw_geometry.cpp").read_text()
        self.assertIn("source_vertex_streams{}",header)
        self.assertIn("draw.source_vertex_streams[slot]=g_original_buffers.Capture(",src)
        self.assertIn("draw.source_vb0=draw.source_vertex_streams[0]",src)
        self.assertIn("draw.source_vertex_streams={};",src)
        # Under the fixed per-Present budget the required indexed buffer
        # MUST be captured ahead of stale secondary vertex bindings.
        start=src.index("void CapturePpcDraw(")
        end=src.index("void CapturePpcCommand(",start)
        capture=src[start:end]
        self.assertLess(capture.index("draw.source_vb0=draw.source_vertex_streams[0]"),
                        capture.index("draw.source_ib=SourceIndexBuffer("))
        self.assertLess(capture.index("draw.source_ib=SourceIndexBuffer("),
                        capture.index("for(uint32_t slot=1;"))
        self.assertIn("call.source_vertex_streams[slot]",geo)
        self.assertIn("source_streams_present=false;break;",geo)
        self.assertIn("if(!source_streams_present)continue;",geo)

    def test_live_original_mesh_cache_is_keyed_to_guest_buffer_generations(self):
        source=(ROOT / "native_port/src/eor_native_draw_geometry.cpp").read_text()
        self.assertIn("using OriginalGeometryKey=std::tuple<",source)
        self.assertIn("exact_source_cache.try_emplace(key)",source)
        self.assertIn("if(inserted)\n        found->second=IdentifyExactOriginalGuestMesh",source)
        self.assertIn("reinterpret_cast<uintptr_t>(call.source_vb0.get())",source)
        self.assertIn("reinterpret_cast<uintptr_t>(call.source_ib.get())",source)
        self.assertIn("call.gpr[1],call.gpr[2],call.gpr[3],call.gpr[4]",source)
        self.assertIn("exact_mesh_at_draw.push_back(exact)",source)

    def test_native_worker_fallback_only_suppressed_after_gpu_begin_attempt(self):
        bridge = (ROOT / "native_port/src/eor_aot_draw_bridge.cpp").read_text()
        worker = (ROOT / "native_port/src/eor_xdk_hook_bridge.cpp").read_text()
        source = (ROOT / "native_port/src/eor_native_draw_geometry.cpp").read_text()
        begin = bridge.split("bool BeginScene(native::Size size,uint32_t samples) override {", 1)[1]
        begin = begin.split("void AbortScene()", 1)[0]
        self.assertLess(begin.index("if(scene_begin_attempted_)*scene_begin_attempted_=true;"),
                        begin.index("return target_.BeginScene(size,samples);"))
        replay = bridge.split("void ReplayAotScene(", 1)[1]
        self.assertLess(replay.index("if(scene_begin_attempted)*scene_begin_attempted=false;"),
                        replay.index("backend.PreflightOriginalScene(draws)"))
        worker = worker.split("bool ReplayNextVerifiedPpcFrame(", 1)[1]
        worker = worker.split("void CapturePpcDraw(", 1)[0]
        self.assertNotIn("if(reached_gpu)*reached_gpu=true;", worker)
        self.assertIn("reached_gpu);", worker)
        self.assertIn("&captured,scene_begin_attempted);", source)
        self.assertIn("bank,effects,reflection,backend,scene_begin_attempted);", source)

    def test_native_index_window_uses_immutable_block_extrema(self):
        backend = (ROOT / "native_port/src/eor_diligent_mesh_buffers.cpp").read_text()
        upload = backend.split("uint64_t DiligentMeshUploadBackend::UploadIndices(", 1)[1]
        upload = upload.split("uint64_t DiligentMeshUploadBackend::UploadDecodedAttributes(", 1)[0]
        window = backend.split("bool DiligentMeshUploadBackend::IndexWindowInRange(", 1)[1]
        window = window.split("size_t DiligentMeshUploadBackend::LiveBufferCount(", 1)[0]
        self.assertIn("mirror.assign(v.begin(),v.end());", upload)
        self.assertIn("extrema.emplace_back(*bounds.first,*bounds.second);", upload)
        self.assertIn("record.index_block_extrema=std::move(extrema);", upload)
        self.assertIn("count>indices.size()-first", window)
        self.assertIn("pos%kIndexBlockElements!=0", window)
        self.assertIn("it->second.index_block_extrema[pos/kIndexBlockElements]", window)
        self.assertIn("int64_t(minimum)+base>=0", window)

    def test_native_eor_one_present_attempt_per_source_frame(self):
        backend = (ROOT / "native_port" / "src" /
                   "eor_diligent_aot_backend.cpp").read_text(encoding="utf-8")
        self.assertIn("bool composite_attempted=false;", backend)
        for name, end in (("BeginScene", "AbortScene"),
                          ("AbortScene", "BindOriginalSurface")):
            segment = backend.split(
                f"void DiligentEorDirectBackend::{name}(", 1)[1] if name == "AbortScene" else backend.split(
                "bool DiligentEorDirectBackend::BeginScene(", 1)[1]
            segment = segment.split(f"DiligentEorDirectBackend::{end}(", 1)[0]
            self.assertIn("s.composite_attempted=false;", segment)
        present = backend.split(
            "bool DiligentEorDirectBackend::Composite(", 1)[1]
        self.assertIn("s.composite_attempted ||", present)
        self.assertLess(present.index("s.composite_attempted ||"),
                        present.index("s.scene.ResolvedColorSRV()"))
        self.assertLess(present.index("if(!image)return false;"),
                        present.index("s.composite_attempted=true;"))
        self.assertLess(present.index("s.composite_attempted=true;"),
                        present.index("s.composite(image,dest,output)"))
        self.assertLess(present.index("s.composite(image,dest,output)"),
                        present.index("s.scene_open=false;"))

    def test_live_vulkan_resolve_waits_for_original_source_resolve(self):
        backend = (ROOT / "native_port" / "src" /
                   "eor_diligent_aot_backend.cpp").read_text(encoding="utf-8")
        resolve = backend.split("bool DiligentEorDirectBackend::ResolveScene(", 1)[1]
        resolve = resolve.split("bool DiligentEorDirectBackend::Composite(", 1)[0]
        self.assertIn("s.source_timeline_active && !s.source_resolve_recorded", resolve)
        self.assertLess(resolve.index("!s.source_resolve_recorded"),
                        resolve.index("s.scene.ResolveScene()"))

    def test_draw_identity_checked_before_replaying_xdk_gpu_side_effects(self):
        bridge = (ROOT / "native_port" / "src" /
                  "eor_aot_draw_bridge.cpp").read_text(encoding="utf-8")
        draw = bridge.split("bool DrawIndexed(const native::IndexedDraw& draw) override {", 1)[1]
        draw = draw.split("bool ResolveScene(", 1)[0]
        self.assertLess(draw.index("EoR command/shader sequence mismatch"),
                        draw.index("if(!ReplayBeforeDraw(draw))"),
                        "Unmatched Draw must not replay XDK GPU commands")

    def test_shader_evidence_checked_before_live_xdk_replay(self):
        bridge = (ROOT / "native_port" / "src" /
                  "eor_aot_draw_bridge.cpp").read_text(encoding="utf-8")
        draw = bridge.split("bool DrawIndexed(const native::IndexedDraw& draw) override {", 1)[1]
        draw = draw.split("bool ResolveScene(", 1)[0]
        self.assertLess(draw.index("Unknown original Xenos shader or constant layout"),
                        draw.index("if(!ReplayBeforeDraw(draw))"),
                        "Unknown shader or reflection must not mutate live GPU state")
        self.assertLess(draw.index("if(!ReplayBeforeDraw(draw))"),
                        draw.index("++position_"),
                        "Rejected source chronology must not consume a paired Draw")

    def test_decoded_original_mesh_attribute_link_dependencies(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        for call in workflow.split("'native_port/src/eor_native_mesh_upload.cpp',")[1:]:
            self.assertIn("'native_port/src/eor_native_attributes.cpp',",
                          call.split("& cl.exe", 1)[0])
        native = (ROOT / "native_port" / "src" /
                  "eor_diligent_aot_backend.cpp").read_text(encoding="utf-8")
        upload = (ROOT / "native_port" / "src" /
                  "eor_native_mesh_upload.cpp").read_text(encoding="utf-8")
        self.assertIn("mesh.decoded_attribute_buffers", native)
        self.assertIn("const auto& layout=layouts[i][j];", upload)
        self.assertIn("DecodeVerifiedAttribute(meshes[i],layout)", upload)
        self.assertIn("backend.UploadDecodedIntegerAttributes(indices)", upload)
        self.assertIn("backend.UploadDecodedAttributes(decoded[mesh_id][i])", upload)

    def test_shader_constant_packer_is_both_linked_and_tested(self):
        cmake = TARGET.read_text(encoding="utf-8")
        workflow = WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("src/eor_native_shader_constants.cpp", cmake)
        self.assertIn("native_port/src/eor_native_shader_constants.cpp", workflow)
        self.assertIn("ci/eor_native_shader_constants_test.cpp", workflow)


    def test_live_game_loads_matching_private_original_pass_and_shader_registers(self):
        app = (ROOT / "native_port" / "src" / "incredible_hulk_app.h").read_text(encoding="utf-8")
        for input_name in ("hulk_shader_bank.bin", "hulk_effect_ledger.bin",
                           "hulk_shader_reflection.bin"):
            self.assertIn(input_name, app)
        self.assertIn("effects->Load(private_effects);", app)
        self.assertIn("reflection->Load(private_reflection);", app)
        self.assertIn("effects->ShaderCount() != bank->expected()", app)
        self.assertIn("reflection->size() != bank->expected()", app)
        # All private products must validate before they can be published as
        # one coherent material/pass/shader set at actual game initialization.
        self.assertLess(app.index("reflection->size() != bank->expected()"),
                        app.index("eor_effects_ = std::move(effects);"))
        self.assertLess(app.index("eor_reflection_ = std::move(reflection);"),
                        app.index("eor_aot_ = std::move(bank);"))
        self.assertIn("eor_reflection_.reset();", app)
        self.assertIn("eor_effects_.reset();", app)
        self.assertNotIn("ReplayAotScene(", app,
                         "A bank without verified EoR draw ABI is NOT a real game frame")

    def test_verified_original_models_are_handed_to_live_vulkan_worker(self):
        bridge=(ROOT / "native_port/src/eor_xdk_hook_bridge.cpp").read_text()
        header=(ROOT / "native_port/src/eor_xdk_hook_bridge.h").read_text()
        source=(ROOT / "native_port/src/eor_native_draw_geometry.cpp").read_text()
        streamer=(ROOT / "native_port/src/eor_native_world_stream.cpp").read_text()
        fn=bridge.split("bool InstallVerifiedWorkerEorFrameWithResidents(",1)[1]
        fn=fn.split("void ClearVerifiedWorkerEorFrame()",1)[0]
        self.assertIn("InstallVerifiedWorkerEorFrameWithResidents(", header)
        self.assertIn("native::RetainVerifiedOriginalFrameResidents(", fn)
        self.assertIn("g_frames_for_native.Peek()", fn)
        self.assertIn("source->sequence!=frame->original_frame_sequence", fn)
        self.assertIn("complete->residents=std::move(residents)", fn)
        self.assertIn("InstallVerifiedWorkerEorFrame(std::move(complete))", fn)
        self.assertIn("ResolveCapturedResourceDraws(frame,witnesses,borrowed)", source)
        self.assertIn("std::shared_ptr<const model::UploadedOriginalModel>", source)
        self.assertIn("ResidentShared(uint32_t hash)", streamer)
        self.assertLess(fn.index("native::RetainVerifiedOriginalFrameResidents("),
                        fn.index("InstallVerifiedWorkerEorFrame(std::move(complete))"))

    def test_private_shader_pack_reaches_existing_gpu_worker(self):
        game = (ROOT / "native_port/src/incredible_hulk_app.h").read_text()
        bridge = (ROOT / "native_port/src/eor_xdk_hook_bridge.cpp").read_text()
        header = (ROOT / "native_port/src/eor_xdk_hook_bridge.h").read_text()
        self.assertIn("auto bank = std::make_shared<hulk::eor::aot::ShaderBank>();", game)
        self.assertIn("auto effects = std::make_shared<hulk::eor::aot::EffectCatalog>();", game)
        self.assertIn("auto reflection = std::make_shared<hulk::eor::aot::ReflectionBank>();", game)
        self.assertIn("InstallVerifiedSourceShaderPack(", game)
        self.assertIn("InstallVerifiedSourceShaderPack(", header)
        self.assertIn("InstallVerifiedSourceShaderPack(", bridge)
        self.assertIn("g_offline_shader_pack=OfflineShaderPack{", bridge)
        self.assertIn("g_verified_worker_frame.reset();", bridge)
        self.assertIn("complete->shaders=pack.shaders;", bridge)
        self.assertIn("complete->effects=pack.effects;", bridge)
        self.assertIn("complete->reflection=pack.reflection;", bridge)
        self.assertIn("ClearVerifiedSourceShaderPack();", game)
        self.assertLess(game.index("InstallVerifiedSourceShaderPack("),
                        game.index("hulk::NativeXInitialize(",
                                   game.index("InstallVerifiedSourceShaderPack(")))
        self.assertNotIn("make_unique<hulk::eor::aot::ShaderBank>", game)
        self.assertIn("frame->witnesses.empty() || frame->residents.empty()", bridge)

    def test_diagnostic_eor_optin_uses_single_immediate_host_present(self):
        game = (ROOT / "native_port/src/native_x_renderer_hulk.cpp").read_text()
        toml = (ROOT / "native_port/incredible_hulk.toml").read_text()
        upstream = (ROOT / "ci/patch_native_x_hulk_present_semantics.py").read_text()
        self.assertIn("native_x_renderer_present_with_swaps = true", toml)
        self.assertIn('const bool requested_eor = rex::cvar::Query<bool>("hulk_eor_vulkan_replay");', game)
        self.assertIn('rex::cvar::SetFlagByName("hulk_eor_xdk_capture","true");', game)
        self.assertIn('rex::cvar::SetFlagByName("native_x_renderer_present_with_swaps","false");', game)
        self.assertIn("eor_worker_allowed ? &hulk::eor::xdk::TryWorkerEorFrame : nullptr", game)
        self.assertIn("!g_present_with_swaps && !g_frame_generation", upstream)

    def test_active_native_vulkan_worker_reaches_original_ppc_replay(self):
        hook = (ROOT / "native_port/src/eor_xdk_hook_bridge.cpp").read_text()
        header = (ROOT / "native_port/src/eor_xdk_hook_bridge.h").read_text()
        game = (ROOT / "native_port/src/native_x_renderer_hulk.cpp").read_text()
        app = (ROOT / "native_port/src/incredible_hulk_app.h").read_text()
        begin = game.index("native_x_renderer::SetHulkEorPresentCallback(")
        launch = game.index("native_x_renderer::Initialize(", begin)
        self.assertLess(begin, launch)
        self.assertIn("&hulk::eor::xdk::TryWorkerEorFrame", game)
        self.assertIn("native_x_renderer::HulkEorPresentResult TryWorkerEorFrame(", header)
        self.assertIn("g_verified_worker_frame", hook)
        self.assertIn("ready->device!=&host", hook)
        self.assertIn("desc.Width!=ready->output.backbuffer.width", hook)
        worker = hook.split("native_x_renderer::HulkEorPresentResult TryWorkerEorFrame(",1)[1]
        self.assertIn("ReplayNextVerifiedPpcFrame(", worker)
        self.assertIn("Result::kBackbufferReady", worker)
        self.assertIn("Result::kFailedAfterGpu", worker)
        self.assertIn("Result::kNotReady", worker)
        self.assertIn("ready->witnesses,ready->output,residents,", worker)
        self.assertIn("original_frame_sequence", header)
        self.assertIn("source_sequence!=ready->original_frame_sequence", worker)
        self.assertIn("original.native_draw.original_frame_sequence ==", hook)
        self.assertLess(worker.index("source_snapshot.reset();"),
                        worker.index("g_frames_for_native.DiscardIfSequence(source_sequence)"))
        self.assertLess(worker.index("g_frames_for_native.DiscardIfSequence(source_sequence)"),
                        worker.index("ReplayNextVerifiedPpcFrame("))
        self.assertNotIn("if(lost)return Result::kNotReady", worker)
        self.assertIn("native_x_renderer::SetHulkEorPresentCallback(nullptr);", app)
        self.assertIn("ClearVerifiedWorkerEorFrame();", app)

    def test_live_replay_denies_black_frame_on_clean_preflight_failure(self):
        bridge = (ROOT / "native_port/src/eor_xdk_hook_bridge.cpp").read_text()
        header = (ROOT / "native_port/src/eor_xdk_hook_bridge.h").read_text()
        replay = bridge.split("bool ReplayNextVerifiedPpcFrame(", 1)[1]
        replay = replay.split("void CapturePpcDraw(", 1)[0]
        worker = bridge.split("native_x_renderer::HulkEorPresentResult TryWorkerEorFrame(", 1)[1]
        self.assertIn("bool* reached_gpu=nullptr", header)
        self.assertIn("if(reached_gpu)*reached_gpu=false;", replay)
        # GPU mutation begins in the forwarding backend, after source
        # validation; setting true in this outer function would black-screen
        # harmless ownership rejections before BeginScene.
        self.assertNotIn("if(reached_gpu)*reached_gpu=true;", replay)
        self.assertLess(replay.index("if(reached_gpu)*reached_gpu=false;"),
                        replay.index("gpu.CanReplayOriginalXdkTimeline(*preview)"))
        self.assertLess(replay.index("g_frames_for_native.TakeIfSequence(sequence)"),
                        replay.index("native::ReplayDeliveredResourceScene("))
        self.assertIn("reached_gpu);", replay.split("native::ReplayDeliveredResourceScene(",1)[1])
        forwarding=(ROOT / "native_port/src/eor_aot_draw_bridge.cpp").read_text()
        begin=forwarding.split("bool BeginScene(",1)[1].split("\n  }",1)[0]
        self.assertLess(begin.index("if(scene_begin_attempted_)*scene_begin_attempted_=true;"),
                        begin.index("return target_.BeginScene(size,samples);"))
        self.assertIn("source_sequence,&reached_gpu);", worker)
        failed=worker.split("if(reached_gpu) {",1)[1]
        self.assertLess(failed.index("return Result::kFailedAfterGpu;"),
                        failed.index("return Result::kNotReady;"))
        self.assertNotIn("return submitted ? Result::kBackbufferReady : Result::kFailedAfterGpu;", worker)

    def test_worker_checks_real_guest_mesh_material_shader_before_vulkan(self):
        worker=(ROOT / "native_port/src/eor_xdk_hook_bridge.cpp").read_text()
        api=(ROOT / "native_port/src/eor_xdk_hook_bridge.h").read_text()
        start=worker.index("native_x_renderer::HulkEorPresentResult TryWorkerEorFrame(")
        source=worker[start:]
        self.assertIn("source_snapshot=g_frames_for_native.Peek();", source)
        self.assertIn("native::ResolveCapturedResourceDraws(", source)
        self.assertIn("native::BindVerifiedGeometryDraws(", source)
        self.assertIn("ready->shaders->Find(draw.vertex_source_sha256,aot::Stage::Vertex)", source)
        self.assertIn("ready->shaders->Find(draw.pixel_source_sha256,aot::Stage::Pixel)", source)
        self.assertIn("ready->gpu->CanReplayOriginalXdkTimeline(*source_snapshot)", source)
        self.assertIn("source_snapshot.reset();", source)
        self.assertIn("Result::kNotReady;", source)
        self.assertLess(source.index("native::ResolveCapturedResourceDraws("),
                        source.index("ReplayNextVerifiedPpcFrame("))
        self.assertLess(source.index("source_snapshot.reset();\n  try {"),
                        source.index("ReplayNextVerifiedPpcFrame("))
        self.assertIn("uint64_t expected_original_frame_sequence=0", api)
        self.assertIn("sequence!=expected_original_frame_sequence", worker)
        self.assertIn("source_sequence);", source)

    def test_actual_present_mailbox_can_feed_verified_native_eor_scene(self):
        hook = (ROOT / "native_port/src/eor_xdk_hook_bridge.cpp").read_text(encoding="utf-8")
        scene = (ROOT / "native_port/src/eor_native_draw_geometry.cpp").read_text(encoding="utf-8")
        self.assertIn("g_frames_for_native.TakeIfSequence(sequence)", hook)
        self.assertLess(hook.index("gpu.CanReplayOriginalXdkTimeline(*preview)"),
                        hook.rindex("g_frames_for_native.TakeIfSequence(sequence)"))
        # A dropped Present must NOT permanently disable native Vulkan
        # replay: consume the stale epoch before expensive PSO/frame preflight.
        self.assertIn("if(missed) {", hook)
        self.assertLess(hook.index("if(missed) {"),
                        hook.index("gpu.CanReplayOriginalXdkTimeline(*preview)"))
        self.assertLess(hook.index("preview.reset();", hook.index("if(missed) {")),
                        hook.index("g_frames_for_native.DiscardIfSequence(sequence)"))
        self.assertLess(hook.index("g_frames_for_native.DiscardIfSequence(sequence)"),
                        hook.index("if(!ValidateOriginalCommandTimeline(*preview))"))
        self.assertIn("native::ReplayDeliveredResourceScene(*delivered,witnesses", hook)
        self.assertIn("if(delivered.overwritten_since_take)", scene)
        self.assertIn("ReplayCapturedResourceScene(delivered.frame,verified,frame,residents,", scene)
        self.assertLess(scene.index("if(delivered.overwritten_since_take)"),
                        scene.index("ReplayCapturedResourceScene(delivered.frame"))
        # The original guest frame must thread the GPU BeginScene attempt.
        # The prior assertion expected an obsolete ReplayAotScene signature.
        self.assertRegex(scene, r"aot::ReplayAotScene\(frame,commands,bank,effects,reflection,backend,\s*&captured,scene_begin_attempted\);")
        cmake = TARGET.read_text(encoding="utf-8")
        self.assertIn("src/eor_xdk_hook_bridge.cpp", cmake)
        self.assertIn("src/eor_native_draw_geometry.cpp", cmake)
        self.assertIn("src/eor_diligent_aot_backend.cpp", cmake)


if __name__ == "__main__":
    unittest.main()
