use rhai::{Dynamic, Engine, EvalAltResult, Scope, AST};
use serde_json::Value;
use std::cell::RefCell;
use std::collections::HashMap;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;
use std::rc::Rc;

const CMD_SET_F32: u16 = 1;
const CMD_SET_VEC3: u16 = 2;
const CMD_SET_VEC4: u16 = 3;
const CMD_DRAW_LINE: u16 = 4;

#[derive(Debug, Clone)]
struct Vec3 {
    x: f32,
    y: f32,
    z: f32,
}

#[derive(Debug, Clone)]
struct Vec4 {
    x: f32,
    y: f32,
    z: f32,
    w: f32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum ParamType {
    F32,
    Vec4,
}

#[derive(Debug, Clone)]
enum ParamValue {
    F32(f32),
    Vec4(Vec4),
}

#[derive(Debug, Clone)]
struct ParamEntry {
    id: u32,
    ty: ParamType,
    value: ParamValue,
}

#[derive(Default)]
struct VmShared {
    path_to_id: HashMap<String, u32>,
    next_path_id: u32,

    param_to_id: HashMap<String, u32>,
    id_to_param: HashMap<u32, String>,
    params: HashMap<String, ParamEntry>,
    next_param_id: u32,

    commands: Vec<u8>,
    pending_error: Option<String>,
}

impl VmShared {
    fn new() -> Self {
        Self {
            path_to_id: HashMap::new(),
            next_path_id: 1,
            param_to_id: HashMap::new(),
            id_to_param: HashMap::new(),
            params: HashMap::new(),
            next_param_id: 1,
            commands: Vec::new(),
            pending_error: None,
        }
    }

    fn clear_frame(&mut self) {
        self.commands.clear();
        self.pending_error = None;
    }

    fn set_error_once(&mut self, msg: impl Into<String>) {
        if self.pending_error.is_none() {
            self.pending_error = Some(msg.into());
        }
    }

    fn intern_path(&mut self, path: &str) -> u32 {
        if let Some(id) = self.path_to_id.get(path) {
            return *id;
        }
        let id = self.next_path_id;
        self.next_path_id = self.next_path_id.saturating_add(1);
        self.path_to_id.insert(path.to_string(), id);
        id
    }

    fn intern_param_existing_only(&self, name: &str) -> u32 {
        self.param_to_id.get(name).copied().unwrap_or(0)
    }

    fn write_header(&mut self, ty: u16, size: u16) {
        self.commands.extend_from_slice(&ty.to_le_bytes());
        self.commands.extend_from_slice(&size.to_le_bytes());
    }

    fn emit_set_f32(&mut self, path_id: u32, v: f32) {
        let size: u16 = 4 + 4 + 4;
        self.write_header(CMD_SET_F32, size);
        self.commands.extend_from_slice(&path_id.to_le_bytes());
        self.commands.extend_from_slice(&v.to_le_bytes());
    }

    fn emit_set_vec3(&mut self, path_id: u32, v: &Vec3) {
        let size: u16 = 4 + 4 + 12;
        self.write_header(CMD_SET_VEC3, size);
        self.commands.extend_from_slice(&path_id.to_le_bytes());
        self.commands.extend_from_slice(&v.x.to_le_bytes());
        self.commands.extend_from_slice(&v.y.to_le_bytes());
        self.commands.extend_from_slice(&v.z.to_le_bytes());
    }

    fn emit_set_vec4(&mut self, path_id: u32, v: &Vec4) {
        let size: u16 = 4 + 4 + 16;
        self.write_header(CMD_SET_VEC4, size);
        self.commands.extend_from_slice(&path_id.to_le_bytes());
        self.commands.extend_from_slice(&v.x.to_le_bytes());
        self.commands.extend_from_slice(&v.y.to_le_bytes());
        self.commands.extend_from_slice(&v.z.to_le_bytes());
        self.commands.extend_from_slice(&v.w.to_le_bytes());
    }

