#include "shader_depth.h"
#include <cstdlib>
#include <cstring>
#include <regex>
#include <string>

namespace {
std::string structure(const std::string &source, const std::string &name, size_t *closing = nullptr)
{
    std::smatch match;
    std::regex declaration("\\bstruct\\s+" + name + "\\s*\\{");
    if (!std::regex_search(source, match, declaration)) return {};
    size_t start = static_cast<size_t>(match.position() + match.length());
    size_t end = source.find('}', start);
    if (end == std::string::npos) return {};
    if (closing) *closing = end;
    return source.substr(start, end - start);
}

std::string builtin(const std::string &text, const char *attribute)
{
    std::smatch match;
    std::regex declaration(std::string("\\b\\w+\\s+(\\w+)\\s*\\[\\[") + attribute + "\\]\\]");
    return std::regex_search(text, match, declaration) ? match[1].str() : std::string();
}
}

extern "C" char *glm_depth_clamp_msl(const char *source, const char *entry)
{
    if (!source || !entry) return nullptr;
    std::string text(source);
    std::smatch match;
    std::regex declaration(std::string("\\bfragment\\s+(\\w+)\\s+") + entry + "\\s*\\(");
    if (!std::regex_search(text, match, declaration)) return nullptr;
    size_t signature_start = static_cast<size_t>(match.position());
    size_t argument_start = static_cast<size_t>(match.position() + match.length());
    std::string output = match[1];
    size_t signature_end = argument_start;
    int nesting = 1;
    for (; signature_end < text.size(); ++signature_end) {
        if (text[signature_end] == '(') ++nesting;
        if (text[signature_end] == ')' && !--nesting) break;
    }
    if (signature_end == text.size()) return nullptr;
    size_t body_start = text.find('{', signature_end);
    if (body_start == std::string::npos) return nullptr;
    size_t body_end = body_start + 1;
    nesting = 1;
    for (; body_end < text.size(); ++body_end) {
        if (text[body_end] == '{') ++nesting;
        if (text[body_end] == '}' && !--nesting) break;
    }
    if (body_end == text.size()) return nullptr;
    size_t output_end = 0;
    std::string output_members = output == "void" ? "" : structure(text, output, &output_end);
    if (output != "void" && output_members.empty()) return nullptr;
    if (output_members.find("[[depth(") != std::string::npos) return nullptr;
    std::string signature = text.substr(argument_start, signature_end - argument_start);
    std::string position = builtin(signature, "position");
    std::string viewport = builtin(signature, "viewport_array_index");
    std::smatch stage_in;
    if (std::regex_search(signature, stage_in, std::regex(R"(\b(\w+)\s+(\w+)\s*\[\[stage_in\]\])"))) {
        std::string members = structure(text, stage_in[1]);
        if (position.empty()) {
            std::string member = builtin(members, "position");
            if (!member.empty()) position = stage_in[2].str() + "." + member;
        }
        if (viewport.empty()) {
            std::string member = builtin(members, "viewport_array_index");
            if (!member.empty()) viewport = stage_in[2].str() + "." + member;
        }
    }
    std::string parameters;
    if (position.empty()) {
        parameters += ", float4 glm_depth_position [[position]]";
        position = "glm_depth_position";
    }
    if (viewport.empty()) {
        parameters += ", uint glm_depth_viewport [[viewport_array_index]]";
        viewport = "glm_depth_viewport";
    }
    parameters += ", constant float2* glm_depth_bounds [[buffer(24)]]";
    if (signature.find_first_not_of(" \t\r\n") == std::string::npos) parameters.erase(0, 2);
    std::string write = "out.glm_clamped_depth = clamp(" + position + ".z, glm_depth_bounds[" + viewport +
                        "].x, glm_depth_bounds[" + viewport + "].y); ";
    std::string body = text.substr(body_start + 1, body_end - body_start - 1);
    if (output == "void") {
        body = "\nGLMDepthOnly out = {};\n" + std::regex_replace(body, std::regex(R"(\breturn\s*;)"),
                                                              "{ " + write + "return out; }");
        body += "\n" + write + "return out;\n";
    } else {
        // SPIRV-Cross and the fixed-function generator return their local `out`.
        if (body.find("return out;") == std::string::npos) return nullptr;
        body = std::regex_replace(body, std::regex(R"(\breturn\s+out\s*;)"), "{ " + write + "return out; }");
    }
    std::string header = text.substr(signature_start, argument_start - signature_start);
    if (output == "void") header.replace(header.find("void"), 4, "GLMDepthOnly");
    size_t entry_offset = header.rfind(entry);
    header.replace(entry_offset, std::strlen(entry), "main0");
    std::string result = text.substr(0, signature_start) + header + signature + parameters + ")" +
                         text.substr(signature_end + 1, body_start - signature_end) + body + "}" + text.substr(body_end + 1);
    if (output == "void")
        result.insert(signature_start, "struct GLMDepthOnly { float glm_clamped_depth [[depth(any)]]; };\n");
    else result.insert(output_end, "\n    float glm_clamped_depth [[depth(any)]];\n");
    result = std::regex_replace(result, std::regex(R"(\[\[early_fragment_tests\]\])"), "");
    char *copy = static_cast<char *>(std::malloc(result.size() + 1));
    if (copy) std::memcpy(copy, result.c_str(), result.size() + 1);
    return copy;
}

