#!/usr/bin/env python3
import hashlib
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NAME = "Jarvis"
BUNDLE = "com.jenda.jarvis"
TEAM = "LRRBKK8652"
DEPLOYMENT = "17.0"

used = set()


def oid(*parts):
    value = hashlib.sha1("::".join(parts).encode()).hexdigest()[:24].upper()
    while value in used:
        value = hashlib.sha1((value + "x").encode()).hexdigest()[:24].upper()
    used.add(value)
    return value


def collect(folder, suffix):
    found = []
    for path, folders, files in os.walk(os.path.join(ROOT, folder)):
        folders[:] = sorted(f for f in folders if not f.endswith(".xcassets"))
        found += [os.path.relpath(os.path.join(path, f), ROOT) for f in sorted(files) if f.endswith(suffix)]
    return sorted(found)


sources = collect(NAME, ".swift")
resources = [f"{NAME}/Assets.xcassets"]
others = [f"{NAME}/Info.plist"]
paths = sources + resources + others
refs = {path: oid("ref", path) for path in paths}
builds = {path: oid("build", path) for path in sources + resources}
product = oid("product")
target = oid("target")
project = oid("project")
main_group = oid("group", "main")
products_group = oid("group", "products")
sources_phase = oid("phase", "sources")
resources_phase = oid("phase", "resources")
frameworks_phase = oid("phase", "frameworks")
project_configs = oid("configs", "project")
target_configs = oid("configs", "target")
project_debug = oid("config", "project", "Debug")
project_release = oid("config", "project", "Release")
target_debug = oid("config", "target", "Debug")
target_release = oid("config", "target", "Release")


def kind(path):
    if path.endswith(".swift"):
        return "sourcecode.swift"
    if path.endswith(".xcassets"):
        return "folder.assetcatalog"
    return "text.plist.xml"


groups = {}
for path in paths:
    parts = path.split("/")
    for depth in range(1, len(parts)):
        folder = "/".join(parts[:depth])
        groups.setdefault(folder, [])
        parent = "/".join(parts[:depth - 1])
        child = ("group", folder)
        if depth > 1 and child not in groups[parent]:
            groups[parent].append(child)
    groups.setdefault("/".join(parts[:-1]), []).append(("file", path))
group_ids = {folder: oid("group", folder) for folder in groups}

out = ["// !$*UTF8*$!", "{", "\tarchiveVersion = 1;", "\tclasses = {", "\t};", "\tobjectVersion = 56;", "\tobjects = {", ""]
out.append("/* Begin PBXBuildFile section */")
for path, build in builds.items():
    phase = "Sources" if path.endswith(".swift") else "Resources"
    out.append(f"\t\t{build} /* {os.path.basename(path)} in {phase} */ = {{isa = PBXBuildFile; fileRef = {refs[path]}; }};")
out.append("/* End PBXBuildFile section */\n")
out.append("/* Begin PBXFileReference section */")
for path, ref in refs.items():
    out.append(f"\t\t{ref} = {{isa = PBXFileReference; lastKnownFileType = {kind(path)}; path = \"{os.path.basename(path)}\"; sourceTree = \"<group>\"; }};")
out.append(f"\t\t{product} = {{isa = PBXFileReference; explicitFileType = wrapper.application; includeInIndex = 0; path = {NAME}.app; sourceTree = BUILT_PRODUCTS_DIR; }};")
out.append("/* End PBXFileReference section */\n")
out.append("/* Begin PBXFrameworksBuildPhase section */")
out.append(f"\t\t{frameworks_phase} = {{isa = PBXFrameworksBuildPhase; buildActionMask = 2147483647; files = (); runOnlyForDeploymentPostprocessing = 0; }};")
out.append("/* End PBXFrameworksBuildPhase section */\n")
out.append("/* Begin PBXGroup section */")
top = [group_ids[f] for f in groups if f and "/" not in f]
out.append(f"\t\t{main_group} = {{isa = PBXGroup; children = ({', '.join(top + [products_group])}); sourceTree = \"<group>\"; }};")
out.append(f"\t\t{products_group} = {{isa = PBXGroup; children = ({product}); name = Products; sourceTree = \"<group>\"; }};")
for folder, children in groups.items():
    if not folder:
        continue
    ids = [group_ids[name] if what == "group" else refs[name] for what, name in children]
    out.append(f"\t\t{group_ids[folder]} = {{isa = PBXGroup; children = ({', '.join(ids)}); path = \"{os.path.basename(folder)}\"; sourceTree = \"<group>\"; }};")
