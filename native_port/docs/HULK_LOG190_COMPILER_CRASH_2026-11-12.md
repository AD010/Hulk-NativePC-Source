# Log 190: repeatable native driver compiler crash

Status: diagnosis incomplete; no crash fix or image-parity claim.
Source baseline: 6374736 (current main inspected on 2026-11-12).

## Evidence

The supplied log 190 has 3,736 lines. Its associated minidump confirms
exception 0xC0000005 at 0x00007FFC1ACBFA65 on thread 18292, reading 0x10.
The address belongs to nvgpucomp64.dll (module base 0x00007FFC1AB00000).
The earlier crash-report(2).txt associated with log 189 has the same fault
address, read address and executable stack offsets, on a different thread.
This supports a repeated failure path, not a diagnosis of its cause.

The executable's minidump module record contains no CodeView symbol record.
No matching PDB/symbol asset was found in the private release asset inventory.
Do not assign function names to the executable offsets without the matching
binary/debug symbols. The driver name alone does not distinguish malformed
shader input, API misuse, a lifetime bug, or a driver defect.

Log 190 enables D132 graphics pipeline libraries. At frame 1200 it reports
131 linked pipelines and 117 whole-pipeline replacements. NativeX's upstream
Diligent patch 0011 schedules whole-pipeline optimization on a separate worker.
The crashing thread has **zero log entries** in log 190. The existing wrapper
BEGIN/END entries occur on the submission thread and therefore cannot identify
which real driver call crashed. Entries even continue after the exception time
while the crash handler writes the dump. The final submitted pipeline is NOT
proven responsible.

The SPIR-V structural check reports no failures. This checks instruction lengths,
not full SPIR-V semantics or Vulkan pipeline validity. Log 190's only error-level
entry is SDL's disabled joystick subsystem; this is not the observed crash stack.

## Source correction

The existing compiler guard now calls the asynchronous wrapper outcome
RETURN/object_created, not END/ok. It installs an additional patch through the
existing NativeX Diligent patch-series loader. This logs BEGIN and END immediately
around the actual vkCreateGraphicsPipelines call, on its executing worker, with
pipeline name, flags, stage count and the returned VkResult. It covers library
parts, linking and deferred whole-pipeline creation. The existing native logger
adds the thread ID used to correlate the exception report.

No draw, shader, effect or library path is disabled. No exceptions are swallowed,
no fake successful pipeline is returned, and the original Vulkan error handling
remains in place. These changes fix misleading diagnostics; they do not establish
that the graphics or driver crash has been repaired. Driver-internal asynchronous
failures and buffered logs can still limit attribution.

## Verification and remaining gate

Five local regression tests passed, including applying/reversing the generated
patch and preserving the real driver call/error path. The generated patch also
passed git apply --check against the actual pinned Diligent source
074060cfd1db73064d7a18cf40df86af6531520c.

Windows compilation/Actions and GPU execution were not performed. The user's
Actions restriction remains in effect. A shader-specific correction still needs
the actual failing pipeline and valid input/lifetime evidence; guessing that the
last shader in the file caused the failure would repeat the previous error.