/* Polygon stipple (GL 2.1 3.5.2): fragments whose window position's bit of
   the 32x32 pattern (buffer 26: a row per window y mod 32, bit 31 for
   x mod 32 = 0) is clear are discarded before the shader runs its body. */
extern "C" char *glm_polygon_stipple_msl(const char *source, const char *entry)
{
    if (!source || !entry) return nullptr;
    std::string text(source);
    std::smatch match;
    std::regex declaration(std::string("\\bfragment\\s+(\\w+)\\s+") + entry + "\\s*\\(");
    if (!std::regex_search(text, match, declaration)) return nullptr;
    size_t signature_start = static_cast<size_t>(match.position());
    size_t argument_start = static_cast<size_t>(match.position() + match.length());
    size_t signature_end = argument_start;
    int nesting = 1;
    for (; signature_end < text.size(); ++signature_end) {
        if (text[signature_end] == '(') ++nesting;
        if (text[signature_end] == ')' && !--nesting) break;
    }
    if (signature_end == text.size()) return nullptr;
    size_t body_start = text.find('{', signature_end);
    if (body_start == std::string::npos) return nullptr;
    std::string signature = text.substr(argument_start, signature_end - argument_start);
    std::string position = builtin(signature, "position");
    std::smatch stage_in;
    if (position.empty() && std::regex_search(signature, stage_in, std::regex(R"(\b(\w+)\s+(\w+)\s*\[\[stage_in\]\])"))) {
        std::string member = builtin(structure(text, stage_in[1]), "position");
        if (!member.empty()) position = stage_in[2].str() + "." + member;
    }
    std::string parameters;
    if (position.empty()) {
        parameters += ", float4 glm_stipple_position [[position]]";
        position = "glm_stipple_position";
    }
    parameters += ", constant uint* glm_stipple [[buffer(26)]]";
    if (signature.find_first_not_of(" \t\r\n") == std::string::npos) parameters.erase(0, 2);
    std::string test = "\n    { uint2 glm_p = uint2(" + position + ".xy) & 31u; "
                       "if (((glm_stipple[glm_p.y] >> (31u - glm_p.x)) & 1u) == 0u) discard_fragment(); }";
    std::string header = text.substr(signature_start, argument_start - signature_start);
    header.replace(header.rfind(entry), std::strlen(entry), "main0");
    std::string result = text.substr(0, signature_start) + header + signature + parameters + ")" +
                         text.substr(signature_end + 1, body_start + 1 - (signature_end + 1)) + test +
                         text.substr(body_start + 1);
    /* The test discards, so depth must not be written early. */
    result = std::regex_replace(result, std::regex(R"(\[\[early_fragment_tests\]\])"), "");
    char *copy = static_cast<char *>(std::malloc(result.size() + 1));
    if (copy) std::memcpy(copy, result.c_str(), result.size() + 1);
    return copy;
}

namespace {
/* The function `entry` of `kind` ("vertex"/"fragment"): offsets of its
   signature, argument list end and body start; false if not found. */
bool function_shape(const std::string &text, const char *kind, const char *entry, size_t *signature_start,
                    size_t *argument_start, size_t *signature_end, size_t *body_start, std::string *output)
{
    std::smatch match;
    std::regex declaration(std::string("\\b") + kind + "\\s+(\\w+)\\s+" + entry + "\\s*\\(");
    if (!std::regex_search(text, match, declaration)) return false;
    *signature_start = static_cast<size_t>(match.position());
    *argument_start = static_cast<size_t>(match.position() + match.length());
    if (output) *output = match[1];
    size_t end = *argument_start;
    int nesting = 1;
    for (; end < text.size(); ++end) {
        if (text[end] == '(') ++nesting;
        if (text[end] == ')' && !--nesting) break;
    }
    if (end == text.size()) return false;
    *signature_end = end;
    *body_start = text.find('{', end);
    return *body_start != std::string::npos;
}

char *copy_out(const std::string &result)
{
    char *copy = static_cast<char *>(std::malloc(result.size() + 1));
    if (copy) std::memcpy(copy, result.c_str(), result.size() + 1);
    return copy;
}
}

