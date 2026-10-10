// CPU-only preparation and serialization comparison. No GL driver is loaded.
#ifndef SHADER_PREPARE_COMPILER_SOURCE
#define SHADER_PREPARE_COMPILER_SOURCE "../../src/shader_compiler.cpp"
#endif
#include SHADER_PREPARE_COMPILER_SOURCE
#include "../../src/compile_cache.cpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

extern "C" bool glm_remote_compile(const glm_compile_request *, glm_compile_result *) { return false; }
extern "C" bool glm_remote_check(glm_stage, const char *, bool *, char **) { return false; }

static void require(bool ok, const std::string &message)
{
    if (!ok) throw std::runtime_error(message);
}

struct Pair { std::string vertex, fragment; };

static std::string read_file(const std::string &path)
{
    std::ifstream input(path);
    require(bool(input), "Cannot read " + path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

static void check_boundaries()
{
    for (const char *name : {"cinematic", "material", "format", "mat", "mat1", "mat5", "mat33",
                             "mat3x5", "mat3_suffix", "prefix_mat3", "dmat3", "1mat3"}) {
        std::string source = "// cinematic material format\nstruct Data { float " + std::string(name) +
                             "; };\nvoid main(){gl_Position=vec4(0);}";
        require(flatten_matrix_array_io(source, GLM_STAGE_VERTEX) == source,
                "Identifier or comment changed: " + std::string(name));
    }
    for (const char *source : {"// mat3\nvoid main(){}", "/* mat2x4 */ void main(){}",
                              "struct Data { mat3 member; }; void main(){}",
                              "out mat3 scalar; void main(){}",
                              "out mat3 glm_internal[2]; void main(){}"})
        require(flatten_matrix_array_io(source, GLM_STAGE_VERTEX) == source, "Non-array interface changed");
    for (const char *boundary : {"", " ", "\n", "/* comment */ ", "float material;\n"}) {
        std::string source = std::string(boundary) + "out mat3 varying[2]; void main(){}";
        std::string flattened = flatten_matrix_array_io(source, GLM_STAGE_VERTEX);
        require(flattened.find("out vec3 glm_flat_varying[6]") != std::string::npos,
                "Valid matrix declaration missed after boundary");
    }
}

static void check_image_type_scan()
{
    std::vector<uint32_t> words = {spv::MagicNumber, 0x10000, 0, 2, 0};
    require(!spirv_has_image_type(words), "Empty module reported an image type");
    words.push_back((1u << 16) | spv::OpNop);
    require(!spirv_has_image_type(words), "Non-image instruction reported an image type");
    for (auto dimension : {spv::Dim2D, spv::DimCube, spv::DimRect}) {
        auto image = words;
        image.insert(image.end(), {(9u << 16) | spv::OpTypeImage, 1, 2,
                                  uint32_t(dimension), 0, 0, 0, 1, 0});
        require(spirv_has_image_type(image), "Image declaration missed");
        require(spirv_has_image_type(image, spv::DimCube) == (dimension == spv::DimCube),
                "Cube dimension incorrectly classified");
    }
    for (auto instruction : {0u, (20u << 16) | spv::OpNop, (1u << 16) | spv::OpTypeImage}) {
        auto malformed = words;
        malformed.push_back(instruction);
        require(spirv_has_image_type(malformed, spv::DimCube),
                "Malformed instruction bypassed the original rewrite");
    }
}

static std::vector<Pair> fixtures()
{
    std::vector<Pair> pairs;
    for (int columns = 2; columns <= 4; ++columns)
        for (int rows = 2; rows <= 4; ++rows) {
            std::string type = "mat" + std::to_string(columns);
            if (columns != rows) type += "x" + std::to_string(rows);
            std::string vertex = "#version 150 core\nout " + type +
                " matrixValue[2];void main(){matrixValue[0]=" + type +
                "(1);matrixValue[1]=" + type + "(2);gl_Position=vec4(0,0,0,1);}";
            std::string fragment = "#version 150 core\nin " + type +
                " matrixValue[2];out vec4 color;void main(){color=vec4(matrixValue[0][0][0],"
                "matrixValue[1][0][0],0,1);}";
            std::string flat = flatten_matrix_array_io(vertex, GLM_STAGE_VERTEX);
            require(flat.find("out vec" + std::to_string(rows) + " glm_flat_matrixValue[" +
                              std::to_string(columns * 2) + "]") != std::string::npos,
                    "Incorrect column or row layout for " + type);
            require(flat.find("matrixValue[1][" + std::to_string(columns - 1) + "]") != std::string::npos,
                    "Last array column missing for " + type);
            pairs.push_back({vertex, fragment});
        }
    for (int shape = 2; shape <= 4; ++shape) {
        auto pair = pairs[(shape - 2) * 3 + shape - 2];
        std::string from = "mat" + std::to_string(shape), to = from + "x" + std::to_string(shape);
        for (auto *source : {&pair.vertex, &pair.fragment}) {
            size_t at = 0;
            while ((at = source->find(from, at)) != std::string::npos) {
                source->replace(at, from.size(), to);
                at += to.size();
            }
        }
        pairs.push_back(std::move(pair));
    }
    for (int variant = 0; variant < 8; ++variant) {
        std::string vertex = "#version 150 core\nin vec4 position;out vec4 value;void main(){"
                             "gl_Position=position;value=position;}";
        std::string fragment = "#version 150 core\n// cinematic material format\n"
                               "uniform vec4 cinematicColor;in vec4 value;out vec4 color;void main(){"
                               "vec4 cinematicValue=value+cinematicColor;";
        for (int operation = 0; operation < 24 + variant; ++operation)
            fragment += "cinematicValue=sin(cinematicValue)+vec4(.01);\n";
        fragment += "color=cinematicValue;}";
        pairs.push_back({vertex, fragment});
    }
    const std::string vertex = "#version 150 core\nin vec4 position;void main(){gl_Position=position;}";
    for (const char *sampler : {"sampler2D", "samplerCube", "sampler2DRect", "samplerCubeShadow"}) {
        std::string type = sampler;
        std::string coord = type == "sampler2D" || type == "sampler2DRect" ? "vec2(.2)" : type == "samplerCubeShadow" ? "vec4(.2,.3,.4,.5)" : "vec3(.2,.3,.4)";
        pairs.push_back({vertex, "#version 150 core\nuniform " + type + " image;out vec4 color;void main(){color=vec4(texture(image," + coord + "));}"});
    }
    pairs.push_back({"#version 400 core\nin vec4 position;out vec4 value;void main(){double d=double(position.x);value=vec4(float(sqrt(d*d+1.0)));gl_Position=position;}", "#version 400 core\nin vec4 value;out vec4 color;void main(){color=value;}"});
    for (const char *declaration : {"uniform bool enabled;", "#define FLAG_TYPE bool\n uniform FLAG_TYPE enabled;"})
        pairs.push_back({vertex, "#version 150 core\n" + std::string(declaration) +
            "\nout vec4 color;void main(){color=enabled?vec4(1):vec4(0);}"});
    return pairs;
}

int main(int argc, char **argv)
{
    try {
        require(argc == 3 || argc == 4, "Usage: shader_prepare_cpu OUTPUT_DIRECTORY ITERATIONS [PAIR_MANIFEST]");
        int iterations = std::stoi(argv[2]);
        require(iterations > 0, "Iterations must be positive");
        check_boundaries();
        check_image_type_scan();
        auto pairs = fixtures();
        if (argc == 4) {
            std::ifstream manifest(argv[3]);
            require(bool(manifest), "Cannot read pair manifest");
            std::string line;
            while (std::getline(manifest, line)) {
                auto tab = line.find('\t');
                size_t separator_width = 1;
                if (tab == std::string::npos) {
                    tab = line.find("\\t");
                    separator_width = 2;
                }
                require(tab != std::string::npos, "Expected tab-separated vertex and fragment paths");
                pairs.push_back({read_file(line.substr(0, tab)), read_file(line.substr(tab + separator_width))});
            }
        }
        initialize();
        double prepare_ms = 0, compile_ms = 0;
        size_t bytes = 0;
        for (size_t index = 0; index < pairs.size(); ++index) {
            const auto &pair = pairs[index];
            for (int iteration = 0; iteration < iterations; ++iteration) {
                auto start = std::chrono::steady_clock::now();
                Bindings bindings;
                auto vertex = prepare(pair.vertex.c_str(), GLM_STAGE_VERTEX, &bindings);
                auto fragment = prepare(pair.fragment.c_str(), GLM_STAGE_FRAGMENT, &bindings);
                prepare_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                glm_compile_request request = {};
                request.sources[GLM_STAGE_VERTEX] = pair.vertex.c_str();
                request.sources[GLM_STAGE_FRAGMENT] = pair.fragment.c_str();
                request.feedback_interleaved = true;
                glm_compile_result result = {};
                start = std::chrono::steady_clock::now();
                compile_uncached(&request, &result);
                compile_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                require(result.ok, "Pair " + std::to_string(index) + " failed: " + (result.log ? result.log : ""));
                auto serialized = encode(&result);
                bytes += serialized.size();
                if (!iteration) {
                    std::string path = std::string(argv[1]) + "/pair-" + std::to_string(index);
                    std::ofstream(path + ".prepared", std::ios::binary) << vertex.text << '\0' << fragment.text;
                    std::ofstream output(path + ".result", std::ios::binary);
                    output.write(reinterpret_cast<const char *>(serialized.data()), serialized.size());
                    require(bool(output), "Cannot write serialized result");
                }
                glm_compile_result_free(&result);
            }
        }
        std::cout << "{\"pairs\":" << pairs.size() << ",\"iterations\":" << iterations
                  << ",\"prepare_ms\":" << prepare_ms << ",\"compile_ms\":" << compile_ms
                  << ",\"serialized_bytes\":" << bytes << "}\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
