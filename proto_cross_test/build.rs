
fn main() {
    println!("cargo:rerun-if-changed=../proto_cpp/include/bytes.h");
    println!("cargo:rerun-if-changed=../proto_cpp/include/frame.h");
    println!("cargo:rerun-if-changed=../proto_cpp/include/static_vec.h");
    println!("cargo:rerun-if-changed=../proto_cpp/src/frame.cpp");
    println!("cargo:rerun-if-changed=bindings/frame.cpp");

    cc::Build::new()
        .cpp(true)
        .std("c++20")
        .file("../proto_cpp/src/frame.cpp")
        .file("bindings/frame.cpp")
        .include("../proto_cpp/include")
        .compile("proto_c_bindings")
}
