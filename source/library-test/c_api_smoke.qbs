import qbs

CppApplication {
    name: "kmx-sat-c-api-smoke"

    Depends { name: "kmx-sat-lib" }

    consoleApplication: true
    files: ["c_api_smoke.c"]
    cpp.cLanguageVersion: "c11"
    cpp.includePaths: ["../library/api"]
    Properties {
        condition: qbs.buildVariant === "release"
        cpp.debugInformation: false
        cpp.defines: ["NDEBUG"]
        cpp.commonCompilerFlags: ["-Ofast", "-march=native", "-flto=auto"]
        cpp.linkerFlags: ["-flto=auto"]
    }
}