# Copyright 2021 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

import sys
import os
import json
import hash_gen
import merge_anari
import argparse
import pathlib

"""static void fillStruct(ANARIFeatures *features, const char **list) {

    for(const char *i = *list;i!=NULL;++i) {
        switch(feature_hash(i)) {

        }
    }
}

"""

class FeatureUtilityGenerator:
    def __init__(self, anari):
        self.anari = anari
        self.features = ["ANARI_"+f for f in self.anari["features"]]

    def generate_feature_header(self):
        code = "typedef struct {\n"
        for feature in self.features:
            code += "   int %s;\n"%feature
        code += "} ANARIFeatures;\n"
        code += "int anariGetDeviceFeatureStruct(ANARIFeatures *features, ANARILibrary library, const char *deviceName);\n"
        code += "int anariGetObjectFeatureStruct(ANARIFeatures *features, ANARIDevice device, ANARIDataType objectType, const char *objectName);\n"
        code += "int anariGetInstanceFeatureStruct(ANARIFeatures *features, ANARIDevice device, ANARIObject object);\n"

        code += "#ifdef ANARI_FEATURE_UTILITY_IMPL\n"
        code += "#include <string.h>\n"
        code += "static " + hash_gen.gen_hash_function("feature_hash", self.features)
        code += "static void fillFeatureStruct(ANARIFeatures *features, const char *const *list) {\n"
        code += "    memset(features, 0, sizeof(ANARIFeatures));\n"
        code += "    for(const char *const *i = list;*i!=NULL;++i) {\n"
        code += "        switch(feature_hash(*i)) {\n"
        for idx, feature in enumerate(self.features):
            code += "            case %d: features->%s = 1; break;\n"%(idx, feature)
        code += "            default: break;\n"
        code += "        }\n"
        code += "    }\n"
        code += "}\n"
        code += "int anariGetDeviceFeatureStruct(ANARIFeatures *features, ANARILibrary library, const char *deviceName) {\n"
        code += "    const char *const *list = (const char *const *)anariGetDeviceFeatures(library, deviceName);\n"
        code += "    if(list) {\n"
        code += "        fillFeatureStruct(features, list);\n"
        code += "        return 0;\n"
        code += "    } else {\n"
        code += "        return 1;\n"
        code += "    }\n"
        code += "}\n"
        code += "int anariGetObjectFeatureStruct(ANARIFeatures *features, ANARIDevice device, ANARIDataType objectType, const char *objectName) {\n"
        code += "    const char *const *list = (const char *const *)anariGetObjectInfo(device, objectType, objectName, \"feature\", ANARI_STRING_LIST);\n"
        code += "    if(list) {\n"
        code += "        fillFeatureStruct(features, list);\n"
        code += "        return 0;\n"
        code += "    } else {\n"
        code += "        return 1;\n"
        code += "    }\n"
        code += "}\n"
        code += "int anariGetInstanceFeatureStruct(ANARIFeatures *features, ANARIDevice device, ANARIObject object) {\n"
        code += "    const char *const *list = NULL;\n"
        code += "    anariGetProperty(device, object, \"feature\", ANARI_STRING_LIST, &list, sizeof(list), ANARI_WAIT);\n"
        code += "    if(list) {\n"
        code += "        fillFeatureStruct(features, list);\n"
        code += "        return 0;\n"
        code += "    } else {\n"
        code += "        return 1;\n"
        code += "    }\n"
        code += "}\n"
        code += "#endif\n"
        return code


parser = argparse.ArgumentParser(description="Generate query functions for an ANARI device.")
parser.add_argument("-d", "--device", dest="devicespec", type=open, help="The device json file.")
parser.add_argument("-j", "--json", dest="json", type=pathlib.Path, action="append", help="Path to the core and extension json root.")
parser.add_argument("-n", "--namespace", dest="namespace", help="Namespace for the classes and filenames.")
parser.add_argument("-o", "--output", dest="outdir", type=pathlib.Path, default=pathlib.Path("."), help="Output directory")
args = parser.parse_args()


#flattened list of all input jsons in supplied directories
jsons = [entry for j in args.json for entry in j.glob("**/*.json")]

#load the device root
device = json.load(args.devicespec)
merge_anari.tag_feature(device)
print("opened " + device["info"]["type"] + " " + device["info"]["name"])

dependencies = merge_anari.crawl_dependencies(device, jsons)
#merge all dependencies
for x in dependencies:
    matches = [p for p in jsons if p.stem == x]
    for m in matches:
        feature = json.load(open(m))
        merge_anari.tag_feature(feature)
        merge_anari.merge(device, feature)

#generate files
gen = FeatureUtilityGenerator(device)


def begin_namespaces(args):
    output = ""
    if args.namespace:
        for n in args.namespace.split("::"):
            output += "namespace %s {\n"%n
    return output

def end_namespaces(args):
    output = ""
    if args.namespace:
        for n in args.namespace.split("::"):
            output += "}\n"
    return output


with open(args.outdir/"anari_feature_utility.h", mode='w') as f:
    f.write("// Copyright 2021 The Khronos Group\n")
    f.write("// SPDX-License-Identifier: Apache-2.0\n\n")
    f.write("// This file was generated by "+os.path.basename(__file__)+"\n")
    f.write("// Don't make changes to this directly\n\n")

    f.write("#pragma once\n")
    f.write("#include <anari/anari.h>\n")
    f.write(gen.generate_feature_header())

