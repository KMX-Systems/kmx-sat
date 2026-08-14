import qbs

CppApplication {
    Depends { name: "kmx-sat-lib" }

    property stringList testSources: []
    property stringList extraSources: []

    consoleApplication: true
    files: [
        "src/kmx/sat/catch2_main.cpp",
        "inc/**/*.hpp",
        "inc_dep/**/*.hpp",
    ].concat(testSources).concat(extraSources)

    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.warningLevel: "all"
    cpp.includePaths: [
        "inc",
        "inc_dep",
    ]
    cpp.systemIncludePaths: [
        "/usr/local/include",
    ]
    cpp.staticLibraries: [
        "/usr/local/lib/libCatch2Main.a",
        "/usr/local/lib/libCatch2.a",
    ]

    Properties {
        condition: qbs.buildVariant === "debug"
        cpp.debugInformation: true
        cpp.optimization: "none"
    }

    Properties {
        condition: qbs.buildVariant === "release"
        cpp.debugInformation: false
        cpp.optimization: "fast"
        cpp.defines: ["NDEBUG"]
        cpp.commonCompilerFlags: ["-O3", "-march=native", "-flto=auto"]
        cpp.linkerFlags: ["-flto=auto"]
    }

}