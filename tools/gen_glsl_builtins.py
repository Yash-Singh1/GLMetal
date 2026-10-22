#!/usr/bin/env python3
"""Writes the shader corpus of tests/probes/glsl_compile.c: a vertex and a
fragment shader calling each built-in function prototype glslang knows
(third_party/glslang/.../Initialize.cpp), in a GLSL 1.20 shader with the
extensions Apple's legacy profile advertises and in a 4.10 core one, plus
the built-in variables of those extensions.

  gen_glsl_builtins.py INITIALIZE_CPP > tests/probes/glsl_builtins.txt
"""
import re
import sys

src = open(sys.argv[1]).read()
prototypes = set()
for m in re.finditer(r'"\s*([a-zA-Z_][a-zA-Z0-9_]*\s+[a-zA-Z_][a-zA-Z0-9_]*\s*\([^"]*?\)\s*;)', src):
    prototypes.add(re.sub(r'\s+', ' ', m.group(1)).strip())
# Vendor and new extensions no Mac GL has (Apple's GLSL stops at 4.10).
skip = re.compile(r'(AMD|NV|EXT|ARB|KHR|QCOM|ANDROID)$|^(rayQuery|hitObject|coopMat|subgroup|traceRay|atomic|image|'
                  r'memoryBarrier|barrier|groupMemory|dotAcc|dotPacked|bitcast|expect|assume|debugPrintf|'
                  r'execute|reportIntersection|ignoreIntersection|terminateRay|controlBarrier|tensor|create|set|slice|'
                  r'fetch|fragment|textureWeighted|textureBlock|textureBoxFilter|spirv|clock|anyInvocation|allInvocation|'
                  r'readInvocation|readFirst|ballot|EmitMesh|SetMesh|reorder|helperInvocation|colorAttachment|depthAttachment|'
                  r'stencilAttachment|float16|int16|uint16|half|int64|uint64|bfloat|floate|floatmx|floatue|intBitsToBF|'
                  r'intBitsToFloate|intBitsToFloatmx|intBitsToFloatue|uintBitsTo|pack16|pack32|pack64|unpack16|unpack32|'
                  r'unpack64|packInt|packUint|unpackInt|unpackUint|packFloat2x16|unpackFloat2x16|packFloat|unpackFloat|'
                  r'saturatedConvert|textureFootprint|sparse|cubeFace|minInvocations|maxInvocations|addInvocations|mbcnt|'
                  r'swizzleInvocations|writeInvocation|min3|max3|mid3|timeAMD|interpolateAtVertex|absoluteDifference|'
                  r'addSaturate|average|subtractSaturate|multiply32x16|countLeadingZeros|countTrailingZeros|'
                  r'EmitStream|EndStream)')
legacy_extensions = ['GL_EXT_gpu_shader4', 'GL_ARB_shader_texture_lod', 'GL_EXT_texture_array', 'GL_ARB_texture_rectangle',
                     'GL_ARB_draw_instanced', 'GL_EXT_draw_instanced']
out = []


def shader(stage, name, profile, decls, body):
    # Shaders writing gl_FragData or user outputs write no gl_FragColor.
    own_outputs = 'gl_FragData' in body or 'varying out' in decls
    if profile == 'core':
        header = '#version 410\n'
        result = 'out vec4 glm_out;\n' if stage == 'fs' else ''
        finish = '  glm_out = vec4(0.0);\n' if stage == 'fs' else '  gl_Position = vec4(0.0);\n'
    else:
        header = '#version 120\n' + ''.join(f'#extension {e} : enable\n' for e in legacy_extensions)
        result = ''
        finish = ('' if own_outputs else '  gl_FragColor = vec4(0.0);\n') if stage == 'fs' else '  gl_Position = vec4(0.0);\n'
    out.append(f'%% {stage} {name}{" core" if profile == "core" else ""}\n{header}{result}{decls}void main() {{\n{body}{finish}}}\n')


opaque = re.compile(r'sampler|image|atomic_uint')
for proto in sorted(prototypes):
    m = re.match(r'(\w+)\s+(\w+)\s*\((.*)\)\s*;', proto)
    ret, fname, params = m.group(1), m.group(2), m.group(3).strip()
    if skip.search(fname) or fname.startswith('gl_'):
        continue
    decls, args, locals_ = [], [], []
    ok = True
    for i, param in enumerate([p.strip() for p in params.split(',')] if params and params != 'void' else []):
        words = [w for w in param.split() if w not in ('in', 'out', 'inout', 'const', 'highp', 'mediump', 'lowp',
                                                         'coherent', 'volatile', 'restrict', 'readonly', 'writeonly')]
        if not words:
            ok = False
            break
        ptype = words[0]
        if opaque.search(ptype):
            decls.append(f'uniform {ptype} a{i};\n')
        else:
            locals_.append(f'  {ptype} a{i};\n')
        args.append(f'a{i}')
    if not ok:
        continue
    call = f'{fname}({", ".join(args)})'
    body = ''.join(locals_) + (f'  {ret} r = {call};\n' if ret != 'void' else f'  {call};\n')
    key = re.sub(r'[^A-Za-z0-9_]+', '_', proto.rstrip(';'))[:200]
    for stage in ('vs', 'fs'):
        for profile in ('legacy', 'core'):
            shader(stage, key, profile, ''.join(decls), body)

