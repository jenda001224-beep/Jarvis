export IDF_TOOLS_PATH=/Volumes/Jan/Z-Macu/Library/Caches/esphome/idf
export IDF_PATH=$IDF_TOOLS_PATH/frameworks/5.5.5
export IDF_PYTHON_ENV_PATH=$IDF_TOOLS_PATH/penvs/5.5.5
export ESP_ROM_ELF_DIR=$(ls -d $IDF_TOOLS_PATH/tools/esp-rom-elfs/*/ | head -1)
export IDF_COMPONENT_CACHE_PATH=/Volumes/Jan/Z-Macu/Library/Caches/Espressif/ComponentManager
export TMPDIR=/Volumes/Jan/Jarvis/.tools/tmp
CMAKE_BIN=$(ls -d $IDF_TOOLS_PATH/tools/cmake/*/CMake.app/Contents/bin 2>/dev/null | head -1)
[ -z "$CMAKE_BIN" ] && CMAKE_BIN=$(ls -d $IDF_TOOLS_PATH/tools/cmake/*/bin | head -1)
export PATH=$IDF_PYTHON_ENV_PATH/bin:$(ls -d $IDF_TOOLS_PATH/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin | head -1):$CMAKE_BIN:$(ls -d $IDF_TOOLS_PATH/tools/ninja/*/ | head -1):$IDF_PATH/tools:$PATH
idf() { python "$IDF_PATH/tools/idf.py" "$@"; }
