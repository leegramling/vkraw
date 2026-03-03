use rhai::{Engine, Scope, AST};
use serde::Deserialize;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

#[derive(Debug, Clone, Deserialize)]
struct Root {
    objects: Objects,
    script: Script,
}

#[derive(Debug, Clone, Deserialize)]
struct Objects {
    ball: Ball,
}

#[derive(Debug, Clone, Deserialize)]
struct Script {
    lang: String,
    code: String,
}

#[derive(Debug, Clone, Deserialize)]
struct Ball {
    x: f32,
    y: f32,
    base_y: f32,
    height: f32,
    period: f32,
}

struct AnimVm {
    engine: Engine,
    scope: Scope<'static>,
    ast: Option<AST>,
    ball: Ball,
    last_error: CString,
}

impl AnimVm {
    fn new() -> Self {
        let mut engine = Engine::new();
        engine.register_type_with_name::<Ball>("Ball");
        engine.register_get_set(
            "x",
            |b: &mut Ball| b.x as rhai::FLOAT,
            |b: &mut Ball, v: rhai::FLOAT| b.x = v as f32,
        );
        engine.register_get_set(
            "y",
            |b: &mut Ball| b.y as rhai::FLOAT,
            |b: &mut Ball, v: rhai::FLOAT| b.y = v as f32,
        );
        engine.register_get_set(
            "base_y",
            |b: &mut Ball| b.base_y as rhai::FLOAT,
            |b: &mut Ball, v: rhai::FLOAT| b.base_y = v as f32,
        );
        engine.register_get_set(
            "height",
            |b: &mut Ball| b.height as rhai::FLOAT,
            |b: &mut Ball, v: rhai::FLOAT| b.height = v as f32,
        );
        engine.register_get_set(
            "period",
            |b: &mut Ball| b.period as rhai::FLOAT,
            |b: &mut Ball, v: rhai::FLOAT| b.period = v as f32,
        );

        Self {
            engine,
            scope: Scope::new(),
            ast: None,
            ball: Ball {
                x: 0.0,
                y: 0.0,
                base_y: 0.0,
                height: 1.0,
                period: 1.0,
            },
            last_error: CString::new("").unwrap_or_else(|_| CString::default()),
        }
    }

    fn set_error(&mut self, msg: impl Into<String>) -> i32 {
        self.last_error = cstring_sanitize(msg.into());
        1
    }
}

fn cstring_sanitize(s: String) -> CString {
    match CString::new(s) {
        Ok(c) => c,
        Err(_) => CString::new("string contains interior nul").unwrap_or_else(|_| CString::default()),
    }
}

fn vm_mut<'a>(vm: *mut AnimVm) -> Result<&'a mut AnimVm, i32> {
    if vm.is_null() {
        return Err(1);
    }
    Ok(unsafe { &mut *vm })
}

fn extract_script_code(raw: &str) -> String {
    let marker = "code:";
    let Some(code_pos) = raw.find(marker) else {
        return raw.to_string();
    };
    let tail = &raw[code_pos + marker.len()..];
    let Some(start_bt_rel) = tail.find('`') else {
        return raw.to_string();
    };
    let start_bt = code_pos + marker.len() + start_bt_rel;
    let after_start = &raw[start_bt + 1..];
    let Some(end_bt_rel) = after_start.find('`') else {
        return raw.to_string();
    };
    let end_bt = start_bt + 1 + end_bt_rel;
    let body = &raw[start_bt + 1..end_bt];

    let escaped = body
        .replace('\\', "\\\\")
        .replace('"', "\\\"")
        .replace('\r', "")
        .replace('\n', "\\n");
    let mut out = String::with_capacity(raw.len() + 16);
    out.push_str(&raw[..start_bt]);
    out.push('"');
    out.push_str(&escaped);
    out.push('"');
    out.push_str(&raw[end_bt + 1..]);
    out
}

