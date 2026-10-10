/* Sampler metadata correctness and CPU benchmark. Captured requests only read
 * existing program caches. This probe never compiles shaders or uses Metal. */
#include "../../src/sampler_plan.h"
#include <cassert>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
extern "C" bool glm_compile_cache_load(const glm_compile_request *, glm_compile_result *);
extern "C" void *glm_remote_decode_request(const unsigned char *, size_t, glm_compile_request *);
extern "C" void glm_remote_free_request(void *);

static uint64_t item_value(int index, bool arb, int unit, int target, const glm_uniform_info &u)
{
    return (uint64_t)(index + 1) * 131 + (uint32_t)u.sampler_slot * 17 + (uint32_t)u.array_size * 29 +
        (arb ? (uint32_t)unit * 43ull + (uint32_t)target * 61ull : 0);
}
__attribute__((noinline)) static uint64_t reference(const glm_compile_result &r)
{
    uint64_t sum = 0;
    for (int i = 0; i < r.uniform_count; ++i) {
        const auto &u = r.uniforms[i];
        if (u.sampler_slot < 0 || u.offset >= 0) continue;
        int unit = 0, target = 0;
        bool arb = sscanf(u.name, "glm_arb_tex%d_%d", &unit, &target) == 2;
        sum += item_value(i, arb, unit, target, u);
    }
    return sum;
}
__attribute__((noinline)) static uint64_t planned(glm_sampler_plan_cache &cache,
    const glm_compile_result &r, uint64_t link)
{
    const auto *plan = glm_sampler_plan_get(&cache, &r, link, r.uniforms, r.uniform_count);
    if (!plan) return reference(r);
    uint64_t sum = 0;
    for (int i = 0; i < plan->count; ++i) {
        const auto &item = plan->items[i];
        sum += item_value(item.uniform_index, item.arb, item.arb_unit, item.arb_target, r.uniforms[item.uniform_index]);
    }
    return sum;
}
static void correctness()
{
    const char *names[] = {"diffuse", "normal", "glm_arb_tex3_2", "glm_arb_tex-1_6",
        "glm_arb_tex 5_2tail", "glm_arb_tex3_bad", "", "glm_arb_tex3_2[0]"};
    std::vector<glm_uniform_info> uniforms;
    for (int i = 0; i < 140; ++i) {
        glm_uniform_info u = {};
        u.name = const_cast<char *>(names[i % 8]);u.offset = i % 3 ? -1 : 0;
        u.sampler_slot = i % 5 ? i % 64 : -1;u.array_size = 1 + i % 4;
        uniforms.push_back(u);
    }
    glm_compile_result r = {};r.uniforms = uniforms.data();r.uniform_count = (int)uniforms.size();
    glm_sampler_plan_cache cache = {};
    assert(planned(cache, r, 1) == reference(r));
    assert(planned(cache, r, 1) == reference(r));
    uniforms[1].name = const_cast<char *>("glm_arb_tex9_4");
    assert(planned(cache, r, 2) == reference(r)); // Same storage after relink.
    std::vector<glm_uniform_info> replacement = uniforms;r.uniforms = replacement.data();
    assert(planned(cache, r, 2) == reference(r)); // Changed reflection pointer.
    for (auto &u : replacement) { u.offset = -1;u.sampler_slot = 0; }
    assert(!glm_sampler_plan_get(&cache, &r, 3, r.uniforms, r.uniform_count));
    assert(planned(cache, r, 3) == reference(r)); // Overflow retains every sampler.
    r.uniforms = nullptr;r.uniform_count = 0;
    assert(planned(cache, r, 4) == 0);
    /* Collisions across more owners than slots must never reuse stale plans. */
    std::vector<glm_compile_result> owners(64);
    for (auto &owner : owners) { owner.uniforms = uniforms.data();owner.uniform_count = (int)uniforms.size(); }
    for (unsigned pass = 0; pass < 2; ++pass)
        for (unsigned i = 0; i < owners.size(); ++i) assert(planned(cache, owners[i], i + 1) == reference(owners[i]));
}
static double now_ms()
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
int main(int argc, char **argv)
{
    correctness();
    if (argc < 2) { puts("sampler-plan CPU correctness PASS");return 0; }
    unsigned draws = argc > 2 ? (unsigned)strtoul(argv[2], nullptr, 10) : 200000;
    std::vector<std::filesystem::path> paths;
    for (const auto &file : std::filesystem::directory_iterator(argv[1]))
        if (file.path().extension() == ".request") paths.push_back(file.path());
    std::sort(paths.begin(), paths.end());
    std::vector<glm_compile_result> results;results.reserve(32);
    for (const auto &path : paths) {
        if (results.size() == 32) break;
        std::ifstream file(path, std::ios::binary);
        std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file), {}};
        if (bytes.empty() || bytes.size() > 64 * 1024 * 1024) continue;
        glm_compile_request request = {};
        void *storage = glm_remote_decode_request(bytes.data(), bytes.size(), &request);
        glm_compile_result result = {};
        bool hit = glm_compile_cache_load(&request, &result);
        glm_remote_free_request(storage);
        if (hit && result.sampler_count) results.push_back(result);
        else glm_compile_result_free(&result);
    }
    if (results.empty()) { fprintf(stderr,"no existing sampler program cache entries; shaders were not compiled\n");return 2; }
    unsigned uniforms = 0, samplers = 0;
    glm_sampler_plan_cache cache = {};
    for (unsigned i = 0; i < results.size(); ++i) {
        assert(planned(cache, results[i], i + 1) == reference(results[i]));
        uniforms += results[i].uniform_count;
        for (int j = 0; j < results[i].uniform_count; ++j)
            samplers += results[i].uniforms[j].sampler_slot >= 0 && results[i].uniforms[j].offset < 0;
    }
    volatile uint64_t consumed = 0;
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        double timings[2] = {};uint64_t sums[2] = {};
        for (unsigned pass = 0; pass < 2; ++pass) {
            unsigned mode = (pass + repeat) % 2;
            double start = now_ms();uint64_t sum = 0;
            for (unsigned draw = 0; draw < draws; ++draw) {
                unsigned index = draw % results.size();
                sum += mode ? planned(cache, results[index], index + 1) : reference(results[index]);
            }
            timings[mode] = now_ms() - start;sums[mode] = sum;consumed = sum;
        }
        assert(sums[0] == sums[1]);
        printf("{\"repeat\":%u,\"programs\":%zu,\"uniforms\":%u,\"sampler_uniforms\":%u,\"draws\":%u,\"baseline_ms\":%.3f,\"plan_ms\":%.3f,\"checksum\":%llu}\n",
            repeat, results.size(), uniforms, samplers, draws, timings[0], timings[1], (unsigned long long)consumed);
    }
    for (auto &result : results) glm_compile_result_free(&result);
    return 0;
}