    fn emit_draw_line(&mut self, p0: &Vec3, p1: &Vec3, c: &Vec4) {
        let size: u16 = 4 + 40;
        self.write_header(CMD_DRAW_LINE, size);
        self.commands.extend_from_slice(&p0.x.to_le_bytes());
        self.commands.extend_from_slice(&p0.y.to_le_bytes());
        self.commands.extend_from_slice(&p0.z.to_le_bytes());
        self.commands.extend_from_slice(&p1.x.to_le_bytes());
        self.commands.extend_from_slice(&p1.y.to_le_bytes());
        self.commands.extend_from_slice(&p1.z.to_le_bytes());
        self.commands.extend_from_slice(&c.x.to_le_bytes());
        self.commands.extend_from_slice(&c.y.to_le_bytes());
        self.commands.extend_from_slice(&c.z.to_le_bytes());
        self.commands.extend_from_slice(&c.w.to_le_bytes());
    }
}

struct AnimVm {
    engine: Engine,
    scope: Scope<'static>,
    ast: Option<AST>,
    shared: Rc<RefCell<VmShared>>,
    last_error: CString,
    loaded: bool,
}

impl AnimVm {
    fn new() -> Self {
        let mut engine = Engine::new();
        engine.register_type_with_name::<Vec3>("Vec3");
        engine.register_get_set("x", |v: &mut Vec3| v.x as rhai::FLOAT, |v: &mut Vec3, x: rhai::FLOAT| v.x = x as f32);
        engine.register_get_set("y", |v: &mut Vec3| v.y as rhai::FLOAT, |v: &mut Vec3, y: rhai::FLOAT| v.y = y as f32);
        engine.register_get_set("z", |v: &mut Vec3| v.z as rhai::FLOAT, |v: &mut Vec3, z: rhai::FLOAT| v.z = z as f32);

        engine.register_type_with_name::<Vec4>("Vec4");
        engine.register_get_set("x", |v: &mut Vec4| v.x as rhai::FLOAT, |v: &mut Vec4, x: rhai::FLOAT| v.x = x as f32);
        engine.register_get_set("y", |v: &mut Vec4| v.y as rhai::FLOAT, |v: &mut Vec4, y: rhai::FLOAT| v.y = y as f32);
        engine.register_get_set("z", |v: &mut Vec4| v.z as rhai::FLOAT, |v: &mut Vec4, z: rhai::FLOAT| v.z = z as f32);
        engine.register_get_set("w", |v: &mut Vec4| v.w as rhai::FLOAT, |v: &mut Vec4, w: rhai::FLOAT| v.w = w as f32);

        engine.register_fn("vec3", |x: rhai::FLOAT, y: rhai::FLOAT, z: rhai::FLOAT| Vec3 {
            x: x as f32,
            y: y as f32,
            z: z as f32,
        });
        engine.register_fn("vec4", |x: rhai::FLOAT, y: rhai::FLOAT, z: rhai::FLOAT, w: rhai::FLOAT| Vec4 {
            x: x as f32,
            y: y as f32,
            z: z as f32,
            w: w as f32,
        });

        let shared = Rc::new(RefCell::new(VmShared::new()));

        {
            let shared = Rc::clone(&shared);
            engine.register_fn("set", move |path: &str, value: Dynamic| {
                let mut sh = shared.borrow_mut();
                let path_id = sh.intern_path(path);
                if value.is::<rhai::FLOAT>() {
                    sh.emit_set_f32(path_id, value.cast::<rhai::FLOAT>() as f32);
                } else if value.is::<Vec3>() {
                    let v = value.cast::<Vec3>();
                    sh.emit_set_vec3(path_id, &v);
                } else if value.is::<Vec4>() {
                    let v = value.cast::<Vec4>();
                    sh.emit_set_vec4(path_id, &v);
                } else {
                    sh.set_error_once(format!("set('{path}', value): unsupported type"));
                }
            });
        }

        {
            let shared = Rc::clone(&shared);
            engine.register_fn("draw_line", move |p0: Vec3, p1: Vec3, c: Vec4| {
                shared.borrow_mut().emit_draw_line(&p0, &p1, &c);
            });
        }

        {
            let shared = Rc::clone(&shared);
            engine.register_fn("param", move |name: &str| -> Dynamic {
                let sh = shared.borrow();
                match sh.params.get(name) {
                    Some(entry) => match &entry.value {
                        ParamValue::F32(v) => Dynamic::from(*v as rhai::FLOAT),
                        ParamValue::Vec4(v) => Dynamic::from(v.clone()),
                    },
                    None => Dynamic::from(0.0 as rhai::FLOAT),
                }
            });
        }

        Self {
            engine,
            scope: Scope::new(),
            ast: None,
            shared,
            last_error: CString::new("").unwrap_or_else(|_| CString::default()),
            loaded: false,
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

fn parse_vec4_from_json(v: &Value) -> Result<Vec4, String> {
    let arr = v.as_array().ok_or_else(|| "vec4 default must be array".to_string())?;
    if arr.len() != 4 {
        return Err("vec4 default must have 4 elements".to_string());
    }
    let f = |i: usize| -> Result<f32, String> {
        arr[i]
            .as_f64()
            .map(|x| x as f32)
            .ok_or_else(|| format!("vec4 default element {i} must be number"))
    };
    Ok(Vec4 {
        x: f(0)?,
        y: f(1)?,
        z: f(2)?,
        w: f(3)?,
    })
}

fn maybe_call_hook(vm: &mut AnimVm, fn_name: &str, args: impl rhai::FuncArgs) -> Result<(), String> {
    let Some(ast) = vm.ast.as_ref() else {
        return Ok(());
    };
    match vm.engine.call_fn::<()>(&mut vm.scope, ast, fn_name, args) {
        Ok(()) => Ok(()),
        Err(e) => {
            if matches!(*e, EvalAltResult::ErrorFunctionNotFound(_, _)) {
                Ok(())
            } else {
                Err(format!("rhai call {fn_name} failed: {e}"))
            }
        }
    }
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
        let root: Value = match json5::from_str(&normalized) {
            Ok(v) => v,
            Err(e) => return vm.set_error(format!("json5 parse failed: {e}")),
        };

        let script_lang = root
            .get("script")
            .and_then(|s| s.get("lang"))
            .and_then(Value::as_str)
            .unwrap_or("");
        if script_lang != "rhai" {
            return vm.set_error("script.lang must be \"rhai\"");
        }

        let code = match root
            .get("script")
            .and_then(|s| s.get("code"))
            .and_then(Value::as_str)
        {
            Some(c) => c,
            None => return vm.set_error("script.code missing or not string"),
        };

        let ast = match vm.engine.compile(code) {
            Ok(ast) => ast,
            Err(e) => return vm.set_error(format!("rhai compile failed: {e}")),
        };

        vm.scope.clear();
        vm.ast = Some(ast);
        vm.loaded = true;

        let mut param_parse_error: Option<String> = None;
        {
            let mut sh = vm.shared.borrow_mut();
            sh.path_to_id.clear();
            sh.next_path_id = 1;
            sh.param_to_id.clear();
            sh.id_to_param.clear();
            sh.params.clear();
            sh.next_param_id = 1;
            sh.clear_frame();

            if let Some(params_obj) = root.get("params").and_then(Value::as_object) {
                for (name, p) in params_obj {
                    let ty = p.get("type").and_then(Value::as_str).unwrap_or("");
                    let default_v = p.get("default");
                    let (ptype, pvalue) = match ty {
                        "f32" => {
                            let v = default_v.and_then(Value::as_f64).unwrap_or(0.0) as f32;
                            (ParamType::F32, ParamValue::F32(v))
                        }
                        "vec4" => {
                            let Some(v) = default_v else {
                                param_parse_error = Some(format!("param '{name}' missing default"));
                                break;
                            };
                            match parse_vec4_from_json(v) {
                                Ok(v4) => (ParamType::Vec4, ParamValue::Vec4(v4)),
                                Err(e) => {
                                    param_parse_error = Some(format!("param '{name}': {e}"));
                                    break;
                                }
                            }
                        }
                        _ => {
                            param_parse_error = Some(format!("unsupported param type '{ty}' for '{name}'"));
                            break;
                        }
                    };

                    let id = sh.next_param_id;
                    sh.next_param_id = sh.next_param_id.saturating_add(1);
                    sh.param_to_id.insert(name.clone(), id);
                    sh.id_to_param.insert(id, name.clone());
                    sh.params.insert(
                        name.clone(),
                        ParamEntry {
                            id,
                            ty: ptype,
                            value: pvalue,
                        },
                    );
                }
            }
        }
        if let Some(e) = param_parse_error {
            return vm.set_error(e);
        }

        if let Err(e) = maybe_call_hook(vm, "on_load", ()) {
            return vm.set_error(e);
        }

        let pending = { vm.shared.borrow_mut().pending_error.take() };
        if let Some(msg) = pending {
            return vm.set_error(msg);
        }

        vm.last_error = cstring_sanitize(String::new());
        0
    })) {
        Ok(code) => code,
        Err(_) => 1,
    }
}

#[no_mangle]
pub extern "C" fn animvm_intern_path(vm: *mut std::ffi::c_void, path: *const c_char) -> u32 {
    let result = catch_unwind(AssertUnwindSafe(|| {
        let Ok(vm) = vm_mut(vm as *mut AnimVm) else {
            return 0;
        };
        if path.is_null() {
            let _ = vm.set_error("path pointer is null");
            return 0;
        }
        let path = unsafe { CStr::from_ptr(path) };
        let Ok(path) = path.to_str() else {
            let _ = vm.set_error("path is not valid UTF-8");
            return 0;
        };
        vm.shared.borrow_mut().intern_path(path)
    }));
    result.unwrap_or(0)
}

#[no_mangle]
pub extern "C" fn animvm_intern_param(vm: *mut std::ffi::c_void, name: *const c_char) -> u32 {
    let result = catch_unwind(AssertUnwindSafe(|| {
        let Ok(vm) = vm_mut(vm as *mut AnimVm) else {
            return 0;
        };
        if name.is_null() {
            let _ = vm.set_error("param name pointer is null");
            return 0;
        }
        let name = unsafe { CStr::from_ptr(name) };
        let Ok(name) = name.to_str() else {
            let _ = vm.set_error("param name is not valid UTF-8");
            return 0;
        };
        vm.shared.borrow().intern_param_existing_only(name)
    }));
    result.unwrap_or(0)
}

#[no_mangle]
pub extern "C" fn animvm_set_param_f32(vm: *mut std::ffi::c_void, param_id: u32, v: f32) -> i32 {
    match catch_unwind(AssertUnwindSafe(|| {
        let vm = match vm_mut(vm as *mut AnimVm) {
            Ok(v) => v,
            Err(_) => return 1,
        };
        let maybe_error = {
            let mut sh = vm.shared.borrow_mut();
            match sh.id_to_param.get(&param_id).cloned() {
                None => Some(format!("unknown param id {param_id}")),
                Some(name) => match sh.params.get_mut(&name) {
                    None => Some(format!("missing param entry for id {param_id}")),
                    Some(entry) => {
                        if entry.ty != ParamType::F32 {
                            Some(format!("param '{name}' is not f32"))
                        } else {
                            entry.value = ParamValue::F32(v);
                            None
                        }
                    }
                },
            }
        };
        if let Some(e) = maybe_error {
            return vm.set_error(e);
        }
        vm.last_error = cstring_sanitize(String::new());
        0
    })) {
        Ok(code) => code,
        Err(_) => 1,
    }
}

#[no_mangle]
pub extern "C" fn animvm_set_param_vec4(
    vm: *mut std::ffi::c_void,
    param_id: u32,
    x: f32,
    y: f32,
    z: f32,
    w: f32,
) -> i32 {
    match catch_unwind(AssertUnwindSafe(|| {
        let vm = match vm_mut(vm as *mut AnimVm) {
            Ok(v) => v,
            Err(_) => return 1,
        };
        let maybe_error = {
            let mut sh = vm.shared.borrow_mut();
            match sh.id_to_param.get(&param_id).cloned() {
                None => Some(format!("unknown param id {param_id}")),
                Some(name) => match sh.params.get_mut(&name) {
                    None => Some(format!("missing param entry for id {param_id}")),
                    Some(entry) => {
                        if entry.ty != ParamType::Vec4 {
                            Some(format!("param '{name}' is not vec4"))
                        } else {
                            entry.value = ParamValue::Vec4(Vec4 { x, y, z, w });
                            None
                        }
                    }
                },
            }
        };
        if let Some(e) = maybe_error {
            return vm.set_error(e);
        }
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
        if !vm.loaded {
            return vm.set_error("script not loaded");
        }

        vm.shared.borrow_mut().clear_frame();
        vm.scope.set_or_push("t", t as rhai::FLOAT);
        vm.scope.set_or_push("dt", dt as rhai::FLOAT);

        if let Err(e) = maybe_call_hook(vm, "on_frame", (t as rhai::FLOAT, dt as rhai::FLOAT)) {
            return vm.set_error(e);
        }

        let pending = { vm.shared.borrow_mut().pending_error.take() };
        if let Some(msg) = pending {
            return vm.set_error(msg);
        }

        vm.last_error = cstring_sanitize(String::new());
        0
    })) {
        Ok(code) => code,
        Err(_) => 1,
    }
}

#[no_mangle]
pub extern "C" fn animvm_get_commands(
    vm: *mut std::ffi::c_void,
    out_ptr: *mut *const u8,
    out_len: *mut usize,
) -> i32 {
    match catch_unwind(AssertUnwindSafe(|| {
        let vm = match vm_mut(vm as *mut AnimVm) {
            Ok(v) => v,
            Err(_) => return 1,
        };
        if out_ptr.is_null() || out_len.is_null() {
            return vm.set_error("out pointers must not be null");
        }
        let sh = vm.shared.borrow();
        unsafe {
            *out_ptr = sh.commands.as_ptr();
            *out_len = sh.commands.len();
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
