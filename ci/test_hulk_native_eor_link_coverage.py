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

    def test_host_vulkan_present_avoids_redundant_fullscreen_clear(self):
        host = (ROOT / "native_port" / "src" /
                "eor_diligent_host_output.cpp").read_text(encoding="utf-8")
        self.assertIn("HasFullNativeCompositePixels(", host)
        self.assertIn("desc.Width!=output.width || desc.Height!=output.height", host)
        self.assertIn("Blitter::Source::kColor,", host)
        self.assertIn("device_->Present();", host)
        self.assertRegex(host, r"SetRenderTargets\(0,nullptr,nullptr,\s*Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE\);")
        self.assertLess(host.index("blitter_->Copy("), host.index("SetRenderTargets(0,nullptr,nullptr,"))
        self.assertLess(host.index("SetRenderTargets(0,nullptr,nullptr,"), host.index("device_->Present();"))
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
        helper = backend.split("bool HasExactSourceVertexSlots(", 1)[1]
        helper = helper.split("\n}", 1)[0]
        self.assertNotIn("slots.size()!=layout.size()", draw,
                         "Layout elements are attributes, not distinct GPU streams")
        self.assertIn("HasExactSourceVertexSlots(layout,slots)", draw)
        self.assertIn("std::array<bool,16> used_slots{}", helper)
        self.assertIn("used_slots[item.BufferSlot]=true;", helper)
        self.assertIn("if(item.BufferSlot>=slots.size() || !slots[item.BufferSlot])", helper)
        self.assertIn("if(!used_slots[i])return false;", helper,
                      "No unexplained source GPU buffers may enter a verified Draw")
        self.assertIn("layout.size()>DILIGENT_MAX_LAYOUT_ELEMENTS", helper)


    def test_live_draw_vertex_slot_count_cannot_overrun_host_arrays(self):
        backend = (ROOT / "native_port" / "src" /
                   "eor_diligent_aot_backend.cpp").read_text(encoding="utf-8")
        # The native Draw path uses fixed 16-entry buffer/offset arrays.
        # DILIGENT_MAX_LAYOUT_ELEMENTS is not necessarily 16.
        self.assertIn("std::array<Diligent::IBuffer*,16> buffers{}", backend)
        self.assertIn("std::array<Diligent::Uint64,16> offsets{}", backend)
        self.assertRegex(
            backend,
            r"slots\.size\(\)>DILIGENT_MAX_LAYOUT_ELEMENTS\s*\|\|\s*slots\.size\(\)>16",
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
        self.assertIn("DecodeVerifiedAttribute(meshes[i],layouts[i][j])", upload)
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
        self.assertLess(worker.index("preview.reset();"),
                        worker.index("(void)g_frames_for_native.TakeIfSequence(source_sequence)"))
        self.assertLess(worker.index("g_frames_for_native.TakeIfSequence(source_sequence)"),
                        worker.index("ReplayNextVerifiedPpcFrame("))
        self.assertNotIn("if(lost)return Result::kNotReady", worker)
        self.assertIn("native_x_renderer::SetHulkEorPresentCallback(nullptr);", app)
        self.assertIn("ClearVerifiedWorkerEorFrame();", app)

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
        self.assertLess(hook.index("preview.reset(); // release immutable ownership"),
                        hook.index("(void)g_frames_for_native.TakeIfSequence(sequence)"))
        self.assertLess(hook.index("(void)g_frames_for_native.TakeIfSequence(sequence)"),
                        hook.index("if(!ValidateOriginalCommandTimeline(*preview))"))
        self.assertIn("native::ReplayDeliveredResourceScene(*delivered,witnesses", hook)
        self.assertIn("if(delivered.overwritten_since_take)", scene)
        self.assertIn("ReplayCapturedResourceScene(delivered.frame,verified,frame,residents,", scene)
        self.assertLess(scene.index("if(delivered.overwritten_since_take)"),
                        scene.index("ReplayCapturedResourceScene(delivered.frame"))
        self.assertIn("aot::ReplayAotScene(frame,commands,bank,effects,reflection,backend,&captured)", scene)
        cmake = TARGET.read_text(encoding="utf-8")
        self.assertIn("src/eor_xdk_hook_bridge.cpp", cmake)
        self.assertIn("src/eor_native_draw_geometry.cpp", cmake)
        self.assertIn("src/eor_diligent_aot_backend.cpp", cmake)


if __name__ == "__main__":
    unittest.main()