/* A vertex function that also stores, at its vertex ID in buffer 27, each
   vertex's clip position, front colour ([[user(locn0)]]) and first texture
   coordinate ([[user(locn5)]]): three float4s, zero for outputs it lacks.
   Line stipple, selection and feedback read them back. */
extern "C" char *glm_position_capture_msl(const char *source, const char *entry)
{
    if (!source || !entry) return nullptr;
    std::string text(source), output;
    size_t signature_start, argument_start, signature_end, body_start;
    if (!function_shape(text, "vertex", entry, &signature_start, &argument_start, &signature_end, &body_start, &output))
        return nullptr;
    std::string members = structure(text, output);
    std::string position = builtin(members, "position");
    if (position.empty()) return nullptr;
    auto float4_member = [&](const char *attribute) {
        std::smatch match;
        std::regex declaration(std::string("\\bfloat4\\s+(\\w+)\\s*\\[\\[") + attribute + "\\]\\]");
        return std::regex_search(members, match, declaration) ? "out." + match[1].str() : std::string("float4(0.0)");
    };
    std::string color = float4_member("user\\(locn0\\)"), texcoord = float4_member("user\\(locn5\\)");
    std::string signature = text.substr(argument_start, signature_end - argument_start);
    std::string vid = builtin(signature, "vertex_id");
    std::string parameters;
    if (vid.empty()) {
        parameters += ", uint glm_capture_vid [[vertex_id]]";
        vid = "glm_capture_vid";
    }
    parameters += ", device float4* glm_capture_outputs [[buffer(27)]]";
    if (signature.find_first_not_of(" \t\r\n") == std::string::npos) parameters.erase(0, 2);
    /* The function's own body (sources may define more functions after it). */
    size_t body_end = body_start + 1;
    for (int nesting = 1; body_end < text.size() && nesting; ++body_end) {
        if (text[body_end] == '{') ++nesting;
        if (text[body_end] == '}') --nesting;
    }
    std::string body = text.substr(body_start, body_end - body_start);
    if (body.find("return out;") == std::string::npos) return nullptr;
    /* Without rasterization Metal needs a void vertex function: the local
       output struct stays, only the captured outputs are stored. */
    std::string store = "{ glm_capture_outputs[" + vid + " * 3u] = out." + position + "; glm_capture_outputs[" + vid +
                        " * 3u + 1u] = " + color + "; glm_capture_outputs[" + vid + " * 3u + 2u] = " + texcoord + "; return; }";
    body = std::regex_replace(body, std::regex(R"(\breturn\s+out\s*;)"), store);
    std::string header = text.substr(signature_start, argument_start - signature_start);
    header.replace(header.rfind(entry), std::strlen(entry), "main0");
    header.replace(header.find(output), output.size(), "void");
    return copy_out(text.substr(0, signature_start) + header + signature + parameters + ")" +
                    text.substr(signature_end + 1, body_start - (signature_end + 1)) + body + text.substr(body_end));
}

/* Line stipple (GL 2.1 3.4.2): buffer 26 holds (factor, pattern) and then,
   per line primitive, its start along the major axis (x), the stipple
   counter there (z) and the axis (w, 1 for y). A fragment's counter adds
   its distance from the start in whole pixels; the pattern bit (counter /
   factor) mod 16 decides. */
