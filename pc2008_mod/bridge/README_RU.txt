HULK PC 2008 / DXVK — Native Resource + HLSL Shader Bridge (Win32 x86)

Состав:
  dinput8.dll — только системный DirectInput8Create + загрузка наших двух ASI
  HulkBridge.asi — перенаправление index.ind + одной проверенной PC ARC
  HulkShader.asi — точечная HLSL/SM3-компиляция взамен оригинального PC VS/PS
  tools/build_hulk_pc_bridge.py — отдельный PC-совместимый index/ARC со всеми каталогами
  tools/map_pc_d3d9_shaders.py — карта оригинальных 196 D3D9 шейдеров по хешу

1. Install.cmd — выбираешь папку оригинальной игры с HULK.EXE.
2. Инсталлятор проверяет SHA256 HULK.EXE, делает резервные копии и
   НЕ МЕНЯЕТ оригинальные .ARC, index.ind, сохранения и DXVK d3d9.dll.
3. В HulkBridge.ini изначально все изменения ВЫКЛЮЧЕНЫ.
4. Подстановку ресурсов включать только после создания HulkBridge/index.ind,
   HulkBridge/data/MODELS.ARC или SHADEREF.ARC из настоящих PC-совместимых
   записей, проверенных через tools/build_hulk_pc_bridge.py.
   Упаковщик сохраняет и основной index.ind, и внутренний каталог ARC.
5. Установка Xbox 360 .ARC или перекодированного Xbox index.ind ЗАПРЕЩЕНА:
   индексы не конвертируют вершинный формат и GPU Xenos шейдеры.
6. При наличии PC HLSL 3.0 для конкретной программы путь формируется по
   оригинальному FNV32 + CRC32 + длине байткода. HulkShader.asi компилирует
   HLSL на ходу через Windows D3DCompiler_47, ошибки возвращают PC shader.
   Имена файлов VS_FNV_CRC_LENGTH.hlsl из original_pc_shader_map_metadata.json.
   Это НЕ автоматическая трансляция Xbox Xenos в PC D3D9.
7. Внешний HulkPC.asi (наша старая версия) НЕ ЗАГРУЖАЕТСЯ новым proxy,
   чтобы не повторить искажения исходного HUD. DXVK остается без изменений.

Верификация: статическая сборка PE32/x86, 4 Python-теста и чтение реальных
PC архивов. В реальной игре эта версия ещё НЕ ИСПЫТАНА. Она не содержит
консольных моделей/текстур и НЕ является готовым ремастером. Конвертер всех
Xenos FX/LOD/теней, render states и недостающих PC-материалов ещё не создан.