# Built-in variables and qualifiers of Apple's legacy GLSL extensions.
variables = [
    ('vs', 'gl_VertexID', '  int v = gl_VertexID;\n'), ('vs', 'gl_InstanceIDARB', '  int v = gl_InstanceIDARB;\n'),
    ('vs', 'gl_InstanceID', '  int v = gl_InstanceID;\n'), ('fs', 'gl_PrimitiveID', '  int v = gl_PrimitiveID;\n'),
    ('fs', 'gl_PointCoord', '  vec2 v = gl_PointCoord;\n'), ('fs', 'gl_FrontFacing', '  bool v = gl_FrontFacing;\n'),
    ('fs', 'gl_FragData', '  gl_FragData[1] = vec4(1.0);\n'), ('fs', 'gl_FragDepth', '  gl_FragDepth = 0.5;\n'),
    ('vs', 'gl_ClipVertex', '  gl_ClipVertex = vec4(1.0);\n'), ('vs', 'gl_PointSize', '  gl_PointSize = 2.0;\n'),
    ('vs', 'gl_BackColor', '  gl_BackColor = vec4(1.0);\n'), ('fs', 'gl_SecondaryColor', '  vec4 v = gl_SecondaryColor;\n'),
    ('vs', 'gl_FogCoord', '  float v = gl_FogCoord;\n'), ('fs', 'gl_FogFragCoord', '  float v = gl_FogFragCoord;\n'),
    ('vs', 'gl_NormalMatrix', '  mat3 v = gl_NormalMatrix;\n'), ('vs', 'gl_TextureMatrixInverseTranspose', '  mat4 v = gl_TextureMatrixInverseTranspose[1];\n'),
    ('vs', 'gl_MaxTextureCoords', '  int v = gl_MaxTextureCoords;\n'), ('fs', 'gl_MaxDrawBuffers', '  int v = gl_MaxDrawBuffers;\n'),
    ('vs', 'gl_MaxVaryingFloats', '  int v = gl_MaxVaryingFloats;\n'), ('vs', 'gl_MaxClipPlanes', '  int v = gl_MaxClipPlanes;\n'),
    ('fs', 'gl_FragCoord', '  vec4 v = gl_FragCoord;\n'), ('vs', 'gl_EyePlaneQ', '  vec4 v = gl_EyePlaneQ[2];\n'),
    ('fs', 'gl_DepthRange', '  float v = gl_DepthRange.diff;\n'), ('vs', 'gl_Point', '  float v = gl_Point.distanceLinearAttenuation;\n'),
    ('vs', 'gl_FrontLightProduct', '  vec4 v = gl_FrontLightProduct[1].specular;\n'),
]
for stage, name, body in variables:
    shader(stage, 'variable_' + name, 'legacy', '', body)
qualifiers = [
    ('fs', 'flat_varying', 'flat varying vec4 v;\n', '  vec4 w = v;\n'),
    ('fs', 'noperspective_varying', 'noperspective varying vec4 v;\n', '  vec4 w = v;\n'),
    ('fs', 'centroid_varying', 'centroid varying vec4 v;\n', '  vec4 w = v;\n'),
    ('fs', 'varying_out', 'varying out vec4 color;\n', '  color = vec4(1.0);\n'),
    ('vs', 'unsigned_attribute', 'attribute uvec4 a;\n', '  uvec4 w = a;\n'),
    ('vs', 'integer_attribute', 'attribute ivec2 a;\n', '  ivec2 w = a;\n'),
    ('fs', 'invariant_varying', 'invariant varying vec4 v;\n', '  vec4 w = v;\n'),
    ('fs', 'bindable_uniform', '#extension GL_EXT_bindable_uniform : enable\nbindable uniform vec4 b[4];\n', '  vec4 w = b[1];\n'),
    ('fs', 'array_constructor', '', '  float a[3] = float[3](1.0, 2.0, 3.0);\n'),
    ('fs', 'uint_literal', '', '  uint u = 3u;\n'),
    ('fs', 'bit_ops', '', '  int i = 5; i = (i << 2) ^ (i & 3) | ~i % 2;\n'),
    ('fs', 'switch', '', '  int i = 1; switch (i) { case 1: break; default: break; }\n'),
    ('fs', 'sampler_buffer', 'uniform samplerBuffer s;\n', '  vec4 w = texelFetchBuffer(s, 2);\n'),
    ('fs', 'usampler2DArray', 'uniform usampler2DArray s;\n', '  uvec4 w = texture2DArray(s, vec3(0.0));\n'),
]
for stage, name, decls, body in qualifiers:
    shader(stage, 'qualifier_' + name, 'legacy', decls, body)
sys.stdout.write(''.join(out))
