// Copyright 2016 The Shaderc Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// The program demonstrates basic shader compilation using the Shaderc C++ API.
// For clarity, each method is deliberately self-contained.
//
// Techniques demonstrated:
//  - Preprocessing GLSL source text
//  - Compiling a shader to SPIR-V assembly text
//  - Compliing a shader to a SPIR-V binary module
//  - Performing optimization with compilation
//  - Setting basic options: setting a preprocessor symbol.
//  - Checking compilation status and extracting an error message.

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <shaderc/shaderc.hpp>

#include <learnogl/shader.h>

const fs::path path_to_shader = fs::path(SOURCE_DIR) / "data" / "fluid_render_vsfs.glsl";

// Returns GLSL shader source text after preprocessing.
std::string
preprocess_shader(const std::string &source_name, shaderc_shader_kind kind, const std::string &source) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;

    // Like -DMY_DEFINE=1
    options.AddMacroDefinition("MY_DEFINE", "1");
    options.AddMacroDefinition("DO_VS");
    options.AddMacroDefinition("MUH_MAIN", "CameraUB");

    shaderc::PreprocessedSourceCompilationResult result =
        compiler.PreprocessGlsl(source, kind, source_name.c_str(), options);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        std::cerr << result.GetErrorMessage();
        return "";
    }

    return { result.cbegin(), result.cend() };
}

// Compiles a shader to SPIR-V assembly. Returns the assembly text
// as a string.
std::string compile_file_to_assembly(const std::string &source_name,
                                     shaderc_shader_kind kind,
                                     const std::string &source,
                                     bool optimize = false) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;

    // Like -DMY_DEFINE=1
    options.AddMacroDefinition("MY_DEFINE", "1");
    if (optimize)
        options.SetOptimizationLevel(shaderc_optimization_level_size);

    shaderc::AssemblyCompilationResult result =
        compiler.CompileGlslToSpvAssembly(source, kind, source_name.c_str(), options);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        std::cerr << result.GetErrorMessage();
        return "";
    }

    return { result.cbegin(), result.cend() };
}

// Compiles a shader to a SPIR-V binary. Returns the binary as
// a vector of 32-bit words.
std::vector<uint32_t> compile_file(const std::string &source_name,
                                   shaderc_shader_kind kind,
                                   const std::string &source,
                                   bool optimize = false) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;

    // Like -DMY_DEFINE=1
    options.AddMacroDefinition("MY_DEFINE", "1");
    if (optimize)
        options.SetOptimizationLevel(shaderc_optimization_level_size);

    shaderc::SpvCompilationResult module =
        compiler.CompileGlslToSpv(source, kind, source_name.c_str(), options);

    if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
        std::cerr << module.GetErrorMessage();
        return std::vector<uint32_t>();
    }

    return { module.cbegin(), module.cend() };
}

int main() {
    fo::memory_globals::init();
    DEFERSTAT(fo::memory_globals::shutdown());
    const char kShaderSource[] = R"(#version 430 core

    #if defined(MY_DEFINE)

    const int y = MY_DEFINE;

    #else
    const int z = 3;

    #endif

    void main() { int x = MY_DEFINE; }

    )";

    { // Preprocessing
#if 0
        auto preprocessed = preprocess_shader("shader_src", shaderc_glsl_vertex_shader, kShaderSource);
        std::cout << "Compiled a vertex shader resulting in preprocessed text:" << std::endl
                  << preprocessed << std::endl;
#endif
        fo::Array<char> contents;
        read_file(path_to_shader, contents, true);
        auto preprocessed = preprocess_shader("shader_src", shaderc_glsl_vertex_shader, fo::data(contents));
        std::cout << "Compiled a vertex shader resulting in preprocessed text:" << std::endl
                  << preprocessed << std::endl;
    }

    { // Compiling
        auto assembly = compile_file_to_assembly("shader_src", shaderc_glsl_vertex_shader, kShaderSource);
        std::cout << "SPIR-V assembly:" << std::endl << assembly << std::endl;

        auto spirv = compile_file("shader_src", shaderc_glsl_vertex_shader, kShaderSource);
        std::cout << "Compiled to a binary module with " << spirv.size() << " words." << std::endl;
    }

    { // Compiling with optimizing
        auto assembly = compile_file_to_assembly(
            "shader_src", shaderc_glsl_vertex_shader, kShaderSource, /* optimize = */ true);
        std::cout << "Optimized SPIR-V assembly:" << std::endl << assembly << std::endl;

        auto spirv =
            compile_file("shader_src", shaderc_glsl_vertex_shader, kShaderSource, /* optimize = */ true);
        std::cout << "Compiled to an optimized binary module with " << spirv.size() << " words." << std::endl;
    }

    { // Error case
        const char kBadShaderSource[] = "#version 310 es\nint main() { int main_should_be_void; }\n";

        std::cout << std::endl << "Compiling a bad shader:" << std::endl;
        compile_file("bad_src", shaderc_glsl_vertex_shader, kBadShaderSource);
    }

    { // Compile using the C API.
        std::cout << "\n\nCompiling with the C API" << std::endl;

        // The first example has a compilation problem.  The second does not.
        const char source[2][80] = { "void main() {}", "#version 450\nvoid main() {}" };

        shaderc_compiler_t compiler = shaderc_compiler_initialize();
        for (int i = 0; i < 2; ++i) {
            std::cout << "  Source is:\n---\n" << source[i] << "\n---\n";
            shaderc_compilation_result_t result = shaderc_compile_into_spv(compiler,
                                                                           source[i],
                                                                           std::strlen(source[i]),
                                                                           shaderc_glsl_vertex_shader,
                                                                           "main.vert",
                                                                           "main",
                                                                           nullptr);
            auto status = shaderc_result_get_compilation_status(result);
            std::cout << "  Result code " << int(status) << std::endl;
            if (status != shaderc_compilation_status_success) {
                std::cout << "error: " << shaderc_result_get_error_message(result) << std::endl;
            }
            shaderc_result_release(result);
        }
        shaderc_compiler_release(compiler);
    }

    return 0;
}