set(compressed_file "${OUTPUT_FILE}.compressed")
execute_process(
    COMMAND "${COMPRESSOR}" -u8 "${FONT_FILE}" Roboto
    OUTPUT_FILE "${compressed_file}"
    RESULT_VARIABLE result
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    file(REMOVE "${compressed_file}")
    message(FATAL_ERROR "Failed to compress the embedded UI font: ${error}")
endif()

file(READ "${compressed_file}" compressed_source)
file(REMOVE "${compressed_file}")
file(WRITE "${OUTPUT_FILE}" "#include \"imgui.h\"\n${compressed_source}")
file(APPEND "${OUTPUT_FILE}" [=[
ImFont* AddEmbeddedUiFont(float size) {
    ImFontAtlas* fonts = ImGui::GetIO().Fonts;
    return fonts->AddFontFromMemoryCompressedTTF(
        Roboto_compressed_data,
        Roboto_compressed_size,
        size,
        nullptr,
        fonts->GetGlyphRangesCyrillic());
}
]=])