extern "C" char *glm_line_stipple_msl(const char *source, const char *entry)
{
    if (!source || !entry) return nullptr;
    std::string text(source);
    size_t signature_start, argument_start, signature_end, body_start;
    if (!function_shape(text, "fragment", entry, &signature_start, &argument_start, &signature_end, &body_start, nullptr))
        return nullptr;
    std::string signature = text.substr(argument_start, signature_end - argument_start);
    std::string position = builtin(signature, "position");
    std::smatch stage_in;
    if (position.empty() && std::regex_search(signature, stage_in, std::regex(R"(\b(\w+)\s+(\w+)\s*\[\[stage_in\]\])"))) {
        std::string member = builtin(structure(text, stage_in[1]), "position");
        if (!member.empty()) position = stage_in[2].str() + "." + member;
    }
    std::string primitive = builtin(signature, "primitive_id");
    std::string parameters;
    if (position.empty()) {
        parameters += ", float4 glm_stipple_position [[position]]";
        position = "glm_stipple_position";
    }
    if (primitive.empty()) {
        parameters += ", uint glm_stipple_primitive [[primitive_id]]";
        primitive = "glm_stipple_primitive";
    }
    parameters += ", constant float4* glm_stipple_segments [[buffer(26)]]";
    if (signature.find_first_not_of(" \t\r\n") == std::string::npos) parameters.erase(0, 2);
    std::string test = "\n    { float4 glm_h = glm_stipple_segments[0]; float4 glm_s = glm_stipple_segments[1u + " + primitive +
                       "]; float glm_d = abs((glm_s.w > 0.5 ? " + position + ".y : " + position + ".x) - glm_s.x); "
                       "uint glm_c = uint(glm_s.z + floor(glm_d + 0.5)); "
                       "if (((uint(glm_h.y) >> ((glm_c / uint(glm_h.x)) & 15u)) & 1u) == 0u) discard_fragment(); }";
    std::string header = text.substr(signature_start, argument_start - signature_start);
    header.replace(header.rfind(entry), std::strlen(entry), "main0");
    std::string result = text.substr(0, signature_start) + header + signature + parameters + ")" +
                         text.substr(signature_end + 1, body_start + 1 - (signature_end + 1)) + test +
                         text.substr(body_start + 1);
    result = std::regex_replace(result, std::regex(R"(\[\[early_fragment_tests\]\])"), "");
    return copy_out(result);
}

extern "C" char *glm_clip_mask_msl(const char *source, const char *entry, unsigned enabled)
{
    if (!source || !entry) return nullptr;
    std::string text(source);
    std::smatch match;
    std::regex declaration(std::string("\\bvertex\\s+(\\w+)\\s+") + entry + "\\s*\\(");
    if (!std::regex_search(text, match, declaration)) return nullptr;
    size_t signature_start = static_cast<size_t>(match.position());
    size_t argument_start = static_cast<size_t>(match.position() + match.length());
    std::string output = match[1];
    std::string members = structure(text, output);
    // Recognize the clip attribute and literal extent without relying on the
    // generated field name. The generators put the attribute before the extent.
    std::regex clip(R"(\bfloat\s+(\w+)\s*(?:\[\s*(\d+)\s*\]\s*\[\[clip_distance\]\]|\[\[clip_distance\]\]\s*\[\s*(\d+)\s*\]))");
    if (!std::regex_search(members, match, clip)) return nullptr;
    std::string field = match[1];
    unsigned count = static_cast<unsigned>(std::stoul(match[2].matched ? match[2].str() : match[3].str()));
    if (count > 8) return nullptr;
    std::string writes;
    for (unsigned i = 0; i < count; ++i)
        if (!(enabled & (1u << i))) writes += "out." + field + "[" + std::to_string(i) + "] = 1.0; ";
    if (writes.empty()) return nullptr;
    size_t signature_end = argument_start;
    int nesting = 1;
    for (; signature_end < text.size(); ++signature_end) {
        if (text[signature_end] == '(') ++nesting;
        if (text[signature_end] == ')' && !--nesting) break;
    }
    if (signature_end == text.size()) return nullptr;
    size_t body_start = text.find('{', signature_end);
    if (body_start == std::string::npos) return nullptr;
    size_t body_end = body_start + 1;
    nesting = 1;
    for (; body_end < text.size(); ++body_end) {
        if (text[body_end] == '{') ++nesting;
        if (text[body_end] == '}' && !--nesting) break;
    }
    if (body_end == text.size()) return nullptr;
    std::string body = text.substr(body_start + 1, body_end - body_start - 1);
    std::regex returns(R"(\breturn\s+out\s*;)");
    if (!std::regex_search(body, returns)) return nullptr;
    body = std::regex_replace(body, returns, "{ " + writes + "return out; }");
    std::string signature = text.substr(signature_start, body_start - signature_start + 1);
    size_t entry_offset = signature.find(entry, signature.find(output) + output.size());
    signature.replace(entry_offset, std::strlen(entry), "main0");
    std::string result = text.substr(0, signature_start) + signature + body + text.substr(body_end);
    char *copy = static_cast<char *>(std::malloc(result.size() + 1));
    if (copy) std::memcpy(copy, result.c_str(), result.size() + 1);
    return copy;
}