#[no_mangle]
pub extern "C" fn animvm_create() -> *mut std::ffi::c_void {
    match catch_unwind(|| Box::new(AnimVm::new())) {
        Ok(vm) => Box::into_raw(vm) as *mut std::ffi::c_void,
        Err(_) => ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "C" fn animvm_load_json5(vm: *mut std::ffi::c_void, json5_utf8: *const c_char) -> i32 {
    match catch_unwind(AssertUnwindSafe(|| {
        let vm = match vm_mut(vm as *mut AnimVm) {
            Ok(v) => v,
            Err(_) => return 1,
        };
        if json5_utf8.is_null() {
            return vm.set_error("json5 pointer is null");
        }

        let text = unsafe { CStr::from_ptr(json5_utf8) };
        let text = match text.to_str() {
            Ok(s) => s,
            Err(_) => return vm.set_error("json5 is not valid UTF-8"),
        };

        let normalized = extract_script_code(text);
        let root: Root = match json5::from_str(&normalized) {
            Ok(cfg) => cfg,
            Err(e) => return vm.set_error(format!("json5 parse failed: {e}")),
        };
        if root.script.lang != "rhai" {
            return vm.set_error("script.lang must be \"rhai\"");
        }

        let ast = match vm.engine.compile(root.script.code.as_str()) {
            Ok(ast) => ast,
            Err(e) => return vm.set_error(format!("rhai compile failed: {e}")),
        };

        vm.scope.clear();
        vm.ball = root.objects.ball;
        vm.ast = Some(ast);
        vm.last_error = cstring_sanitize(String::new());
        0
    })) {
        Ok(code) => code,
        Err(_) => 1,
    }
}

#[no_mangle]
pub extern "C" fn animvm_tick(vm: *mut std::ffi::c_void, t: f32, dt: f32) -> i32 {
    match catch_unwind(AssertUnwindSafe(|| {
        let vm = match vm_mut(vm as *mut AnimVm) {
            Ok(v) => v,
            Err(_) => return 1,
        };
        let Some(ast) = vm.ast.as_ref() else {
            return vm.set_error("script not loaded");
        };

        vm.scope.set_or_push("t", t as rhai::FLOAT);
        vm.scope.set_or_push("dt", dt as rhai::FLOAT);
        vm.scope.set_or_push("ball", vm.ball.clone());

        if let Err(e) = vm.engine.eval_ast_with_scope::<()>(&mut vm.scope, ast) {
            return vm.set_error(format!("rhai eval failed: {e}"));
        }

        match vm.scope.get_value::<Ball>("ball") {
            Some(ball) => vm.ball = ball,
            None => return vm.set_error("script did not provide ball object"),
        }

        vm.last_error = cstring_sanitize(String::new());
        0
    })) {
        Ok(code) => code,
        Err(_) => 1,
    }
}

#[no_mangle]
pub extern "C" fn animvm_get_ball(vm: *mut std::ffi::c_void, out_x: *mut f32, out_y: *mut f32) -> i32 {
    match catch_unwind(AssertUnwindSafe(|| {
        let vm = match vm_mut(vm as *mut AnimVm) {
            Ok(v) => v,
            Err(_) => return 1,
        };
        if out_x.is_null() || out_y.is_null() {
            return vm.set_error("output pointers must not be null");
        }
        unsafe {
            *out_x = vm.ball.x;
            *out_y = vm.ball.y;
        }
        vm.last_error = cstring_sanitize(String::new());
        0
    })) {
        Ok(code) => code,
        Err(_) => 1,
    }
}

#[no_mangle]
pub extern "C" fn animvm_last_error(vm: *mut std::ffi::c_void) -> *const c_char {
    if vm.is_null() {
        static MSG: &[u8] = b"vm is null\0";
        return MSG.as_ptr() as *const c_char;
    }
    let vm = unsafe { &mut *(vm as *mut AnimVm) };
    vm.last_error.as_ptr()
}

#[no_mangle]
pub extern "C" fn animvm_destroy(vm: *mut std::ffi::c_void) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if vm.is_null() {
            return;
        }
        unsafe {
            drop(Box::from_raw(vm as *mut AnimVm));
        }
    }));
}
