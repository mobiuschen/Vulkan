#!/usr/bin/python
# -*- coding: UTF-8 -*-

import sys, getopt, os
from datetime import date
from datetime import datetime

# Compile all the shaders

VULKAN_SDK = os.getenv('VULKAN_SDK')
GLSLC = VULKAN_SDK + "/bin/glslc"
HLSL_DIR = "./data/shaders/hlsl"
GLSL_DIR = "./data/shaders/glsl"

def compile_language(proj, language):
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
            if ext == ".frag" or ext == ".vert":
                # print("compiling {path}/{path}".format(glslc=GLSLC, file=file, path=path))
                # command = "{glslc} -x {lang} {path}/{file} -o {path}/{file}.spv".format(glslc=GLSLC, lang=language, file=file, path=path)
                command = "{glslc} -x {lang} {path}/{file} -o {path}/{file}.spv".format(glslc=GLSLC, lang=language, file=file, path=path)
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

    print("GLSLC version:")
    os.system("{glslc} --version".format(glslc=GLSLC))
    print("\n")
    print("Compile shaders in project {dir} ☕".format(dir=proj))
    # compile_language(proj, language)
    compile_language(proj, "hlsl")
    compile_language(proj, "glsl")
    print("📣 Compiling has done.")

if __name__ == "__main__":
    main(sys.argv[1:]) 