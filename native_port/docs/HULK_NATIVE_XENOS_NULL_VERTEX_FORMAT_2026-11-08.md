# NativeX shader C02B93AAA048CD3B — real source investigation

User log099:
- At 14:25:36.846 a 27-dword immediate vertex program failed analysis:
  ShaderCache::Get caught a std::exception with `string pointer is null`.
- The same game kept running, observer frame 655 was recorded at 14:25:40,
  and the user closed the process at 14:25:42. This is NOT a crash.
- This program's address 0xA290B8C8 is command-buffer guest virtual
  memory containing inline shader words, NOT a string-table file offset.
- ReXGlue src/graphics/pipeline/shader/translator_disasm.cpp passes
  kVertexFetchDataFormats[format].name to StringBuffer::AppendFormat;
  table contains explicit null slots. fmt throws "string pointer is null"
  for null const char*. Patch logs numeric unlisted format instead, so
  disassembly no longer prevents shader analysis on this particular path.
- This is the identified plausible throw site. Without an exception
  stack or step-through, it is not proven as the *only* possible source.
- Does NOT establish that the vertex fetch format itself is supported by
  the runtime shader translator or that GPU output becomes nonblack.
- Independent NativeX observer at frame 655: 362 / 1397 exact state
  matches (25.9%), 1035 wrong pixel programs and SQ_INTERPOLATOR_CNTL.
  Disabling observer only hides evidence; not a renderer fix.
- The 0xA290B8C8 guest address has NO PAL/US string-table inference.
- Runtime's other persistent warnings: index buffer length 0 with
  738 requested indexed vertices, another separate GPU input issue.