out.append("/* End PBXGroup section */\n")
out.append("/* Begin PBXNativeTarget section */")
out.append(f"\t\t{target} = {{isa = PBXNativeTarget; buildConfigurationList = {target_configs}; buildPhases = ({sources_phase}, {frameworks_phase}, {resources_phase}); buildRules = (); dependencies = (); name = {NAME}; productName = {NAME}; productReference = {product}; productType = \"com.apple.product-type.application\"; }};")
out.append("/* End PBXNativeTarget section */\n")
out.append("/* Begin PBXProject section */")
out.append(f"\t\t{project} = {{isa = PBXProject; attributes = {{BuildIndependentTargetsInParallel = 1; LastSwiftUpdateCheck = 1640; LastUpgradeCheck = 1640; TargetAttributes = {{{target} = {{CreatedOnToolsVersion = 16.4; }}; }}; }}; buildConfigurationList = {project_configs}; compatibilityVersion = \"Xcode 14.0\"; developmentRegion = cs; hasScannedForEncodings = 0; knownRegions = (cs, en, Base); mainGroup = {main_group}; productRefGroup = {products_group}; projectDirPath = \"\"; projectRoot = \"\"; targets = ({target}); }};")
out.append("/* End PBXProject section */\n")
out.append("/* Begin PBXResourcesBuildPhase section */")
out.append(f"\t\t{resources_phase} = {{isa = PBXResourcesBuildPhase; buildActionMask = 2147483647; files = ({', '.join(builds[p] for p in resources)}); runOnlyForDeploymentPostprocessing = 0; }};")
out.append("/* End PBXResourcesBuildPhase section */\n")
out.append("/* Begin PBXSourcesBuildPhase section */")
out.append(f"\t\t{sources_phase} = {{isa = PBXSourcesBuildPhase; buildActionMask = 2147483647; files = ({', '.join(builds[p] for p in sources)}); runOnlyForDeploymentPostprocessing = 0; }};")
out.append("/* End PBXSourcesBuildPhase section */\n")

common = {
    "ALWAYS_SEARCH_USER_PATHS": "NO",
    "CLANG_ENABLE_MODULES": "YES",
    "CLANG_ENABLE_OBJC_ARC": "YES",
    "ENABLE_STRICT_OBJC_MSGSEND": "YES",
    "GCC_NO_COMMON_BLOCKS": "YES",
    "IPHONEOS_DEPLOYMENT_TARGET": DEPLOYMENT,
    "SDKROOT": "iphoneos",
    "SWIFT_VERSION": "5.0",
}
debug_only = {"DEBUG_INFORMATION_FORMAT": "dwarf", "ONLY_ACTIVE_ARCH": "YES", "SWIFT_OPTIMIZATION_LEVEL": "\"-Onone\"",
              "SWIFT_ACTIVE_COMPILATION_CONDITIONS": "DEBUG", "ENABLE_TESTABILITY": "YES", "GCC_OPTIMIZATION_LEVEL": "0"}
release_only = {"DEBUG_INFORMATION_FORMAT": "\"dwarf-with-dsym\"", "SWIFT_COMPILATION_MODE": "wholemodule", "VALIDATE_PRODUCT": "YES"}
target_settings = {
    "ASSETCATALOG_COMPILER_APPICON_NAME": "AppIcon",
    "ASSETCATALOG_COMPILER_GLOBAL_ACCENT_COLOR_NAME": "AccentColor",
    "CODE_SIGN_STYLE": "Automatic",
    "CURRENT_PROJECT_VERSION": "1",
    "DEVELOPMENT_TEAM": TEAM,
    "INFOPLIST_FILE": f"{NAME}/Info.plist",
    "LD_RUNPATH_SEARCH_PATHS": "\"$(inherited) @executable_path/Frameworks\"",
    "MARKETING_VERSION": "1.0",
    "PRODUCT_BUNDLE_IDENTIFIER": BUNDLE,
    "PRODUCT_NAME": "\"$(TARGET_NAME)\"",
    "SUPPORTED_PLATFORMS": "\"iphoneos iphonesimulator\"",
    "SUPPORTS_MACCATALYST": "NO",
    "TARGETED_DEVICE_FAMILY": "1",
    "SWIFT_EMIT_LOC_STRINGS": "YES",
}


def config(identifier, name, values):
    body = " ".join(f"{key} = {value};" for key, value in sorted(values.items()))
    return f"\t\t{identifier} = {{isa = XCBuildConfiguration; buildSettings = {{{body} }}; name = {name}; }};"


out.append("/* Begin XCBuildConfiguration section */")
out.append(config(project_debug, "Debug", {**common, **debug_only}))
out.append(config(project_release, "Release", {**common, **release_only}))
out.append(config(target_debug, "Debug", target_settings))
out.append(config(target_release, "Release", target_settings))
out.append("/* End XCBuildConfiguration section */\n")
out.append("/* Begin XCConfigurationList section */")
out.append(f"\t\t{project_configs} = {{isa = XCConfigurationList; buildConfigurations = ({project_debug}, {project_release}); defaultConfigurationIsVisible = 0; defaultConfigurationName = Release; }};")
out.append(f"\t\t{target_configs} = {{isa = XCConfigurationList; buildConfigurations = ({target_debug}, {target_release}); defaultConfigurationIsVisible = 0; defaultConfigurationName = Release; }};")
out.append("/* End XCConfigurationList section */")
out += ["\t};", f"\trootObject = {project};", "}", ""]

folder = os.path.join(ROOT, f"{NAME}.xcodeproj")
os.makedirs(folder, exist_ok=True)
with open(os.path.join(folder, "project.pbxproj"), "w", encoding="utf-8") as handle:
    handle.write("\n".join(out))
print(f"{len(sources)} sources")
