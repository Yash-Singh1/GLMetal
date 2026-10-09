// Compile shadow-border variants without executing GL or GPU work.
#define main glm_clip_probe_main
#include "shader_clip_cpu.cpp"
#undef main

int main(int argc, char **argv)
{
    try {
        require(argc == 2, "Usage: shadow_border_compiler ARTIFACT_DIRECTORY");
        initialize();
        struct Fixture { const char *name, *type, *sample; bool legacy; };
        const Fixture fixtures[] = {
            {"lod", "sampler2DShadow", "textureLod(atlas,vec3(uv,.25),0.)", false},
            {"projective", "sampler2DShadow", "textureProj(atlas,vec4(uv*2.,.5,2.))", false},
            {"offset", "sampler2DShadow", "textureLodOffset(atlas,vec3(uv,.5),0.,ivec2(1,-1))", false},
            {"gradient", "sampler2DShadow", "textureGrad(atlas,vec3(uv,.75),dFdx(uv),dFdy(uv))", false},
            {"array_1d", "sampler1DArrayShadow", "texture(atlas,vec3(uv.x,2.,.5))", false},
            {"array_2d", "sampler2DArrayShadow", "textureGrad(atlas,vec4(uv,2.,.5),dFdx(uv),dFdy(uv))", false},
            {"legacy_lod", "sampler2DShadow", "shadow2DLod(atlas,vec3(uv,.25),0.).r", true},
            {"legacy_projective", "sampler2DShadow", "shadow2DProj(atlas,vec4(uv*2.,.5,2.)).r", true},
        };
        Artifacts artifacts(argv[1]);
        for (const auto &fixture : fixtures) {
            std::string vs = fixture.legacy ?
                "#version 120\nvarying vec2 uv;void main(){gl_Position=gl_Vertex;uv=gl_MultiTexCoord0.xy;}" :
                "#version 410 core\nout vec2 uv;void main(){gl_Position=vec4(float(gl_VertexID),0,0,1);uv=gl_Position.xy;}";
            std::string fs = fixture.legacy ?
                "#version 120\n#extension GL_ARB_shader_texture_lod : require\nvarying vec2 uv;" :
                "#version 410 core\nin vec2 uv;out vec4 color;";
            fs += std::string("uniform ") + fixture.type + " atlas;void main(){" +
                  (fixture.legacy ? "gl_FragColor" : "color") + "=vec4(" + fixture.sample + ");}";
            glm_compile_request request = {};
            request.sources[GLM_STAGE_VERTEX] = vs.c_str();
            request.sources[GLM_STAGE_FRAGMENT] = fs.c_str();
            request.border_samplers[0] = "atlas";
            Result result;
            compile_uncached(&request, &result.value);
            require(result.value.ok, std::string(fixture.name) + ": " + (result.value.log ? result.value.log : "Compilation failed"));
            require(result.value.sampler_count == 3, "Shadow variant must reserve one comparison and two depth samplers");
            for (int i = 0; i < result.value.uniform_count; ++i) {
                const char *name = result.value.uniforms[i].name;
                require(!name || (std::strncmp(name, "glm_bb_", 7) && std::strncmp(name, "glm_bw_", 7)),
                        "Hidden depth samplers leaked into application reflection");
            }
            check_cache(result.value);
            artifacts.write(std::string(fixture.name) + "-vertex", result.value.msl[GLM_STAGE_VERTEX]);
            artifacts.write(std::string(fixture.name) + "-fragment", result.value.msl[GLM_STAGE_FRAGMENT]);
        }
        const char *unsupported[] = {
            "float lookup(sampler2DShadow s,vec3 p){return texture(s,p);}void main(){color=vec4(lookup(atlas,vec3(.2,.5,.25)));}",
            "void main(){color=textureGather(atlas,vec2(.2,.5),.25);}"
        };
        for (unsigned i = 0; i < 2; ++i) {
            std::string fs = "#version 410 core\nuniform sampler2DShadow atlas;out vec4 color;";
            fs += unsupported[i];
            glm_compile_request request = {};
            request.sources[GLM_STAGE_VERTEX] = "#version 410 core\nvoid main(){gl_Position=vec4(float(gl_VertexID),0,0,1);}";
            request.sources[GLM_STAGE_FRAGMENT] = fs.c_str();
            request.border_samplers[0] = "atlas";
            Result variant;
            compile_uncached(&request, &variant.value);
            require(!variant.value.ok && variant.value.log &&
                    std::strstr(variant.value.log, "direct supported sampling calls"),
                    "Unsupported shadow sampler must reject border emulation");
            request.border_samplers[0] = nullptr;
            Result fallback;
            compile_uncached(&request, &fallback.value);
            require(fallback.value.ok, fallback.value.log ? fallback.value.log : "Ordinary fallback failed");
            require(fallback.value.sampler_count == 1, "Fallback must retain its original sampler allocation");
            artifacts.write("fallback-" + std::to_string(i) + "-vertex", fallback.value.msl[GLM_STAGE_VERTEX]);
            artifacts.write("fallback-" + std::to_string(i) + "-fragment", fallback.value.msl[GLM_STAGE_FRAGMENT]);
        }
        std::cout << "Eight shadow border compiler/cache fixtures and two unsupported fallbacks passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
