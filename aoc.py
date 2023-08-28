#!/usr/bin/python
# -*- coding: UTF-8 -*-

import sys, getopt, os
from compile_spirv import compile as compile_spirv

# Compile all the shaders

VULKAN_SDK = os.getenv('VULKAN_SDK')
GLSLC = VULKAN_SDK + "/bin/glslc"
AOC = ".\\aoc.exe"
GPU = "a650"
HLSL_DIR = "./data/shaders/hlsl"
GLSL_DIR = "./data/shaders/glsl"

def analysis_spirv(proj, language):
    match language:
        case "hlsl":
            shaderdir = os.walk(HLSL_DIR)
        case "glsl":
            shaderdir = os.walk(GLSL_DIR)
        case _:
            raise Exception(f"ERROR: unexpected language: {language}")

    for path, dirs, files in shaderdir:
        # print("files: {files}".format(files=files))
        # print("dirs: {dirs}".format(dirs=dirs))
        # print("path {path}".format(path=path))
        pathTail = os.path.split(path)[1]
        if proj != "all" and proj != pathTail:
            continue;

        for file in files:
            _, ext = os.path.splitext(file)
            
            if ext == ".frag" or ext == ".vert" or ext == ".comp" \
                or ext == ".geom" or ext == ".tesc" or ext == ".tese"\
                or ext == ".rgen" or ext == ".rchit" or ext == ".rmiss":
                # print("compiling {path}/{path}".format(glslc=GLSLC, file=file, path=path))
                # malioc --vulkan -c Mali-G72 --spirv -n main hlsl\triangle\triangle.vert.spv -o triangle.o
                # aoc.exe -arch=a650 -api=Vulkan ssao.vert.spv ssao.frag.spv
                input = os.path.abspath("{path}/{file}.spv".format(path=path, file=file))
                output = os.path.abspath("{path}/{file}.aoc".format(path=path, file=file))
                command = "\"{aoc}\" -dump=stats -arch={arch} -api=Vulkan {input} > {output}".format(aoc=AOC, arch=GPU, input=input, output=output)
                print(command)
                os.system(command)

def main(argv):
    proj="none"
    if len(argv) == 0:
        help = ("Useage: cmd project_name")
        print(help)
        exit(2)
    else:
        proj=argv[0]

    print("Adreno offline compiler analyses \"{dir}\" ☕".format(dir=proj))
    compile_spirv(proj, "hlsl")
    compile_spirv(proj, "glsl")
    analysis_spirv(proj, "hlsl")
    analysis_spirv(proj, "glsl")
    print("📣 Aanlysis has been done.")

if __name__ == "__main__":
    main(sys.argv[1:]) 