use std::ffi::{CStr, CString};
use std::os::raw::c_char;

// Simple Rust shim: exposes C ABI `parse` function that accepts a C string
// and returns a heap-allocated C string (caller must free with free_string).
// The shim forwards the request to the HTTP NLU adapter at http://nlu:5000/parse

#[no_mangle]
pub extern "C" fn parse(input: *const c_char) -> *mut c_char {
    if input.is_null() {
        return std::ptr::null_mut();
    }

    // Safety: caller must pass a NUL-terminated UTF-8 string
    let cstr = unsafe { CStr::from_ptr(input) };
    let text = match cstr.to_str() {
        Ok(s) => s,
        Err(_) => return std::ptr::null_mut(),
    };

    // Build a blocking client and POST to the NLU adapter
    let client = match reqwest::blocking::Client::new().post("http://nlu:5000/parse").body(text.to_string()).send() {
        Ok(r) => r,
        Err(_) => return std::ptr::null_mut(),
    };

    let body = match client.text() {
        Ok(t) => t,
        Err(_) => return std::ptr::null_mut(),
    };

    // Return a newly allocated C string; caller should free
    let cstring = match CString::new(body) {
        Ok(cs) => cs,
        Err(_) => return std::ptr::null_mut(),
    };

    cstring.into_raw()
}

#[no_mangle]
pub extern "C" fn free_string(s: *mut c_char) {
    if s.is_null() { return; }
    unsafe { CString::from_raw(s); }
}
