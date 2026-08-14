import qbs

CppApplication {
    name: "kmx-sat"
    Depends { name: "cpp" }
    Depends { name: "kmx-sat-lib" }
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.enableExceptions: true
    cpp.warningLevel: "all"
    files: [
        "cli/kmx-sat-main.cpp",
    ]
    Properties {
        condition: qbs.buildVariant === "release"
        cpp.debugInformation: false
        cpp.optimization: "fast"
        cpp.defines: ["NDEBUG"]
        cpp.commonCompilerFlags: ["-Ofast", "-march=native", "-flto=auto"]
        cpp.linkerFlags: ["-flto=auto"]
    }
    Group {
        fileTagsFilter: product.type
        qbs.install: true
        qbs.installDir: "bin"
    }
}