extern "C" char *glm_sample_shading_msl(const char *source, const char *entry)
{
    if (!source || !entry) return nullptr;
    std::string text(source);
    std::smatch match;
    std::regex function(std::string("\\bfragment\\s+\\w+\\s+") + entry + "\\s*\\(");
    if (!std::regex_search(text, match, function)) return nullptr;
    size_t argument_start = static_cast<size_t>(match.position() + match.length());
    size_t argument_end = argument_start;
    int nesting = 1;
    for (; argument_end < text.size(); ++argument_end) {
        if (text[argument_end] == '(') ++nesting;
        if (text[argument_end] == ')' && !--nesting) break;
    }
    if (argument_end == text.size()) return nullptr;
    std::string arguments = text.substr(argument_start, argument_end - argument_start);
    int sample_inputs = 0;
    if (arguments.find("[[user(") != std::string::npos) return nullptr;
    if (arguments.find("[[stage_in]]") != std::string::npos &&
        !std::regex_search(arguments, std::regex(R"(\b(\w+)\s+\w+\s*\[\[stage_in\]\])"))) return nullptr;
    if (std::regex_search(arguments, match, std::regex(R"(\b(\w+)\s+\w+\s*\[\[stage_in\]\])"))) {
        size_t closing;
        std::string members = structure(text, match[1], &closing);
        if (members.empty()) return nullptr;
        std::string rewritten;
        size_t start = 0;
        while (start < members.size()) {
            size_t end = members.find(';', start);
            if (end == std::string::npos) { rewritten += members.substr(start); break; }
            std::string field = members.substr(start, end + 1 - start);
            if (field.find("[[user(") != std::string::npos) {
                const size_t attribute_start = field.find("[[user(");
                const size_t attribute_close = field.find("]]", attribute_start);
                if (attribute_close == std::string::npos) return nullptr;
                const std::string qualifiers = field.substr(attribute_start, attribute_close + 2 - attribute_start);
                if (qualifiers.find("sample_perspective") != std::string::npos ||
                    qualifiers.find("sample_no_perspective") != std::string::npos) ++sample_inputs;
                else if (!std::regex_search(qualifiers, std::regex(R"(\bflat\b)")) && qualifiers.find("centroid_") == std::string::npos &&
                         std::regex_search(field, std::regex(R"(\b(?:float|half)[1-4]?\s+\w+)"))) {
                    size_t attribute_end = field.find("]]", field.find("[[user("));
                    if (attribute_end == std::string::npos) return nullptr;
                    bool linear = qualifiers.find("center_no_perspective") != std::string::npos;
                    field = std::regex_replace(field, std::regex(R"(,\s*center_(?:no_)?perspective)"), "");
                    attribute_end = field.find("]]", field.find("[[user("));
                    field.insert(attribute_end, linear ? ", sample_no_perspective" : ", sample_perspective");
                    ++sample_inputs;
                }
            }
            rewritten += field;
            start = end + 1;
        }
        size_t opening = closing - members.size();
        text.replace(opening, members.size(), rewritten);
        // Struct rewriting precedes the entry signature; rediscover its offsets.
        if (!std::regex_search(text, match, function)) return nullptr;
        argument_start = static_cast<size_t>(match.position() + match.length());
        argument_end = argument_start; nesting = 1;
        for (; argument_end < text.size(); ++argument_end) {
            if (text[argument_end] == '(') ++nesting;
            if (text[argument_end] == ')' && !--nesting) break;
        }
        if (argument_end == text.size()) return nullptr;
    }
    if (!sample_inputs && arguments.find("[[sample_id]]") == std::string::npos) {
        const bool empty = arguments.find_first_not_of(" \t\r\n") == std::string::npos;
        text.insert(argument_end, std::string(empty ? "" : ", ") + "uint glm_forced_sample_id [[sample_id]]");
    }
    if (!std::regex_search(text, match, function)) return nullptr;
    const size_t entry_offset = static_cast<size_t>(match.position()) + match.str().rfind(entry);
    text.replace(entry_offset, std::strlen(entry), "main0");
    char *copy = static_cast<char *>(std::malloc(text.size() + 1));
    if (copy) std::memcpy(copy, text.c_str(), text.size() + 1);
    return copy;
}
